/// @file test_analyzer.cpp
/// @brief Unit tests for the AI music analyzer (BPM, key, levels).

#include <cmath>
#include <vector>

#include <gtest/gtest.h>

#include "Aura/MusicAnalyzer.hpp"

namespace {
constexpr double kTestRate = 8000.0;

std::vector<double> makeSine(double freq, double seconds) {
    const auto count = static_cast<std::size_t>(seconds * kTestRate);
    std::vector<double> out(count);
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = 0.5 * std::sin(2.0 * 3.141592653589793 * freq * i / kTestRate);
    }
    return out;
}

// Click track: short decaying bursts on every beat.
std::vector<double> makeClickTrack(double bpm, double seconds) {
    const auto count = static_cast<std::size_t>(seconds * kTestRate);
    std::vector<double> out(count, 0.0);
    const auto period = static_cast<std::size_t>(60.0 / bpm * kTestRate);
    for (std::size_t start = 0; start < count; start += period) {
        for (std::size_t i = 0; i < 64 && start + i < count; ++i) {
            out[start + i] += 0.9 * std::exp(-static_cast<double>(i) / 8.0) *
                              std::sin(2.0 * 3.141592653589793 * 1000.0 * i / kTestRate);
        }
    }
    return out;
}
} // namespace

// A-major triad: A3 + C#4 + E4 (unambiguous major key signature).
std::vector<double> makeAMajorTriad(double seconds) {
    const auto count = static_cast<std::size_t>(seconds * kTestRate);
    std::vector<double> out(count, 0.0);
    for (double freq : {220.0, 277.1826, 329.6276}) {
        for (std::size_t i = 0; i < count; ++i) {
            out[i] += 0.25 * std::sin(2.0 * 3.141592653589793 * freq * i / kTestRate);
        }
    }
    return out;
}

TEST(Analyzer, DetectsKeyOfPureTone) {
    Aura::Ai::MusicAnalyzer analyzer;
    const auto tone = makeSine(440.0, 2.0); // A4
    const auto levels = analyzer.analyzeMono(tone.data(), tone.size(), kTestRate);
    EXPECT_NEAR(levels.peakDb, -6.02, 0.5);
    EXPECT_NEAR(levels.rmsDb, -9.03, 0.5);
    EXPECT_DOUBLE_EQ(levels.durationSec, 2.0);

    const auto triad = makeAMajorTriad(2.0);
    const auto result = analyzer.analyzeMono(triad.data(), triad.size(), kTestRate);
    EXPECT_EQ(result.key, "A major");
    EXPECT_GT(result.keyConfidence, 0.3);
}

TEST(Analyzer, DetectsBpmOfClickTrack) {
    Aura::Ai::MusicAnalyzer analyzer;
    const auto clicks = makeClickTrack(120.0, 8.0);
    const auto result = analyzer.analyzeMono(clicks.data(), clicks.size(), kTestRate);
    EXPECT_NEAR(result.bpm, 120.0, 3.0);
    EXPECT_GT(result.bpmConfidence, 0.05);
}

TEST(Analyzer, SilenceGivesNoEstimates) {
    Aura::Ai::MusicAnalyzer analyzer;
    const std::vector<double> silence(16000, 0.0);
    const auto result = analyzer.analyzeMono(silence.data(), silence.size(), kTestRate);
    EXPECT_EQ(result.bpm, 0.0);
    EXPECT_EQ(result.peakDb, -120.0);
}

TEST(Analyzer, StereoMixesToMono) {
    Aura::Ai::MusicAnalyzer analyzer;
    const auto left = makeAMajorTriad(2.0);
    const auto right = makeAMajorTriad(2.0);
    const auto result = analyzer.analyzeStereo(left.data(), right.data(), left.size(), kTestRate);
    EXPECT_EQ(result.key, "A major");
}

TEST(Analyzer, ChromagramPeaksAtPitchClass) {
    Aura::Ai::MusicAnalyzer analyzer;
    const auto tone = makeSine(261.63, 2.0); // C4
    const auto chroma = analyzer.chromagram(tone.data(), tone.size(), kTestRate);
    // C pitch class (index 0) should dominate.
    EXPECT_GT(chroma[0], 0.9);
    for (int i = 1; i < 12; ++i) {
        EXPECT_LT(chroma[static_cast<std::size_t>(i)], chroma[0]);
    }
}
