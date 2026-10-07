/// @file Sampler.cpp
/// @brief Implementation of AURA Sampler with a minimal WAV parser.

#include "Aura/Sampler.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

namespace Aura::Instrument {

namespace {
double dbToGain(double db) {
    return std::pow(10.0, db / 20.0);
}

std::uint32_t readU32LE(std::istream& in) {
    std::uint8_t b[4] = {0, 0, 0, 0};
    in.read(reinterpret_cast<char*>(b), 4);
    return static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8) |
           (static_cast<std::uint32_t>(b[2]) << 16) | (static_cast<std::uint32_t>(b[3]) << 24);
}

std::uint16_t readU16LE(std::istream& in) {
    std::uint8_t b[2] = {0, 0};
    in.read(reinterpret_cast<char*>(b), 2);
    return static_cast<std::uint16_t>(b[0]) | (static_cast<std::uint16_t>(b[1]) << 8);
}

std::filesystem::path pathFromUtf8(const std::string& text) {
    std::u8string encoded;
    encoded.reserve(text.size());
    for (unsigned char byte : text) {
        encoded.push_back(static_cast<char8_t>(byte));
    }
    return std::filesystem::path(encoded);
}
} // namespace

WavMetadataResult inspectWavFile(const std::string& path) {
    WavMetadataResult result;
    std::ifstream in(pathFromUtf8(path), std::ios::binary);
    if (!in) {
        result.error = "Cannot open file: " + path;
        return result;
    }

    in.seekg(0, std::ios::end);
    const std::streamoff fileEnd = in.tellg();
    if (fileEnd < 12) {
        result.error = "WAV file is too short to contain a RIFF header: " + path;
        return result;
    }
    const auto fileSize = static_cast<std::uint64_t>(fileEnd);
    in.seekg(0, std::ios::beg);

    char riff[4] = {};
    char wave[4] = {};
    in.read(riff, 4);
    const std::uint32_t riffSize = readU32LE(in);
    in.read(wave, 4);
    if (!in || std::string(riff, 4) != "RIFF" || std::string(wave, 4) != "WAVE") {
        result.error = "Not a valid RIFF/WAVE file: " + path;
        return result;
    }
    const std::uint64_t containerEnd = 8ULL + riffSize;
    if (riffSize < 4 || containerEnd > fileSize) {
        result.error = "WAV RIFF size is truncated or invalid: " + path;
        return result;
    }

    bool foundFormat = false;
    bool foundData = false;
    std::uint32_t dataSize = 0;
    std::uint16_t blockAlign = 0;
    std::uint64_t position = 12;
    while (position < containerEnd) {
        if (containerEnd - position < 8) {
            result.error = "WAV contains a truncated chunk header: " + path;
            return result;
        }
        char chunkId[4] = {};
        in.read(chunkId, 4);
        const std::uint32_t chunkSize = readU32LE(in);
        if (!in) {
            result.error = "WAV contains a truncated chunk header: " + path;
            return result;
        }
        const std::uint64_t payloadStart = position + 8;
        const std::uint64_t paddedChunkSize =
            static_cast<std::uint64_t>(chunkSize) + (chunkSize & 1U);
        if (paddedChunkSize > containerEnd - payloadStart) {
            result.error = "WAV chunk extends beyond the RIFF container: " + path;
            return result;
        }

        const std::string chunkName(chunkId, 4);
        if (chunkName == "fmt ") {
            if (foundFormat || chunkSize < 16) {
                result.error = "WAV format chunk is duplicated or too short: " + path;
                return result;
            }
            result.metadata.audioFormat = readU16LE(in);
            result.metadata.channels = readU16LE(in);
            result.metadata.sampleRate = readU32LE(in);
            (void)readU32LE(in); // byte rate
            blockAlign = readU16LE(in);
            result.metadata.bitsPerSample = readU16LE(in);
            if (!in) {
                result.error = "WAV format chunk is truncated: " + path;
                return result;
            }
            foundFormat = true;
        } else if (chunkName == "data") {
            if (foundData) {
                result.error = "Multiple WAV data chunks are not supported: " + path;
                return result;
            }
            foundData = true;
            dataSize = chunkSize;
            result.metadata.dataOffset = payloadStart;
            result.metadata.dataBytes = chunkSize;
        }

        position += 8 + paddedChunkSize;
        in.seekg(static_cast<std::streamoff>(position), std::ios::beg);
        if (!in) {
            result.error = "WAV chunk could not be skipped: " + path;
            return result;
        }
    }

    const bool supportedEncoding =
        (result.metadata.audioFormat == 1 &&
         (result.metadata.bitsPerSample == 16 || result.metadata.bitsPerSample == 24)) ||
        (result.metadata.audioFormat == 3 && result.metadata.bitsPerSample == 32);
    if (!foundFormat || !foundData || result.metadata.channels < 1 ||
        result.metadata.channels > 2 || result.metadata.sampleRate < 8000 ||
        result.metadata.sampleRate > 384000 || !supportedEncoding) {
        result.error = "Unsupported WAV format (need mono/stereo PCM16/PCM24/FLOAT32): " + path;
        return result;
    }

    const std::uint32_t bytesPerSample = result.metadata.bitsPerSample / 8;
    const std::uint32_t expectedBlockAlign = result.metadata.channels * bytesPerSample;
    if (blockAlign != expectedBlockAlign || dataSize == 0 || dataSize % blockAlign != 0) {
        result.error = "WAV data alignment is invalid or contains no audio: " + path;
        return result;
    }
    result.metadata.frames = dataSize / blockAlign;
    result.metadata.durationSeconds =
        static_cast<double>(result.metadata.frames) / result.metadata.sampleRate;
    if (result.metadata.frames == 0 || !std::isfinite(result.metadata.durationSeconds)) {
        result.error = "WAV duration is invalid: " + path;
        return result;
    }

    result.ok = true;
    return result;
}

SampleLoadResult parseWavFile(const std::string& path, SampleData& out,
                              std::uint64_t maxDecodedBytes) {
    SampleLoadResult result;
    const WavMetadataResult inspection = inspectWavFile(path);
    if (!inspection.ok) {
        result.error = inspection.error;
        return result;
    }

    const WavMetadata& metadata = inspection.metadata;
    const std::uint64_t bytesPerFrame =
        static_cast<std::uint64_t>(metadata.channels) * (metadata.bitsPerSample / 8U);
    const std::uint64_t bytesPerDecodedFrame =
        static_cast<std::uint64_t>(metadata.channels) * sizeof(double);
    if (metadata.frames > std::numeric_limits<std::uint64_t>::max() / bytesPerDecodedFrame ||
        metadata.frames > std::numeric_limits<std::size_t>::max()) {
        result.error = "Decoded WAV is too large to represent in memory: " + path;
        return result;
    }
    const std::uint64_t decodedBytes = metadata.frames * bytesPerDecodedFrame;
    if (decodedBytes > maxDecodedBytes) {
        result.error = "Decoded WAV exceeds the configured memory limit: " + path;
        return result;
    }

    std::ifstream in(pathFromUtf8(path), std::ios::binary);
    if (!in) {
        result.error = "Cannot reopen file for decoding: " + path;
        return result;
    }
    in.seekg(static_cast<std::streamoff>(metadata.dataOffset), std::ios::beg);
    if (!in) {
        result.error = "Cannot seek to WAV audio data: " + path;
        return result;
    }

    SampleData decoded;
    decoded.sampleRate = static_cast<double>(metadata.sampleRate);
    decoded.name = path;
    const std::size_t frameCount = static_cast<std::size_t>(metadata.frames);
    decoded.left.reserve(frameCount);
    if (metadata.channels == 2) {
        decoded.right.reserve(frameCount);
    }

    constexpr std::size_t kFramesPerRead = 4096;
    constexpr std::size_t kMaxBytesPerFrame = 6;
    std::array<std::uint8_t, kFramesPerRead * kMaxBytesPerFrame> block{};
    std::uint64_t framesRemaining = metadata.frames;
    while (framesRemaining > 0) {
        const std::size_t framesThisRead =
            static_cast<std::size_t>(std::min<std::uint64_t>(framesRemaining, kFramesPerRead));
        const std::size_t bytesThisRead = framesThisRead * static_cast<std::size_t>(bytesPerFrame);
        in.read(reinterpret_cast<char*>(block.data()), static_cast<std::streamsize>(bytesThisRead));
        if (in.gcount() != static_cast<std::streamsize>(bytesThisRead)) {
            result.error = "WAV audio data was truncated while decoding: " + path;
            return result;
        }

        for (std::size_t frame = 0; frame < framesThisRead; ++frame) {
            const std::uint8_t* samples = block.data() + frame * bytesPerFrame;
            auto decode = [&](const std::uint8_t* sampleBytes) -> double {
                if (metadata.audioFormat == 3) {
                    const std::uint32_t bits = static_cast<std::uint32_t>(sampleBytes[0]) |
                                               (static_cast<std::uint32_t>(sampleBytes[1]) << 8U) |
                                               (static_cast<std::uint32_t>(sampleBytes[2]) << 16U) |
                                               (static_cast<std::uint32_t>(sampleBytes[3]) << 24U);
                    return static_cast<double>(std::bit_cast<float>(bits));
                }
                if (metadata.bitsPerSample == 16) {
                    const std::uint16_t bits = static_cast<std::uint16_t>(sampleBytes[0]) |
                                               (static_cast<std::uint16_t>(sampleBytes[1]) << 8U);
                    return static_cast<double>(std::bit_cast<std::int16_t>(bits)) / 32768.0;
                }
                const std::uint32_t bits = static_cast<std::uint32_t>(sampleBytes[0]) |
                                           (static_cast<std::uint32_t>(sampleBytes[1]) << 8U) |
                                           (static_cast<std::uint32_t>(sampleBytes[2]) << 16U);
                const std::int32_t value = (bits & 0x800000U) != 0
                                               ? static_cast<std::int32_t>(bits) - 0x1000000
                                               : static_cast<std::int32_t>(bits);
                return static_cast<double>(value) / 8388608.0;
            };

            const double left = decode(samples);
            if (!std::isfinite(left)) {
                result.error = "WAV contains a non-finite float sample: " + path;
                return result;
            }
            decoded.left.push_back(left);
            if (metadata.channels == 2) {
                const double right = decode(samples + metadata.bitsPerSample / 8U);
                if (!std::isfinite(right)) {
                    result.error = "WAV contains a non-finite float sample: " + path;
                    return result;
                }
                decoded.right.push_back(right);
            }
        }
        framesRemaining -= framesThisRead;
    }

    out = std::move(decoded);
    result.ok = true;
    return result;
}

SampleLoadResult Sampler::loadWavFile(const std::string& path) {
    SampleData data;
    SampleLoadResult parsed = parseWavFile(path, data);
    if (!parsed.ok) {
        return parsed;
    }
    SampleLoadResult result;
    result.ok = true;
    result.sampleIndex = addMemorySample(std::move(data));
    return result;
}

std::size_t Sampler::addMemorySample(SampleData data) {
    samples_.push_back(std::move(data));
    return samples_.size() - 1;
}

void Sampler::addZone(const SampleZone& zone) {
    if (zone.sampleIndex < samples_.size()) {
        zones_.push_back(zone);
    }
}

void Sampler::clearZones() {
    zones_.clear();
}

void Sampler::prepare(double sampleRate) {
    outputRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    if (voices_.empty()) {
        voices_.resize(kMaxVoices);
    }
    allNotesOff();
}

const SampleZone* Sampler::zoneForNote(int note) const {
    for (const auto& z : zones_) {
        if (note >= z.keyLow && note <= z.keyHigh) {
            return &z;
        }
    }
    return nullptr;
}

void Sampler::noteOn(int midiNote, int velocity) {
    const SampleZone* zone = zoneForNote(midiNote);
    if (zone == nullptr || zone->sampleIndex >= samples_.size()) {
        return;
    }
    const SampleData& sample = samples_[zone->sampleIndex];
    if (sample.frames() == 0) {
        return;
    }
    // Find a free voice.
    Voice* target = nullptr;
    for (auto& v : voices_) {
        if (!v.active) {
            target = &v;
            break;
        }
    }
    if (target == nullptr) {
        target = &voices_.front(); // steal oldest slot
    }
    const double semitones = static_cast<double>(midiNote - zone->rootNote);
    target->active = true;
    target->sample = &sample;
    target->position = 0.0;
    target->increment = std::pow(2.0, semitones / 12.0) * (sample.sampleRate / outputRate_);
    target->gain = (std::clamp(velocity, 1, 127) / 127.0) * dbToGain(zone->gainDb);
    target->note = midiNote;
    target->releasing = false;
    target->releaseGain = 1.0;
}

void Sampler::noteOff(int midiNote) {
    for (auto& v : voices_) {
        if (v.active && v.note == midiNote) {
            v.releasing = true;
        }
    }
}

void Sampler::allNotesOff() {
    for (auto& v : voices_) {
        v.active = false;
        v.sample = nullptr;
        v.note = -1;
    }
}

int Sampler::activeVoiceCount() const {
    int count = 0;
    for (const auto& v : voices_) {
        if (v.active) {
            ++count;
        }
    }
    return count;
}

void Sampler::renderBlock(double* left, double* right, int numSamples) {
    const double masterGain = dbToGain(masterGainDb_);
    // 5 ms release fade to avoid clicks on note-off.
    const double fadeStep = 1.0 / (0.005 * outputRate_);

    for (auto& v : voices_) {
        if (!v.active || v.sample == nullptr) {
            continue;
        }
        const SampleData& sample = *v.sample;
        const double numFrames = static_cast<double>(sample.frames());
        for (int i = 0; i < numSamples; ++i) {
            if (v.position >= numFrames - 1) {
                v.active = false;
                break;
            }
            const auto i0 = static_cast<std::size_t>(v.position);
            const double frac = v.position - static_cast<double>(i0);
            double l = sample.left[i0] * (1.0 - frac) + sample.left[i0 + 1] * frac;
            double r = sample.isStereo()
                           ? (sample.right[i0] * (1.0 - frac) + sample.right[i0 + 1] * frac)
                           : l;
            if (v.releasing) {
                v.releaseGain -= fadeStep;
                if (v.releaseGain <= 0.0) {
                    v.active = false;
                    v.releasing = false;
                    break;
                }
                l *= v.releaseGain;
                r *= v.releaseGain;
            }
            left[i] += l * v.gain * masterGain;
            right[i] += r * v.gain * masterGain;
            v.position += v.increment;
        }
    }
}

} // namespace Aura::Instrument
