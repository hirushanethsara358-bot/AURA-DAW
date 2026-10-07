# AURA DAW — Engineering Prototype

**Repository version:** `1.0.0` (version metadata only; **not** a stable or commercial release)

AURA DAW is an early C++20 digital-audio-workstation project. A shared-mode WASAPI backend, one-WAV playback session, and QML transport/mixer wiring now exist in this working copy, but the Windows device path and Qt application have not been run on real Windows hardware. It remains an **experimental MVP, not a complete or production-ready DAW**. Multi-clip playback, the production mixer/engine, VST3 loading, non-WAV codecs, and installer release path remain unfinished.

> **Current state (2026-10-07):** Linux core tests pass locally (84 passed; one platform-specific test is skipped). The Qt 6.8.2 application builds and starts in an offscreen Linux smoke test; `qmllint` exits successfully with 59 unqualified-access warnings. GitHub Actions passed the native `windows-latest` core build/tests and clang-format check for commit `63b97cf` ([run](https://github.com/hirushanethsara358-bot/AURA-DAW/actions/runs/37573620173)); the Windows Qt build and real WASAPI hardware playback remain unverified.

## What exists today

| Area | Available now | Not delivered yet |
|---|---|---|
| Audio engine | C++ device/configuration API, callback format negotiation, CPU/underrun counters, offline `Dummy`, and a Windows event-driven shared-mode WASAPI output backend | Real-device validation/recovery on Windows, ASIO, CoreAudio, capture and production-grade latency/stress QA |
| Mixer | Experimental stereo strip, fader, pan, mute/solo, bus, send and insert processing | Real-time-safe callback path; current processing still uses locks/allocations and is not suitable for a production audio thread |
| DSP | Tested prototype filters, EQ, dynamics and time/modulation effects | Listening/measurement QA, automation integration and release validation |
| MIDI | Note/clip structures and editing helpers | MIDI device I/O, recording, playback scheduling and complete piano-roll/session integration |
| Projects | `.aura` JSON model and transactional Save/Open; Qt New/Open/Save; `AURA::Session` prepares one WAV clip for the callback and is wired to QML Play/Stop/seek, transport polling and live track gain/pan | Multi-clip/session graph, project-relative media relocation, streaming, periodic autosave scheduling, schema migration and recovery workflow |
| Instruments | AURA Synth prototype; bounded PCM16/PCM24/FLOAT32 WAV decode; control-thread `AudioBufferManager` with path cache and PCM memory budget | Streaming for large files, full instrument UI, factory library and production sample management |
| Audio files | Bounded PCM16/PCM24/FLOAT32 WAV decoding, path-cached `AudioBufferManager`, one-clip timeline scheduling/resampling and UI-connected Play/Stop/seek with live track fader/pan | Multi-clip scheduling/mixing, background decode/large-file streaming, MP3/FLAC/AIFF codecs/export; playback remains hardware-unverified |
| Plugins | Plugin descriptors, filesystem scanning and manager API | VST3 execution, SDK-backed loading, validated scanning, process isolation and crash recovery |
| AI | Heuristic BPM/key analysis and music-theory/mix suggestions | Neural inference, generative features or claims of mastering-grade output |
| UI | Qt 6.8.2/QML shell is wired to the one-clip session and audio engine for transport, playhead, output status/retry, track gain/pan, peak/CPU meters and a callback-over-budget counter; Linux build and offscreen startup smoke test pass | Windows Qt app build and audible Windows acceptance remain unverified; mixer intentionally bypasses the unsafe generalized mixer path |
| Installer | CPack/NSIS packaging configuration | Verified Windows package, custom assets, signing and release smoke tests |

See [`docs/PROJECT_STATUS.md`](docs/PROJECT_STATUS.md) for the code/CI audit and prioritized work, [`docs/ROADMAP.md`](docs/ROADMAP.md) for the release gates, and [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the distinction between current code and target design. The shared [Google Docs readiness brief](https://docs.google.com/document/d/1wdpC-b0h-TnPhNSog-JmHtrdolgXWu_gDD1TGgRDWdk/edit?usp=drivesdk) is paired with the [Linear Phase 1 task](https://linear.app/hirushanet/issue/HIR-6/phase-1-aura-daw-01-playable-windows-mvp).

## Repository layout

```text
AURA-DAW/
├── Engine/        # Audio API, offline driver, mixer, DSP, MIDI and transport
├── UI/            # Qt6/QML shell wired to the one-clip playback MVP
├── Plugins/       # VST3 host interface/stub
├── AI/            # Heuristic analyzer and assistant
├── Effects/       # Preset examples and notes
├── Instruments/   # AURA Synth and WAV sampler prototypes
├── Project/       # .aura project model and serialization helper
├── Session/       # one-clip project-to-callback MVP adapter
├── Tests/         # Google Test suite
├── Installer/     # Experimental CPack/NSIS configuration
└── docs/          # Build notes, architecture, status and roadmap
```

## Build and test the core

Requirements: CMake 3.24+, Ninja, and a C++20 compiler. The first configure needs network access because CMake FetchContent downloads `nlohmann/json` and GoogleTest.

```bash
cmake --preset core
cmake --build --preset core
ctest --preset core --output-on-failure
```

On Linux this builds the portable core libraries/tests and uses the offline `Dummy` driver. The 85-test suite passes locally (84 passed, one platform-specific test skipped). GitHub Actions also passed the native Windows core build and CTest suite for commit `63b97cf` (84 passed, one test skipped); the Windows factory test ran there. The MinGW cross-built executable was not run on Linux, and hosted CI does not prove audible playback on a real endpoint. See the status report for verification limits.

## Windows UI/audio/installer status

The `windows-full` preset remains an **incomplete integration scaffold**, not a supported release build. It expects Qt 6 and a JUCE checkout at `third_party/JUCE`; that submodule is absent, and the direct WASAPI backend does not depend on JUCE. The Qt UI builds and starts on Linux, and the Windows engine path cross-compiles, but a Windows Qt build, real-device playback, VST3 host and installer have not been validated as a complete Windows product. Follow [`docs/BUILD.md`](docs/BUILD.md); do not assume a `Setup.exe` is releasable.

## Contribution workflow

Use a focused branch such as `feature/<short-name>`, keep the core test suite green, and run the repository's clang-format check before opening a PR. Do not describe a feature as complete until it is integrated in the application and has an end-to-end acceptance test.

## License

MIT License — see [`LICENSE`](LICENSE).
