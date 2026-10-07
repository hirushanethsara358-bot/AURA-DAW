/// @file Project.cpp
/// @brief Implementation of the .aura project format and autosave.

#include "Aura/Project.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <system_error>
#include <unordered_set>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <nlohmann/json.hpp>

namespace Aura::Project {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

constexpr std::uintmax_t kMaxProjectBytes = 64U * 1024U * 1024U;
constexpr std::size_t kMaxTracks = 1024;
constexpr std::size_t kMaxClipsPerTrack = 10000;
constexpr std::size_t kMaxNotesPerClip = 100000;
constexpr double kMinSampleRate = 8000.0;
constexpr double kMaxSampleRate = 384000.0;

std::string currentTimestampIso() {
    std::time_t now = std::time(nullptr);
    std::tm tm_{};
#if defined(_WIN32)
    gmtime_s(&tm_, &now);
#else
    gmtime_r(&now, &tm_);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::string ensureAuraExtension(std::string path) {
    if (path.empty()) {
        return path;
    }
    if (path.size() >= 5) {
        std::string suffix = path.substr(path.size() - 5);
        std::transform(suffix.begin(), suffix.end(), suffix.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (suffix == ".aura") {
            return path;
        }
    }
    path += ".aura";
    return path;
}

fs::path pathFromUtf8(const std::string& text) {
    std::u8string encoded;
    encoded.reserve(text.size());
    for (unsigned char byte : text) {
        encoded.push_back(static_cast<char8_t>(byte));
    }
    return fs::path(encoded);
}

std::string pathToUtf8(const fs::path& path) {
    const auto encoded = path.u8string();
    return std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size());
}

bool isInRange(double value, double low, double high) {
    return std::isfinite(value) && value >= low && value <= high;
}

std::string validateClip(const ClipState& clip, TrackType trackType) {
    if (clip.id.empty()) {
        return "Clip id cannot be empty.";
    }
    if (clip.name.empty()) {
        return "Clip name cannot be empty.";
    }
    if (!isInRange(clip.startBar, 0.0, 1.0e7) ||
        !isInRange(clip.lengthBars, std::numeric_limits<double>::min(), 1.0e7)) {
        return "Clip position or length is outside the supported range.";
    }
    if (trackType == TrackType::Audio && clip.sourcePath.empty()) {
        return "Audio clips must reference a WAV asset.";
    }
    if (clip.sourcePath.size() > 4096) {
        return "Clip asset path is too long.";
    }
    if (clip.notes.size() > kMaxNotesPerClip) {
        return "Clip contains too many MIDI notes.";
    }
    for (const auto& note : clip.notes) {
        if (note.note < 0 || note.note > 127 || note.velocity < 1 || note.velocity > 127 ||
            !isInRange(note.beat, 0.0, 1.0e7) ||
            !isInRange(note.lengthBeats, std::numeric_limits<double>::min(), 1.0e7)) {
            return "Clip contains an invalid MIDI note.";
        }
    }
    return {};
}

std::string validateProjectState(const std::string& name, double tempo, double sampleRate,
                                 const std::vector<TrackState>& tracks) {
    if (name.empty() || name.size() > 1024) {
        return "Project name must contain between 1 and 1024 bytes.";
    }
    if (!isInRange(tempo, 20.0, 999.0)) {
        return "Project tempo must be between 20 and 999 BPM.";
    }
    if (!isInRange(sampleRate, kMinSampleRate, kMaxSampleRate)) {
        return "Project sample rate is outside the supported range.";
    }
    if (tracks.size() > kMaxTracks) {
        return "Project contains too many tracks.";
    }

    std::unordered_set<std::string> ids;
    std::size_t totalClips = 0;
    for (const auto& track : tracks) {
        if (track.id.empty() || !ids.insert(track.id).second) {
            return "Project contains an empty or duplicate track id.";
        }
        if (track.name.empty() || track.name.size() > 1024) {
            return "Track name must contain between 1 and 1024 bytes.";
        }
        if (!isInRange(track.mixer.volumeDb, -96.0, 12.0) ||
            !isInRange(track.mixer.pan, -1.0, 1.0)) {
            return "Track mixer volume or pan is outside the supported range.";
        }
        if (track.pluginIds.size() > 1024) {
            return "Track contains too many plugin references.";
        }
        if (track.clips.size() > kMaxClipsPerTrack) {
            return "Track contains too many clips.";
        }
        totalClips += track.clips.size();
        if (totalClips > 100000) {
            return "Project contains too many clips.";
        }
        for (const auto& clip : track.clips) {
            if (!ids.insert(clip.id).second) {
                return "Project contains a duplicate track or clip id.";
            }
            if (const std::string error = validateClip(clip, track.type); !error.empty()) {
                return error;
            }
        }
    }
    return {};
}

bool replaceFileAtomically(const fs::path& temporaryPath, const fs::path& targetPath,
                           std::error_code& error) {
#if defined(_WIN32)
    if (MoveFileExW(temporaryPath.c_str(), targetPath.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
        return false;
    }
    error.clear();
    return true;
#else
    fs::rename(temporaryPath, targetPath, error);
    return !error;
#endif
}

std::string writeTextAtomically(const fs::path& targetPath, const std::string& text) {
    static std::atomic<std::uint64_t> nextTemporaryId{1};
    const fs::path parent = targetPath.has_parent_path() ? targetPath.parent_path() : fs::path(".");
    const std::string temporaryName =
        pathToUtf8(targetPath.filename()) + ".tmp-" + std::to_string(nextTemporaryId.fetch_add(1));
    const fs::path temporaryPath = parent / pathFromUtf8(temporaryName);

    std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        return "Cannot open temporary project file for writing: " + pathToUtf8(temporaryPath);
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.flush();
    if (!output) {
        output.close();
        std::error_code ignored;
        fs::remove(temporaryPath, ignored);
        return "Failed while writing temporary project file: " + pathToUtf8(temporaryPath);
    }
    output.close();
    if (!output) {
        std::error_code ignored;
        fs::remove(temporaryPath, ignored);
        return "Failed while closing temporary project file: " + pathToUtf8(temporaryPath);
    }

    std::error_code error;
    if (!replaceFileAtomically(temporaryPath, targetPath, error)) {
        std::error_code ignored;
        fs::remove(temporaryPath, ignored);
        return "Cannot replace project file " + pathToUtf8(targetPath) + ": " + error.message();
    }
    return {};
}

std::string safeFileStem(const std::string& input) {
    std::string result;
    result.reserve(std::min<std::size_t>(input.size(), 80));
    for (unsigned char ch : input) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == ' ') {
            result.push_back(static_cast<char>(ch));
        } else {
            result.push_back('_');
        }
        if (result.size() == 80) {
            break;
        }
    }
    while (!result.empty() && (result.back() == ' ' || result.back() == '.')) {
        result.pop_back();
    }
    if (result.empty() || result == "." || result == "..") {
        return "Untitled";
    }
    return result;
}

std::uint64_t nextNumericId(const std::string& id, const std::string& prefix,
                            std::uint64_t current) {
    if (id.rfind(prefix, 0) != 0) {
        return current;
    }
    const std::string digits = id.substr(prefix.size());
    try {
        std::size_t consumed = 0;
        const auto parsed = std::stoull(digits, &consumed);
        if (consumed == digits.size() && parsed < std::numeric_limits<std::uint64_t>::max()) {
            return std::max(current, static_cast<std::uint64_t>(parsed + 1));
        }
    } catch (...) {
        // Non-numeric suffix: ignore it and retain the current generator value.
    }
    return current;
}

} // namespace

const char* toString(TrackType type) {
    switch (type) {
    case TrackType::Audio:
        return "audio";
    case TrackType::Midi:
        return "midi";
    case TrackType::Instrument:
        return "instrument";
    case TrackType::Bus:
        return "bus";
    }
    return "audio";
}

TrackType trackTypeFromString(const std::string& text) {
    if (text == "midi") {
        return TrackType::Midi;
    }
    if (text == "instrument") {
        return TrackType::Instrument;
    }
    if (text == "bus") {
        return TrackType::Bus;
    }
    return TrackType::Audio;
}

Project::Project() {
    createNew("Untitled");
}

void Project::createNew(std::string name) {
    name_ = name.empty() ? "Untitled" : std::move(name);
    tempo_ = 120.0;
    sampleRate_ = 48000.0;
    tracks_.clear();
    filePath_.clear();
    dirty_ = false;
    nextId_ = 1;
    nextClipId_ = 1;
    lastAutosave_ = std::chrono::steady_clock::now();
}

void Project::setName(std::string name) {
    if (!name.empty() && name != name_) {
        name_ = std::move(name);
        dirty_ = true;
    }
}

void Project::setTempo(double bpm) {
    if (!std::isfinite(bpm)) {
        return;
    }
    const double clamped = std::clamp(bpm, 20.0, 999.0);
    if (clamped != tempo_) {
        tempo_ = clamped;
        dirty_ = true;
    }
}

void Project::setSampleRate(double rate) {
    if (isInRange(rate, kMinSampleRate, kMaxSampleRate) && rate != sampleRate_) {
        sampleRate_ = rate;
        dirty_ = true;
    }
}

std::string Project::addTrack(std::string trackName, TrackType type) {
    TrackState track;
    track.id = "track-" + std::to_string(nextId_++);
    track.name = trackName.empty() ? track.id : std::move(trackName);
    track.type = type;
    tracks_.push_back(std::move(track));
    dirty_ = true;
    return tracks_.back().id;
}

bool Project::addClip(const std::string& trackId, ClipState clip, std::string& error) {
    error.clear();
    TrackState* track = findTrack(trackId);
    if (track == nullptr) {
        error = "Cannot add clip: track does not exist.";
        return false;
    }
    if (clip.id.empty()) {
        clip.id = "clip-" + std::to_string(nextClipId_++);
    }
    if (clip.name.empty()) {
        clip.name = clip.id;
    }
    if (const std::string validation = validateClip(clip, track->type); !validation.empty()) {
        error = validation;
        return false;
    }
    for (const auto& candidateTrack : tracks_) {
        if (candidateTrack.id == clip.id) {
            error = "Clip id duplicates an existing track id.";
            return false;
        }
        for (const auto& existing : candidateTrack.clips) {
            if (existing.id == clip.id) {
                error = "Clip id already exists in this project.";
                return false;
            }
        }
    }
    track->clips.push_back(std::move(clip));
    dirty_ = true;
    return true;
}

bool Project::setTrackMixerState(const std::string& trackId, const TrackMixerState& state) {
    if (!isInRange(state.volumeDb, -96.0, 12.0) || !isInRange(state.pan, -1.0, 1.0)) {
        return false;
    }
    TrackState* track = findTrack(trackId);
    if (track == nullptr) {
        return false;
    }
    track->mixer = state;
    dirty_ = true;
    return true;
}

bool Project::removeTrack(const std::string& id) {
    const auto it = std::remove_if(tracks_.begin(), tracks_.end(),
                                   [&](const TrackState& track) { return track.id == id; });
    if (it == tracks_.end()) {
        return false;
    }
    tracks_.erase(it, tracks_.end());
    dirty_ = true;
    return true;
}

TrackState* Project::findTrack(const std::string& id) {
    for (auto& track : tracks_) {
        if (track.id == id) {
            return &track;
        }
    }
    return nullptr;
}

const TrackState* Project::findTrack(const std::string& id) const {
    for (const auto& track : tracks_) {
        if (track.id == id) {
            return &track;
        }
    }
    return nullptr;
}

std::string Project::serialize() const {
    json root;
    root["format"] = "aura-project";
    root["version"] = 1;
    root["name"] = name_;
    root["tempo"] = tempo_;
    root["sampleRate"] = sampleRate_;
    root["modified"] = currentTimestampIso();

    json tracks = json::array();
    for (const auto& track : tracks_) {
        json jsonTrack;
        jsonTrack["id"] = track.id;
        jsonTrack["name"] = track.name;
        jsonTrack["type"] = toString(track.type);
        jsonTrack["mixer"] = {{"volumeDb", track.mixer.volumeDb},
                              {"pan", track.mixer.pan},
                              {"mute", track.mixer.mute},
                              {"solo", track.mixer.solo}};
        jsonTrack["pluginIds"] = track.pluginIds;

        json clips = json::array();
        for (const auto& clip : track.clips) {
            json jsonClip;
            jsonClip["id"] = clip.id;
            jsonClip["name"] = clip.name;
            jsonClip["startBar"] = clip.startBar;
            jsonClip["lengthBars"] = clip.lengthBars;
            jsonClip["sourcePath"] = clip.sourcePath;
            json notes = json::array();
            for (const auto& note : clip.notes) {
                notes.push_back({{"note", note.note},
                                 {"velocity", note.velocity},
                                 {"beat", note.beat},
                                 {"lengthBeats", note.lengthBeats}});
            }
            jsonClip["notes"] = std::move(notes);
            clips.push_back(std::move(jsonClip));
        }
        jsonTrack["clips"] = std::move(clips);
        tracks.push_back(std::move(jsonTrack));
    }
    root["tracks"] = std::move(tracks);
    return root.dump(2);
}

std::string Project::deserialize(const std::string& text) {
    try {
        const json root = json::parse(text);
        if (!root.is_object()) {
            return "Invalid .aura file: root must be an object.";
        }
        if (root.value("format", std::string{}) != "aura-project") {
            return "Not an AURA project file (missing format marker).";
        }
        if (root.value("version", 0) != 1) {
            return "Unsupported .aura project version.";
        }

        std::string candidateName = root.value("name", std::string("Untitled"));
        const double candidateTempo = root.value("tempo", 120.0);
        const double candidateSampleRate = root.value("sampleRate", 48000.0);
        const json jsonTracks = root.value("tracks", json::array());
        if (!jsonTracks.is_array()) {
            return "Invalid .aura file: tracks must be an array.";
        }
        if (jsonTracks.size() > kMaxTracks) {
            return "Invalid .aura file: too many tracks.";
        }

        std::vector<TrackState> candidateTracks;
        candidateTracks.reserve(jsonTracks.size());
        std::uint64_t candidateNextId = 1;
        std::uint64_t candidateNextClipId = 1;
        std::unordered_set<std::string> ids;
        std::size_t totalClips = 0;

        for (const auto& jsonTrack : jsonTracks) {
            if (!jsonTrack.is_object()) {
                return "Invalid .aura file: each track must be an object.";
            }
            TrackState track;
            track.id = jsonTrack.value("id", std::string{});
            track.name = jsonTrack.value("name", std::string{});
            const std::string typeText = jsonTrack.value("type", std::string("audio"));
            if (typeText != "audio" && typeText != "midi" && typeText != "instrument" &&
                typeText != "bus") {
                return "Invalid .aura file: unknown track type.";
            }
            track.type = trackTypeFromString(typeText);
            const json jsonMixer = jsonTrack.value("mixer", json::object());
            if (!jsonMixer.is_object()) {
                return "Invalid .aura file: track mixer state must be an object.";
            }
            track.mixer.volumeDb = jsonMixer.value("volumeDb", 0.0);
            track.mixer.pan = jsonMixer.value("pan", 0.0);
            track.mixer.mute = jsonMixer.value("mute", false);
            track.mixer.solo = jsonMixer.value("solo", false);
            track.pluginIds = jsonTrack.value("pluginIds", std::vector<std::string>{});

            const json jsonClips = jsonTrack.value("clips", json::array());
            if (!jsonClips.is_array() || jsonClips.size() > kMaxClipsPerTrack) {
                return "Invalid .aura file: clips must be an array within the per-track limit.";
            }
            totalClips += jsonClips.size();
            if (totalClips > 100000) {
                return "Invalid .aura file: too many clips.";
            }
            for (const auto& jsonClip : jsonClips) {
                if (!jsonClip.is_object()) {
                    return "Invalid .aura file: each clip must be an object.";
                }
                ClipState clip;
                clip.id = jsonClip.value("id", std::string{});
                clip.name = jsonClip.value("name", std::string{});
                clip.startBar = jsonClip.value("startBar", 0.0);
                clip.lengthBars = jsonClip.value("lengthBars", 1.0);
                clip.sourcePath = jsonClip.value("sourcePath", std::string{});
                const json jsonNotes = jsonClip.value("notes", json::array());
                if (!jsonNotes.is_array() || jsonNotes.size() > kMaxNotesPerClip) {
                    return "Invalid .aura file: notes must be an array within the per-clip limit.";
                }
                for (const auto& jsonNote : jsonNotes) {
                    if (!jsonNote.is_object()) {
                        return "Invalid .aura file: each note must be an object.";
                    }
                    StoredNote note;
                    note.note = jsonNote.value("note", 60);
                    note.velocity = jsonNote.value("velocity", 100);
                    note.beat = jsonNote.value("beat", 0.0);
                    note.lengthBeats = jsonNote.value("lengthBeats", 1.0);
                    clip.notes.push_back(note);
                }
                if (clip.id.empty() || !ids.insert(clip.id).second) {
                    return "Invalid .aura file: empty or duplicate clip id.";
                }
                if (const std::string error = validateClip(clip, track.type); !error.empty()) {
                    return "Invalid .aura file: " + error;
                }
                candidateNextClipId = nextNumericId(clip.id, "clip-", candidateNextClipId);
                track.clips.push_back(std::move(clip));
            }
            if (track.id.empty() || !ids.insert(track.id).second) {
                return "Invalid .aura file: empty or duplicate track id.";
            }
            candidateNextId = nextNumericId(track.id, "track-", candidateNextId);
            candidateTracks.push_back(std::move(track));
        }

        if (const std::string error = validateProjectState(candidateName, candidateTempo,
                                                           candidateSampleRate, candidateTracks);
            !error.empty()) {
            return "Invalid .aura file: " + error;
        }

        name_ = std::move(candidateName);
        tempo_ = candidateTempo;
        sampleRate_ = candidateSampleRate;
        tracks_ = std::move(candidateTracks);
        nextId_ = candidateNextId;
        nextClipId_ = candidateNextClipId;
        return {};
    } catch (const std::exception& error) {
        return std::string("Invalid .aura file: ") + error.what();
    }
}

std::string Project::save(const std::string& path) {
    if (path.empty()) {
        return "Project path cannot be empty.";
    }
    if (const std::string error = validateProjectState(name_, tempo_, sampleRate_, tracks_);
        !error.empty()) {
        return "Cannot save invalid project: " + error;
    }

    const std::string fullPath = ensureAuraExtension(path);
    std::string data;
    try {
        data = serialize();
    } catch (const std::exception& error) {
        return std::string("Cannot serialize project: ") + error.what();
    }
    if (data.size() > kMaxProjectBytes) {
        return "Project exceeds the 64 MiB file-size limit.";
    }

    const std::string error = writeTextAtomically(pathFromUtf8(fullPath), data);
    if (!error.empty()) {
        return error;
    }
    filePath_ = fullPath;
    dirty_ = false;
    lastAutosave_ = std::chrono::steady_clock::now();
    return {};
}

std::string Project::load(const std::string& path) {
    if (path.empty()) {
        return "Project path cannot be empty.";
    }
    const fs::path sourcePath = pathFromUtf8(path);
    std::error_code sizeError;
    const std::uintmax_t fileSize = fs::file_size(sourcePath, sizeError);
    if (sizeError) {
        return "Cannot inspect project file: " + path + ": " + sizeError.message();
    }
    if (fileSize > kMaxProjectBytes) {
        return "Project file exceeds the 64 MiB size limit.";
    }

    std::ifstream input(sourcePath, std::ios::binary);
    if (!input) {
        return "Cannot open file for reading: " + path;
    }
    std::string contents(static_cast<std::size_t>(fileSize), '\0');
    if (!contents.empty()) {
        input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    }
    if ((!contents.empty() && input.gcount() != static_cast<std::streamsize>(contents.size())) ||
        input.bad()) {
        return "Failed while reading project file: " + path;
    }

    if (const std::string error = deserialize(contents); !error.empty()) {
        return error;
    }
    filePath_ = path;
    dirty_ = false;
    lastAutosave_ = std::chrono::steady_clock::now();
    return {};
}

bool Project::needsAutosave() const {
    return dirty_ && (std::chrono::steady_clock::now() - lastAutosave_ >= kAutosaveInterval);
}

std::string Project::autosave(const std::string& backupDirectory) {
    if (backupDirectory.empty()) {
        return "Autosave directory cannot be empty.";
    }
    if (const std::string error = validateProjectState(name_, tempo_, sampleRate_, tracks_);
        !error.empty()) {
        return "Cannot autosave invalid project: " + error;
    }

    std::error_code ec;
    const fs::path backupPath = pathFromUtf8(backupDirectory);
    fs::create_directories(backupPath, ec);
    if (ec) {
        return "Cannot create autosave directory: " + backupDirectory + ": " + ec.message();
    }
    std::string stamp = currentTimestampIso();
    std::replace(stamp.begin(), stamp.end(), ':', '-');
    const std::string fileName = safeFileStem(name_) + "-autosave-" + stamp + ".aura";
    const fs::path destination = backupPath / pathFromUtf8(fileName);
    std::string data;
    try {
        data = serialize();
    } catch (const std::exception& error) {
        return std::string("Cannot serialize autosave: ") + error.what();
    }
    if (data.size() > kMaxProjectBytes) {
        return "Autosave exceeds the 64 MiB file-size limit.";
    }
    if (const std::string error = writeTextAtomically(destination, data); !error.empty()) {
        return error;
    }
    lastAutosave_ = std::chrono::steady_clock::now();
    return {};
}

void Project::setLastAutosaveNow() {
    lastAutosave_ = std::chrono::steady_clock::now();
}

} // namespace Aura::Project
