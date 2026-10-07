/// @file Assistant.cpp
/// @brief Implementation of the AURA AI assistant modules.

#include "Aura/Assistant.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <sstream>

namespace Aura::Ai {

// ------------------------------------------------------------ MusicTheory

std::vector<int> MusicTheory::scaleIntervals(bool minor) {
    if (minor) {
        return {0, 2, 3, 5, 7, 8, 10};
    }
    return {0, 2, 4, 5, 7, 9, 11};
}

std::vector<std::string> MusicTheory::pitchNames() {
    return {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
}

bool MusicTheory::parseKey(const std::string& key, int& rootOut, bool& minorOut) {
    std::istringstream in(key);
    std::string rootName;
    std::string mode;
    in >> rootName >> mode;
    const auto names = pitchNames();
    const auto it = std::find(names.begin(), names.end(), rootName);
    if (it == names.end()) {
        return false;
    }
    rootOut = static_cast<int>(it - names.begin());
    std::transform(mode.begin(), mode.end(), mode.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    minorOut = (mode == "minor" || mode == "min" || mode == "m");
    return true;
}

std::string MusicTheory::formatKey(int root, bool minor) {
    const auto names = pitchNames();
    const int r = ((root % 12) + 12) % 12;
    return names[static_cast<std::size_t>(r)] + (minor ? " minor" : " major");
}

std::vector<int> MusicTheory::scaleNotes(int root, bool minor, int lowOctave, int highOctave) {
    std::vector<int> notes;
    const auto intervals = scaleIntervals(minor);
    const int r = ((root % 12) + 12) % 12;
    for (int octave = lowOctave; octave <= highOctave; ++octave) {
        for (int interval : intervals) {
            const int midi = 12 * (octave + 1) + r + interval;
            if (midi >= 0 && midi <= 127) {
                notes.push_back(midi);
            }
        }
    }
    return notes;
}

// ----------------------------------------------------------------- Chords

namespace {
struct DegreeChord {
    int degree;              // 0-based scale degree of the chord root.
    const char* suffixMajor; // "", "m", "dim" in major keys.
    const char* suffixMinor; // "", "m", "dim" in (natural) minor keys.
    const char* functionMajor;
    const char* functionMinor;
    int orderMinor; // commonness order in minor keys.
    int orderMajor; // commonness order in major keys.
};

// Major: I ii iii IV V vi vii°   |   Minor: i ii° III iv v VI VII
constexpr std::array<DegreeChord, 7> kDegrees = {{
    {0, "", "m", "I", "i", 0, 0},
    {1, "m", "dim", "ii", "ii°", 6, 5},
    {2, "m", "", "iii", "III", 2, 4},
    {3, "", "m", "IV", "iv", 4, 1},
    {4, "", "m", "V", "v", 3, 2},
    {5, "m", "", "vi", "VI", 1, 3},
    {6, "dim", "", "vii°", "VII", 5, 6},
}};
} // namespace

std::vector<Chord> ChordSuggester::suggest(const std::string& key, int maxCount) const {
    int root = 0;
    bool minor = false;
    if (!MusicTheory::parseKey(key, root, minor)) {
        return {};
    }
    const auto intervals = MusicTheory::scaleIntervals(minor);
    const auto names = MusicTheory::pitchNames();

    std::vector<Chord> chords;
    for (const auto& deg : kDegrees) {
        const int chordRootPc = (root + intervals[static_cast<std::size_t>(deg.degree)]) % 12;
        const std::string suffix = minor ? deg.suffixMinor : deg.suffixMajor;
        Chord chord;
        chord.name = names[static_cast<std::size_t>(chordRootPc)] + suffix;
        chord.function = minor ? deg.functionMinor : deg.functionMajor;
        // Root-position triad around octave 3.
        const int base = 48 + chordRootPc; // C3 + root
        const bool isMinor = suffix == "m";
        const bool isDim = suffix == "dim";
        chord.midiNotes = {base, base + (isMinor || isDim ? 3 : 4), base + (isDim ? 6 : 7)};
        chords.push_back(chord);
    }
    std::sort(chords.begin(), chords.end(), [&](const Chord& a, const Chord& b) {
        auto orderOf = [&](const Chord& c) {
            for (const auto& deg : kDegrees) {
                const char* fn = minor ? deg.functionMinor : deg.functionMajor;
                if (c.function == fn) {
                    return minor ? deg.orderMinor : deg.orderMajor;
                }
            }
            return 99;
        };
        return orderOf(a) < orderOf(b);
    });
    if (maxCount > 0 && static_cast<int>(chords.size()) > maxCount) {
        chords.resize(static_cast<std::size_t>(maxCount));
    }
    return chords;
}

std::vector<Chord> ChordSuggester::progression(const std::string& key) const {
    const auto all = suggest(key, 7);
    auto find = [&](const std::string& fn) -> Chord {
        for (const auto& c : all) {
            if (c.function == fn) {
                return c;
            }
        }
        return all.empty() ? Chord{"?", {}, "?"} : all.front();
    };
    int root = 0;
    bool minor = false;
    if (!MusicTheory::parseKey(key, root, minor)) {
        return {};
    }
    if (minor) {
        return {find("i"), find("VI"), find("III"), find("VII")};
    }
    return {find("I"), find("V"), find("vi"), find("IV")};
}

// ----------------------------------------------------------------- Melody

std::vector<MelodyNote> MelodyGenerator::generate(const std::string& key, int bars,
                                                  std::uint32_t seed) const {
    int root = 0;
    bool minor = false;
    if (!MusicTheory::parseKey(key, root, minor)) {
        return {};
    }
    const int numBars = std::clamp(bars, 1, 64);
    const auto scale = MusicTheory::scaleNotes(root, minor, 4, 5);
    if (scale.empty()) {
        return {};
    }

    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> stepDist(-2, 2);
    std::uniform_int_distribution<int> rhythmDist(0, 3); // 16th/8th/8th/quarter
    std::uniform_int_distribution<int> velDist(75, 105);

    std::vector<MelodyNote> notes;
    double beat = 0.0;
    const double totalBeats = numBars * 4.0;
    // Start on the tonic.
    std::size_t scaleIndex = 0;
    for (std::size_t i = 0; i < scale.size(); ++i) {
        if (scale[i] % 12 == ((root % 12) + 12) % 12 && scale[i] >= 60) {
            scaleIndex = i;
            break;
        }
    }

    bool firstNote = true;
    while (beat < totalBeats) {
        static constexpr std::array<double, 4> kRhythms = {0.25, 0.5, 0.5, 1.0};
        const double len = kRhythms[static_cast<std::size_t>(rhythmDist(rng))];
        if (!firstNote) {
            const int step = stepDist(rng);
            int next = static_cast<int>(scaleIndex) + step;
            next = std::clamp(next, 0, static_cast<int>(scale.size()) - 1);
            scaleIndex = static_cast<std::size_t>(next);
        }
        firstNote = false;
        MelodyNote note;
        note.midi = scale[scaleIndex];
        note.beat = beat;
        note.lengthBeats = std::min(len, totalBeats - beat);
        note.velocity = velDist(rng);
        notes.push_back(note);
        beat += len;
    }
    // Resolve the final note to the tonic.
    if (!notes.empty()) {
        notes.back().midi = scale[0] + 12 <= 127 ? scale[0] + 12 : scale[0];
    }
    return notes;
}

// -------------------------------------------------------------- MixAdvisor

std::vector<MixAdvice> MixAdvisor::advise(double peakDb, double rmsDb) const {
    std::vector<MixAdvice> out;
    if (peakDb >= -0.5) {
        out.push_back({"Level", "Master is clipping (peak " + std::to_string(peakDb) +
                                    " dBFS). Pull the master fader down 3–6 dB."});
    } else if (peakDb < -12.0) {
        out.push_back({"Level", "Master peak is low (" + std::to_string(peakDb) +
                                    " dBFS). You have plenty of headroom — "
                                    "raise buses or gain-stage up."});
    }
    const double crest = peakDb - rmsDb;
    if (crest > 20.0) {
        out.push_back({"Dynamics", "Crest factor is " + std::to_string(crest) +
                                       " dB (very dynamic). Consider gentle bus "
                                       "compression (2:1, slow attack) to glue the mix."});
    } else if (crest < 6.0 && peakDb > -3.0) {
        out.push_back({"Dynamics", "Crest factor is only " + std::to_string(crest) +
                                       " dB — the mix may be over-compressed. "
                                       "Ease off the master limiter."});
    }
    if (rmsDb < -30.0) {
        out.push_back({"Headroom", "Average loudness is very low (RMS " + std::to_string(rmsDb) +
                                       " dBFS). Check for muted buses or "
                                       "excessively low faders."});
    }
    if (out.empty()) {
        out.push_back({"Level", "Levels look healthy. Keep peaks under -1 dBFS "
                                "before mastering."});
    }
    return out;
}

// -------------------------------------------------------------- Mastering

MasterChainSuggestion MasteringAssistant::suggest(double peakDb, double rmsDb,
                                                  const std::string& targetPlatform) const {
    MasterChainSuggestion chain;
    const double crest = peakDb - rmsDb;

    if (crest > 16.0) {
        chain.compThresholdDb = -20.0;
        chain.compRatio = 2.5;
    } else if (crest > 10.0) {
        chain.compThresholdDb = -16.0;
        chain.compRatio = 2.0;
    } else {
        chain.compThresholdDb = -12.0;
        chain.compRatio = 1.5;
    }

    std::string platform = targetPlatform;
    std::transform(platform.begin(), platform.end(), platform.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (platform == "club" || platform == "cd") {
        chain.targetLufs = -9.0;
        chain.limiterCeilingDb = -0.5;
    } else if (platform == "streaming") {
        chain.targetLufs = -14.0;
        chain.limiterCeilingDb = -1.0;
    } else if (platform == "broadcast" || platform == "film") {
        chain.targetLufs = -23.0;
        chain.limiterCeilingDb = -1.0;
    }

    if (rmsDb < -24.0) {
        chain.eqLowShelfDb = 0.5;
        chain.notes = "Quiet mix: gentle low lift and make-up gain recommended. ";
    } else if (rmsDb > -10.0) {
        chain.notes = "Hot mix: go easy on limiting to avoid pumping. ";
    }
    chain.notes += "Target " + std::to_string(chain.targetLufs) + " LUFS for " + targetPlatform +
                   ", ceiling " + std::to_string(chain.limiterCeilingDb) + " dBFS.";
    return chain;
}

} // namespace Aura::Ai
