#pragma once

/// @file WavClipPlayer.hpp
/// @brief Minimal allocation-free stereo renderer for one prepared WAV clip.

#include <atomic>
#include <cstdint>

#include "Aura/AudioEngine.hpp"
#include "Aura/Sampler.hpp"

namespace Aura::Instrument {

static_assert(std::atomic<float>::is_always_lock_free,
              "The MVP callback requires lock-free float atomics.");
static_assert(std::atomic<bool>::is_always_lock_free,
              "The MVP callback requires lock-free bool atomics.");
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "The MVP callback requires lock-free 64-bit atomics.");

/// @brief Plays one immutable, decoded SampleData source through a stereo fader/pan.
///
/// This is a deliberately small 0.1 rendering path, not the generalized Mixer.
/// Call prepare(), setSource(), and setGainPan() on a control thread while the
/// audio callback is stopped. The source must remain alive and unchanged until
/// playback is stopped and the callback has quiesced. processBlock() performs
/// no allocation, locks, file access, or other control-thread work.
class WavClipPlayer final : public Audio::IAudioCallback {
  public:
    WavClipPlayer() = default;

    /// @brief Configure output-rate resampling. Call while the callback is stopped.
    void prepare(double outputSampleRate) noexcept;
    /// @brief Bind an immutable decoded buffer. Call while the callback is stopped.
    void setSource(const SampleData* source) noexcept;
    /// @brief Set this clip's timeline range in output frames. Stopped-only control call.
    /// @param clipStartFrame Absolute timeline start.
    /// @param clipLengthFrames Arrangement clip duration, independent of source file length.
    /// @param timelineEndFrame Arrangement end; transport stops here.
    [[nodiscard]] bool setTimelineRange(std::uint64_t clipStartFrame,
                                        std::uint64_t clipLengthFrames,
                                        std::uint64_t timelineEndFrame) noexcept;
    /// @brief Queue a sample-position seek; safe to call while the callback runs.
    void seekTimelineFrame(std::uint64_t frame) noexcept;
    [[nodiscard]] std::uint64_t timelinePositionFrames() const noexcept {
        return timelinePositionPublished_.load(std::memory_order_relaxed);
    }
    /// @brief Set track gain and equal-power pan, outside the audio callback.
    void setGainPan(double volumeDb, double pan) noexcept;

    /// @brief Start or resume from the current transport position.
    void play() noexcept;
    void stop() noexcept;
    [[nodiscard]] bool isPlaying() const noexcept {
        return playing_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::uint64_t playheadSourceFrame() const noexcept {
        return playheadSourceFrame_.load(std::memory_order_relaxed);
    }

    void processBlock(const double* const* inputs, double* const* outputs, int numInputs,
                      int numOutputs, int numSamples) override;

  private:
    void updateDefaultTimelineRange() noexcept;

    const SampleData* source_ = nullptr;
    double outputSampleRate_ = 48000.0;
    double sourceFramesPerOutputFrame_ = 1.0;
    double sourcePosition_ = 0.0; // Owned by the audio callback after playback starts.
    std::uint64_t clipStartFrame_ = 0;
    std::uint64_t clipEndFrame_ = 0;
    std::uint64_t timelineEndFrame_ = 0;
    std::uint64_t timelinePosition_ = 0; // Owned by the audio callback after playback starts.
    bool customTimelineRange_ = false;   // Stopped-only control-thread state.
    std::atomic<std::uint64_t> requestedSeekFrame_{0};
    std::atomic<bool> seekRequested_{false};
    std::atomic<std::uint64_t> timelinePositionPublished_{0};
    std::atomic<float> leftGain_{0.70710678F};
    std::atomic<float> rightGain_{0.70710678F};
    std::atomic<bool> restartRequested_{false};
    std::atomic<bool> playing_{false};
    std::atomic<std::uint64_t> playheadSourceFrame_{0};
};

} // namespace Aura::Instrument
