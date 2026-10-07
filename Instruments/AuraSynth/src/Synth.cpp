/// @file Synth.cpp
/// @brief Implementation of AURA Synth.

#include "Aura/Synth.hpp"

#include <algorithm>
#include <cmath>

#include "Aura/DSP.hpp"

namespace Aura::Instrument {

namespace {
constexpr double kPi = 3.14159265358979323846;

double dbToGain(double db) {
    return std::pow(10.0, db / 20.0);
}
} // namespace

double midiToFrequency(int midiNote) {
    return 440.0 * std::pow(2.0, (static_cast<double>(std::clamp(midiNote, 0, 127)) - 69.0) / 12.0);
}

// ------------------------------------------------------------------ Adsr

void Adsr::setParams(const AdsrParams& params) {
    params_.attack = std::max(0.0005, params.attack);
    params_.decay = std::max(0.001, params.decay);
    params_.sustain = std::clamp(params.sustain, 0.0, 1.0);
    params_.release = std::max(0.001, params.release);
}

void Adsr::setSampleRate(double sampleRate) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
}

void Adsr::noteOn() {
    stage_ = Stage::Attack;
}

void Adsr::noteOff() {
    if (stage_ != Stage::Idle) {
        stage_ = Stage::Release;
    }
}

bool Adsr::isActive() const {
    return stage_ != Stage::Idle;
}

double Adsr::next() {
    switch (stage_) {
    case Stage::Idle:
        level_ = 0.0;
        break;
    case Stage::Attack:
        level_ += 1.0 / (params_.attack * sampleRate_);
        if (level_ >= 1.0) {
            level_ = 1.0;
            stage_ = Stage::Decay;
        }
        break;
    case Stage::Decay: {
        const double coeff = 1.0 - std::exp(-1.0 / (params_.decay * sampleRate_));
        level_ += (params_.sustain - level_) * coeff;
        if (std::abs(level_ - params_.sustain) < 0.001) {
            level_ = params_.sustain;
            stage_ = Stage::Sustain;
        }
        break;
    }
    case Stage::Sustain:
        level_ = params_.sustain;
        break;
    case Stage::Release: {
        const double coeff = 1.0 - std::exp(-1.0 / (params_.release * sampleRate_));
        level_ += (0.0 - level_) * coeff;
        if (level_ < 0.0001) {
            level_ = 0.0;
            stage_ = Stage::Idle;
        }
        break;
    }
    }
    return level_;
}

void Adsr::reset() {
    stage_ = Stage::Idle;
    level_ = 0.0;
}

// ------------------------------------------------------------ Oscillator

void Oscillator::reset() {
    phase_ = 0.0;
}

double Oscillator::next() {
    const double inc = frequency_ / sampleRate_;
    phase_ += inc;
    if (phase_ >= 1.0) {
        phase_ -= 1.0;
    }
    switch (type_) {
    case OscillatorType::Sine:
        return std::sin(2.0 * kPi * phase_);
    case OscillatorType::Saw:
        return 2.0 * phase_ - 1.0;
    case OscillatorType::Square:
        return phase_ < 0.5 ? 1.0 : -1.0;
    case OscillatorType::Triangle:
        return 4.0 * std::abs(phase_ - 0.5) - 1.0;
    case OscillatorType::Noise:
        noiseSeed_ = noiseSeed_ * 1664525u + 1013904223u;
        return (static_cast<double>(noiseSeed_ >> 8) / 8388608.0) - 1.0;
    }
    return 0.0;
}

// ----------------------------------------------------------------- Synth

Synth::Synth(int numVoices) {
    voices_.resize(static_cast<std::size_t>(std::clamp(numVoices, 1, kMaxVoices)));
}

void Synth::setParams(const SynthParams& params) {
    params_ = params;
    params_.cutoffHz = std::clamp(params_.cutoffHz, 40.0, 20000.0);
    params_.resonance = std::clamp(params_.resonance, 0.5, 12.0);
    params_.lfoRateHz = std::clamp(params_.lfoRateHz, 0.01, 30.0);
    params_.lfoDepth = std::clamp(params_.lfoDepth, 0.0, 1.0);
    for (auto& v : voices_) {
        v.adsr.setParams(params_.adsr);
        v.osc.setType(params_.oscType);
    }
}

void Synth::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    for (auto& v : voices_) {
        v.osc.setSampleRate(sampleRate);
        v.adsr.setSampleRate(sampleRate);
        v.adsr.setParams(params_.adsr);
        v.filter.setType(Dsp::Biquad::Type::LowPass);
        v.filter.prepare(sampleRate, 1);
        v.filter.setParams(params_.cutoffHz, params_.resonance, 0.0);
    }
    lfoPhase_ = 0.0;
}

void Synth::noteOn(int midiNote, int velocity) {
    Voice& voice = stealVoice();
    voice.note = std::clamp(midiNote, 0, 127);
    voice.velocityGain = std::clamp(velocity, 1, 127) / 127.0;
    voice.osc.setFrequency(midiToFrequency(voice.note));
    voice.osc.reset();
    voice.adsr.noteOn();
    voice.active = true;
    voice.age = ++ageCounter_;
}

void Synth::noteOff(int midiNote) {
    for (auto& v : voices_) {
        if (v.active && v.note == midiNote) {
            v.adsr.noteOff();
        }
    }
}

void Synth::allNotesOff() {
    for (auto& v : voices_) {
        v.active = false;
        v.adsr.reset();
        v.filter.reset();
        v.note = -1;
    }
}

int Synth::activeVoiceCount() const {
    int count = 0;
    for (const auto& v : voices_) {
        if (v.active && v.adsr.isActive()) {
            ++count;
        }
    }
    return count;
}

Synth::Voice& Synth::stealVoice() {
    // Prefer a completely idle voice, otherwise steal the oldest.
    for (auto& v : voices_) {
        if (!v.active || !v.adsr.isActive()) {
            return v;
        }
    }
    Voice* oldest = &voices_.front();
    for (auto& v : voices_) {
        if (v.age < oldest->age) {
            oldest = &v;
        }
    }
    return *oldest;
}

void Synth::renderBlock(double* left, double* right, int numSamples) {
    const double masterGain = dbToGain(params_.masterGainDb);
    const double lfoInc = 2.0 * kPi * params_.lfoRateHz / sampleRate_;

    for (int i = 0; i < numSamples; ++i) {
        const double lfo = std::sin(lfoPhase_);
        lfoPhase_ += lfoInc;
        if (lfoPhase_ > 2.0 * kPi) {
            lfoPhase_ -= 2.0 * kPi;
        }

        double mixed = 0.0;
        for (auto& v : voices_) {
            if (!v.active) {
                continue;
            }
            const double env = v.adsr.next();
            if (!v.adsr.isActive()) {
                v.active = false;
                continue;
            }
            // Filter cutoff with LFO + velocity modulation (updated per block
            // would click less, but per-sample keeps vibrato sweeps smooth;
            // coefficient update is cheap relative to the oscillator).
            const double cutoffMod = 1.0 + params_.lfoDepth * 3.0 * lfo;
            const double velocityMod = 0.5 + 0.5 * v.velocityGain;
            const double cutoff = std::clamp(
                params_.cutoffHz * cutoffMod * (0.5 + 0.5 * velocityMod), 40.0, sampleRate_ * 0.49);
            if ((i & 31) == 0) { // amortize: refresh coeffs every 32 samples
                v.filter.setParams(cutoff, params_.resonance, 0.0);
            }
            const double sample = v.filter.processSample(v.osc.next() * env, 0);
            mixed += sample * (0.3 + 0.7 * v.velocityGain);
        }
        mixed *= masterGain / 2.0;
        left[i] += mixed;
        right[i] += mixed;
    }
}

} // namespace Aura::Instrument
