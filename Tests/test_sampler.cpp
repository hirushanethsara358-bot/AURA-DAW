/// @file test_sampler.cpp
/// @brief Unit tests for AURA Sampler.

#include <cmath>
#include <vector>

#include <gtest/gtest.h>

#include "Aura/Sampler.hpp"

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
    const auto result =
        Aura::Instrument::parseWavFile("/nonexistent/path/file.wav", data);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.empty());
}
