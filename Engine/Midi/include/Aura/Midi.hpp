#pragma once

/// @file Midi.hpp
/// @brief MIDI messages, clips and editing operations (quantize, humanize).

#include <cstdint>
#include <string>
#include <vector>

namespace Aura::Midi {

/// @brief MIDI note utilities.
[[nodiscard]] std::string noteName(int midiNote); ///< e.g. 60 -> "C4"
[[nodiscard]] double noteFrequency(int midiNote); ///< e.g. 69 -> 440.0
[[nodiscard]] bool isBlackKey(int midiNote);

/// @brief A single timestamped MIDI event (time in beats).
struct Message {
    enum class Type { NoteOn, NoteOff, ControlChange, PitchBend, ProgramChange };

    Type type = Type::NoteOn;
    int channel = 0;          ///< 0..15
    int note = 60;            ///< 0..127
    int velocity = 100;       ///< 0..127
    int cc = 0;               ///< controller number for ControlChange
    int ccValue = 0;          ///< controller value
    int pitchBend = 0;        ///< -8192..8191
    double beat = 0.0;        ///< position in beats
    double lengthBeats = 1.0; ///< note duration (for paired note events)

    static Message noteOn(int note, int velocity, double beat, double lengthBeats = 1.0,
                          int channel = 0);
    static Message noteOff(int note, double beat, int channel = 0);
};

/// @brief A MIDI clip: an ordered collection of messages with editing ops.
class Clip {
  public:
    explicit Clip(double lengthBeats = 4.0);

    void addMessage(const Message& msg);
    void removeMessage(std::size_t index);
    void clear();

    [[nodiscard]] const std::vector<Message>& messages() const { return messages_; }
    [[nodiscard]] std::size_t size() const { return messages_.size(); }

    [[nodiscard]] double lengthBeats() const { return lengthBeats_; }
    void setLengthBeats(double beats);

    /// @brief Snaps note starts to a grid. strength 0..1 (1 = full quantize).
    void quantize(double gridBeats, double strength = 1.0);
    /// @brief Randomizes timing/velocity slightly for a human feel.
    /// @param timingBeats   Max timing offset in beats (e.g. 0.02).
    /// @param velocityRange Max velocity deviation (e.g. 8).
    /// @param seed          RNG seed for deterministic results.
    void humanize(double timingBeats, int velocityRange, std::uint32_t seed);
    /// @brief Transposes all notes by semitones (clamped to 0..127).
    void transpose(int semitones);
    /// @brief Scales all velocities by a factor (clamped to 1..127).
    void scaleVelocity(double factor);

  private:
    void sortByBeat();

    std::vector<Message> messages_;
    double lengthBeats_;
};

} // namespace Aura::Midi
