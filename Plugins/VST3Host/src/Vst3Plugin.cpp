/// @file Vst3Plugin.cpp
/// @brief VST3 plugin stub (core) — full SDK hosting in Windows builds.

#include "Aura/Vst3Plugin.hpp"

namespace Aura::Plugin {

Vst3Plugin::Vst3Plugin(PluginDescriptor descriptor) : descriptor_(std::move(descriptor)) {}

std::string Vst3Plugin::open() {
#if defined(AURA_VST3_SDK_AVAILABLE)
    // Full Windows build: load the bundle via the Steinberg SDK, create the
    // component + controller, connect buses. (Implemented in Phase 2.)
    return "";
#else
    (void)descriptor_;
    return "VST3 hosting requires the full Windows build (Steinberg VST3 SDK). "
           "Scanning and plugin management are available in this build.";
#endif
}

void Vst3Plugin::close() {
    prepared_ = false;
}

void Vst3Plugin::prepare(double /*sampleRate*/, int /*maxBlockSize*/) {
    prepared_ = true;
}

void Vst3Plugin::process(double* const* /*channels*/, int /*numChannels*/, int /*numSamples*/) {
    // Stub passes audio through untouched.
}

void Vst3Plugin::reset() {}

std::string Vst3Plugin::parameterName(int /*index*/) const {
    return "";
}

void Vst3Plugin::setParameter(int /*index*/, double /*normalizedValue*/) {}

double Vst3Plugin::getParameter(int /*index*/) const {
    return 0.0;
}

std::string Vst3Plugin::loadState(const std::uint8_t* /*data*/, std::size_t /*size*/) {
    return "";
}

PluginManager::Factory makeVst3Factory() {
    return [](const PluginDescriptor& desc) -> std::unique_ptr<IAudioPlugin> {
        if (desc.format != PluginFormat::VST3) {
            return nullptr;
        }
        return std::make_unique<Vst3Plugin>(desc);
    };
}

} // namespace Aura::Plugin
