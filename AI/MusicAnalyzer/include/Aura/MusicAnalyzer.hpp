#pragma once

/// @file MusicAnalyzer.hpp
/// @brief Offline music analysis: BPM, musical key, levels.
///
/// BPM detection uses an onset-envelope + autocorrelation approach over the
/// 60–200 BPM range. Key detection builds a Goertzel-based chromagram and
/// correlates it against Krumhansl-Schmuckler tonal profiles.

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace Aura::Ai {

/// @brief Result of analyzing an audio selection.
struct AnalysisResult {
    double bpm = 0.0;          ///< 0 when no confident estimate.
    double bpmConfidence = 0.0; ///< 0..1
    std::string key = "Unknown"; ///< e.g. "A minor", "F# major".
    double keyConfidence = 0.0;  ///< 0..1
    double peakDb = -120.0;
    double rmsDb = -120.0;
    double durationSec = 0.0;
};

/// @brief Offline analyzer (runs on a worker thread, not real-time).
class MusicAnalyzer {
public:
    [[nodiscard]] AnalysisResult analyzeMono(const double* samples, std::size_t count,
                                             double sampleRate) const;
    [[nodiscard]] AnalysisResult analyzeStereo(const double* left, const double* right,
                                               std::size_t count, double sampleRate) const;

    /// @brief 12-bin chroma vector (C..B), normalized to unit peak.
    [[nodiscard]] std::array<double, 12> chromagram(const double* samples, std::size_t count,
                                                    double sampleRate) const;

private:
    [[nodiscard]] double detectBpm(const std::vector<double>& envelope, double envelopeRate,
                                   double& confidenceOut) const;
    [[nodiscard]] std::string detectKey(const std::array<double, 12>& chroma,
                                        double& confidenceOut) const;
};

} // namespace Aura::Ai
