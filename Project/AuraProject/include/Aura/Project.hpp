#pragma once

/// @file Project.hpp
/// @brief AURA project model (.aura JSON format) with autosave support.
///
/// The .aura file stores the full session: tracks, clips, mixer state,
/// plugin references, automation and sample references. Audio/MIDI payloads
/// stay in external files; the project only references them by path.

#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace Aura::Project {

/// @brief Interval between automatic backups.
constexpr std::chrono::minutes kAutosaveInterval{5};

/// @brief Track type discriminator.
enum class TrackType {
    Audio,
    Midi,
    Instrument,
    Bus
};

[[nodiscard]] const char* toString(TrackType type);
[[nodiscard]] TrackType trackTypeFromString(const std::string& text);

/// @brief A note stored inside a MIDI clip.
struct StoredNote {
    int note = 60;
    int velocity = 100;
    double beat = 0.0;
    double lengthBeats = 1.0;
};

/// @brief An arrangement clip placed on a track timeline.
struct ClipState {
    std::string id;
    std::string name;
    double startBar = 0.0;
    double lengthBars = 1.0;
    std::string sourcePath; ///< Audio file for audio clips (empty = MIDI).
    std::vector<StoredNote> notes; ///< MIDI payload for MIDI clips.
};

/// @brief Mixer state for one track.
struct TrackMixerState {
    double volumeDb = 0.0;
    double pan = 0.0;
    bool mute = false;
    bool solo = false;
};

/// @brief A single project track.
struct TrackState {
    std::string id;
    std::string name;
    TrackType type = TrackType::Audio;
    TrackMixerState mixer;
    std::vector<ClipState> clips;
    std::vector<std::string> pluginIds; ///< Insert plugin descriptor ids.
};

/// @brief Complete serializable project.
class Project {
public:
    Project();

    void createNew(std::string name);
    [[nodiscard]] const std::string& name() const { return name_; }
    void setName(std::string name);

    [[nodiscard]] double tempo() const { return tempo_; }
    void setTempo(double bpm);
    [[nodiscard]] double sampleRate() const { return sampleRate_; }
    void setSampleRate(double rate);

    [[nodiscard]] const std::vector<TrackState>& tracks() const { return tracks_; }
    /// @brief Adds a track and returns its id.
    std::string addTrack(std::string name, TrackType type);
    bool removeTrack(const std::string& id);
    [[nodiscard]] TrackState* findTrack(const std::string& id);
    [[nodiscard]] const TrackState* findTrack(const std::string& id) const;

    [[nodiscard]] bool isDirty() const { return dirty_; }
    void markDirty() { dirty_ = true; }

    /// @brief Saves to path (appends .aura when missing). Returns "" on success.
    [[nodiscard]] std::string save(const std::string& path);
    /// @brief Loads from path. Returns "" on success.
    [[nodiscard]] std::string load(const std::string& path);

    [[nodiscard]] const std::string& filePath() const { return filePath_; }

    // -- Autosave ----------------------------------------------------------
    /// @brief True when dirty and the autosave interval has elapsed.
    [[nodiscard]] bool needsAutosave() const;
    /// @brief Writes a timestamped backup next to the project. "" on success.
    [[nodiscard]] std::string autosave(const std::string& backupDirectory);
    void setLastAutosaveNow();

private:
    std::string serialize() const;
    [[nodiscard]] std::string deserialize(const std::string& json);

    std::string name_ = "Untitled";
    double tempo_ = 120.0;
    double sampleRate_ = 48000.0;
    std::vector<TrackState> tracks_;
    std::string filePath_;
    bool dirty_ = false;
    std::chrono::steady_clock::time_point lastAutosave_ = std::chrono::steady_clock::now();
    std::uint64_t nextId_ = 1;
};

} // namespace Aura::Project
