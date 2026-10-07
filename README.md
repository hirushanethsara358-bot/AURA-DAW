# 🎧 AURA Digital Audio Workstation

**AURA DAW 1.0.0** — a professional music production environment built with
modern C++20, Qt 6, and JUCE.

> Original code, design and branding. Inspired in capability by Ableton Live,
> FL Studio, Studio One, Cubase and Logic Pro — but 100% original implementation.

![CI](https://github.com/hirushanethsara358-bot/AURA-DAW/actions/workflows/ci.yml/badge.svg)
![License](https://img.shields.io/badge/license-MIT-blue.svg)
![C++](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11%20x64-lightgrey.svg)

---

## ✨ Features

| Area | Highlights |
|------|-----------|
| 🔊 Audio Engine | 64-bit float processing, ASIO / WASAPI, 44.1–192 kHz, low-latency mode |
| 🎛 Mixer | Unlimited tracks & buses, fader / pan / mute / solo, inserts, sends, side-chain |
| 🎹 MIDI | Piano roll, recording, quantize, velocity edit, humanize |
| 🔌 Plugins | VST3 hosting, plugin manager, sandbox-safe scanning |
| 🎚 Built-in FX | Parametric EQ, compressor, limiter, gate, reverb, delay, chorus, flanger, distortion |
| 🎹 Instruments | **AURA Synth** (osc / filter / ADSR / LFO) + **AURA Sampler** (multi-layer) |
| 🤖 AI Assistant | BPM & key detection, chord/melody suggestions, mix & mastering advisor |
| 💾 Projects | `.aura` format (JSON), autosave every 5 min |
| 🖥 UI | Premium dark theme, GPU-accelerated QML, customizable layouts |

## 📁 Repository layout

```
AURA-DAW/
├── Engine/        # AudioEngine, MixerEngine, DSP, PluginHost, MIDI, Transport
├── UI/            # Qt6/QML MainWindow, MixerView, PianoRoll, Browser
├── Plugins/       # VST3 host integration
├── AI/            # AURA Assistant, Music Analyzer
├── Effects/       # Built-in effect suite
├── Instruments/   # AURA Synth, AURA Sampler
├── Project/       # .aura project format + autosave
├── Tests/         # Google Test unit tests
├── Samples/       # Factory sound library placeholder
├── Projects/      # Demo projects placeholder
├── Installer/     # NSIS-based Windows installer
└── docs/          # Architecture, build guide, roadmap
```

## 🚀 Quick start (full Windows build)

Requirements: Windows 10/11 x64, Visual Studio 2022, CMake ≥ 3.24,
Ninja, Qt 6.5+, JUCE 8, NSIS (installer only).

```powershell
git clone https://github.com/hirushanethsara358-bot/AURA-DAW.git
cd AURA-DAW
cmake --preset windows-full
cmake --build --preset windows-full --config Release
ctest --preset windows-full --output-on-failure
```

The installer is produced at `build/windows-full/Installer/AURA-DAW-Setup.exe`.

> Full instructions: [`docs/BUILD.md`](docs/BUILD.md)

## 🧪 Core-only build (no Qt/JUCE needed)

The DSP / mixer / MIDI / project / synth / AI core is dependency-free
and builds anywhere for development and CI:

```bash
cmake -S . -B build -DAURA_ENABLE_QT6=OFF -DAURA_ENABLE_JUCE=OFF
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

## 🗺 Roadmap

- **Phase 1** — Audio engine, project system, basic UI ✅ (this release)
- **Phase 2** — Mixer, MIDI, plugin hosting
- **Phase 3** — Effects, instruments
- **Phase 4** — AI assistant
- **Phase 5** — Professional release + installer signing

See [`docs/ROADMAP.md`](docs/ROADMAP.md) for details.

## 📖 Documentation

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — module design
- [`docs/BUILD.md`](docs/BUILD.md) — build instructions
- [`docs/CONTRIBUTING.md`](docs/CONTRIBUTING.md) — contribution guide

## 📄 License

MIT License — see [`LICENSE`](LICENSE).

---
*Built with C++20 · Qt 6 · JUCE · CMake · Arena AI Agent*
