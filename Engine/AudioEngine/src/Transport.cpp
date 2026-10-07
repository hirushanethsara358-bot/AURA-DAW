/// @file Transport.cpp
/// @brief Implementation of transport state and musical position.

#include "Aura/Transport.hpp"

#include <algorithm>
#include <cmath>

namespace Aura::Transport {

Transport::Transport(double sampleRate) : sampleRate_(sampleRate) {}

void Transport::setSampleRate(double sampleRate) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (sampleRate > 0.0) {
        sampleRate_ = sampleRate;
    }
}

double Transport::sampleRate() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sampleRate_;
}

void Transport::play() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = TransportState::Playing;
}

void Transport::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = TransportState::Stopped;
}

void Transport::record() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = TransportState::Recording;
}

TransportState Transport::state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

void Transport::advance(std::int64_t numSamples) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == TransportState::Stopped || numSamples <= 0) {
        return;
    }
    positionSamples_ += numSamples;

    if (loopEnabled_ && loopEndBar_ > loopStartBar_) {
        const double beatsPerBar = (4.0 * timeSigNum_) / timeSigDen_;
        const double samplesPerBeat = sampleRate_ * secondsPerBeat();
        const auto loopStart = static_cast<std::int64_t>(loopStartBar_ * beatsPerBar * samplesPerBeat);
        const auto loopEnd = static_cast<std::int64_t>(loopEndBar_ * beatsPerBar * samplesPerBeat);
        if (positionSamples_ >= loopEnd) {
            positionSamples_ = loopStart + ((positionSamples_ - loopStart) % (loopEnd - loopStart));
        }
    }
}

void Transport::seekSamples(std::int64_t samples) {
    std::lock_guard<std::mutex> lock(mutex_);
    positionSamples_ = std::max<std::int64_t>(0, samples);
}

void Transport::seekBars(double bars) {
    std::lock_guard<std::mutex> lock(mutex_);
    const double beatsPerBar = (4.0 * timeSigNum_) / timeSigDen_;
    positionSamples_ = static_cast<std::int64_t>(std::max(0.0, bars) * beatsPerBar * sampleRate_ *
                                                 secondsPerBeat());
}

std::int64_t Transport::positionSamples() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return positionSamples_;
}

double Transport::positionSeconds() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<double>(positionSamples_) / sampleRate_;
}

double Transport::positionBeats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<double>(positionSamples_) / (sampleRate_ * secondsPerBeat());
}

double Transport::positionBars() const {
    std::lock_guard<std::mutex> lock(mutex_);
    const double beatsPerBar = (4.0 * timeSigNum_) / timeSigDen_;
    return static_cast<double>(positionSamples_) / (sampleRate_ * secondsPerBeat() * beatsPerBar);
}

void Transport::setTempo(double bpm) {
    std::lock_guard<std::mutex> lock(mutex_);
    tempo_ = std::clamp(bpm, 20.0, 999.0);
}

double Transport::tempo() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tempo_;
}

void Transport::setTempoMap(std::vector<TempoMarker> markers) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!markers.empty()) {
        std::sort(markers.begin(), markers.end(),
                  [](const TempoMarker& a, const TempoMarker& b) { return a.bar < b.bar; });
        tempoMap_ = std::move(markers);
        tempo_ = tempoMap_.front().bpm;
    }
}

void Transport::setTimeSignature(int numerator, int denominator) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (numerator > 0 && (denominator == 2 || denominator == 4 || denominator == 8 ||
                          denominator == 16)) {
        timeSigNum_ = numerator;
        timeSigDen_ = denominator;
    }
}

void Transport::setLoop(bool enabled, double startBar, double endBar) {
    std::lock_guard<std::mutex> lock(mutex_);
    loopEnabled_ = enabled;
    loopStartBar_ = std::max(0.0, startBar);
    loopEndBar_ = std::max(loopStartBar_, endBar);
}

void Transport::setMetronome(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    metronome_ = enabled;
}

bool Transport::metronome() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return metronome_;
}

double Transport::secondsPerBeat() const {
    return 60.0 / tempo_;
}

} // namespace Aura::Transport
