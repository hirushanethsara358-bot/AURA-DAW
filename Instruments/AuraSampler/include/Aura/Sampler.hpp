#pragma once

/// @file Sampler.hpp
/// @brief AURA Sampler: multi-sample key-mapped sample playback.
///
/// Loads WAV files (PCM 16/24-bit, float32, mono/stereo), maps them to key
/// zones with per-zone root notes, and renders polyphonic voices with
/// linear-interpolated resampling. Real-time safe after prepare().

#include <cstddef>
#include <string>
#include <vector>

namespace Aura::Instrument {

/// @brief Decoded sample data (mono or stereo, 64-bit float).
struct SampleData {
    std::vector<double> left;
    std::vector<double> right; // empty when mono
    double sampleRate = 48000.0;
    std::string name;
    [[nodiscard]] bool isStereo() const { return !right.empty(); }
    [[nodiscard]] std::size_t frames() const { return left.size(); }
};

/// @brief Maps a key range to a sample with a root note.
struct SampleZone {
    std::size_t sampleIndex = 0;
    int keyLow = 0;
    int keyHigh = 127;
    int rootNote = 60;
    double gainDb = 0.0;
};

/// @brief Result of loading a sample file.
struct SampleLoadResult {
    bool ok = false;
    std::size_t sampleIndex = 0;
    std::string error;
};

/// @brief Polyphonic key-mapped sampler.
class Sampler {
public:
    static constexpr int kMaxVoices = 64;

    void setMasterGainDb(double db) { masterGainDb_ = db; }

    /// @brief Loads a WAV file into the sample pool.
    [[nodiscard]] SampleLoadResult loadWavFile(const std::string& path);
    /// @brief Adds in-memory sample data (useful for tests / factory content).
    std::size_t addMemorySample(SampleData data);
    [[nodiscard]] std::size_t sampleCount() const { return samples_.size(); }

    void addZone(const SampleZone& zone);
    void clearZones();
    [[nodiscard]] const std::vector<SampleZone>& zones() const { return zones_; }

    void prepare(double sampleRate);
    void noteOn(int midiNote, int velocity);
    void noteOff(int midiNote);
    void allNotesOff();
    [[nodiscard]] int activeVoiceCount() const;

    /// @brief Renders voices (adds into the buffers).
    void renderBlock(double* left, double* right, int numSamples);

private:
    struct Voice {
        bool active = false;
        const SampleData* sample = nullptr;
        double position = 0.0;  ///< Fractional playback position in frames.
        double increment = 1.0; ///< Resampling ratio (pitch shift).
        double gain = 0.0;
        int note = -1;
        bool releasing = false;
        double releaseGain = 1.0;
    };

    [[nodiscard]] const SampleZone* zoneForNote(int note) const;

    std::vector<SampleData> samples_;
    std::vector<SampleZone> zones_;
    std::vector<Voice> voices_;
    double outputRate_ = 48000.0;
    double masterGainDb_ = 0.0;
};

/// @brief Parses a WAV file into SampleData. ok=false with error on failure.
[[nodiscard]] SampleLoadResult parseWavFile(const std::string& path, SampleData& out);

} // namespace Aura::Instrument
