/// @file Transport.cpp
/// @brief Implementation of transport state and musical position.

#include "Aura/Transport.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

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
    state_.store(TransportState::Playing, std::memory_order_relaxed);
}

void Transport::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.store(TransportState::Stopped, std::memory_order_relaxed);
}

void Transport::record() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.store(TransportState::Recording, std::memory_order_relaxed);
}

TransportState Transport::state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_.load(std::memory_order_relaxed);
}

void Transport::advance(std::int64_t numSamples) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.load(std::memory_order_relaxed) == TransportState::Stopped || numSamples <= 0) {
        return;
    }

    const std::int64_t currentPosition = positionSamples_.load(std::memory_order_relaxed);
    std::int64_t nextPosition =
        numSamples > std::numeric_limits<std::int64_t>::max() - currentPosition
            ? std::numeric_limits<std::int64_t>::max()
            : currentPosition + numSamples;

    if (loopEnabled_ && loopEndBar_ > loopStartBar_) {
        const double beatsPerBar = (4.0 * timeSigNum_) / timeSigDen_;
        const double samplesPerBeat = sampleRate_ * secondsPerBeat();
        const auto loopStart =
            static_cast<std::int64_t>(loopStartBar_ * beatsPerBar * samplesPerBeat);
        const auto loopEnd = static_cast<std::int64_t>(loopEndBar_ * beatsPerBar * samplesPerBeat);
        if (nextPosition >= loopEnd) {
            nextPosition = loopStart + ((nextPosition - loopStart) % (loopEnd - loopStart));
        }
    }
    positionSamples_.store(nextPosition, std::memory_order_relaxed);
}

void Transport::publishAudioThreadPositionSamples(std::int64_t samples) noexcept {
    positionSamples_.store(std::max<std::int64_t>(0, samples), std::memory_order_relaxed);
}

void Transport::publishAudioThreadState(TransportState state) noexcept {
    state_.store(state, std::memory_order_relaxed);
}

void Transport::seekSamples(std::int64_t samples) {
    std::lock_guard<std::mutex> lock(mutex_);
    positionSamples_.store(std::max<std::int64_t>(0, samples), std::memory_order_relaxed);
}

void Transport::seekBars(double bars) {
    std::lock_guard<std::mutex> lock(mutex_);
    const double beatsPerBar = (4.0 * timeSigNum_) / timeSigDen_;
    const double safeBars = std::isfinite(bars) ? std::max(0.0, bars) : 0.0;
    const double samplePosition = safeBars * beatsPerBar * sampleRate_ * secondsPerBeat();
    const double maxPosition = static_cast<double>(std::numeric_limits<std::int64_t>::max());
    const std::int64_t roundedPosition =
        !std::isfinite(samplePosition) || samplePosition >= maxPosition
            ? std::numeric_limits<std::int64_t>::max()
            : static_cast<std::int64_t>(samplePosition);
    positionSamples_.store(roundedPosition, std::memory_order_relaxed);
}

std::int64_t Transport::positionSamples() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return positionSamples_.load(std::memory_order_relaxed);
}

double Transport::positionSeconds() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<double>(positionSamples_.load(std::memory_order_relaxed)) / sampleRate_;
}

double Transport::positionBeats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<double>(positionSamples_.load(std::memory_order_relaxed)) /
           (sampleRate_ * secondsPerBeat());
}

double Transport::positionBars() const {
    std::lock_guard<std::mutex> lock(mutex_);
    const double beatsPerBar = (4.0 * timeSigNum_) / timeSigDen_;
    return static_cast<double>(positionSamples_.load(std::memory_order_relaxed)) /
           (sampleRate_ * secondsPerBeat() * beatsPerBar);
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
    if (numerator > 0 &&
        (denominator == 2 || denominator == 4 || denominator == 8 || denominator == 16)) {
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
