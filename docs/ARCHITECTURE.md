# AURA DAW — Architecture

Version 1.0.0 · C++20 · CMake + Ninja

## Design principles

1. **Real-time safety** — the audio callback never allocates, locks, or does I/O.
   UI → engine communication uses lock-free FIFOs and atomics.
2. **64-bit float DSP** — all internal processing is `double` precision.
3. **Modular libraries** — each subsystem is a standalone CMake target with a
   documented public API under `Aura::*`.
4. **Dependency-free core** — DSP, mixer, MIDI, project, synth and AI analyzer
   compile with only the C++ standard library (+ nlohmann/json for
   serialization). Qt 6 and JUCE are required only for the UI and the
   hardware audio backend respectively.
5. **Tested** — every DSP/engine module has Google Test coverage; CI builds
   the core on Ubuntu + Windows and runs the full suite.

## Module map

```
┌──────────────────────────────────────────────────────────┐
│ UI (Qt6/QML)  MainWindow · MixerView · PianoRoll · Browser│
└─────────────────────────┬────────────────────────────────┘
                          │ lock-free commands / meters
┌─────────────────────────▼────────────────────────────────┐
│ Engine                                                    │
│  ┌────────────┐  ┌──────────┐  ┌───────────────────────┐  │
│  │ AudioEngine│  │ Mixer    │  │ Transport             │  │
│  │ ASIO/WASAPI│◄─┤ strips,  │  │ tempo map, play state │  │
│  │ JUCE backnd│  │ buses    │  └───────────────────────┘  │
│  └────────────┘  └──────────┘  ┌───────────────────────┐  │
│  ┌────────────┐  ┌──────────┐  │ PluginHost (VST3)     │  │
│  │ DSP        │  │ MIDI     │  │ scan · sandbox · exec │  │
│  │ EQ/dyn/FX  │  │ clips,   │  └───────────────────────┘  │
│  └────────────┘  │ quantize │                             │
└──────────────────┴──────────┴──────────────────────────────┘
┌──────────────────────────────────────────────────────────┐
│ Instruments      AURA Synth · AURA Sampler                │
│ AI               MusicAnalyzer · Assistant (theory/suggest)│
│ Project          .aura JSON format · autosave             │
└──────────────────────────────────────────────────────────┘
```

## Namespaces

| Namespace | Content |
|-----------|---------|
| `Aura::Audio` | Device config, engine, drivers |
| `Aura::Dsp` | Filters, dynamics, time-based FX |
| `Aura::Mixer` | Channel strips, buses, master |
| `Aura::Midi` | Messages, clips, editing ops |
| `Aura::Plugin` | Plugin API, scanner, manager |
| `Aura::Transport` | Playback state, tempo map |
| `Aura::Instrument` | Synth, sampler |
| `Aura::Ai` | Analyzer, assistant |
| `Aura::Project` | Project model, serialization |

## Audio graph threading

- **Audio thread**: `AudioEngine` callback → `Mixer::processBlock` →
  per-strip inserts → bus sums → master chain. Wait-free.
- **Message thread**: UI, project load/save, plugin scan.
- **Worker pool**: offline render, AI analysis, file import/export.

## Adding a new effect

1. Create `YourFx` in `Engine/DSP/` implementing
   `void process(double* const* channels, int numChannels, int numSamples)`.
2. Register parameters via `Aura::Dsp::ParameterSet`.
3. Add presets + unit tests in `Tests/test_dsp.cpp`.
4. Expose to QML through `UI/Effects/EffectView.qml`.
