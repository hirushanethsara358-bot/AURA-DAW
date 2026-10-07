/// @file Midi.cpp
/// @brief Implementation of MIDI messages, clips and editing operations.

#include "Aura/Midi.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <random>

namespace Aura::Midi {

std::string noteName(int midiNote) {
    static constexpr std::array<const char*, 12> names = {"C",  "C#", "D",  "D#", "E", "F",
                                                         "F#", "G",  "G#", "A",  "A#", "B"};
    const int clamped = std::clamp(midiNote, 0, 127);
    const int octave = (clamped / 12) - 1;
    return std::string(names[static_cast<std::size_t>(clamped % 12)]) + std::to_string(octave);
}

double noteFrequency(int midiNote) {
    return 440.0 * std::pow(2.0, (static_cast<double>(midiNote) - 69.0) / 12.0);
}

bool isBlackKey(int midiNote) {
    static constexpr std::array<bool, 12> black = {false, true,  false, true,  false, false,
                                                   true,  false, true,  false, true,  false};
    return black[static_cast<std::size_t>(std::clamp(midiNote, 0, 127) % 12)];
}

Message Message::noteOn(int note, int velocity, double beat, double lengthBeats, int channel) {
    Message m;
    m.type = Type::NoteOn;
    m.note = std::clamp(note, 0, 127);
    m.velocity = std::clamp(velocity, 0, 127);
    m.beat = beat < 0.0 ? 0.0 : beat;
    m.lengthBeats = lengthBeats > 0.0 ? lengthBeats : 0.25;
    m.channel = std::clamp(channel, 0, 15);
    return m;
}

Message Message::noteOff(int note, double beat, int channel) {
    Message m;
    m.type = Type::NoteOff;
    m.note = std::clamp(note, 0, 127);
    m.velocity = 0;
    m.beat = beat < 0.0 ? 0.0 : beat;
    m.channel = std::clamp(channel, 0, 15);
    return m;
}

Clip::Clip(double lengthBeats) : lengthBeats_(lengthBeats > 0.0 ? lengthBeats : 4.0) {}

void Clip::addMessage(const Message& msg) {
    messages_.push_back(msg);
    sortByBeat();
}

void Clip::removeMessage(std::size_t index) {
    if (index < messages_.size()) {
        messages_.erase(messages_.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

void Clip::clear() {
    messages_.clear();
}

void Clip::setLengthBeats(double beats) {
    if (beats > 0.0) {
        lengthBeats_ = beats;
    }
}

void Clip::quantize(double gridBeats, double strength) {
    if (gridBeats <= 0.0) {
        return;
    }
    const double s = std::clamp(strength, 0.0, 1.0);
    for (auto& msg : messages_) {
        if (msg.type != Message::Type::NoteOn) {
            continue;
        }
        const double snapped = std::round(msg.beat / gridBeats) * gridBeats;
        msg.beat = msg.beat + (snapped - msg.beat) * s;
    }
    sortByBeat();
}

void Clip::humanize(double timingBeats, int velocityRange, std::uint32_t seed) {
    if (timingBeats <= 0.0 && velocityRange <= 0) {
        return;
    }
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> timing(-timingBeats, timingBeats);
    std::uniform_int_distribution<int> velocity(-velocityRange, velocityRange);
    for (auto& msg : messages_) {
        if (msg.type != Message::Type::NoteOn) {
            continue;
        }
        msg.beat = std::max(0.0, msg.beat + timing(rng));
        msg.velocity = std::clamp(msg.velocity + velocity(rng), 1, 127);
    }
    sortByBeat();
}

void Clip::transpose(int semitones) {
    for (auto& msg : messages_) {
        if (msg.type == Message::Type::NoteOn || msg.type == Message::Type::NoteOff) {
            msg.note = std::clamp(msg.note + semitones, 0, 127);
        }
    }
}

void Clip::scaleVelocity(double factor) {
    for (auto& msg : messages_) {
        if (msg.type == Message::Type::NoteOn) {
            msg.velocity =
                std::clamp(static_cast<int>(std::round(msg.velocity * factor)), 1, 127);
        }
    }
}

void Clip::sortByBeat() {
    std::stable_sort(messages_.begin(), messages_.end(),
                     [](const Message& a, const Message& b) { return a.beat < b.beat; });
}

} // namespace Aura::Midi
