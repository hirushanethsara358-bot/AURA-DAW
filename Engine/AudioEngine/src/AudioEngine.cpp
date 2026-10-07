/// @file AudioEngine.cpp
/// @brief Implementation of the AURA real-time audio engine.

#include "Aura/AudioEngine.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

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

    void processBlock(const double* const* inputs, double* const* outputs, int numInputs,
                      int numOutputs, int numSamples) override {
        const auto start = std::chrono::steady_clock::now();

        if (auto* cb = owner_.userCallback_; cb != nullptr) {
            cb->processBlock(inputs, outputs, numInputs, numOutputs, numSamples);
        } else {
            for (int ch = 0; ch < numOutputs; ++ch) {
                std::memset(outputs[ch], 0, sizeof(double) * static_cast<size_t>(numSamples));
            }
        }

        // Underrun detection: callback took longer than one buffer period.
        const auto end = std::chrono::steady_clock::now();
        const double elapsed =
            std::chrono::duration<double>(end - start).count();
        const double budget =
            static_cast<double>(numSamples) / owner_.config_.sampleRate;
        if (elapsed > budget) {
            owner_.underruns_.fetch_add(1, std::memory_order_relaxed);
        }

        // Smoothed CPU load: EMA with 0.1 coefficient.
        const double load = std::clamp(elapsed / budget, 0.0, 1.0);
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
        info.supportedSampleRates = {SampleRates::k44100, SampleRates::k48000,
                                    SampleRates::k96000, SampleRates::k192000};
        info.isDefaultOutput = true;
        return {info};
    }

    std::string start(const AudioDeviceConfig& /*config*/, IAudioCallback& /*cb*/) override {
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
    // NOTE: WASAPI/ASIO drivers live in the JUCE backend (full Windows build).
    // The core library always falls back to the dummy driver so that the
    // engine, mixer and tests run on any platform without hardware.
    (void)type;
    return std::make_unique<DummyDriver>();
}

AudioEngine::AudioEngine() : engineCallback_(std::make_unique<EngineCallback>(*this)) {
    driver_ = createDriver(config_.driver);
}

AudioEngine::~AudioEngine() {
    stop();
}

std::vector<AudioDeviceInfo> AudioEngine::enumerateDevices() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AudioDeviceInfo> devices;
    for (DriverType t : {DriverType::WASAPI, DriverType::ASIO, DriverType::Dummy}) {
        auto driver = createDriver(t);
        auto list = driver->enumerateDevices();
        devices.insert(devices.end(), list.begin(), list.end());
    }
    return devices;
}

std::string AudioEngine::setConfig(const AudioDeviceConfig& config) {
    if (std::string error = config.validate(); !error.empty()) {
        return error;
    }
    const bool wasRunning = isRunning();
    if (wasRunning) {
        stop();
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = config;
        driver_ = createDriver(config_.driver);
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
    if (std::string error = config_.validate(); !error.empty()) {
        return error;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (std::string error = driver_->start(config_, *engineCallback_); !error.empty()) {
        return error;
    }
    lastCallbackTime_ = std::chrono::steady_clock::now();
    running_.store(true, std::memory_order_release);
    return "";
}

void AudioEngine::stop() {
    if (!isRunning()) {
        return;
    }
    running_.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lock(mutex_);
    driver_->stop();
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
    // Driver buffer + one safety buffer (exclusive low-latency mode halves it).
    return config_.lowLatencyMode ? config_.bufferSize : config_.bufferSize * 2;
}

} // namespace Aura::Audio
