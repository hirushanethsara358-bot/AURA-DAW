#pragma once

/// @file AudioDeviceTypes.hpp
/// @brief Audio driver / device configuration types for the AURA audio engine.

#include <array>
#include <string>
#include <vector>

namespace Aura::Audio {

/// @brief Supported low-level audio driver backends.
enum class DriverType {
    WASAPI,    ///< Windows Audio Session API (default on Windows).
    ASIO,      ///< Steinberg ASIO (pro interfaces, lowest latency).
    CoreAudio, ///< macOS CoreAudio (compatibility layer).
    Dummy      ///< Offline / test driver with no hardware.
};

/// @brief Well-known sample rates supported by the engine.
struct SampleRates {
    static constexpr double k44100 = 44100.0;
    static constexpr double k48000 = 48000.0;
    static constexpr double k96000 = 96000.0;
    static constexpr double k192000 = 192000.0;

    static constexpr std::array<double, 4> all() {
        return {k44100, k48000, k96000, k192000};
    }
};

/// @brief Import/export audio file formats.
enum class AudioFileFormat {
    WAV,
    MP3,
    FLAC,
    AIFF
};

/// @brief Returns the file extension (without dot) for a format.
constexpr const char* fileExtension(AudioFileFormat format) {
    switch (format) {
    case AudioFileFormat::WAV:
        return "wav";
    case AudioFileFormat::MP3:
        return "mp3";
    case AudioFileFormat::FLAC:
        return "flac";
    case AudioFileFormat::AIFF:
        return "aiff";
    }
    return "wav";
}

/// @brief Complete configuration for opening an audio device.
struct AudioDeviceConfig {
    DriverType driver = DriverType::WASAPI;
    double sampleRate = SampleRates::k48000;
    int bufferSize = 256; ///< Frames per callback. 64–2048, power of two preferred.
    int numInputChannels = 2;
    int numOutputChannels = 2;
    bool lowLatencyMode = true; ///< Exclusive-mode streams + smallest safe buffers.
    bool enableInputs = true;

    /// @brief Validates the configuration; returns empty string when valid.
    [[nodiscard]] std::string validate() const;
};

/// @brief Describes one enumerated hardware device.
struct AudioDeviceInfo {
    std::string id;
    std::string name;
    DriverType driver = DriverType::WASAPI;
    int maxInputChannels = 0;
    int maxOutputChannels = 0;
    std::vector<double> supportedSampleRates;
    bool isDefaultInput = false;
    bool isDefaultOutput = false;
};

/// @brief Converts a driver type to a human-readable name.
const char* toString(DriverType driver);

} // namespace Aura::Audio
