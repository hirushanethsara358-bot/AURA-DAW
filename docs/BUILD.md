# AURA DAW — Build instructions

## 1. Core build (any platform, no Qt/JUCE)

Used for DSP/engine development and CI.

Requirements: CMake ≥ 3.24, Ninja, C++20 compiler
(GCC 12+, Clang 15+, MSVC 2022).

```bash
cmake --preset core
cmake --build --preset core
ctest --preset core
```

## 2. Full Windows build

Requirements:

| Tool | Version |
|------|---------|
| Windows | 10/11 x64 |
| Visual Studio | 2022 (MSVC 19.35+, Windows SDK 10.0.22621+) |
| CMake | ≥ 3.24 |
| Ninja | latest |
| Qt | 6.5+ (`QT6_DIR` env var pointing at `lib/cmake/Qt6`) |
| JUCE | 8.x as `third_party/JUCE` submodule |
| NSIS | 3.x (installer only) |

```powershell
git clone --recurse-submodules https://github.com/hirushanethsara358-bot/AURA-DAW.git
cd AURA-DAW
$env:QT6_DIR = "C:\Qt\6.7.0\msvc2022_64"
cmake --preset windows-full
cmake --build --preset windows-full --config Release
ctest --preset windows-full --config Release
```

The installer is produced at:

```
build/windows-full/Installer/AURA-DAW-Setup.exe
```

### JUCE submodule setup

```powershell
git submodule add https://github.com/juce-framework/JUCE.git third_party/JUCE
```

### Code signing (release)

Set `AURA_SIGN_TOOL` to your `signtool.exe` command line; the installer
target signs `AURA DAW.exe` and the setup binary before packaging.
See `Installer/CMakeLists.txt`.

## 3. Audio import/export codecs

WAV/AIFF/FLAC are decoded internally. MP3 import/export uses the
platform Media Foundation encoder on Windows (no third-party binaries
shipped).

## 4. Troubleshooting

| Symptom | Fix |
|---------|-----|
| `Qt6 not found` | Set `QT6_DIR` / `CMAKE_PREFIX_PATH` |
| JUCE CMake errors | Ensure `third_party/JUCE` submodule is initialized |
| ASIO not listed | Install vendor ASIO driver; run DAW with admin once for registration |
| High DPC latency | Increase buffer size in Audio Settings; enable low-latency mode |
