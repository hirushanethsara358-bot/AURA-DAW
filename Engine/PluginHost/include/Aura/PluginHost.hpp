#pragma once

/// @file PluginHost.hpp
/// @brief Plugin hosting API: descriptors, scanner, manager and presets.
///
/// The core library defines the host-side API plus a sandboxed scanner.
/// Native VST3 loading is provided by the full Windows build (Steinberg SDK);
/// third-party formats implement @ref IAudioPlugin.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace Aura::Plugin {

/// @brief Supported plugin formats.
enum class PluginFormat {
    VST3,
    AudioUnit,
    AuraNative,
    Unknown
};

[[nodiscard]] const char* toString(PluginFormat format);

/// @brief Static description of a discovered plugin.
struct PluginDescriptor {
    std::string id; ///< Stable id, e.g. "vst3:MyComp:abcd1234".
    std::string name;
    std::string vendor = "Unknown";
    std::string version = "1.0.0";
    PluginFormat format = PluginFormat::Unknown;
    std::string path; ///< Bundle path on disk.
    int numInputs = 2;
    int numOutputs = 2;
    bool isInstrument = false;
    bool enabled = true;
};

/// @brief Host-side audio plugin interface (real-time safe after prepare).
class IAudioPlugin {
public:
    virtual ~IAudioPlugin() = default;

    [[nodiscard]] virtual const PluginDescriptor& descriptor() const = 0;
    /// @brief Opens the plugin (loads DSP). Returns "" on success.
    virtual std::string open() = 0;
    virtual void close() = 0;
    virtual void prepare(double sampleRate, int maxBlockSize) = 0;
    virtual void process(double* const* channels, int numChannels, int numSamples) = 0;
    virtual void reset() = 0;

    [[nodiscard]] virtual int numParameters() const = 0;
    [[nodiscard]] virtual std::string parameterName(int index) const = 0;
    virtual void setParameter(int index, double normalizedValue) = 0;
    [[nodiscard]] virtual double getParameter(int index) const = 0;

    /// @brief Serializes the plugin state (for .aura projects / presets).
    [[nodiscard]] virtual std::vector<std::uint8_t> saveState() const = 0;
    /// @brief Restores state. Returns "" on success.
    virtual std::string loadState(const std::uint8_t* data, std::size_t size) = 0;
};

/// @brief Scans folders for plugin bundles (safe: never loads code while scanning).
class PluginScanner {
public:
    void addSearchPath(std::string path);
    void clearSearchPaths();
    void setRecursive(bool recursive) { recursive_ = recursive; }

    /// @brief Scans all search paths. Rescans from scratch each call.
    [[nodiscard]] std::vector<PluginDescriptor> scan();

    /// @brief Default Steinberg / system plugin locations for this OS.
    [[nodiscard]] static std::vector<std::string> defaultSearchPaths();

private:
    [[nodiscard]] static PluginFormat formatForPath(const std::string& path);

    std::vector<std::string> searchPaths_;
    bool recursive_ = true;
};

/// @brief Owns plugin lifecycle: instantiation, enable/disable, presets.
class PluginManager {
public:
    /// @brief Registers descriptors (typically from PluginScanner::scan).
    void registerPlugins(const std::vector<PluginDescriptor>& descriptors);
    [[nodiscard]] std::vector<PluginDescriptor> listPlugins() const;
    [[nodiscard]] bool contains(const std::string& id) const;

    void setEnabled(const std::string& id, bool enabled);
    [[nodiscard]] bool isEnabled(const std::string& id) const;

    /// @brief Attaches a factory used to instantiate plugins by id.
    using Factory = std::function<std::unique_ptr<IAudioPlugin>(const PluginDescriptor&)>;
    void setFactory(Factory factory);

    /// @brief Instantiates a plugin. Returns nullptr when unknown/disabled.
    [[nodiscard]] std::unique_ptr<IAudioPlugin> create(const std::string& id) const;

    // -- Presets -----------------------------------------------------------
    void savePreset(const std::string& pluginId, const std::string& presetName,
                    const std::vector<std::uint8_t>& state);
    [[nodiscard]] std::vector<std::string> listPresets(const std::string& pluginId) const;
    [[nodiscard]] bool loadPreset(const std::string& pluginId, const std::string& presetName,
                                  std::vector<std::uint8_t>& stateOut) const;
    void deletePreset(const std::string& pluginId, const std::string& presetName);

private:
    mutable std::mutex mutex_;
    std::map<std::string, PluginDescriptor> plugins_;
    std::map<std::string, std::map<std::string, std::vector<std::uint8_t>>> presets_;
    Factory factory_;
};

} // namespace Aura::Plugin
