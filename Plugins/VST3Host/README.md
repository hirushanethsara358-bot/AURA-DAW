# AURA VST3 Host

Scanning (`Aura::Plugin::PluginScanner`) and management (`PluginManager`)
work in every build without loading any third-party code.

## Full VST3 hosting (Phase 2, Windows)

1. Clone the Steinberg SDK next to the repo:
   `git clone https://github.com/steinbergmedia/vst3sdk.git third_party/vst3sdk`
2. Configure with `-DAURA_VST3_SDK_AVAILABLE=ON`
   (defines `AURA_VST3_SDK_AVAILABLE` for `aura_plugins`).
3. `Vst3Plugin::open()` loads the bundle out-of-process first
   (sandbox scan), then in-process for execution.

Sandboxing: untrusted bundles are validated in a sacrificial process with
a 10 s timeout; crashes are reported with the bundle path and the plugin
is quarantined (auto-disabled) instead of crashing the DAW.
