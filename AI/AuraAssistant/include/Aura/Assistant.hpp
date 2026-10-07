#pragma once

/// @file Assistant.hpp
/// @brief AURA AI assistant: music theory engine, chord/melody suggestions,
///        mix and mastering advice.
///
/// Deterministic and dependency-free: suggestions are generated from music
/// theory rules plus offline analysis results. Neural models plug in behind
/// the same interfaces in Phase 4.

#include <cstdint>
#include <string>
#include <vector>

namespace Aura::Ai {

// ------------------------------------------------------------ MusicTheory

/// @brief Music theory helpers: scales, keys, chord spellings.
class MusicTheory {
  public:
    /// @brief Semitone offsets for the natural major / minor scale.
    [[nodiscard]] static std::vector<int> scaleIntervals(bool minor);
    /// @brief Pitch-class names C..B.
    [[nodiscard]] static std::vector<std::string> pitchNames();

    /// @brief Parses keys like "A minor" / "F# major". Returns false when unknown.
    static bool parseKey(const std::string& key, int& rootOut, bool& minorOut);
    /// @brief Formats a key from root pitch class + mode.
    [[nodiscard]] static std::string formatKey(int root, bool minor);

    /// @brief MIDI notes of the scale across the given octave range.
    [[nodiscard]] static std::vector<int> scaleNotes(int root, bool minor, int lowOctave = 3,
                                                     int highOctave = 5);
};

// ----------------------------------------------------------------- Chords

/// @brief A suggested chord in root position.
struct Chord {
    std::string name;           ///< e.g. "Am", "F", "G".
    std::vector<int> midiNotes; ///< MIDI pitches (octave 3-4).
    std::string function;       ///< e.g. "i", "VI", "VII".
};

/// @brief Suggests diatonic chords and progressions for a key.
class ChordSuggester {
  public:
    /// @brief Returns up to maxCount diatonic triads ordered by commonness.
    [[nodiscard]] std::vector<Chord> suggest(const std::string& key, int maxCount = 8) const;
    /// @brief Returns a full 4-chord progression (e.g. i–VI–III–VII in minor).
    [[nodiscard]] std::vector<Chord> progression(const std::string& key) const;
};

// ----------------------------------------------------------------- Melody

/// @brief A generated melody note.
struct MelodyNote {
    int midi = 60;
    double beat = 0.0;
    double lengthBeats = 0.5;
    int velocity = 90;
};

/// @brief Generates melodies by random-walk over the key scale (seeded).
class MelodyGenerator {
  public:
    /// @param key   Musical key, e.g. "A minor".
    /// @param bars  Number of bars to generate (4/4).
    /// @param seed  RNG seed for reproducible output.
    [[nodiscard]] std::vector<MelodyNote> generate(const std::string& key, int bars,
                                                   std::uint32_t seed) const;
};

// -------------------------------------------------------------- MixAdvice

/// @brief One actionable mixing suggestion.
struct MixAdvice {
    std::string category; ///< "Level", "Dynamics", "Stereo", "Headroom".
    std::string message;
};

/// @brief Rule-based mix advisor driven by level analysis.
class MixAdvisor {
  public:
    /// @param peakDb Peak level in dBFS, @param rmsDb RMS level in dBFS.
    [[nodiscard]] std::vector<MixAdvice> advise(double peakDb, double rmsDb) const;
};

// -------------------------------------------------------------- Mastering

/// @brief Suggested mastering chain settings.
struct MasterChainSuggestion {
    double eqLowShelfDb = 0.0;
    double eqHighShelfDb = 0.0;
    double compThresholdDb = -18.0;
    double compRatio = 2.0;
    double limiterCeilingDb = -1.0;
    double targetLufs = -14.0;
    std::string notes;
};

/// @brief Mastering assistant: maps analysis to a starting chain.
class MasteringAssistant {
  public:
    [[nodiscard]] MasterChainSuggestion
    suggest(double peakDb, double rmsDb, const std::string& targetPlatform = "streaming") const;
};

} // namespace Aura::Ai
