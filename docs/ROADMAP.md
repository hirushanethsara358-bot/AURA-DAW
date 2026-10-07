# AURA DAW — Development roadmap

## Phase 1 — Foundation ✅ (v1.0.0, this release)

- [x] CMake monorepo, presets, CI (Ubuntu + Windows), clang-format
- [x] 64-bit audio engine core + driver abstraction (ASIO/WASAPI/CoreAudio)
- [x] Mixer: strips, buses, master, inserts/sends
- [x] DSP: parametric EQ, compressor, limiter, gate, reverb, delay,
      chorus, flanger, distortion
- [x] MIDI: messages, clips, quantize, humanize, velocity tools
- [x] `.aura` project format + autosave
- [x] Transport: tempo map, play/record state
- [x] AURA Synth (osc/filter/ADSR/LFO) + sampler core
- [x] AI: BPM/key detection, chord & melody suggestion, mix advisor
- [x] Plugin host API + VST3 scanner/manager (sandboxed)
- [x] Qt6/QML UI shell: main window, mixer, piano roll, browser
- [x] NSIS installer definition
- [x] Unit tests for all core modules

## Phase 2 — Mixer, MIDI, plugin hosting

- [ ] Side-chain routing UI
- [ ] Automation lanes + curves
- [ ] Full VST3 hosting via Steinberg SDK (in-process + sandboxed)
- [ ] MIDI controller mapping (Mackie/HUI learn)
- [ ] Time-stretch (élastique-style) & pitch-shift DSP

## Phase 3 — Effects & instruments

- [ ] Spectrum analyzer overlay in EQ view
- [ ] Multiband compressor, transient shaper
- [ ] Convolution reverb + IR library
- [ ] AURA Sampler: key-mapping editor, multi-layer velocity stacks
- [ ] 200+ factory presets + 2 GB sample library

## Phase 4 — AI assistant

- [ ] On-device neural mastering chain
- [ ] Stem separation preview
- [ ] Generative arrangement suggestions
- [ ] Voice-controlled transport ("AURA, record the chorus")

## Phase 5 — Professional release

- [ ] Signed binaries & installer, crash reporter (Sentry)
- [ ] Activation/licensing server
- [ ] User manual, video tutorials, demo projects
- [ ] AAX/AU compatibility layers
- [ ] Performance certification on min-spec hardware (i5-6xxx, 8 GB RAM)
