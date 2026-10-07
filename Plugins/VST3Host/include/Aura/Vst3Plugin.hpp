#pragma once

/// @file Vst3Plugin.hpp
/// @brief VST3 plugin instance + factory for the AURA plugin host.
///
/// Core builds ship a safe stub: scanning/registration work everywhere, while
/// actual bundle loading requires the full Windows build with the Steinberg
/// VST3 SDK (see Plugins/VST3Host/README.md). The stub passes audio through
/// untouched and reports a clear error from open().

#include "Aura/PluginHost.hpp"

namespace Aura::Plugin {

/// @brief VST3 plugin instance.
class Vst3Plugin : public IAudioPlugin {
  public:
    explicit Vst3Plugin(PluginDescriptor descriptor);

    [[nodiscard]] const PluginDescriptor& descriptor() const override { return descriptor_; }
    [[nodiscard]] std::string open() override;
    void close() override;
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(double* const* channels, int numChannels, int numSamples) override;
    void reset() override;

    [[nodiscard]] int numParameters() const override { return 0; }
    [[nodiscard]] std::string parameterName(int index) const override;
    void setParameter(int index, double normalizedValue) override;
    [[nodiscard]] double getParameter(int index) const override;

    [[nodiscard]] std::vector<std::uint8_t> saveState() const override { return {}; }
    [[nodiscard]] std::string loadState(const std::uint8_t* data, std::size_t size) override;

  private:
    PluginDescriptor descriptor_;
    bool prepared_ = false;
};

/// @brief Factory for VST3 descriptors (returns nullptr for other formats).
[[nodiscard]] PluginManager::Factory makeVst3Factory();

} // namespace Aura::Plugin
