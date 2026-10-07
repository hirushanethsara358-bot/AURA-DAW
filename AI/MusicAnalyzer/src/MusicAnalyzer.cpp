/// @file MusicAnalyzer.cpp
/// @brief Implementation of BPM and key detection.

#include "Aura/MusicAnalyzer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace Aura::Ai {

namespace {
constexpr double kMinBpm = 60.0;
constexpr double kMaxBpm = 200.0;
constexpr double kEnvelopeRate = 200.0; // Hz after downsampling.

double amplitudeToDb(double amp) {
    if (amp <= 1e-9) {
        return -120.0;
    }
    return std::max(-120.0, 20.0 * std::log10(amp));
}

// Goertzel magnitude for a single target frequency.
double goertzelMagnitude(const double* samples, std::size_t count, double sampleRate,
                         double targetFreq) {
    const double normalized = targetFreq / sampleRate;
    const double omega = 2.0 * 3.14159265358979323846 * normalized;
    const double cosine = std::cos(omega);
    const double coeff = 2.0 * cosine;
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    // Process in blocks to avoid float drift on long signals.
    constexpr std::size_t kBlock = 4096;
    double power = 0.0;
    std::size_t processed = 0;
    while (processed < count) {
        const std::size_t n = std::min(kBlock, count - processed);
        s0 = s1 = s2 = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const double x = samples[processed + i];
            s0 = x + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        power += s1 * s1 + s2 * s2 - coeff * s1 * s2;
        processed += n;
    }
    return std::sqrt(std::max(0.0, power));
}
} // namespace

AnalysisResult MusicAnalyzer::analyzeMono(const double* samples, std::size_t count,
                                          double sampleRate) const {
    AnalysisResult result;
    if (samples == nullptr || count == 0 || sampleRate <= 0.0) {
        return result;
    }
    result.durationSec = static_cast<double>(count) / sampleRate;

    double peak = 0.0;
    double sumSquares = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double abs = std::abs(samples[i]);
        peak = std::max(peak, abs);
        sumSquares += samples[i] * samples[i];
    }
    result.peakDb = amplitudeToDb(peak);
    result.rmsDb = amplitudeToDb(std::sqrt(sumSquares / static_cast<double>(count)));

    // Envelope follower downsampled to kEnvelopeRate.
    const auto hop = static_cast<std::size_t>(std::max(1.0, sampleRate / kEnvelopeRate));
    std::vector<double> envelope;
    envelope.reserve(count / hop + 1);
    double env = 0.0;
    const double attack = 1.0 - std::exp(-1.0 / (0.001 * sampleRate));
    const double release = 1.0 - std::exp(-1.0 / (0.05 * sampleRate));
    for (std::size_t i = 0; i < count; ++i) {
        const double x = std::abs(samples[i]);
        env += ((x > env) ? attack : release) * (x - env);
        if (i % hop == 0) {
            envelope.push_back(env);
        }
    }

    double bpmConf = 0.0;
    result.bpm = detectBpm(envelope, sampleRate / static_cast<double>(hop), bpmConf);
    result.bpmConfidence = bpmConf;

    double keyConf = 0.0;
    const auto chroma = chromagram(samples, count, sampleRate);
    result.key = detectKey(chroma, keyConf);
    result.keyConfidence = keyConf;
    return result;
}

AnalysisResult MusicAnalyzer::analyzeStereo(const double* left, const double* right,
                                            std::size_t count, double sampleRate) const {
    if (left == nullptr || right == nullptr) {
        return {};
    }
    std::vector<double> mono(count);
    for (std::size_t i = 0; i < count; ++i) {
        mono[i] = 0.5 * (left[i] + right[i]);
    }
    return analyzeMono(mono.data(), mono.size(), sampleRate);
}

std::array<double, 12> MusicAnalyzer::chromagram(const double* samples, std::size_t count,
                                                 double sampleRate) const {
    std::array<double, 12> chroma{};
    chroma.fill(0.0);
    if (samples == nullptr || count < 64 || sampleRate <= 0.0) {
        return chroma;
    }
    // MIDI notes C2 (36) .. B5 (83): 4 octaves of pitch classes.
    for (int midi = 36; midi <= 83; ++midi) {
        const double freq = 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
        if (freq >= sampleRate * 0.49) {
            continue;
        }
        const double mag = goertzelMagnitude(samples, count, sampleRate, freq);
        chroma[static_cast<std::size_t>(midi % 12)] += mag;
    }
    const double peak = *std::max_element(chroma.begin(), chroma.end());
    if (peak > 0.0) {
        for (double& v : chroma) {
            v /= peak;
        }
    }
    return chroma;
}

double MusicAnalyzer::detectBpm(const std::vector<double>& envelope, double envelopeRate,
                                double& confidenceOut) const {
    confidenceOut = 0.0;
    if (envelope.size() < static_cast<std::size_t>(envelopeRate * 2.0) || envelopeRate <= 0.0) {
        return 0.0; // need at least ~2 seconds
    }
    // Remove DC.
    const double mean = std::accumulate(envelope.begin(), envelope.end(), 0.0) / envelope.size();
    std::vector<double> centered(envelope.size());
    for (std::size_t i = 0; i < envelope.size(); ++i) {
        centered[i] = envelope[i] - mean;
    }
    const double energy =
        std::inner_product(centered.begin(), centered.end(), centered.begin(), 0.0);
    if (energy <= 1e-12) {
        return 0.0;
    }

    const auto minLag = static_cast<std::size_t>(envelopeRate * 60.0 / kMaxBpm);
    const auto maxLag = static_cast<std::size_t>(envelopeRate * 60.0 / kMinBpm);

    double bestScore = 0.0;
    std::size_t bestLag = 0;
    for (std::size_t lag = minLag; lag <= maxLag && lag < centered.size(); ++lag) {
        double corr = 0.0;
        for (std::size_t i = 0; i + lag < centered.size(); ++i) {
            corr += centered[i] * centered[i + lag];
        }
        corr /= energy;
        // Slight preference for slower (non-doubled) tempi.
        corr *= 1.0 + 0.02 * (static_cast<double>(lag - minLag) / (maxLag - minLag + 1));
        if (corr > bestScore) {
            bestScore = corr;
            bestLag = lag;
        }
    }
    if (bestLag == 0 || bestScore < 0.05) {
        return 0.0;
    }
    confidenceOut = std::clamp(bestScore, 0.0, 1.0);
    return 60.0 * envelopeRate / static_cast<double>(bestLag);
}

std::string MusicAnalyzer::detectKey(const std::array<double, 12>& chroma,
                                     double& confidenceOut) const {
    // Krumhansl-Schmuckler key profiles.
    static constexpr std::array<double, 12> major = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09,
                                                     2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
    static constexpr std::array<double, 12> minor = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53,
                                                     2.54, 4.75, 3.98, 2.69, 3.34, 3.17};
    static constexpr std::array<const char*, 12> names = {"C",  "C#", "D",  "D#", "E",  "F",
                                                          "F#", "G",  "G#", "A",  "A#", "B"};

    const double chromaMean = std::accumulate(chroma.begin(), chroma.end(), 0.0) / 12.0;
    double bestScore = -2.0;
    int bestRoot = 0;
    bool bestMinor = false;

    for (int root = 0; root < 12; ++root) {
        for (int mode = 0; mode < 2; ++mode) {
            const auto& profile = (mode == 0) ? major : minor;
            const double profMean = std::accumulate(profile.begin(), profile.end(), 0.0) / 12.0;
            double num = 0.0, denA = 0.0, denB = 0.0;
            for (int i = 0; i < 12; ++i) {
                const double a = chroma[static_cast<std::size_t>(i)] - chromaMean;
                const double b = profile[static_cast<std::size_t>((i - root + 12) % 12)] - profMean;
                num += a * b;
                denA += a * a;
                denB += b * b;
            }
            const double denom = std::sqrt(denA * denB);
            const double corr = denom > 1e-9 ? num / denom : 0.0;
            if (corr > bestScore) {
                bestScore = corr;
                bestRoot = root;
                bestMinor = (mode == 1);
            }
        }
    }
    confidenceOut = std::clamp((bestScore + 1.0) / 2.0, 0.0, 1.0);
    if (bestScore < 0.3) {
        return "Unknown";
    }
    return std::string(names[static_cast<std::size_t>(bestRoot)]) +
           (bestMinor ? " minor" : " major");
}

} // namespace Aura::Ai
