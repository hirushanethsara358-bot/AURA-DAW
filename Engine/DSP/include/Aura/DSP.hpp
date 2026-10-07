#pragma once

/// @file DSP.hpp
/// @brief Built-in DSP suite: filters, EQ, dynamics and creative effects.
///
/// All processors work in 64-bit float, are real-time safe after prepare(),
/// and process interleaved channel arrays in place.

#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace Aura::Dsp {

/// @brief A single automatable parameter with range and default.
struct Parameter {
    std::string id;
    std::string name;
    double value = 0.0;
    double min = 0.0;
    double max = 1.0;
    double defaultValue = 0.0;

    void set(double v);
    [[nodiscard]] double normalized() const;
};

// ---------------------------------------------------------------- Biquad

/// @brief RBJ cookbook biquad filter (mono state per channel).
class Biquad {
public:
    enum class Type { LowPass, HighPass, Peaking, LowShelf, HighShelf, Notch };

    void setType(Type type);
    /// @param freqHz  Center/cutoff frequency in Hz.
    /// @param q       Quality factor (> 0).
    /// @param gainDb  Gain for shelving/peaking types.
    void setParams(double freqHz, double q, double gainDb = 0.0);
    void prepare(double sampleRate, int numChannels);
    void reset();
    double processSample(double x, int channel);
    void processBlock(double* const* channels, int numChannels, int numSamples);

private:
    void updateCoefficients();

    Type type_ = Type::Peaking;
    double freq_ = 1000.0;
    double q_ = 0.7071;
    double gainDb_ = 0.0;
    double sampleRate_ = 48000.0;

    double b0_ = 1.0, b1_ = 0.0, b2_ = 0.0;
    double a1_ = 0.0, a2_ = 0.0;

    struct State {
        double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
    };
    std::vector<State> states_;
};

// ---------------------------------------------------------- ParametricEQ

/// @brief 6-band parametric EQ: HPF + LPF + 4 peaking bands.
class ParametricEQ {
public:
    static constexpr int kNumBands = 6;

    void prepare(double sampleRate, int numChannels);
    void reset();
    Biquad& band(int index);
    [[nodiscard]] const Biquad& band(int index) const;
    void processBlock(double* const* channels, int numChannels, int numSamples);

private:
    std::array<Biquad, kNumBands> bands_;
};

// ----------------------------------------------------------- Compressor

/// @brief Feed-forward peak compressor with soft knee.
class Compressor {
public:
    void setThresholdDb(double v) { thresholdDb_ = v; }
    void setRatio(double v) { ratio_ = v < 1.0 ? 1.0 : v; }
    void setAttackMs(double v) { attackMs_ = v; }
    void setReleaseMs(double v) { releaseMs_ = v; }
    void setKneeDb(double v) { kneeDb_ = v; }
    void setMakeupGainDb(double v) { makeupDb_ = v; }

    void prepare(double sampleRate, int numChannels);
    void reset();
    void processBlock(double* const* channels, int numChannels, int numSamples);
    /// @brief Current gain reduction in dB (positive number).
    [[nodiscard]] double gainReductionDb() const { return gainReductionDb_; }

private:
    double thresholdDb_ = -18.0;
    double ratio_ = 4.0;
    double attackMs_ = 10.0;
    double releaseMs_ = 120.0;
    double kneeDb_ = 6.0;
    double makeupDb_ = 0.0;
    double sampleRate_ = 48000.0;
    std::vector<double> envelope_;
    double gainReductionDb_ = 0.0;
};

// -------------------------------------------------------------- Limiter

/// @brief Brick-wall limiter (fast attack, high ratio, output ceiling).
class Limiter {
public:
    void setCeilingDb(double v) { ceilingDb_ = v; }
    void setReleaseMs(double v) { releaseMs_ = v; }
    void prepare(double sampleRate, int numChannels);
    void reset();
    void processBlock(double* const* channels, int numChannels, int numSamples);

private:
    double ceilingDb_ = -0.5;
    double releaseMs_ = 80.0;
    double sampleRate_ = 48000.0;
    std::vector<double> envelope_;
};

// ------------------------------------------------------------------ Gate

/// @brief Downward-expansion noise gate.
class Gate {
public:
    void setThresholdDb(double v) { thresholdDb_ = v; }
    void setRangeDb(double v) { rangeDb_ = v; }
    void setAttackMs(double v) { attackMs_ = v; }
    void setReleaseMs(double v) { releaseMs_ = v; }
    void prepare(double sampleRate, int numChannels);
    void reset();
    void processBlock(double* const* channels, int numChannels, int numSamples);

private:
    double thresholdDb_ = -48.0;
    double rangeDb_ = 80.0;
    double attackMs_ = 1.0;
    double releaseMs_ = 100.0;
    double sampleRate_ = 48000.0;
    std::vector<double> envelope_;
    std::vector<double> gain_;
};

// ----------------------------------------------------------------- Delay

/// @brief Stereo tempo-syncable delay line with feedback and damping.
class Delay {
public:
    void setDelayMs(double v) { delayMs_ = v; }
    void setFeedback(double v);
    void setMix(double v);
    void prepare(double sampleRate, int numChannels);
    void reset();
    void processBlock(double* const* channels, int numChannels, int numSamples);

private:
    double delayMs_ = 250.0;
    double feedback_ = 0.35;
    double mix_ = 0.25;
    double sampleRate_ = 48000.0;
    std::vector<std::vector<double>> buffer_;
    std::vector<std::size_t> writePos_;
};

// ---------------------------------------------------------------- Reverb

/// @brief Schroeder-style algorithmic reverb (4 combs + 2 all-passes).
class Reverb {
public:
    void setRoomSize(double v); ///< 0..1
    void setDamping(double v);  ///< 0..1
    void setMix(double v);      ///< 0..1
    void prepare(double sampleRate, int numChannels);
    void reset();
    void processBlock(double* const* channels, int numChannels, int numSamples);

private:
    struct Comb {
        std::vector<double> buffer;
        std::size_t pos = 0;
        double filtered = 0.0;
    };
    struct AllPass {
        std::vector<double> buffer;
        std::size_t pos = 0;
    };

    double roomSize_ = 0.6;
    double damping_ = 0.4;
    double mix_ = 0.2;
    double sampleRate_ = 48000.0;
    std::vector<std::array<Comb, 4>> combs_;
    std::vector<std::array<AllPass, 2>> allPasses_;
};

// -------------------------------------------------------- ModulatedDelay

/// @brief Shared LFO-modulated delay core for chorus and flanger.
class ModulatedDelay {
public:
    void setBaseDelayMs(double v) { baseMs_ = v; }
    void setDepthMs(double v) { depthMs_ = v; }
    void setRateHz(double v) { rateHz_ = v; }
    void setFeedback(double v) { feedback_ = v; }
    void setMix(double v) { mix_ = v; }
    void prepare(double sampleRate, int numChannels);
    void reset();
    void processBlock(double* const* channels, int numChannels, int numSamples);

private:
    double baseMs_ = 20.0;
    double depthMs_ = 5.0;
    double rateHz_ = 0.8;
    double feedback_ = 0.0;
    double mix_ = 0.4;
    double sampleRate_ = 48000.0;
    double phase_ = 0.0;
    std::vector<std::vector<double>> buffer_;
    std::vector<std::size_t> writePos_;
};

/// @brief Chorus: slow, deep modulated delay, no feedback.
class Chorus : public ModulatedDelay {
public:
    Chorus();
};

/// @brief Flanger: short modulated delay with high feedback.
class Flanger : public ModulatedDelay {
public:
    Flanger();
};

// ------------------------------------------------------------ Distortion

/// @brief Soft-clipping distortion with tone control.
class Distortion {
public:
    void setDrive(double v); ///< 1..100
    void setTone(double v);  ///< 0..1 (low-pass brightness)
    void setMix(double v);   ///< 0..1
    void prepare(double sampleRate, int numChannels);
    void reset();
    void processBlock(double* const* channels, int numChannels, int numSamples);

private:
    double drive_ = 10.0;
    double tone_ = 0.7;
    double mix_ = 0.8;
    std::vector<double> toneState_;
};

/// @brief Converts decibels to linear gain.
[[nodiscard]] inline double dbToGain(double db) {
    return std::pow(10.0, db / 20.0);
}

/// @brief Converts linear amplitude to decibels (floored at -120 dB).
[[nodiscard]] double gainToDb(double gain);

} // namespace Aura::Dsp
