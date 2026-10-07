/// @file Project.cpp
/// @brief Implementation of the .aura project format and autosave.

#include "Aura/Project.hpp"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

namespace Aura::Project {

namespace fs = std::filesystem;
using nlohmann::json;

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

namespace {
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
    if (path.size() < 5 || path.substr(path.size() - 5) != ".aura") {
        path += ".aura";
    }
    return path;
}
} // namespace

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
    lastAutosave_ = std::chrono::steady_clock::now();
}

void Project::setName(std::string name) {
    if (!name.empty() && name != name_) {
        name_ = std::move(name);
        dirty_ = true;
    }
}

void Project::setTempo(double bpm) {
    tempo_ = std::clamp(bpm, 20.0, 999.0);
    dirty_ = true;
}

void Project::setSampleRate(double rate) {
    if (rate > 0.0) {
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

bool Project::removeTrack(const std::string& id) {
    const auto it = std::remove_if(tracks_.begin(), tracks_.end(),
                                   [&](const TrackState& t) { return t.id == id; });
    if (it == tracks_.end()) {
        return false;
    }
    tracks_.erase(it, tracks_.end());
    dirty_ = true;
    return true;
}

TrackState* Project::findTrack(const std::string& id) {
    for (auto& t : tracks_) {
        if (t.id == id) {
            return &t;
        }
    }
    return nullptr;
}

const TrackState* Project::findTrack(const std::string& id) const {
    for (const auto& t : tracks_) {
        if (t.id == id) {
            return &t;
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
    for (const auto& t : tracks_) {
        json jt;
        jt["id"] = t.id;
        jt["name"] = t.name;
        jt["type"] = toString(t.type);
        jt["mixer"] = {{"volumeDb", t.mixer.volumeDb},
                       {"pan", t.mixer.pan},
                       {"mute", t.mixer.mute},
                       {"solo", t.mixer.solo}};
        jt["pluginIds"] = t.pluginIds;
        json clips = json::array();
        for (const auto& c : t.clips) {
            json jc;
            jc["id"] = c.id;
            jc["name"] = c.name;
            jc["startBar"] = c.startBar;
            jc["lengthBars"] = c.lengthBars;
            jc["sourcePath"] = c.sourcePath;
            json notes = json::array();
            for (const auto& n : c.notes) {
                notes.push_back({{"note", n.note},
                                 {"velocity", n.velocity},
                                 {"beat", n.beat},
                                 {"lengthBeats", n.lengthBeats}});
            }
            jc["notes"] = std::move(notes);
            clips.push_back(std::move(jc));
        }
        jt["clips"] = std::move(clips);
        tracks.push_back(std::move(jt));
    }
    root["tracks"] = std::move(tracks);
    return root.dump(2);
}

std::string Project::deserialize(const std::string& text) {
    json root;
    try {
        root = json::parse(text);
    } catch (const std::exception& e) {
        return std::string("Invalid .aura file: ") + e.what();
    }
    if (root.value("format", "") != std::string("aura-project")) {
        return "Not an AURA project file (missing format marker).";
    }
    name_ = root.value("name", "Untitled");
    tempo_ = root.value("tempo", 120.0);
    sampleRate_ = root.value("sampleRate", 48000.0);
    tracks_.clear();
    for (const auto& jt : root.value("tracks", json::array())) {
        TrackState t;
        t.id = jt.value("id", "");
        t.name = jt.value("name", "Track");
        t.type = trackTypeFromString(jt.value("type", "audio"));
        const auto mixer = jt.value("mixer", json::object());
        t.mixer.volumeDb = mixer.value("volumeDb", 0.0);
        t.mixer.pan = mixer.value("pan", 0.0);
        t.mixer.mute = mixer.value("mute", false);
        t.mixer.solo = mixer.value("solo", false);
        t.pluginIds = jt.value("pluginIds", std::vector<std::string>{});
        for (const auto& jc : jt.value("clips", json::array())) {
            ClipState c;
            c.id = jc.value("id", "");
            c.name = jc.value("name", "Clip");
            c.startBar = jc.value("startBar", 0.0);
            c.lengthBars = jc.value("lengthBars", 1.0);
            c.sourcePath = jc.value("sourcePath", "");
            for (const auto& jn : jc.value("notes", json::array())) {
                StoredNote n;
                n.note = jn.value("note", 60);
                n.velocity = jn.value("velocity", 100);
                n.beat = jn.value("beat", 0.0);
                n.lengthBeats = jn.value("lengthBeats", 1.0);
                c.notes.push_back(n);
            }
            t.clips.push_back(std::move(c));
        }
        tracks_.push_back(std::move(t));
    }
    // Keep id generator ahead of loaded numeric ids.
    for (const auto& t : tracks_) {
        const std::string prefix = "track-";
        if (t.id.rfind(prefix, 0) == 0) {
            try {
                const auto num = std::stoull(t.id.substr(prefix.size()));
                nextId_ = std::max(nextId_, static_cast<std::uint64_t>(num + 1));
            } catch (...) {
                // Non-numeric suffix: ignore.
            }
        }
    }
    return "";
}

std::string Project::save(const std::string& path) {
    const std::string fullPath = ensureAuraExtension(path);
    std::ofstream out(fullPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        return "Cannot open file for writing: " + fullPath;
    }
    out << serialize();
    out.close();
    if (!out) {
        return "Failed while writing file: " + fullPath;
    }
    filePath_ = fullPath;
    dirty_ = false;
    lastAutosave_ = std::chrono::steady_clock::now();
    return "";
}

std::string Project::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return "Cannot open file for reading: " + path;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (std::string error = deserialize(buffer.str()); !error.empty()) {
        return error;
    }
    filePath_ = path;
    dirty_ = false;
    lastAutosave_ = std::chrono::steady_clock::now();
    return "";
}

bool Project::needsAutosave() const {
    return dirty_ && (std::chrono::steady_clock::now() - lastAutosave_ >= kAutosaveInterval);
}

std::string Project::autosave(const std::string& backupDirectory) {
    std::error_code ec;
    fs::create_directories(backupDirectory, ec);
    if (ec) {
        return "Cannot create autosave directory: " + backupDirectory;
    }
    std::string stamp = currentTimestampIso();
    std::replace(stamp.begin(), stamp.end(), ':', '-');
    const std::string fileName = name_ + "-autosave-" + stamp + ".aura";
    const std::string fullPath = (fs::path(backupDirectory) / fileName).generic_string();
    std::ofstream out(fullPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        return "Cannot write autosave file: " + fullPath;
    }
    out << serialize();
    out.close();
    if (!out) {
        return "Failed while writing autosave file: " + fullPath;
    }
    lastAutosave_ = std::chrono::steady_clock::now();
    return "";
}

void Project::setLastAutosaveNow() {
    lastAutosave_ = std::chrono::steady_clock::now();
}

} // namespace Aura::Project
