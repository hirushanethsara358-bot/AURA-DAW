#pragma once

/// @file Transport.hpp
/// @brief Transport state, tempo map and sample-accurate musical position.

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace Aura::Transport {

/// @brief Playback state of the transport.
enum class TransportState { Stopped, Playing, Recording };

static_assert(std::atomic<std::int64_t>::is_always_lock_free,
              "Audio-thread transport position requires lock-free 64-bit atomics.");
static_assert(std::atomic<TransportState>::is_always_lock_free,
              "Audio-thread transport state requires lock-free enum atomics.");

/// @brief A tempo change at a given bar position.
struct TempoMarker {
    double bar = 0.0; ///< Bar position (fractional bars allowed).
    double bpm = 120.0;
};

/// @brief Time signature active from a bar position.
struct TimeSignatureMarker {
    double bar = 0.0;
    int numerator = 4;
    int denominator = 4;
};

/// @brief Sample-accurate position with musical conversions.
class Transport {
  public:
    explicit Transport(double sampleRate = 48000.0);

    void setSampleRate(double sampleRate);
    [[nodiscard]] double sampleRate() const;

    void play();
    void stop();
    void record();
    [[nodiscard]] TransportState state() const;

    /// @brief Advances the position by a rendered block (called by the engine).
    void advance(std::int64_t numSamples);

    /// @brief Publish callback-owned position/state without taking the control mutex.
    /// Use instead of advance() for a stream whose renderer already owns exact position; do not
    /// publish and advance concurrently for the same stream.
    void publishAudioThreadPositionSamples(std::int64_t samples) noexcept;
    void publishAudioThreadState(TransportState state) noexcept;

    /// @brief Jumps to an absolute sample position.
    void seekSamples(std::int64_t samples);
    /// @brief Jumps to an absolute bar position.
    void seekBars(double bars);

    [[nodiscard]] std::int64_t positionSamples() const;
    [[nodiscard]] double positionSeconds() const;
    [[nodiscard]] double positionBeats() const;
    [[nodiscard]] double positionBars() const;

    void setTempo(double bpm);
    [[nodiscard]] double tempo() const;
    void setTempoMap(std::vector<TempoMarker> markers);
    void setTimeSignature(int numerator, int denominator);

    /// @brief Loop region in bars; when enabled, advance() wraps inside it.
    void setLoop(bool enabled, double startBar, double endBar);

    /// @brief Metronome click enabled (rendered by the mixer).
    void setMetronome(bool enabled);
    [[nodiscard]] bool metronome() const;

  private:
    [[nodiscard]] double secondsPerBeat() const;

    mutable std::mutex mutex_;
    double sampleRate_;
    std::atomic<TransportState> state_{TransportState::Stopped};
    std::atomic<std::int64_t> positionSamples_{0};
    double tempo_ = 120.0;
    std::vector<TempoMarker> tempoMap_{{}};
    int timeSigNum_ = 4;
    int timeSigDen_ = 4;
    bool loopEnabled_ = false;
    double loopStartBar_ = 0.0;
    double loopEndBar_ = 4.0;
    bool metronome_ = false;
};

} // namespace Aura::Transport
