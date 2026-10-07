/// @file PluginHost.cpp
/// @brief Implementation of the plugin scanner and manager.

#include "Aura/PluginHost.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace Aura::Plugin {

namespace fs = std::filesystem;

const char* toString(PluginFormat format) {
    switch (format) {
    case PluginFormat::VST3:
        return "VST3";
    case PluginFormat::AudioUnit:
        return "AU";
    case PluginFormat::AuraNative:
        return "AURA";
    case PluginFormat::Unknown:
        return "Unknown";
    }
    return "Unknown";
}

void PluginScanner::addSearchPath(std::string path) {
    if (!path.empty()) {
        searchPaths_.push_back(std::move(path));
    }
}

void PluginScanner::clearSearchPaths() {
    searchPaths_.clear();
}

std::vector<std::string> PluginScanner::defaultSearchPaths() {
#if defined(_WIN32)
    return {"C:/Program Files/Common Files/VST3", "C:/Program Files/Steinberg/VstPlugins"};
#elif defined(__APPLE__)
    return {"/Library/Audio/Plug-Ins/VST3", "/Library/Audio/Plug-Ins/Components",
            "~/Library/Audio/Plug-Ins/VST3"};
#else
    return {"/usr/lib/vst3", "~/.vst3"};
#endif
}

PluginFormat PluginScanner::formatForPath(const std::string& path) {
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.ends_with(".vst3")) {
        return PluginFormat::VST3;
    }
    if (lower.ends_with(".component")) {
        return PluginFormat::AudioUnit;
    }
    if (lower.ends_with(".aura-plugin")) {
        return PluginFormat::AuraNative;
    }
    return PluginFormat::Unknown;
}

std::vector<PluginDescriptor> PluginScanner::scan() {
    std::vector<PluginDescriptor> found;
    for (const auto& root : searchPaths_) {
        std::error_code ec;
        if (!fs::exists(root, ec) || ec) {
            continue;
        }
        auto handle = [&](const fs::directory_entry& entry) {
            const std::string path = entry.path().generic_string();
            const PluginFormat format = formatForPath(path);
            if (format == PluginFormat::Unknown) {
                return;
            }
            PluginDescriptor desc;
            desc.name = entry.path().stem().generic_string();
            desc.format = format;
            desc.path = path;
            desc.id = std::string(toString(format)) + ":" + desc.name;
            found.push_back(std::move(desc));
        };
        if (recursive_) {
            for (auto it = fs::recursive_directory_iterator(root, ec);
                 it != fs::recursive_directory_iterator() && !ec; it.increment(ec)) {
                handle(*it);
            }
        } else {
            for (auto it = fs::directory_iterator(root, ec); it != fs::directory_iterator() && !ec;
                 it.increment(ec)) {
                handle(*it);
            }
        }
    }
    std::sort(found.begin(), found.end(),
              [](const PluginDescriptor& a, const PluginDescriptor& b) { return a.id < b.id; });
    return found;
}

void PluginManager::registerPlugins(const std::vector<PluginDescriptor>& descriptors) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& desc : descriptors) {
        auto it = plugins_.find(desc.id);
        const bool wasEnabled = it != plugins_.end() ? it->second.enabled : desc.enabled;
        plugins_[desc.id] = desc;
        plugins_[desc.id].enabled = wasEnabled;
    }
}

std::vector<PluginDescriptor> PluginManager::listPlugins() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<PluginDescriptor> list;
    list.reserve(plugins_.size());
    for (const auto& [id, desc] : plugins_) {
        (void)id;
        list.push_back(desc);
    }
    return list;
}

bool PluginManager::contains(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return plugins_.count(id) > 0;
}

void PluginManager::setEnabled(const std::string& id, bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = plugins_.find(id);
    if (it != plugins_.end()) {
        it->second.enabled = enabled;
    }
}

bool PluginManager::isEnabled(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = plugins_.find(id);
    return it != plugins_.end() && it->second.enabled;
}

void PluginManager::setFactory(Factory factory) {
    std::lock_guard<std::mutex> lock(mutex_);
    factory_ = std::move(factory);
}

std::unique_ptr<IAudioPlugin> PluginManager::create(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = plugins_.find(id);
    if (it == plugins_.end() || !it->second.enabled || !factory_) {
        return nullptr;
    }
    return factory_(it->second);
}

void PluginManager::savePreset(const std::string& pluginId, const std::string& presetName,
                               const std::vector<std::uint8_t>& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    presets_[pluginId][presetName] = state;
}

std::vector<std::string> PluginManager::listPresets(const std::string& pluginId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    auto it = presets_.find(pluginId);
    if (it != presets_.end()) {
        for (const auto& [name, state] : it->second) {
            (void)state;
            names.push_back(name);
        }
    }
    return names;
}

bool PluginManager::loadPreset(const std::string& pluginId, const std::string& presetName,
                               std::vector<std::uint8_t>& stateOut) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = presets_.find(pluginId);
    if (it == presets_.end()) {
        return false;
    }
    auto jt = it->second.find(presetName);
    if (jt == it->second.end()) {
        return false;
    }
    stateOut = jt->second;
    return true;
}

void PluginManager::deletePreset(const std::string& pluginId, const std::string& presetName) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = presets_.find(pluginId);
    if (it != presets_.end()) {
        it->second.erase(presetName);
    }
}

} // namespace Aura::Plugin
