# AURA DAW — Product roadmap

**Planning baseline:** 2026-10-07

**Current status:** tested C++ core prototype; the repository's `1.0.0` value is version metadata, not release approval. No delivery dates are assigned until scope and capacity are agreed.

The project has not failed. It has a useful, tested foundation; it is not yet a commercial-grade or end-to-end DAW. The next product milestone is **AURA DAW 0.1**, not 1.0.

## Phase 0 — Core prototype (current)

### Verified foundation

- Linux `core` configure/build and unit tests pass locally: **84 passed, 1 platform-specific test skipped** out of 85 on 2026-10-07. GitHub Actions native Ubuntu and Windows core build/tests plus clang-format passed for commit `63b97cf` ([run](https://github.com/hirushanethsara358-bot/AURA-DAW/actions/runs/37573620173)); actual Windows audio hardware remains untested.
- DSP/mixer logic, MIDI helpers, project serialization, synth/sampler prototypes and heuristic analysis are present. WAV decode is bounded and memory-capped.
- `ProjectPlaybackSession` and the QML controller now connect a single project WAV clip to Play/Stop/rewind/seek, negotiated rate/tempo rescheduling, live track gain/pan and CPU/peak meters. Multi-clip playback, background decode/streaming and the generalized real-time mixer remain incomplete.
- The Windows shared-mode WASAPI backend uses the default render endpoint's mix format and reports startup/runtime errors. It compiles and links, but sample-rate/channel behavior, audible playback, disconnect, underrun and restart have not been validated on Windows hardware.
- Qt 6.8.2 UI build and offscreen Linux startup smoke test pass; `qmllint` exits 0 with 59 unqualified-access warnings. The UI displays negotiated output rate/channels, estimated latency and a callback deadline-miss counter for acceptance testing. Windows Qt integration, real-device playback, installer and VST3 execution remain unverified.
- Plugin scanning/management scaffolding exists, but there is no actual VST3 loading or processing.
- The GitHub Actions format check now passes, including `Plugins/` and the new `Session/` module. The original published baseline's formatting failure was corrected and verified in the pushed branch.

## Phase 1 — AURA DAW 0.1: real DAW MVP

**Target workflow:** WAV → timeline → Play/Stop → volume/pan mixer → real WASAPI output → Save/Open.

### Scope

1. A basic Qt interface with New/Open/Save, a usable timeline, and project tracks/clips.
2. Import supported WAV files into an arrangement track; decode/load assets away from the audio callback.
3. Play/Stop timeline transport and route WAV playback through a simple mixer to a real Windows WASAPI output device.
4. Basic track/master volume and pan controls connected to the audio path. If the current generalized mixer cannot meet the callback constraints, use a minimal preallocated MVP mixer rather than calling the known unsafe processing path.
5. Save and reopen the project with clear errors for failed imports, unsupported devices, or invalid project files.
6. Reproducible Windows 10/11 x64 application build. Installer and signing are deliberately deferred to Phase 5.

### 0.1 acceptance gates

- On Windows 10/11 x64, create a project, import a supported WAV, see it on the timeline, press Play/Stop, hear it through the selected WASAPI device, adjust volume/pan, save, restart, and reopen it.
- Playback uses prepared audio assets and bounded callback work: no heap allocation, blocking locks, disk/UI work, or unbounded operations on the audio thread. A general lock-free command/event transport and advanced scheduling remain Phase 2 work.
- Device and file errors are reported honestly; there is no silent fallback to Dummy output.
- Core tests and a Windows application build pass. No 0.1 claim implies ASIO, VST3, MIDI recording, AI, non-WAV codecs, installer, or code signing.

## Phase 2 — Professional audio engine

- Define a real-time thread/ownership model and bounded lock-free command/event queues for control-to-audio updates.
- Expand beyond the 0.1 playback path to a robust, preallocated multi-track DSP graph and multicore processing.
- Add low-latency configuration and profiling, callback-budget/underrun metrics, stress tests, and device-loss/recovery coverage.
- Add and validate an ASIO backend alongside WASAPI; document supported devices and operating modes.
- Preserve deterministic offline rendering and test real-time behavior under load.

## Phase 3 — Music production features

- MIDI input and recording, sample-timed note scheduling, and an editable piano roll connected to projects and playback.
- SDK-backed VST3 loading and processing, parameter/state persistence, validation with real test plugins, and a genuine separate-process sandbox if sandboxing is claimed.
- Automation lanes and safe parameter updates; production effects/instrument workflows integrated with the mixer.

## Phase 4 — AURA AI

- Present BPM, key, chord, and melody analysis in the product, with confidence/uncertainty clearly communicated. Basic heuristic analysis already exists in the prototype.
- Add explainable mixing-fix, EQ, compression, mastering-chain, and song-arrangement suggestions.
- Treat inference, generated material, and any voice features as optional until privacy, latency, licensing, safety, and reproducibility are verified.

## Phase 5 — AURA DAW 1.0 release

- Complete release validation for the full Windows x64 product: audio engine, mixer, instruments, MIDI, VST3, and AURA AI capabilities.
- Produce and test the Windows installer on a clean machine; implement a real code-signing and signature-verification pipeline before claiming signed binaries.
- Add any committed non-WAV codec support (for example MP3/FLAC/AIFF) only with dependency/license review, fixtures, malformed-file tests, and export verification.
- Complete project migration, scheduled autosave/recovery and backups, compatibility/performance matrices, crash/device-failure regression tests, accessible UI, user documentation, security review, and license inventory.

**1.0 release gate:** all advertised features must be implemented and tested on supported Windows x64 configurations; the installer must install, launch, upgrade/recover as specified, and uninstall cleanly. Never describe unsigned artifacts as signed or a stub as a working feature.
