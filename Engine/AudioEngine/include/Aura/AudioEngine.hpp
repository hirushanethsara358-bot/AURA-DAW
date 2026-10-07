#pragma once

/// @file AudioEngine.hpp
/// @brief Real-time audio engine: device management, 64-bit float callback,
///        CPU metering and underrun tracking.
///
/// Threading contract: the audio callback (@ref IAudioCallback::processBlock)
/// runs on a high-priority real-time thread and must never allocate memory,
/// take locks, or perform I/O. All configuration changes are applied via
/// @ref setConfig which safely restarts the stream.

#include "Aura/AudioDeviceTypes.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Aura::Audio {

/// @brief Real-time audio callback interface (64-bit floating point).
class IAudioCallback {
public:
    virtual ~IAudioCallback() = default;

    /// @param inputs      Array of input channel buffers (may be nullptr).
    /// @param outputs     Array of output channel buffers to fill.
    /// @param numInputs   Number of input channels.
    /// @param numOutputs  Number of output channels.
    /// @param numSamples  Frames to process this block.
    virtual void processBlock(const double* const* inputs, double* const* outputs, int numInputs,
                              int numOutputs, int numSamples) = 0;
};

/// @brief Minimal driver abstraction; concrete ASIO/WASAPI drivers implement this.
class IAudioDriver {
public:
    virtual ~IAudioDriver() = default;
    [[nodiscard]] virtual DriverType type() const = 0;
    [[nodiscard]] virtual std::vector<AudioDeviceInfo> enumerateDevices() = 0;
    /// @brief Runs the callback on the driver's real-time thread. Returns error or "".
    virtual std::string start(const AudioDeviceConfig& config, IAudioCallback& callback) = 0;
    virtual void stop() = 0;
    [[nodiscard]] virtual bool isRunning() const = 0;
};

/// @brief Central audio engine. Owns the active driver and exposes meters.
class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    /// @brief Lists devices for every available driver backend.
    [[nodiscard]] std::vector<AudioDeviceInfo> enumerateDevices();

    /// @brief Applies a new device configuration (restarts stream if running).
    /// @return Empty string on success, otherwise an error message.
    std::string setConfig(const AudioDeviceConfig& config);

    [[nodiscard]] AudioDeviceConfig config() const;

    /// @brief Attaches the processing callback (typically the Mixer).
    void setCallback(IAudioCallback* callback);

    /// @brief Starts the audio stream. Returns "" on success.
    std::string start();
    void stop();
    [[nodiscard]] bool isRunning() const { return running_.load(std::memory_order_acquire); }

    /// @brief Renders blocks through the callback without hardware (offline/test).
    /// @return Number of blocks rendered.
    std::int64_t renderOffline(std::int64_t numBlocks);

    /// @brief Smoothed audio-thread load in [0, 1].
    [[nodiscard]] double cpuLoad() const { return cpuLoad_.load(std::memory_order_relaxed); }
    /// @brief Total callback underruns since construction.
    [[nodiscard]] std::uint64_t underrunCount() const {
        return underruns_.load(std::memory_order_relaxed);
    }
    /// @brief Output latency estimate in samples (driver + buffering).
    [[nodiscard]] int latencySamples() const;

private:
    class EngineCallback;
    std::unique_ptr<EngineCallback> engineCallback_;
    std::unique_ptr<IAudioDriver> driver_;
    IAudioCallback* userCallback_ = nullptr;

    mutable std::mutex mutex_;
    AudioDeviceConfig config_;
    std::atomic<bool> running_{false};
    std::atomic<double> cpuLoad_{0.0};
    std::atomic<std::uint64_t> underruns_{0};
    std::chrono::steady_clock::time_point lastCallbackTime_{};
};

/// @brief Creates the best driver for the requested type (falls back to Dummy).
std::unique_ptr<IAudioDriver> createDriver(DriverType type);

} // namespace Aura::Audio
