#pragma once

/// @file WasapiDriver.hpp
/// @brief Private Windows shared-mode WASAPI render backend.

#include "Aura/AudioEngine.hpp"

#include <memory>

namespace Aura::Audio {

class WasapiDriver final : public IAudioDriver {
  public:
    WasapiDriver();
    ~WasapiDriver() override;

    WasapiDriver(const WasapiDriver&) = delete;
    WasapiDriver& operator=(const WasapiDriver&) = delete;

    [[nodiscard]] DriverType type() const override { return DriverType::WASAPI; }
    [[nodiscard]] std::vector<AudioDeviceInfo> enumerateDevices() override;
    std::string start(const AudioDeviceConfig& config, IAudioCallback& callback) override;
    void stop() override;
    [[nodiscard]] bool isRunning() const override;
    [[nodiscard]] int latencySamples() const override;
    [[nodiscard]] std::string lastError() const override;

  private:
    struct State;
    static void publishStartupResult(State& state, bool succeeded, std::string error = {});
    static void publishRuntimeError(State& state, std::string error);
    static std::string initializeAndRender(State& state, const AudioDeviceConfig& config,
                                           IAudioCallback& callback);

    std::unique_ptr<State> state_;
};

} // namespace Aura::Audio
