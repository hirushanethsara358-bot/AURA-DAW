/// @file AudioEngine.cpp
/// @brief Implementation of the AURA real-time audio engine.

#include "Aura/AudioEngine.hpp"

#if defined(_WIN32)
#include "WasapiDriver.hpp"
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace Aura::Audio {

namespace {

constexpr int kMinBufferSize = 16;
constexpr int kMaxBufferSize = 8192;
constexpr int kMaxChannels = 64;

bool isSupportedRate(double rate) {
    for (double known : SampleRates::all()) {
        if (std::abs(known - rate) < 1.0) {
            return true;
        }
    }
    return false;
}

} // namespace

std::string AudioDeviceConfig::validate() const {
    if (!isSupportedRate(sampleRate)) {
        return "Unsupported sample rate. Use 44.1 / 48 / 96 / 192 kHz.";
    }
    if (bufferSize < kMinBufferSize || bufferSize > kMaxBufferSize) {
        return "Buffer size must be within [16, 8192] frames.";
    }
    if (numInputChannels < 0 || numInputChannels > kMaxChannels) {
        return "Input channel count out of range.";
    }
    if (numOutputChannels <= 0 || numOutputChannels > kMaxChannels) {
        return "Output channel count out of range.";
    }
    if (outputDeviceId.size() > 1024) {
        return "Output device identifier is too long.";
    }
    return "";
}

const char* toString(DriverType driver) {
    switch (driver) {
    case DriverType::WASAPI:
        return "WASAPI";
    case DriverType::ASIO:
        return "ASIO";
    case DriverType::CoreAudio:
        return "CoreAudio";
    case DriverType::Dummy:
        return "Dummy";
    }
    return "Unknown";
}

/// @brief Internal callback: wraps the user callback with metering.
class AudioEngine::EngineCallback : public IAudioCallback {
  public:
    explicit EngineCallback(AudioEngine& owner) : owner_(owner) {}

    std::string configureDeviceFormat(double sampleRate, int numInputs, int numOutputs) override {
        if (!std::isfinite(sampleRate) || sampleRate <= 0.0 || numOutputs <= 0) {
            return "The audio driver supplied an invalid negotiated format.";
        }
        owner_.negotiatedSampleRate_.store(sampleRate, std::memory_order_relaxed);
        owner_.negotiatedOutputChannels_.store(numOutputs, std::memory_order_relaxed);
        if (auto* cb = owner_.userCallback_; cb != nullptr) {
            return cb->configureDeviceFormat(sampleRate, numInputs, numOutputs);
        }
        return {};
    }

    void processBlock(const double* const* inputs, double* const* outputs, int numInputs,
                      int numOutputs, int numSamples) override {
        const auto start = std::chrono::steady_clock::now();

        if (auto* cb = owner_.userCallback_; cb != nullptr) {
            cb->processBlock(inputs, outputs, numInputs, numOutputs, numSamples);
        } else if (outputs != nullptr) {
            for (int ch = 0; ch < numOutputs; ++ch) {
                if (outputs[ch] != nullptr) {
                    std::memset(outputs[ch], 0, sizeof(double) * static_cast<size_t>(numSamples));
                }
            }
        }

        // Underrun detection: callback took longer than one buffer period.
        const auto end = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(end - start).count();
        const double sampleRate = owner_.negotiatedSampleRate_.load(std::memory_order_relaxed);
        const double budget = sampleRate > 0.0 ? static_cast<double>(numSamples) / sampleRate : 0.0;
        if (budget > 0.0 && elapsed > budget) {
            owner_.underruns_.fetch_add(1, std::memory_order_relaxed);
        }

        // Smoothed CPU load: EMA with 0.1 coefficient.
        const double load = budget > 0.0 ? std::clamp(elapsed / budget, 0.0, 1.0) : 0.0;
        const double prev = owner_.cpuLoad_.load(std::memory_order_relaxed);
        owner_.cpuLoad_.store(prev * 0.9 + load * 0.1, std::memory_order_relaxed);
    }

  private:
    AudioEngine& owner_;
};

/// @brief Hardware-free driver used for tests and offline rendering.
class DummyDriver : public IAudioDriver {
  public:
    [[nodiscard]] DriverType type() const override { return DriverType::Dummy; }

    [[nodiscard]] std::vector<AudioDeviceInfo> enumerateDevices() override {
        AudioDeviceInfo info;
        info.id = "dummy-default";
        info.name = "Dummy Output (offline)";
        info.driver = DriverType::Dummy;
        info.maxInputChannels = 2;
        info.maxOutputChannels = 2;
        info.supportedSampleRates = {SampleRates::k44100, SampleRates::k48000, SampleRates::k96000,
                                     SampleRates::k192000};
        info.isDefaultOutput = true;
        return {info};
    }

    std::string start(const AudioDeviceConfig& config, IAudioCallback& callback) override {
        if (std::string error = callback.configureDeviceFormat(
                config.sampleRate, config.numInputChannels, config.numOutputChannels);
            !error.empty()) {
            return error;
        }
        running_.store(true, std::memory_order_release);
        return "";
    }

    void stop() override { running_.store(false, std::memory_order_release); }
    [[nodiscard]] bool isRunning() const override {
        return running_.load(std::memory_order_acquire);
    }

  private:
    std::atomic<bool> running_{false};
};

std::unique_ptr<IAudioDriver> createDriver(DriverType type) {
    // Always expose the offline driver, and compile native hardware backends
    // only on supported platforms. Returning nullptr for unavailable drivers
    // prevents a requested stream from appearing to start while rendering nothing.
    if (type == DriverType::Dummy) {
        return std::make_unique<DummyDriver>();
    }
#if defined(_WIN32)
    if (type == DriverType::WASAPI) {
        return std::make_unique<WasapiDriver>();
    }
#endif
    return nullptr;
}

AudioEngine::AudioEngine() : engineCallback_(std::make_unique<EngineCallback>(*this)) {
    driver_ = createDriver(config_.driver);
}

AudioEngine::~AudioEngine() {
    stop();
}

std::vector<AudioDeviceInfo> AudioEngine::enumerateDevices() {
    std::vector<AudioDeviceInfo> devices;
    for (DriverType type :
         {DriverType::WASAPI, DriverType::ASIO, DriverType::CoreAudio, DriverType::Dummy}) {
        auto driver = createDriver(type);
        if (!driver) {
            continue;
        }
        auto list = driver->enumerateDevices();
        devices.insert(devices.end(), list.begin(), list.end());
    }
    return devices;
}

std::string AudioEngine::setConfig(const AudioDeviceConfig& config) {
    if (std::string error = config.validate(); !error.empty()) {
        return error;
    }
    auto nextDriver = createDriver(config.driver);
    if (!nextDriver) {
        return std::string(toString(config.driver)) + " backend is not available in this build.";
    }

    const bool wasRunning = isRunning();
    if (wasRunning) {
        stop();
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = config;
        driver_ = std::move(nextDriver);
        negotiatedSampleRate_.store(config.sampleRate, std::memory_order_relaxed);
        negotiatedOutputChannels_.store(config.numOutputChannels, std::memory_order_relaxed);
    }
    if (wasRunning) {
        return start();
    }
    return "";
}

AudioDeviceConfig AudioEngine::config() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

void AudioEngine::setCallback(IAudioCallback* callback) {
    stop();
    userCallback_ = callback;
}

std::string AudioEngine::start() {
    if (isRunning()) {
        return "";
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (std::string error = config_.validate(); !error.empty()) {
        return error;
    }
    if (!driver_) {
        return std::string(toString(config_.driver)) + " backend is not available in this build.";
    }
    running_.store(false, std::memory_order_release);
    if (std::string error = driver_->start(config_, *engineCallback_); !error.empty()) {
        return error;
    }
    lastCallbackTime_ = std::chrono::steady_clock::now();
    running_.store(true, std::memory_order_release);
    return "";
}

void AudioEngine::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (driver_) {
        driver_->stop();
    }
    running_.store(false, std::memory_order_release);
}

bool AudioEngine::isRunning() const {
    if (!running_.load(std::memory_order_acquire)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    return driver_ != nullptr && driver_->isRunning();
}

std::string AudioEngine::lastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return driver_ != nullptr ? driver_->lastError() : std::string{};
}

std::int64_t AudioEngine::renderOffline(std::int64_t numBlocks) {
    if (userCallback_ == nullptr || numBlocks <= 0) {
        return 0;
    }
    const int numOut = std::min(config_.numOutputChannels, kMaxChannels);
    const int blockSize = config_.bufferSize;

    std::vector<std::vector<double>> buffers(static_cast<size_t>(numOut),
                                             std::vector<double>(static_cast<size_t>(blockSize)));
    std::vector<double*> outputs(static_cast<size_t>(numOut));
    for (int ch = 0; ch < numOut; ++ch) {
        outputs[static_cast<size_t>(ch)] = buffers[static_cast<size_t>(ch)].data();
    }

    for (std::int64_t i = 0; i < numBlocks; ++i) {
        engineCallback_->processBlock(nullptr, outputs.data(), 0, numOut, blockSize);
    }
    return numBlocks;
}

int AudioEngine::latencySamples() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (driver_ != nullptr) {
        const int negotiatedLatency = driver_->latencySamples();
        if (negotiatedLatency > 0) {
            return negotiatedLatency;
        }
    }
    // Fallback estimate for offline/legacy drivers without a device-buffer report.
    return config_.lowLatencyMode ? config_.bufferSize : config_.bufferSize * 2;
}

} // namespace Aura::Audio
