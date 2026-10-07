/// @file test_plugin.cpp
/// @brief Unit tests for the plugin scanner and manager.

#include <filesystem>
#include <fstream>
#include <memory>

#include <gtest/gtest.h>

#include "Aura/PluginHost.hpp"

namespace fs = std::filesystem;

TEST(Plugin, ScannerFindsBundles) {
    const fs::path dir = fs::temp_directory_path() / "aura_plugin_scan";
    fs::remove_all(dir);
    fs::create_directories(dir / "sub");
    std::ofstream(dir / "Comp.vst3") << "fake";
    std::ofstream(dir / "sub" / "Synth.vst3") << "fake";
    std::ofstream(dir / "readme.txt") << "ignored";
    std::ofstream(dir / "legacy.dll") << "ignored";

    Aura::Plugin::PluginScanner scanner;
    scanner.addSearchPath(dir.generic_string());
    const auto found = scanner.scan();
    ASSERT_EQ(found.size(), 2);
    EXPECT_EQ(found[0].format, Aura::Plugin::PluginFormat::VST3);
    EXPECT_EQ(found[0].name, "Comp");

    // Non-recursive misses the nested one.
    scanner.setRecursive(false);
    EXPECT_EQ(scanner.scan().size(), 1);

    // Missing paths are skipped silently.
    scanner.clearSearchPaths();
    scanner.addSearchPath((dir / "nope").generic_string());
    EXPECT_TRUE(scanner.scan().empty());

    fs::remove_all(dir);
}

TEST(Plugin, FormatNames) {
    EXPECT_STREQ(Aura::Plugin::toString(Aura::Plugin::PluginFormat::VST3), "VST3");
    EXPECT_STREQ(Aura::Plugin::toString(Aura::Plugin::PluginFormat::AuraNative), "AURA");
}

namespace {
class MockPlugin : public Aura::Plugin::IAudioPlugin {
public:
    explicit MockPlugin(Aura::Plugin::PluginDescriptor desc) : desc_(std::move(desc)) {}

    const Aura::Plugin::PluginDescriptor& descriptor() const override { return desc_; }
    std::string open() override {
        opened_ = true;
        return "";
    }
    void close() override { opened_ = false; }
    void prepare(double /*sampleRate*/, int /*maxBlock*/) override {}
    void process(double* const* channels, int numChannels, int numSamples) override {
        for (int ch = 0; ch < numChannels; ++ch) {
            for (int i = 0; i < numSamples; ++i) {
                channels[ch][i] *= 0.5; // -6 dB utility gain
            }
        }
    }
    void reset() override {}
    int numParameters() const override { return 1; }
    std::string parameterName(int /*index*/) const override { return "Gain"; }
    void setParameter(int /*index*/, double v) override { param_ = v; }
    double getParameter(int /*index*/) const override { return param_; }
    std::vector<std::uint8_t> saveState() const override {
        return {static_cast<std::uint8_t>(param_ * 100)};
    }
    std::string loadState(const std::uint8_t* data, std::size_t size) override {
        if (size < 1) {
            return "empty state";
        }
        param_ = data[0] / 100.0;
        return "";
    }

private:
    Aura::Plugin::PluginDescriptor desc_;
    bool opened_ = false;
    double param_ = 0.5;
};
} // namespace

TEST(Plugin, ManagerLifecycleAndPresets) {
    Aura::Plugin::PluginDescriptor desc;
    desc.id = "VST3:Mock";
    desc.name = "Mock";
    desc.format = Aura::Plugin::PluginFormat::VST3;

    Aura::Plugin::PluginManager manager;
    manager.registerPlugins({desc});
    EXPECT_TRUE(manager.contains("VST3:Mock"));
    EXPECT_EQ(manager.listPlugins().size(), 1);

    manager.setFactory([](const Aura::Plugin::PluginDescriptor& d) {
        return std::make_unique<MockPlugin>(d);
    });

    auto plugin = manager.create("VST3:Mock");
    ASSERT_NE(plugin, nullptr);
    EXPECT_TRUE(plugin->open().empty());

    double buf[4] = {1.0, 1.0, 1.0, 1.0};
    double* ch[1] = {buf};
    plugin->process(ch, 1, 4);
    EXPECT_DOUBLE_EQ(buf[0], 0.5);

    // Disable blocks instantiation but keeps registration.
    manager.setEnabled("VST3:Mock", false);
    EXPECT_EQ(manager.create("VST3:Mock"), nullptr);
    EXPECT_TRUE(manager.contains("VST3:Mock"));

    // Presets round-trip.
    manager.savePreset("VST3:Mock", "Init", {1, 2, 3});
    manager.savePreset("VST3:Mock", "Bright", {9});
    EXPECT_EQ(manager.listPresets("VST3:Mock").size(), 2);
    std::vector<std::uint8_t> state;
    EXPECT_TRUE(manager.loadPreset("VST3:Mock", "Init", state));
    EXPECT_EQ(state, (std::vector<std::uint8_t>{1, 2, 3}));
    manager.deletePreset("VST3:Mock", "Init");
    EXPECT_EQ(manager.listPresets("VST3:Mock").size(), 1);
}
