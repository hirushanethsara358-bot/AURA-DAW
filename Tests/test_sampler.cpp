/// @file test_sampler.cpp
/// @brief Unit tests for AURA Sampler.

#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "Aura/AudioBufferManager.hpp"
#include "Aura/Sampler.hpp"
#include "Aura/WavClipPlayer.hpp"

namespace {
Aura::Instrument::SampleData makeSineSample(double freq = 440.0, double seconds = 1.0,
                                            double sampleRate = 48000.0) {
    Aura::Instrument::SampleData data;
    data.sampleRate = sampleRate;
    data.name = "test-sine";
    const auto frames = static_cast<std::size_t>(seconds * sampleRate);
    data.left.resize(frames + 1);
    for (std::size_t i = 0; i <= frames; ++i) {
        data.left[i] = 0.5 * std::sin(2.0 * 3.141592653589793 * freq * i / sampleRate);
    }
    return data;
}

double bufferRms(const std::vector<double>& v) {
    double sum = 0.0;
    for (double x : v) {
        sum += x * x;
    }
    return std::sqrt(sum / v.size());
}

void writeU16(std::ofstream& output, std::uint16_t value) {
    const char bytes[] = {static_cast<char>(value & 0xffU),
                          static_cast<char>((value >> 8U) & 0xffU)};
    output.write(bytes, 2);
}

void writeU32(std::ofstream& output, std::uint32_t value) {
    const char bytes[] = {
        static_cast<char>(value & 0xffU), static_cast<char>((value >> 8U) & 0xffU),
        static_cast<char>((value >> 16U) & 0xffU), static_cast<char>((value >> 24U) & 0xffU)};
    output.write(bytes, 4);
}

void writeMinimalMonoPcm16Wav(const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary);
    output.write("RIFF", 4);
    writeU32(output, 40); // RIFF payload size, excludes the first 8 bytes.
    output.write("WAVEfmt ", 8);
    writeU32(output, 16);
    writeU16(output, 1); // PCM
    writeU16(output, 1); // mono
    writeU32(output, 48000);
    writeU32(output, 96000);
    writeU16(output, 2);
    writeU16(output, 16);
    output.write("data", 4);
    writeU32(output, 4);
    writeU16(output, 0);
    writeU16(output, 0);
}

void writeWav(const std::filesystem::path& path, std::uint16_t format, std::uint16_t channels,
              std::uint32_t sampleRate, std::uint16_t bitsPerSample,
              const std::vector<std::uint8_t>& payload) {
    std::ofstream output(path, std::ios::binary);
    const std::uint16_t blockAlign = static_cast<std::uint16_t>(channels * bitsPerSample / 8U);
    const std::uint32_t byteRate = sampleRate * blockAlign;
    const std::uint32_t paddedPayloadSize =
        static_cast<std::uint32_t>(payload.size() + (payload.size() & 1U));
    output.write("RIFF", 4);
    writeU32(output, 36U + paddedPayloadSize);
    output.write("WAVEfmt ", 8);
    writeU32(output, 16);
    writeU16(output, format);
    writeU16(output, channels);
    writeU32(output, sampleRate);
    writeU32(output, byteRate);
    writeU16(output, blockAlign);
    writeU16(output, bitsPerSample);
    output.write("data", 4);
    writeU32(output, static_cast<std::uint32_t>(payload.size()));
    output.write(reinterpret_cast<const char*>(payload.data()),
                 static_cast<std::streamsize>(payload.size()));
    if ((payload.size() & 1U) != 0) {
        output.put('\0');
    }
}
} // namespace

TEST(Sampler, PlaysMemorySample) {
    Aura::Instrument::Sampler sampler;
    const std::size_t idx = sampler.addMemorySample(makeSineSample());
    sampler.addZone({idx, 0, 127, 69, 0.0});
    sampler.prepare(48000.0);

    sampler.noteOn(69, 100);
    EXPECT_EQ(sampler.activeVoiceCount(), 1);

    std::vector<double> left(4800, 0.0), right(4800, 0.0);
    sampler.renderBlock(left.data(), right.data(), 4800);
    EXPECT_GT(bufferRms(left), 0.1); // velocity-scaled 0.5 sine
}

TEST(Sampler, PitchShiftChangesPlaybackRate) {
    Aura::Instrument::Sampler sampler;
    const std::size_t idx = sampler.addMemorySample(makeSineSample());
    sampler.addZone({idx, 0, 127, 69, 0.0});
    sampler.prepare(48000.0);

    sampler.noteOn(81, 100); // +12 semitones = double speed
    std::vector<double> left(4800, 0.0), right(4800, 0.0);
    sampler.renderBlock(left.data(), right.data(), 4800);
    EXPECT_GT(bufferRms(left), 0.05);
}

TEST(Sampler, NoteOffReleasesVoice) {
    Aura::Instrument::Sampler sampler;
    const std::size_t idx = sampler.addMemorySample(makeSineSample());
    sampler.addZone({idx, 0, 127, 69, 0.0});
    sampler.prepare(48000.0);

    sampler.noteOn(60, 100);
    sampler.noteOff(60);
    std::vector<double> left(48000, 0.0), right(48000, 0.0);
    sampler.renderBlock(left.data(), right.data(), 48000); // 1 s >> 5 ms fade
    EXPECT_EQ(sampler.activeVoiceCount(), 0);
}

TEST(Sampler, IgnoresUnmappedKeys) {
    Aura::Instrument::Sampler sampler;
    const std::size_t idx = sampler.addMemorySample(makeSineSample());
    sampler.addZone({idx, 60, 72, 60, 0.0});
    sampler.prepare(48000.0);
    sampler.noteOn(40, 100); // outside zone
    EXPECT_EQ(sampler.activeVoiceCount(), 0);
}

TEST(Sampler, LoadMissingFileFails) {
    Aura::Instrument::SampleData data;
    const auto result = Aura::Instrument::parseWavFile("/nonexistent/path/file.wav", data);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.empty());
}

TEST(Sampler, InspectWavReadsMetadataWithoutDecoding) {
    const auto path = std::filesystem::temp_directory_path() / "aura_metadata_test.wav";
    writeMinimalMonoPcm16Wav(path);

    const auto result = Aura::Instrument::inspectWavFile(path.string());
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.metadata.channels, 1);
    EXPECT_EQ(result.metadata.sampleRate, 48000U);
    EXPECT_EQ(result.metadata.bitsPerSample, 16);
    EXPECT_EQ(result.metadata.frames, 2U);
    EXPECT_EQ(result.metadata.dataOffset, 44U);
    EXPECT_EQ(result.metadata.dataBytes, 4U);
    EXPECT_DOUBLE_EQ(result.metadata.durationSeconds, 2.0 / 48000.0);

    std::filesystem::remove(path);
}

TEST(Sampler, InspectWavRejectsTruncatedChunks) {
    const auto path = std::filesystem::temp_directory_path() / "aura_truncated_test.wav";
    {
        std::ofstream output(path, std::ios::binary);
        output.write("RIFF", 4);
        writeU32(output, 100);
        output.write("WAVE", 4);
    }

    const auto result = Aura::Instrument::inspectWavFile(path.string());
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.empty());
    std::filesystem::remove(path);
}

TEST(Sampler, DecodesPcm16Extremes) {
    const auto path = std::filesystem::temp_directory_path() / "aura_pcm16_decode_test.wav";
    writeWav(path, 1, 1, 48000, 16, {0x00, 0x80, 0x00, 0x00, 0xff, 0x7f});

    Aura::Instrument::SampleData data;
    const auto result = Aura::Instrument::parseWavFile(path.string(), data);
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(data.left.size(), 3U);
    EXPECT_DOUBLE_EQ(data.left[0], -1.0);
    EXPECT_DOUBLE_EQ(data.left[1], 0.0);
    EXPECT_DOUBLE_EQ(data.left[2], 32767.0 / 32768.0);
    EXPECT_TRUE(data.right.empty());
    std::filesystem::remove(path);
}

TEST(Sampler, DecodesSignedPcm24Extremes) {
    const auto path = std::filesystem::temp_directory_path() / "aura_pcm24_decode_test.wav";
    writeWav(path, 1, 1, 48000, 24, {0x00, 0x00, 0x80, 0xff, 0xff, 0x7f});

    Aura::Instrument::SampleData data;
    const auto result = Aura::Instrument::parseWavFile(path.string(), data);
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(data.left.size(), 2U);
    EXPECT_DOUBLE_EQ(data.left[0], -1.0);
    EXPECT_DOUBLE_EQ(data.left[1], 8388607.0 / 8388608.0);
    std::filesystem::remove(path);
}

TEST(Sampler, DecodesInterleavedFloat32Stereo) {
    const auto path = std::filesystem::temp_directory_path() / "aura_float32_decode_test.wav";
    std::vector<std::uint8_t> payload;
    const auto appendFloat = [&payload](float value) {
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
        payload.push_back(static_cast<std::uint8_t>(bits & 0xffU));
        payload.push_back(static_cast<std::uint8_t>((bits >> 8U) & 0xffU));
        payload.push_back(static_cast<std::uint8_t>((bits >> 16U) & 0xffU));
        payload.push_back(static_cast<std::uint8_t>((bits >> 24U) & 0xffU));
    };
    appendFloat(-0.5F);
    appendFloat(0.25F);
    appendFloat(0.5F);
    appendFloat(0.75F);
    writeWav(path, 3, 2, 48000, 32, payload);

    Aura::Instrument::SampleData data;
    const auto result = Aura::Instrument::parseWavFile(path.string(), data);
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(data.left.size(), 2U);
    ASSERT_EQ(data.right.size(), 2U);
    EXPECT_DOUBLE_EQ(data.left[0], -0.5);
    EXPECT_DOUBLE_EQ(data.right[0], 0.25);
    EXPECT_DOUBLE_EQ(data.left[1], 0.5);
    EXPECT_DOUBLE_EQ(data.right[1], 0.75);
    std::filesystem::remove(path);
}

TEST(Sampler, RejectsNonFiniteFloatSamples) {
    const auto path = std::filesystem::temp_directory_path() / "aura_float32_nan_test.wav";
    const std::uint32_t nanBits =
        std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN());
    const std::vector<std::uint8_t> payload = {static_cast<std::uint8_t>(nanBits & 0xffU),
                                               static_cast<std::uint8_t>((nanBits >> 8U) & 0xffU),
                                               static_cast<std::uint8_t>((nanBits >> 16U) & 0xffU),
                                               static_cast<std::uint8_t>((nanBits >> 24U) & 0xffU)};
    writeWav(path, 3, 1, 48000, 32, payload);

    Aura::Instrument::SampleData data;
    const auto result = Aura::Instrument::parseWavFile(path.string(), data);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.empty());
    EXPECT_TRUE(data.left.empty());
    std::filesystem::remove(path);
}

TEST(Sampler, EnforcesDecodeMemoryLimitWithoutChangingOutput) {
    const auto path = std::filesystem::temp_directory_path() / "aura_decode_limit_test.wav";
    writeWav(path, 1, 1, 48000, 16, {0x00, 0x00, 0x00, 0x00});

    Aura::Instrument::SampleData data;
    data.left = {0.25};
    const auto result = Aura::Instrument::parseWavFile(path.string(), data, 15);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.empty());
    ASSERT_EQ(data.left.size(), 1U);
    EXPECT_DOUBLE_EQ(data.left.front(), 0.25);
    std::filesystem::remove(path);
}

TEST(AudioBufferManager, LoadsCachesAndReleasesDecodedWav) {
    const auto path = std::filesystem::temp_directory_path() / "aura_buffer_cache_test.wav";
    writeWav(path, 1, 1, 48000, 16, {0x00, 0x40});

    Aura::Instrument::AudioBufferManager manager(8);
    const auto loaded = manager.loadWavFile(path.string());
    ASSERT_TRUE(loaded.ok) << loaded.error;
    EXPECT_EQ(loaded.decodedBytes, 8U);
    EXPECT_EQ(manager.decodedBytesInUse(), 8U);
    ASSERT_EQ(manager.bufferCount(), 1U);
    ASSERT_NE(manager.get(loaded.handle), nullptr);
    EXPECT_DOUBLE_EQ(manager.get(loaded.handle)->left.front(), 0.5);

    const auto cached = manager.loadWavFile(path.string());
    ASSERT_TRUE(cached.ok) << cached.error;
    EXPECT_EQ(cached.handle, loaded.handle);
    EXPECT_EQ(manager.bufferCount(), 1U);
    EXPECT_EQ(manager.decodedBytesInUse(), 8U);
    EXPECT_TRUE(manager.release(loaded.handle));
    EXPECT_FALSE(manager.release(loaded.handle));
    EXPECT_EQ(manager.get(loaded.handle), nullptr);
    EXPECT_EQ(manager.decodedBytesInUse(), 0U);
    std::filesystem::remove(path);
}

TEST(AudioBufferManager, EnforcesMemoryBudgetAcrossFiles) {
    const auto firstPath = std::filesystem::temp_directory_path() / "aura_buffer_budget_first.wav";
    const auto secondPath =
        std::filesystem::temp_directory_path() / "aura_buffer_budget_second.wav";
    writeWav(firstPath, 1, 1, 48000, 16, {0x00, 0x00});
    writeWav(secondPath, 1, 1, 48000, 16, {0x00, 0x00});

    Aura::Instrument::AudioBufferManager manager(8);
    const auto first = manager.loadWavFile(firstPath.string());
    ASSERT_TRUE(first.ok) << first.error;
    EXPECT_FALSE(manager.setMemoryLimit(7));
    EXPECT_TRUE(manager.setMemoryLimit(8));
    const auto second = manager.loadWavFile(secondPath.string());
    EXPECT_FALSE(second.ok);
    EXPECT_FALSE(second.error.empty());
    EXPECT_EQ(manager.bufferCount(), 1U);
    EXPECT_EQ(manager.decodedBytesInUse(), 8U);
    manager.clear();
    EXPECT_EQ(manager.bufferCount(), 0U);
    EXPECT_EQ(manager.decodedBytesInUse(), 0U);
    std::filesystem::remove(firstPath);
    std::filesystem::remove(secondPath);
}

TEST(WavClipPlayer, RendersPreparedMonoClipAndStopsAtEnd) {
    Aura::Instrument::SampleData source;
    source.sampleRate = 48000.0;
    source.left = {0.25, 0.5, 0.75};

    Aura::Instrument::WavClipPlayer player;
    player.prepare(48000.0);
    player.setSource(&source);
    player.setGainPan(0.0, 0.0);
    player.play();

    std::vector<double> left(4, -1.0);
    std::vector<double> right(4, -1.0);
    double* outputs[] = {left.data(), right.data()};
    player.processBlock(nullptr, outputs, 0, 2, 4);

    constexpr double kCenterGain = 0.7071067811865475;
    EXPECT_NEAR(left[0], 0.25 * kCenterGain, 1.0e-7);
    EXPECT_NEAR(right[0], 0.25 * kCenterGain, 1.0e-7);
    EXPECT_NEAR(left[2], 0.75 * kCenterGain, 1.0e-7);
    EXPECT_DOUBLE_EQ(left[3], 0.0);
    EXPECT_DOUBLE_EQ(right[3], 0.0);
    EXPECT_FALSE(player.isPlaying());
    EXPECT_EQ(player.playheadSourceFrame(), 3U);
}

TEST(WavClipPlayer, AppliesGainPanAndSupportsSampleRateConversion) {
    Aura::Instrument::SampleData source;
    source.sampleRate = 48000.0;
    source.left = {0.0, 1.0, 2.0, 3.0};

    Aura::Instrument::WavClipPlayer player;
    player.prepare(96000.0);
    player.setSource(&source);
    player.setGainPan(-6.020599913, 1.0);
    player.play();

    std::vector<double> left(6, 0.0);
    std::vector<double> right(6, 0.0);
    double* outputs[] = {left.data(), right.data()};
    player.processBlock(nullptr, outputs, 0, 2, 6);

    EXPECT_NEAR(left[5], 0.0, 1.0e-7);
    EXPECT_NEAR(right[1], 0.25, 1.0e-7);
    EXPECT_NEAR(right[3], 0.75, 1.0e-7);
    EXPECT_NEAR(right[5], 1.25, 1.0e-7);
    EXPECT_EQ(player.playheadSourceFrame(), 3U);
}

TEST(WavClipPlayer, StopSilencesOutputAndPlaybackCanResume) {
    Aura::Instrument::SampleData source;
    source.sampleRate = 48000.0;
    source.left = {0.5, 0.5, 0.5};

    Aura::Instrument::WavClipPlayer player;
    player.prepare(48000.0);
    player.setSource(&source);
    player.play();

    std::vector<double> left(1, 0.0);
    std::vector<double> right(1, 0.0);
    double* outputs[] = {left.data(), right.data()};
    player.processBlock(nullptr, outputs, 0, 2, 1);
    const double firstSample = left[0];
    EXPECT_GT(firstSample, 0.0);
    EXPECT_EQ(player.timelinePositionFrames(), 1U);

    player.stop();
    left[0] = 99.0;
    right[0] = 99.0;
    player.processBlock(nullptr, outputs, 0, 2, 1);
    EXPECT_DOUBLE_EQ(left[0], 0.0);
    EXPECT_DOUBLE_EQ(right[0], 0.0);
    EXPECT_EQ(player.timelinePositionFrames(), 1U);

    player.play();
    player.processBlock(nullptr, outputs, 0, 2, 1);
    EXPECT_DOUBLE_EQ(left[0], firstSample);
    EXPECT_EQ(player.timelinePositionFrames(), 2U);
}

TEST(WavClipPlayer, SchedulesClipOnTimelineAndAppliesTransportSeek) {
    Aura::Instrument::SampleData source;
    source.sampleRate = 48000.0;
    source.left = {0.25, 0.5, 0.75};

    Aura::Instrument::WavClipPlayer player;
    player.prepare(48000.0);
    player.setSource(&source);
    ASSERT_TRUE(player.setTimelineRange(2, 3, 6));
    player.play();

    std::vector<double> firstLeft(3, -1.0);
    std::vector<double> firstRight(3, -1.0);
    double* firstOutputs[] = {firstLeft.data(), firstRight.data()};
    player.processBlock(nullptr, firstOutputs, 0, 2, 3);
    EXPECT_DOUBLE_EQ(firstLeft[0], 0.0);
    EXPECT_DOUBLE_EQ(firstLeft[1], 0.0);
    EXPECT_NEAR(firstLeft[2], 0.25 * 0.7071067811865475, 1.0e-7);
    EXPECT_EQ(player.timelinePositionFrames(), 3U);
    EXPECT_TRUE(player.isPlaying());

    player.seekTimelineFrame(4);
    std::vector<double> secondLeft(2, -1.0);
    std::vector<double> secondRight(2, -1.0);
    double* secondOutputs[] = {secondLeft.data(), secondRight.data()};
    player.processBlock(nullptr, secondOutputs, 0, 2, 2);
    EXPECT_NEAR(secondLeft[0], 0.75 * 0.7071067811865475, 1.0e-7);
    EXPECT_DOUBLE_EQ(secondLeft[1], 0.0);
    EXPECT_EQ(player.timelinePositionFrames(), 6U);
    EXPECT_FALSE(player.isPlaying());
}
