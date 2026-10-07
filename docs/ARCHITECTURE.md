# AURA DAW — Architecture and implementation status

**Language:** C++20 · **Build:** CMake/Ninja · **Product status:** experimental core prototype

This page describes what exists separately from the architecture AURA should grow into. Terms such as "real-time safe", "sandboxed" and "hardware backend" below are targets unless explicitly marked implemented.

## Current implementation

- **Engine:** audio-device configuration types, callback format negotiation/error reporting, basic meters, offline `DummyDriver`, and a Windows-only event-driven shared-mode WASAPI render backend using the endpoint mix format. The WASAPI source and complete core target cross-compile/link with MinGW; GitHub Actions' native Windows build/tests pass (84 passed, one platform-specific test skipped). No real Windows audio endpoint has been exercised. ASIO/CoreAudio remain unavailable.
- **Mixer/DSP:** experimental in-memory stereo strip/bus processing and DSP algorithms with unit tests. Mixer callback processing currently takes locks and makes dynamic allocations; it is not suitable for a production real-time audio callback.
- **Transport/MIDI:** transport state, position conversion and MIDI editing helpers. Tempo markers are stored but do not yet drive variable-tempo position conversion or sample scheduling.
- **Project/session:** `.aura` JSON model with transactional save/load plus `AURA::Session::ProjectPlaybackSession`, which prepares exactly one WAV clip for the callback, maps 4/4 bars to negotiated output frames, preserves bar position across device-rate/tempo changes, publishes transport state, applies live track gain/pan and measures a callback-safe master peak. The Qt controller now wires Play/Stop/rewind/seek, output status/retry, CPU/peak meters and mixer faders to this session. Multi-clip support and the generalized mixer path remain out of scope; WAV decoding still happens synchronously when Play is first pressed.
- **Instruments/AI:** polyphonic synth, bounded PCM16/PCM24/FLOAT32 WAV decoder/cache manager, and deterministic/heuristic analyzer/advisor code. The underlying `WavClipPlayer` supports timeline-frame scheduling, seek, linear resampling and atomic fader/pan; `AURA::Session` consumes it for one project clip in offline tests. Streaming and release-quality instrument workflows remain unfinished.
- **Plugins:** descriptor scanner/manager interface and VST3 placeholder. No VST3 component is instantiated; no separate-process sandbox exists.
- **UI:** Qt6/QML New/Open/Save, WAV timeline, transport/seek, output status/retry, CPU/peak meters and live track fader/pan controls are wired to the one-clip session. Qt 6.8.2 Linux build and offscreen startup smoke test pass; `qmllint` exits 0 with 59 unqualified-access warnings. The UI displays negotiated rate/channel status, estimated output latency, and a callback deadline-miss counter. Windows Qt build and audible Windows hardware playback are unverified.

## Target design principles

1. **Audio-thread discipline:** after device start, the audio callback must not allocate, block on mutexes, access files, wait on the UI, or run unbounded work. Use prepared buffers and bounded, non-blocking command/event queues.
2. **Explicit device capability:** only enumerate and start backends that are compiled and available. Never silently substitute offline rendering for a requested hardware stream.
3. **64-bit DSP path:** keep internal DSP in `double` where practical, while negotiating device formats and documenting any conversions at the boundary.
4. **Clear ownership:** the message thread owns project/UI state; the audio thread owns active render state. Transfer changes through immutable snapshots or bounded queues.
5. **Incremental integration:** tests should cover both isolated algorithms and complete paths (project → track/source → mixer → device; save → close → reopen).
6. **Truthful feature contracts:** a scanner is not a plugin host, an enum is not a codec, a QML mock is not a session editor, and a packaging script is not a signed release.

## Target module map

```text
Qt/QML UI (target)
  ├─ project editor / timeline / piano roll / mixer
  └─ bounded commands + read-only meter snapshots
                 │
                 ▼
Application/session layer (incomplete)
  ├─ versioned project model + migrations + recovery
  ├─ media asset manager and decoding workers
  └─ track graph / transport / automation scheduler
                 │
                 ▼
Audio runtime (incomplete; Dummy offline/Linux, WASAPI on Windows)
  ├─ device backend (shared-mode WASAPI on Windows; ASIO follow-up)
  ├─ preallocated graph and mixer render path
  ├─ instruments, effects and output metering
  └─ plugin host (SDK-backed VST3; isolation design pending)
```

## Current CMake libraries

| CMake target | Contents |
|---|---|
| `AURA::Engine` | Device/config API, offline driver, Windows shared-mode WASAPI backend, DSP, mixer, MIDI helpers, transport and plugin host API |
| `AURA::Plugins` | VST3 wrapper stub |
| `AURA::AI` | Music analyzer and assistant heuristics |
| `AURA::Instruments` | AURA Synth and WAV sampler prototypes |
| `AURA::Project` | `.aura` model and JSON save/load/autosave helper |
| `AURA::Session` | One-clip WAV preparation, rate/tempo rescheduling, live track controls and callback adapter (offline-tested; Linux Qt integration built) |
| `aura_daw_app` | Qt/QML project/timeline shell (Qt 6.8.2 Linux build/startup smoke verified; Windows build remains unverified) |

## Next integration boundary

The one-clip session now feeds both offline tests and a Qt controller path. The shared-mode WASAPI backend is implemented and cross-builds, and QML transport/mixer/output-status controls are connected. Linux Qt build/startup is verified; the remaining 0.1 gate is Windows verification: Qt build, device negotiation, audible Play/Stop, disconnect/retry, underrun and restart, then Save/Open acceptance on Windows 10/11 x64. Multi-clip scheduling, streaming, ASIO and the generalized real-time mixer remain later work. See [`ROADMAP.md`](ROADMAP.md).
