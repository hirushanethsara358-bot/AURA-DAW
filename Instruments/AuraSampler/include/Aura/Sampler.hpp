#pragma once

/// @file Sampler.hpp
/// @brief AURA Sampler: multi-sample key-mapped sample playback.
///
/// Loads WAV files (PCM 16/24-bit, float32, mono/stereo), maps them to key
/// zones with per-zone root notes, and renders polyphonic voices with
/// linear-interpolated resampling. Real-time safe after prepare().

#include <cstddef>
#include <cstdint>
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

/// @brief WAV header details needed by the arrangement without decoding PCM data.
struct WavMetadata {
    std::uint16_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t bitsPerSample = 0;
    std::uint16_t audioFormat = 0;
    std::uint64_t frames = 0;
    std::uint64_t dataOffset = 0;
    std::uint64_t dataBytes = 0;
    double durationSeconds = 0.0;
};

struct WavMetadataResult {
    bool ok = false;
    WavMetadata metadata;
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

/// @brief Reads/validates the supported WAV header without allocating for the audio payload.
[[nodiscard]] WavMetadataResult inspectWavFile(const std::string& path);

/// @brief Default decoded-memory cap for one WAV file (interleaved source bytes excluded).
inline constexpr std::uint64_t kDefaultMaxDecodedWavBytes = 512ULL * 1024ULL * 1024ULL;

/// @brief Decodes a WAV file into SampleData using bounded temporary storage.
/// @param maxDecodedBytes Maximum PCM memory to allocate for the decoded channels.
///        Large files that exceed the limit fail explicitly; callers should stream them later.
[[nodiscard]] SampleLoadResult
parseWavFile(const std::string& path, SampleData& out,
             std::uint64_t maxDecodedBytes = kDefaultMaxDecodedWavBytes);

} // namespace Aura::Instrument
