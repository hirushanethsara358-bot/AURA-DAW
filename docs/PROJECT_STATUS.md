# AURA DAW — Engineering readiness audit

**Audit date:** 2026-10-07

**Repository:** `hirushanethsara358-bot/AURA-DAW`

**Audit branch:** `feature/readiness-audit` (local working copy; no push performed during this audit)

## Executive summary

The repository is a promising, tested **core prototype**, not a commercial-grade or end-to-end digital audio workstation. The earlier `1.0.0` README language overstated what is implemented. This audit corrects the product claims and identifies the shortest safe path to a usable Windows MVP.

There are tested core libraries for DSP, mixing, MIDI helpers, project serialization, a synth/sampler prototype and heuristic music analysis. The one-WAV `AURA::Session` callback path now has sample-rate and tempo rescheduling, live track gain/pan, seek/stop, and a master peak meter. It is wired to QML transport/mixer/status/retry controls. A Windows shared-mode WASAPI backend is implemented, and the Windows core plus test executable cross-compile/link with MinGW. The Qt 6.8.2 application now builds and starts in an offscreen Linux smoke test; `qmllint` exits 0 with unqualified-access warnings. Windows tests, a Windows Qt build, and real endpoint playback have not been run; no audible playback claim is made. Multi-clip scheduling, production mixer real-time safety, VST3 and installer validation remain release blockers.

## Verification performed

### Local working branch

- Configured `cmake --preset core` with CMake 4.4.4 and Ninja.
- Built all core libraries and the Google Test executable successfully.
- The latest Linux core build and `ctest --preset core --output-on-failure` completed all **85** tests on 2026-10-07: **84 passed, 1 Windows-only test skipped**, 0 failed. Coverage includes negotiated sample-rate and tempo rescheduling plus callback deadline-miss counting.
- Cross-compiled the Windows `AURA::Engine`, `AURA::Session`, all core libraries and the test executable with MinGW; the WASAPI smoke executable linked. Windows tests were not executed because this environment cannot run PE programs.
- Configured/built the Qt 6.8.2 application in `build/ui`; QML cache generation succeeded. An offscreen Linux startup smoke test ran for eight seconds with no QML runtime errors. `qmllint` exits 0 and reports 59 unqualified-access warnings. Windows Qt build/device playback have not been run.
- clang-format 18 dry-run passed across all C++ sources. The core preset does not build the Qt UI or NSIS installer.

### Published GitHub baseline before this branch's changes

The public repository's latest CI runs for commit `a050f6ba99e8abe9b37950ce6f05d618ede44a38` reported:

- Ubuntu core build + tests: **passed**.
- Windows core build + tests: **passed**.
- clang-format check: **failed** on both `main` and `development`.

Run links: [main CI](https://github.com/hirushanethsara358-bot/AURA-DAW/actions/runs/37560699462) · [development CI](https://github.com/hirushanethsara358-bot/AURA-DAW/actions/runs/37560698604).

This branch reformats the C++ files and includes `Plugins/` and the new `Session/` module in the format check. GitHub has not verified these local changes; a fresh CI run requires the branch to be pushed.

## Important findings

| Finding | Evidence in code | Severity |
|---|---|---|
| Windows hardware playback remains unverified | Windows shared-mode WASAPI is implemented and cross-compiles/links, but has not been exercised on a real endpoint; device negotiation, disconnect recovery, underruns and restart still need Windows QA. | Release blocker |
| Mixer is not audio-thread safe | `Mixer::processBlock()` constructs a vector each call; strip processing copies sends and takes mutexes while processing inserts; state access is lock-based. | Release blocker |
| Windows Qt/audio application is unverified | MainWindow/QML connects Play/Stop/rewind/seek, output status/retry, CPU/peak meters and track fader/pan to the one-clip session. Linux Qt 6.8.2 build/startup is verified; Windows Qt integration and Windows end-to-end audio behavior remain untested. | Release blocker |
| VST3 is not hosted | VST3 `open()` is a stub; it does not instantiate the Steinberg SDK component. The README previously implied scanning/sandboxing that the implementation does not provide. | Release blocker |
| Codec claims were inaccurate | The sampler contains a basic WAV parser for limited PCM/float formats; `AudioFileFormat` enum values do not implement MP3/FLAC/AIFF import/export. | High |
| Project recovery is incomplete | Serialization covers a basic session model; autosave is a callable helper, not a running five-minute scheduler. UI does not call it. | High |
| Full Windows product path is unverified | The Windows core/WASAPI code cross-builds, but `windows-full` still requires Qt and installer validation; the optional JUCE submodule is absent and is not used by the direct WASAPI backend. | High |
| Installer/signing claims were premature | CPack/NSIS metadata exists, but there is no verified install/launch test, the referenced custom icon is absent, and the `AURA_SIGN_TOOL` variable does not actually sign anything. | High |
| Tempo-map behavior is not complete | `Transport` stores tempo markers, but position conversion uses one `tempo_` value; markers are not applied as the playhead moves. | Medium |
| Documentation overstated release state | Original README called the app professional and listed hardware drivers, codecs, side-chain, sandboxing and a signed setup that are not delivered. | High (trust) |

## Changes made on this branch

- Added a Windows-only shared-mode WASAPI output driver with endpoint enumeration, mix-format conversion, callback format negotiation, underrun/error reporting, restart support and actual endpoint-buffer latency reporting; Linux continues to expose only the offline `Dummy` driver.
- Added a Windows factory test, negotiated-format tests, callback deadline-miss counting coverage, and session tests for sample-rate/tempo rescheduling, transport position, peak meters and playback integration. GoogleTest executable discovery is skipped during cross-compilation so MinGW can link the PE test binary without trying to run it on Linux. Unsupported ASIO/CoreAudio backends still fail explicitly.
- Formatted the C++ sources and expanded the GitHub clang-format check to include `Plugins/` and `Session/`.
- Rewrote README, architecture, build and roadmap documentation to distinguish prototype code from target design and to remove unsupported release claims.
- Hardened `.aura` load into a validate-then-commit operation; saves now validate and write through a temporary file before atomic replacement, preserving the existing project on failure.
- Added project clip/mixer mutators, structural/range checks and path/file-size guards, with regression coverage for round-trips, transactional load/save and invalid inputs.
- Hardened WAV decoding for PCM16, PCM24 and FLOAT32: validates container/chunks, reads payload in bounded blocks, rejects non-finite float samples, and enforces a configurable decoded-memory cap. Added `AudioBufferManager` with path caching, handle lookup/release and a total PCM budget. Streaming for files beyond that budget is not implemented yet.
- Added timeline-frame placement, seek, resume and end-of-range behavior to the standalone one-clip `WavClipPlayer`, with linear resampling and atomic gain/pan controls. Its render path uses no heap allocation, blocking lock, or file I/O; offline tests cover scheduling and transport.
- `ProjectPlaybackSession` now accepts one audio clip, reschedules 4/4 timeline frames after negotiated sample-rate or tempo changes while preserving musical position, supports bar seek/stop, applies live track gain/pan/master gain and publishes callback-safe playhead/peak data. Multi-clip scheduling is still rejected.
- MainWindow/QML now starts the audio engine, prepares the session, wires Play/Stop/rewind/seek and tempo changes, reflects clip-end/device errors, allows audio retry, and connects track fader/pan plus CPU/peak meters. WAV decoding still happens synchronously at first Play.
- Qt 6.8.2 Linux UI build and offscreen startup smoke test pass. Added the missing `QtQuick.Layouts` import and corrected Browser delegate sizing; `qmllint` exits 0 with 59 remaining unqualified-access warnings. The UI exposes negotiated rate/channel status, approximate latency, and the callback deadline-miss counter for manual QA. Qt Windows build, Windows tests, device negotiation/audible playback, installer and real VST3 execution remain open.

## Recommended development order (confirmed scope)

1. **Phase 1 — AURA DAW 0.1 MVP:** Qt project/timeline → WAV import → Play/Stop → basic volume/pan → real WASAPI output → Save/Open. Use prepared assets and a bounded callback with no heap allocation, blocking locks, or I/O; prove the slice on Windows 10/11 x64 before broadening scope.
2. **Phase 2 — Professional audio engine:** general lock-free command/event path, real-time thread model, preallocated/multicore DSP, low-latency mode, ASIO, and load/device-failure stress tests.
3. **Phase 3 — Music production:** MIDI recording and piano roll, SDK-backed VST3 processing and state, automation and integrated effects/instruments.
4. **Phase 4 — AURA AI:** surface BPM/key/chord/melody analysis and build mixing, EQ, compression, mastering-chain, and arrangement recommendations with clear confidence/limitations.
5. **Phase 5 — AURA DAW 1.0:** full feature verification, Windows installer, actual code signing, scheduled autosave/recovery, compatibility/performance QA, documentation, security and license review, and any approved non-WAV codecs.

The MVP still requires a minimally safe, bounded playback callback; Phase 2 is for the generalized professional engine (lock-free control transport, multicore processing, ASIO and advanced low-latency behavior). Installer/signing are Phase 5, not 0.1. No release dates are assigned.

## Connected-app coordination

The initial search found no AURA DAW-specific Google Drive document, Linear project or Notion page. This audit created a Google Docs brief and a Linear project/backlog so there is one task tracker rather than duplicate boards:

- [Google Docs readiness brief](https://docs.google.com/document/d/1wdpC-b0h-TnPhNSog-JmHtrdolgXWu_gDD1TGgRDWdk/edit?usp=drivesdk)
- [Phase 1 — AURA DAW 0.1 MVP, HIR-6](https://linear.app/hirushanet/issue/HIR-6/phase-1-aura-daw-01-playable-windows-mvp)
- [Phase 2 — Professional audio engine, HIR-13](https://linear.app/hirushanet/issue/HIR-13/phase-2-professional-audio-engine); callback/thread-safety task HIR-11.
- [Phase 3 — Music production, HIR-14](https://linear.app/hirushanet/issue/HIR-14/phase-3-music-production-features); VST3 task HIR-10.
- [Phase 4 — AURA AI, HIR-16](https://linear.app/hirushanet/issue/HIR-16/phase-4-aura-ai-analysis-and-production-assistant).
- [Phase 5 — AURA DAW 1.0 release, HIR-15](https://linear.app/hirushanet/issue/HIR-15/phase-5-aura-daw-10-release-installer-and-signing).
- Phase 1 tasks: HIR-7 through HIR-9 and HIR-12. CI/publish follow-up: [HIR-5](https://linear.app/hirushanet/issue/HIR-5/publish-the-readiness-audit-branch-and-confirm-ci-is-green).

No duplicate Notion project was created. Gmail and Calendar were left untouched because there is no recipient or agreed milestone date, so sending mail or placing events would be inappropriate. This Markdown report remains version-controlled in the repository.
