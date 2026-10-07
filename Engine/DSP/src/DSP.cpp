/// @file DSP.cpp
/// @brief Implementation of the AURA built-in DSP suite.

#include "Aura/DSP.hpp"

#include <algorithm>
#include <cmath>

namespace Aura::Dsp {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kMinDb = -120.0;

double clampFreq(double f, double sampleRate) {
    return std::clamp(f, 5.0, sampleRate * 0.49);
}
} // namespace

void Parameter::set(double v) {
    value = std::clamp(v, min, max);
}

double Parameter::normalized() const {
    if (max <= min) {
        return 0.0;
    }
    return (value - min) / (max - min);
}

double gainToDb(double gain) {
    if (gain <= 0.0) {
        return kMinDb;
    }
    return std::max(kMinDb, 20.0 * std::log10(gain));
}

// ---------------------------------------------------------------- Biquad

void Biquad::setType(Type type) {
    type_ = type;
    updateCoefficients();
}

void Biquad::setParams(double freqHz, double q, double gainDb) {
    freq_ = freqHz;
    q_ = q > 0.01 ? q : 0.01;
    gainDb_ = gainDb;
    updateCoefficients();
}

void Biquad::prepare(double sampleRate, int numChannels) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    states_.assign(static_cast<std::size_t>(std::max(1, numChannels)), State{});
    updateCoefficients();
}

void Biquad::reset() {
    for (auto& s : states_) {
        s = State{};
    }
}

double Biquad::processSample(double x, int channel) {
    State& s =
        states_[static_cast<std::size_t>(channel) % states_.size()];
    const double y = b0_ * x + b1_ * s.x1 + b2_ * s.x2 - a1_ * s.y1 - a2_ * s.y2;
    s.x2 = s.x1;
    s.x1 = x;
    s.y2 = s.y1;
    s.y1 = y;
    return y;
}

void Biquad::processBlock(double* const* channels, int numChannels, int numSamples) {
    for (int ch = 0; ch < numChannels; ++ch) {
        for (int i = 0; i < numSamples; ++i) {
            channels[ch][i] = processSample(channels[ch][i], ch);
        }
    }
}

void Biquad::updateCoefficients() {
    const double f0 = clampFreq(freq_, sampleRate_);
    const double w0 = 2.0 * kPi * f0 / sampleRate_;
    const double cosW0 = std::cos(w0);
    const double sinW0 = std::sin(w0);
    const double alpha = sinW0 / (2.0 * q_);
    const double A = std::pow(10.0, gainDb_ / 40.0);

    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;

    switch (type_) {
    case Type::LowPass:
        b0 = (1.0 - cosW0) / 2.0;
        b1 = 1.0 - cosW0;
        b2 = (1.0 - cosW0) / 2.0;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosW0;
        a2 = 1.0 - alpha;
        break;
    case Type::HighPass:
        b0 = (1.0 + cosW0) / 2.0;
        b1 = -(1.0 + cosW0);
        b2 = (1.0 + cosW0) / 2.0;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosW0;
        a2 = 1.0 - alpha;
        break;
    case Type::Peaking:
        b0 = 1.0 + alpha * A;
        b1 = -2.0 * cosW0;
        b2 = 1.0 - alpha * A;
        a0 = 1.0 + alpha / A;
        a1 = -2.0 * cosW0;
        a2 = 1.0 - alpha / A;
        break;
    case Type::LowShelf: {
        const double sqrtA = 2.0 * std::sqrt(A) * alpha;
        b0 = A * ((A + 1.0) - (A - 1.0) * cosW0 + sqrtA);
        b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosW0);
        b2 = A * ((A + 1.0) - (A - 1.0) * cosW0 - sqrtA);
        a0 = (A + 1.0) + (A - 1.0) * cosW0 + sqrtA;
        a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosW0);
        a2 = (A + 1.0) + (A - 1.0) * cosW0 - sqrtA;
        break;
    }
    case Type::HighShelf: {
        const double sqrtA = 2.0 * std::sqrt(A) * alpha;
        b0 = A * ((A + 1.0) + (A - 1.0) * cosW0 + sqrtA);
        b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosW0);
        b2 = A * ((A + 1.0) + (A - 1.0) * cosW0 - sqrtA);
        a0 = (A + 1.0) - (A - 1.0) * cosW0 + sqrtA;
        a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosW0);
        a2 = (A + 1.0) - (A - 1.0) * cosW0 - sqrtA;
        break;
    }
    case Type::Notch:
        b0 = 1.0;
        b1 = -2.0 * cosW0;
        b2 = 1.0;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosW0;
        a2 = 1.0 - alpha;
        break;
    }

    b0_ = b0 / a0;
    b1_ = b1 / a0;
    b2_ = b2 / a0;
    a1_ = a1 / a0;
    a2_ = a2 / a0;
}

// ---------------------------------------------------------- ParametricEQ

void ParametricEQ::prepare(double sampleRate, int numChannels) {
    // HPF, LF peak, LM peak, HM peak, HF peak, LPF.
    const std::array<Biquad::Type, kNumBands> types = {
        Biquad::Type::HighPass, Biquad::Type::Peaking, Biquad::Type::Peaking,
        Biquad::Type::Peaking, Biquad::Type::Peaking, Biquad::Type::LowPass};
    const std::array<double, kNumBands> freqs = {30.0, 120.0, 800.0, 2500.0, 8000.0, 18000.0};
    for (int i = 0; i < kNumBands; ++i) {
        bands_[static_cast<std::size_t>(i)].setType(types[static_cast<std::size_t>(i)]);
        bands_[static_cast<std::size_t>(i)].setParams(freqs[static_cast<std::size_t>(i)], 0.7071, 0.0);
        bands_[static_cast<std::size_t>(i)].prepare(sampleRate, numChannels);
    }
}

void ParametricEQ::reset() {
    for (auto& b : bands_) {
        b.reset();
    }
}

Biquad& ParametricEQ::band(int index) {
    return bands_[static_cast<std::size_t>(std::clamp(index, 0, kNumBands - 1))];
}

const Biquad& ParametricEQ::band(int index) const {
    return bands_[static_cast<std::size_t>(std::clamp(index, 0, kNumBands - 1))];
}

void ParametricEQ::processBlock(double* const* channels, int numChannels, int numSamples) {
    for (auto& b : bands_) {
        b.processBlock(channels, numChannels, numSamples);
    }
}

// ----------------------------------------------------------- Compressor

void Compressor::prepare(double sampleRate, int numChannels) {
    sampleRate_ = sampleRate;
    envelope_.assign(static_cast<std::size_t>(std::max(1, numChannels)), 0.0);
}

void Compressor::reset() {
    std::fill(envelope_.begin(), envelope_.end(), 0.0);
    gainReductionDb_ = 0.0;
}

void Compressor::processBlock(double* const* channels, int numChannels, int numSamples) {
    const double attackCoeff = 1.0 - std::exp(-1.0 / (attackMs_ * 0.001 * sampleRate_ + 1.0));
    const double releaseCoeff = 1.0 - std::exp(-1.0 / (releaseMs_ * 0.001 * sampleRate_ + 1.0));
    const double makeup = dbToGain(makeupDb_);
    double maxReduction = 0.0;

    for (int ch = 0; ch < numChannels; ++ch) {
        double& env = envelope_[static_cast<std::size_t>(ch) % envelope_.size()];
        for (int i = 0; i < numSamples; ++i) {
            const double input = std::abs(channels[ch][i]);
            const double coeff = input > env ? attackCoeff : releaseCoeff;
            env += coeff * (input - env);

            const double envDb = gainToDb(env);
            double overDb = envDb - thresholdDb_;
            // Soft knee.
            if (kneeDb_ > 0.0 && std::abs(overDb) < kneeDb_ / 2.0) {
                overDb = (overDb + kneeDb_ / 2.0) * (overDb + kneeDb_ / 2.0) / (2.0 * kneeDb_);
            } else if (overDb < 0.0) {
                overDb = 0.0;
            }
            const double reduction = overDb * (1.0 - 1.0 / ratio_);
            maxReduction = std::max(maxReduction, reduction);
            channels[ch][i] = channels[ch][i] * dbToGain(-reduction) * makeup;
        }
    }
    gainReductionDb_ = maxReduction;
}

// -------------------------------------------------------------- Limiter

void Limiter::prepare(double sampleRate, int numChannels) {
    sampleRate_ = sampleRate;
    envelope_.assign(static_cast<std::size_t>(std::max(1, numChannels)), 0.0);
}

void Limiter::reset() {
    std::fill(envelope_.begin(), envelope_.end(), 0.0);
}

void Limiter::processBlock(double* const* channels, int numChannels, int numSamples) {
    const double releaseCoeff = 1.0 - std::exp(-1.0 / (releaseMs_ * 0.001 * sampleRate_ + 1.0));
    const double ceiling = dbToGain(ceilingDb_);
    for (int ch = 0; ch < numChannels; ++ch) {
        double& env = envelope_[static_cast<std::size_t>(ch) % envelope_.size()];
        for (int i = 0; i < numSamples; ++i) {
            const double input = std::abs(channels[ch][i]);
            env = input > env ? input : env + releaseCoeff * (input - env);
            const double gain = env > ceiling ? ceiling / env : 1.0;
            channels[ch][i] = channels[ch][i] * gain;
        }
    }
}

// ------------------------------------------------------------------ Gate

void Gate::prepare(double sampleRate, int numChannels) {
    sampleRate_ = sampleRate;
    const auto n = static_cast<std::size_t>(std::max(1, numChannels));
    envelope_.assign(n, 0.0);
    gain_.assign(n, 0.0);
}

void Gate::reset() {
    std::fill(envelope_.begin(), envelope_.end(), 0.0);
    std::fill(gain_.begin(), gain_.end(), 0.0);
}

void Gate::processBlock(double* const* channels, int numChannels, int numSamples) {
    const double attackCoeff = 1.0 - std::exp(-1.0 / (attackMs_ * 0.001 * sampleRate_ + 1.0));
    const double releaseCoeff = 1.0 - std::exp(-1.0 / (releaseMs_ * 0.001 * sampleRate_ + 1.0));
    const double closedGain = dbToGain(-rangeDb_);
    for (int ch = 0; ch < numChannels; ++ch) {
        const auto idx = static_cast<std::size_t>(ch) % envelope_.size();
        double& env = envelope_[idx];
        double& gain = gain_[idx];
        for (int i = 0; i < numSamples; ++i) {
            const double input = std::abs(channels[ch][i]);
            const double envCoeff = input > env ? attackCoeff : releaseCoeff;
            env += envCoeff * (input - env);
            const double target = gainToDb(env) > thresholdDb_ ? 1.0 : closedGain;
            const double gainCoeff = target > gain ? attackCoeff : releaseCoeff;
            gain += gainCoeff * (target - gain);
            channels[ch][i] = channels[ch][i] * gain;
        }
    }
}

// ----------------------------------------------------------------- Delay

void Delay::setFeedback(double v) {
    feedback_ = std::clamp(v, 0.0, 0.95);
}

void Delay::setMix(double v) {
    mix_ = std::clamp(v, 0.0, 1.0);
}

void Delay::prepare(double sampleRate, int numChannels) {
    sampleRate_ = sampleRate;
    constexpr double kMaxDelaySec = 5.0;
    const auto len = static_cast<std::size_t>(kMaxDelaySec * sampleRate_) + 8;
    const auto n = static_cast<std::size_t>(std::max(1, numChannels));
    buffer_.assign(n, std::vector<double>(len, 0.0));
    writePos_.assign(n, 0);
}

void Delay::reset() {
    for (auto& buf : buffer_) {
        std::fill(buf.begin(), buf.end(), 0.0);
    }
    std::fill(writePos_.begin(), writePos_.end(), 0);
}

void Delay::processBlock(double* const* channels, int numChannels, int numSamples) {
    const double delaySamples = std::clamp(delayMs_ * 0.001 * sampleRate_, 1.0,
                                           static_cast<double>(buffer_.front().size() - 2));
    for (int ch = 0; ch < numChannels; ++ch) {
        const auto idx = static_cast<std::size_t>(ch) % buffer_.size();
        auto& buf = buffer_[idx];
        std::size_t& pos = writePos_[idx];
        const double len = static_cast<double>(buf.size());
        for (int i = 0; i < numSamples; ++i) {
            double readPos = static_cast<double>(pos) - delaySamples;
            while (readPos < 0.0) {
                readPos += len;
            }
            const auto i0 = static_cast<std::size_t>(readPos) % buf.size();
            const auto i1 = (i0 + 1) % buf.size();
            const double frac = readPos - std::floor(readPos);
            const double delayed = buf[i0] * (1.0 - frac) + buf[i1] * frac;

            const double input = channels[ch][i];
            buf[pos] = input + delayed * feedback_;
            pos = (pos + 1) % buf.size();
            channels[ch][i] = input * (1.0 - mix_) + delayed * mix_;
        }
    }
}

// ---------------------------------------------------------------- Reverb

void Reverb::setRoomSize(double v) {
    roomSize_ = std::clamp(v, 0.0, 1.0);
}

void Reverb::setDamping(double v) {
    damping_ = std::clamp(v, 0.0, 1.0);
}

void Reverb::setMix(double v) {
    mix_ = std::clamp(v, 0.0, 1.0);
}

void Reverb::prepare(double sampleRate, int numChannels) {
    sampleRate_ = sampleRate;
    // Classic Schroeder lengths (ms), slightly spread per channel.
    const std::array<double, 4> combMs = {29.7, 37.1, 41.1, 43.7};
    const std::array<double, 2> apMs = {5.0, 1.7};
    const auto n = static_cast<std::size_t>(std::max(1, numChannels));
    combs_.clear();
    allPasses_.clear();
    combs_.resize(n);
    allPasses_.resize(n);
    for (std::size_t ch = 0; ch < n; ++ch) {
        const double spread = 1.0 + 0.01 * static_cast<double>(ch);
        for (std::size_t c = 0; c < 4; ++c) {
            const auto len =
                static_cast<std::size_t>(combMs[c] * spread * 0.001 * sampleRate_) + 1;
            combs_[ch][c].buffer.assign(len, 0.0);
            combs_[ch][c].pos = 0;
            combs_[ch][c].filtered = 0.0;
        }
        for (std::size_t a = 0; a < 2; ++a) {
            const auto len = static_cast<std::size_t>(apMs[a] * spread * 0.001 * sampleRate_) + 1;
            allPasses_[ch][a].buffer.assign(len, 0.0);
            allPasses_[ch][a].pos = 0;
        }
    }
}

void Reverb::reset() {
    for (auto& perChannel : combs_) {
        for (auto& c : perChannel) {
            std::fill(c.buffer.begin(), c.buffer.end(), 0.0);
            c.pos = 0;
            c.filtered = 0.0;
        }
    }
    for (auto& perChannel : allPasses_) {
        for (auto& a : perChannel) {
            std::fill(a.buffer.begin(), a.buffer.end(), 0.0);
            a.pos = 0;
        }
    }
}

void Reverb::processBlock(double* const* channels, int numChannels, int numSamples) {
    const double feedback = 0.24 + roomSize_ * 0.7;
    for (int ch = 0; ch < numChannels; ++ch) {
        const auto idx = static_cast<std::size_t>(ch) % combs_.size();
        for (int i = 0; i < numSamples; ++i) {
            const double input = channels[ch][i];
            double acc = 0.0;
            for (auto& comb : combs_[idx]) {
                const double delayed = comb.buffer[comb.pos];
                comb.filtered = delayed * (1.0 - damping_) + comb.filtered * damping_;
                comb.buffer[comb.pos] = input + comb.filtered * feedback;
                comb.pos = (comb.pos + 1) % comb.buffer.size();
                acc += delayed;
            }
            acc *= 0.25;
            for (auto& ap : allPasses_[idx]) {
                const double delayed = ap.buffer[ap.pos];
                const double out = -acc + delayed;
                ap.buffer[ap.pos] = acc + delayed * 0.5;
                ap.pos = (ap.pos + 1) % ap.buffer.size();
                acc = out;
            }
            channels[ch][i] = input * (1.0 - mix_) + acc * mix_;
        }
    }
}

// -------------------------------------------------------- ModulatedDelay

void ModulatedDelay::prepare(double sampleRate, int numChannels) {
    sampleRate_ = sampleRate;
    const double maxMs = baseMs_ + depthMs_ + 5.0;
    const auto len = static_cast<std::size_t>(maxMs * 0.001 * sampleRate_) + 8;
    const auto n = static_cast<std::size_t>(std::max(1, numChannels));
    buffer_.assign(n, std::vector<double>(len, 0.0));
    writePos_.assign(n, 0);
}

void ModulatedDelay::reset() {
    for (auto& buf : buffer_) {
        std::fill(buf.begin(), buf.end(), 0.0);
    }
    std::fill(writePos_.begin(), writePos_.end(), 0);
    phase_ = 0.0;
}

void ModulatedDelay::processBlock(double* const* channels, int numChannels, int numSamples) {
    const double phaseInc = 2.0 * kPi * rateHz_ / sampleRate_;
    const double len = static_cast<double>(buffer_.front().size());
    for (int i = 0; i < numSamples; ++i) {
        const double lfo = std::sin(phase_);
        phase_ += phaseInc;
        if (phase_ > 2.0 * kPi) {
            phase_ -= 2.0 * kPi;
        }
        const double delaySamples =
            std::clamp((baseMs_ + depthMs_ * lfo) * 0.001 * sampleRate_, 1.0, len - 2.0);
        for (int ch = 0; ch < numChannels; ++ch) {
            const auto idx = static_cast<std::size_t>(ch) % buffer_.size();
            auto& buf = buffer_[idx];
            std::size_t& pos = writePos_[idx];
            double readPos = static_cast<double>(pos) - delaySamples;
            while (readPos < 0.0) {
                readPos += len;
            }
            const auto i0 = static_cast<std::size_t>(readPos) % buf.size();
            const auto i1 = (i0 + 1) % buf.size();
            const double frac = readPos - std::floor(readPos);
            const double delayed = buf[i0] * (1.0 - frac) + buf[i1] * frac;
            const double input = channels[ch][i];
            buf[pos] = input + delayed * feedback_;
            pos = (pos + 1) % buf.size();
            channels[ch][i] = input * (1.0 - mix_) + delayed * mix_;
        }
    }
}

Chorus::Chorus() {
    setBaseDelayMs(22.0);
    setDepthMs(6.0);
    setRateHz(0.8);
    setFeedback(0.0);
    setMix(0.4);
}

Flanger::Flanger() {
    setBaseDelayMs(3.0);
    setDepthMs(2.5);
    setRateHz(0.4);
    setFeedback(0.7);
    setMix(0.5);
}

// ------------------------------------------------------------ Distortion

void Distortion::setDrive(double v) {
    drive_ = std::clamp(v, 1.0, 100.0);
}

void Distortion::setTone(double v) {
    tone_ = std::clamp(v, 0.0, 1.0);
}

void Distortion::setMix(double v) {
    mix_ = std::clamp(v, 0.0, 1.0);
}

void Distortion::prepare(double sampleRate, int numChannels) {
    (void)sampleRate;
    toneState_.assign(static_cast<std::size_t>(std::max(1, numChannels)), 0.0);
}

void Distortion::reset() {
    std::fill(toneState_.begin(), toneState_.end(), 0.0);
}

void Distortion::processBlock(double* const* channels, int numChannels, int numSamples) {
    const double toneCoeff = 0.02 + tone_ * 0.5;
    for (int ch = 0; ch < numChannels; ++ch) {
        double& tone = toneState_[static_cast<std::size_t>(ch) % toneState_.size()];
        for (int i = 0; i < numSamples; ++i) {
            const double input = channels[ch][i];
            const double shaped = std::tanh(drive_ * input) / std::tanh(drive_);
            tone += toneCoeff * (shaped - tone);
            channels[ch][i] = input * (1.0 - mix_) + tone * mix_;
        }
    }
}

} // namespace Aura::Dsp
