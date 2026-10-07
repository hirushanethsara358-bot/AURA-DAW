#pragma once

/// @file Synth.hpp
/// @brief AURA Synth: polyphonic subtractive virtual instrument.
///
/// Architecture per voice: oscillator (sine/saw/square/triangle/noise) →
/// resonant low-pass filter (LFO + velocity modulated) → ADSR → master.
/// Real-time safe after prepare().

#include <array>
#include <cstdint>
#include <vector>

#include "Aura/DSP.hpp"

namespace Aura::Instrument {

/// @brief Oscillator waveform.
enum class OscillatorType {
    Sine,
    Saw,
    Square,
    Triangle,
    Noise
};

/// @brief ADSR envelope stages in seconds (sustain = level 0..1).
struct AdsrParams {
    double attack = 0.01;
    double decay = 0.1;
    double sustain = 0.8;
    double release = 0.2;
};

/// @brief Sample-accurate ADSR envelope generator.
class Adsr {
public:
    void setParams(const AdsrParams& params);
    void setSampleRate(double sampleRate);
    void noteOn();
    void noteOff();
    [[nodiscard]] bool isActive() const;
    double next();
    void reset();

private:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    AdsrParams params_;
    double sampleRate_ = 48000.0;
    Stage stage_ = Stage::Idle;
    double level_ = 0.0;
};

/// @brief Single oscillator with phase-continuous waveforms.
class Oscillator {
public:
    void setType(OscillatorType type) { type_ = type; }
    void setFrequency(double freqHz) { frequency_ = freqHz > 0.0 ? freqHz : 440.0; }
    void setSampleRate(double sampleRate) { sampleRate_ = sampleRate; }
    void reset();
    double next();

private:
    OscillatorType type_ = OscillatorType::Saw;
    double frequency_ = 440.0;
    double sampleRate_ = 48000.0;
    double phase_ = 0.0;
    std::uint32_t noiseSeed_ = 22222;
};

/// @brief Global synth settings (shared by all voices).
struct SynthParams {
    OscillatorType oscType = OscillatorType::Saw;
    double cutoffHz = 4000.0;
    double resonance = 0.7; ///< Filter Q 0.5..12.
    double lfoRateHz = 4.0;
    double lfoDepth = 0.0; ///< 0..1 filter-modulation depth.
    double masterGainDb = -6.0;
    AdsrParams adsr;
};

/// @brief Polyphonic subtractive synthesizer with voice stealing.
class Synth {
public:
    static constexpr int kDefaultVoices = 8;
    static constexpr int kMaxVoices = 32;

    explicit Synth(int numVoices = kDefaultVoices);

    void setParams(const SynthParams& params);
    [[nodiscard]] const SynthParams& params() const { return params_; }

    void prepare(double sampleRate);
    void noteOn(int midiNote, int velocity);
    void noteOff(int midiNote);
    void allNotesOff();
    [[nodiscard]] int activeVoiceCount() const;

    /// @brief Renders stereo output (adds into the buffers).
    void renderBlock(double* left, double* right, int numSamples);

private:
    struct Voice {
        Oscillator osc;
        Adsr adsr;
        Dsp::Biquad filter;
        int note = -1;
        double velocityGain = 0.0;
        std::uint64_t age = 0;
        bool active = false;
    };

    Voice& stealVoice();

    SynthParams params_;
    double sampleRate_ = 48000.0;
    std::vector<Voice> voices_;
    std::uint64_t ageCounter_ = 0;
    double lfoPhase_ = 0.0;
};

/// @brief Converts a MIDI note number to frequency in Hz.
[[nodiscard]] double midiToFrequency(int midiNote);

} // namespace Aura::Instrument
