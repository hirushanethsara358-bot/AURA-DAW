#pragma once

/// @file ProjectPlaybackSession.hpp
/// @brief Control-thread project-to-WAV preparation and one-clip MVP callback.

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "Aura/AudioBufferManager.hpp"
#include "Aura/AudioEngine.hpp"
#include "Aura/Project.hpp"
#include "Aura/Transport.hpp"
#include "Aura/WavClipPlayer.hpp"

namespace Aura::Session {

static_assert(std::atomic<float>::is_always_lock_free,
              "The MVP master fader requires lock-free float atomics.");

/// @brief A transactional adapter from one .aura WAV clip to a callback renderer.
///
/// The current 0.1 slice intentionally accepts exactly one audio clip. loadProject()
/// decodes on its caller/control thread, maps 4/4 project bars to output frames,
/// and prepares an allocation-free callback with track gain/pan and a minimal
/// atomic master fader, and publishes its sample position/state to a session-owned
/// Aura::Transport. Stop/quiesce the device before loading/replacing a session or
/// clearing its buffers. This is not a WASAPI driver.
class ProjectPlaybackSession final : public Audio::IAudioCallback {
  public:
    explicit ProjectPlaybackSession(
        std::uint64_t decodedMemoryLimitBytes = Instrument::kDefaultMaxDecodedWavBytes);

    /// @brief Load and prepare one audio clip. Existing state survives on failure.
    [[nodiscard]] bool loadProject(const Project::Project& project, double outputSampleRate,
                                   std::string& error);
    /// @brief Clear the prepared session. Caller must stop and quiesce the driver first.
    void clear() noexcept;

    void play() noexcept;
    void stop() noexcept;
    /// @brief Seek using 4/4 bar positions, matching the current project timeline.
    [[nodiscard]] bool seekBars(double bars) noexcept;
    /// @brief Update the single track's fader/pan while playback is running.
    [[nodiscard]] bool setGainPan(double volumeDb, double pan) noexcept;
    /// @brief Set a bounded, callback-safe stereo master fader in decibels.
    [[nodiscard]] bool setMasterGainDb(double volumeDb) noexcept;
    /// @brief Change the project tempo and reschedule its clip. Stop/quiesce the device first.
    [[nodiscard]] std::string setProjectTempo(double bpm);

    [[nodiscard]] bool isLoaded() const noexcept { return loaded_; }
    [[nodiscard]] double masterPeak() const noexcept {
        return masterPeak_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] bool isPlaying() const noexcept;
    [[nodiscard]] std::uint64_t positionFrames() const noexcept;
    [[nodiscard]] double positionBars() const noexcept;
    /// @brief Reconfigure resampling/timeline for the negotiated output rate before playback.
    [[nodiscard]] std::string configureDeviceFormat(double sampleRate, int numInputs,
                                                    int numOutputs) override;
    /// @brief Read-only transport view for UI meters/polling.
    [[nodiscard]] const Aura::Transport::Transport& transport() const noexcept {
        return transport_;
    }

    void processBlock(const double* const* inputs, double* const* outputs, int numInputs,
                      int numOutputs, int numSamples) override;

  private:
    const std::uint64_t decodedMemoryLimitBytes_;
    Aura::Transport::Transport transport_;
    std::unique_ptr<Instrument::AudioBufferManager> buffers_;
    std::unique_ptr<Instrument::WavClipPlayer> player_;
    double outputSampleRate_ = 48000.0;
    double samplesPerBar_ = 96000.0;
    double projectTempo_ = 120.0;
    double clipStartBar_ = 0.0;
    double clipLengthBars_ = 1.0;
    std::atomic<float> masterGain_{1.0F};
    std::atomic<float> masterPeak_{0.0F};
    bool loaded_ = false;
};

} // namespace Aura::Session
