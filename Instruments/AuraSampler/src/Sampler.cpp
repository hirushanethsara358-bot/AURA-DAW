/// @file Sampler.cpp
/// @brief Implementation of AURA Sampler with a minimal WAV parser.

#include "Aura/Sampler.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
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
} // namespace

SampleLoadResult parseWavFile(const std::string& path, SampleData& out) {
    SampleLoadResult result;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        result.error = "Cannot open file: " + path;
        return result;
    }

    char riff[4] = {};
    in.read(riff, 4);
    (void)readU32LE(in); // file size
    char wave[4] = {};
    in.read(wave, 4);
    if (std::string(riff, 4) != "RIFF" || std::string(wave, 4) != "WAVE") {
        result.error = "Not a RIFF/WAVE file: " + path;
        return result;
    }

    std::uint16_t audioFormat = 0;
    std::uint16_t numChannels = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t bitsPerSample = 0;
    std::vector<std::uint8_t> audioBytes;

    // Walk chunks.
    while (in) {
        char chunkId[4] = {};
        in.read(chunkId, 4);
        if (in.gcount() != 4) {
            break;
        }
        const std::uint32_t chunkSize = readU32LE(in);
        const std::string id(chunkId, 4);
        if (id == "fmt ") {
            audioFormat = readU16LE(in);
            numChannels = readU16LE(in);
            sampleRate = readU32LE(in);
            (void)readU32LE(in); // byte rate
            (void)readU16LE(in); // block align
            bitsPerSample = readU16LE(in);
            if (chunkSize > 16) {
                in.seekg(chunkSize - 16, std::ios::cur);
            }
        } else if (id == "data") {
            audioBytes.resize(chunkSize);
            in.read(reinterpret_cast<char*>(audioBytes.data()),
                    static_cast<std::streamsize>(chunkSize));
            if (chunkSize % 2 == 1) {
                in.seekg(1, std::ios::cur); // word alignment
            }
        } else {
            in.seekg(chunkSize + (chunkSize % 2), std::ios::cur);
        }
    }

    if (numChannels == 0 || numChannels > 2 || sampleRate == 0) {
        result.error = "Unsupported WAV format (need 1-2 channels): " + path;
        return result;
    }
    if (!((audioFormat == 1 && (bitsPerSample == 16 || bitsPerSample == 24)) ||
          (audioFormat == 3 && bitsPerSample == 32))) {
        result.error = "Unsupported WAV encoding (need PCM16/PCM24/FLOAT32): " + path;
        return result;
    }

    const std::size_t bytesPerSample = bitsPerSample / 8;
    const std::size_t frameSize = bytesPerSample * numChannels;
    const std::size_t numFrames = audioBytes.size() / frameSize;
    if (numFrames == 0) {
        result.error = "WAV file contains no audio: " + path;
        return result;
    }

    SampleData data;
    data.sampleRate = static_cast<double>(sampleRate);
    data.name = path;
    data.left.reserve(numFrames);
    if (numChannels == 2) {
        data.right.reserve(numFrames);
    }

    auto decode = [&](std::size_t byteOffset) -> double {
        const std::uint8_t* p = audioBytes.data() + byteOffset;
        if (audioFormat == 3) { // float32
            float f = 0.0F;
            std::copy(p, p + 4, reinterpret_cast<std::uint8_t*>(&f));
            return static_cast<double>(f);
        }
        if (bitsPerSample == 16) {
            std::int16_t s = static_cast<std::int16_t>(p[0] | (p[1] << 8));
            return static_cast<double>(s) / 32768.0;
        }
        // 24-bit
        std::int32_t s = static_cast<std::int32_t>(p[0] | (p[1] << 8) | (p[2] << 16));
        if ((s & 0x800000) != 0) {
            s |= ~0xFFFFFF;
        }
        return static_cast<double>(s) / 8388608.0;
    };

    for (std::size_t f = 0; f < numFrames; ++f) {
        data.left.push_back(decode(f * frameSize));
        if (numChannels == 2) {
            data.right.push_back(decode(f * frameSize + bytesPerSample));
        }
    }

    out = std::move(data);
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
            double r = sample.isStereo() ? (sample.right[i0] * (1.0 - frac) +
                                            sample.right[i0 + 1] * frac)
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
