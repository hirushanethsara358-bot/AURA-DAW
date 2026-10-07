# AURA DAW — Build and verification notes

This guide distinguishes the portable core from the currently incomplete Qt/JUCE/Windows product path. See [`PROJECT_STATUS.md`](PROJECT_STATUS.md) before treating a successful core build as a usable DAW release.

## 1. Core libraries and tests

### Requirements

- CMake 3.24 or newer
- Ninja
- A C++20 compiler (GCC 12+, Clang 15+, or MSVC 2022)
- Network access for the first configure; CMake FetchContent retrieves `nlohmann/json` and GoogleTest

### Commands

```bash
cmake --preset core
cmake --build --preset core
ctest --preset core --output-on-failure
```

On non-Windows platforms this compiles the portable static libraries/tests with only the offline `Dummy` driver. Windows builds also compile and link the event-driven shared-mode WASAPI output backend. WASAPI device initialization and audible playback still require testing on a Windows machine with an active render endpoint.

## 2. Experimental Windows UI/JUCE configuration

The `windows-full` preset is **not a verified release configuration** yet. It currently requires:

- Windows 10/11 x64 and Visual Studio 2022 with the Windows SDK
- Qt 6.5+ with Quick, Quick Controls 2, Quick Dialogs 2 and Concurrent modules; set `QT6_DIR` to the Qt installation root expected by the preset
- A JUCE source tree at `third_party/JUCE` (the repository currently does not include it as a submodule)
- NSIS 3.x for packaging

The optional JUCE source tree is not used by the audio engine; Windows WASAPI is implemented directly against the Windows audio APIs. The engine and test executable cross-compile/link with MinGW, but were not run as Windows programs in this Linux environment. Qt 6.8.2 has been used to build the QML UI on Linux, and an offscreen startup smoke test runs without QML runtime errors; `qmllint` exits 0 with 59 unqualified-access warnings. The UI reports negotiated output rate/channels, approximate latency, and callback deadline misses to aid Windows acceptance testing. The Windows Qt build is still unverified. The VST3 implementation remains a stub. Until a Qt Windows build and device-level Play/Stop, disconnect/restart and Save/Open tests pass, this is not a releasable audio workstation.

The intended experimental commands, after Qt and JUCE are set up, are:

```powershell
cmake --preset windows-full
cmake --build --preset windows-full --config Release
ctest --preset windows-full --config Release --output-on-failure
```

These commands are documentation for the scaffold only; the full preset is not currently covered by CI or acceptance testing.

## 3. File formats and codecs

The AURA Sampler decodes mono/stereo PCM16, PCM24 and float32 WAV data in bounded read blocks; each decode is limited by a configurable decoded-memory budget (512 MiB by default). `AudioBufferManager` caches decoded buffers by path and tracks/reclaims their estimated PCM memory. `AURA::Session::ProjectPlaybackSession` loads exactly one audio clip from a `.aura` project, reschedules 4/4 bars for the output rate and tempo, supports seek/stop, applies live track gain/pan and reports a callback-safe peak meter. QML Play starts the audio engine, and the Windows backend converts the callback's double-precision stereo data to the endpoint mix format. Offline core tests cover the callback path; the Linux Qt UI builds and starts, but Windows hardware playback and the Windows Qt app remain unverified. Multi-clip mixing is not implemented. WAV decode currently runs synchronously on first Play and rejects files exceeding the memory budget; no streaming/background decode exists. MP3, FLAC and AIFF remain unimplemented.

## 4. Installer and code signing

`Installer/CMakeLists.txt` contains an experimental CPack/NSIS definition. The installer has not been validated on a clean Windows system. The custom icon is optional and not currently present. Code signing is **not implemented**; setting `AURA_SIGN_TOOL` does not sign the application or installer. Do not distribute a package as signed until a real signing command, certificate handling and verification are implemented and tested.

## 5. Formatting check

The GitHub Actions workflow uses clang-format 18. On Ubuntu, the equivalent check is:

```bash
find Engine AI Instruments Project Session Tests UI Plugins \
  \( -name '*.hpp' -o -name '*.cpp' \) -print0 \
  | xargs -0 clang-format-18 --dry-run --Werror
```

Run the same formatter in-place with `clang-format-18 -i` on the changed C++ files before pushing.

## 6. Troubleshooting

| Symptom | Guidance |
|---|---|
| FetchContent download fails | Check network/proxy access to GitHub; the first core configure downloads JSON and GoogleTest. |
| `Qt6 not found` | Install the required Qt 6 modules and set `QT6_DIR`/`CMAKE_PREFIX_PATH`. |
| `third_party/JUCE` is missing | The repository does not currently include JUCE; obtain the pinned source before enabling the experimental option. |
| `WASAPI` unavailable | Expected on non-Windows builds. Windows compiles the shared-mode backend; hardware availability/start errors are reported by the UI/engine. |
| Installer has no icon | The custom icon is optional and not checked into the repository. |
| Build succeeds but no sound plays | The Linux core preset uses `Dummy`; on Windows verify the default render endpoint, start error/status text, and select a valid WAV. Real-device playback is not yet validated in CI. |
