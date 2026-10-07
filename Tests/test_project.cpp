/// @file test_project.cpp
/// @brief Unit tests for the .aura project format and autosave.

#include <filesystem>
#include <fstream>
#include <iterator>

#include <gtest/gtest.h>

#include "Aura/Project.hpp"

namespace fs = std::filesystem;

namespace {
fs::path uniqueTempDir(const std::string& tag) {
    static int counter = 0;
    fs::path dir = fs::temp_directory_path() / ("aura_test_" + tag + std::to_string(counter++));
    fs::create_directories(dir);
    return dir;
}
} // namespace

TEST(Project, SaveLoadRoundTrip) {
    Aura::Project::Project project;
    project.createNew("Demo Song");
    project.setTempo(128.0);
    project.setSampleRate(48000.0);

    const std::string drums = project.addTrack("Drums", Aura::Project::TrackType::Audio);
    const std::string keys = project.addTrack("Keys", Aura::Project::TrackType::Midi);
    ASSERT_NE(project.findTrack(drums), nullptr);
    project.findTrack(drums)->mixer.volumeDb = -3.0;
    project.findTrack(keys)->mixer.pan = -0.5;

    Aura::Project::ClipState clip;
    clip.id = "clip-1";
    clip.name = "Intro";
    clip.startBar = 0.0;
    clip.lengthBars = 4.0;
    clip.notes.push_back({60, 100, 0.0, 1.0});
    clip.notes.push_back({64, 90, 1.0, 1.0});
    project.findTrack(keys)->clips.push_back(clip);
    project.findTrack(drums)->pluginIds.push_back("VST3:Comp");

    EXPECT_TRUE(project.isDirty());

    const fs::path dir = uniqueTempDir("project");
    const std::string path = (dir / "demo").generic_string(); // no extension on purpose
    EXPECT_TRUE(project.save(path).empty());
    EXPECT_FALSE(project.isDirty());
    EXPECT_TRUE(fs::exists(dir / "demo.aura"));

    Aura::Project::Project loaded;
    EXPECT_TRUE(loaded.load((dir / "demo.aura").generic_string()).empty());
    EXPECT_EQ(loaded.name(), "Demo Song");
    EXPECT_DOUBLE_EQ(loaded.tempo(), 128.0);
    ASSERT_EQ(loaded.tracks().size(), 2);
    EXPECT_EQ(loaded.tracks()[0].name, "Drums");
    EXPECT_DOUBLE_EQ(loaded.tracks()[0].mixer.volumeDb, -3.0);
    EXPECT_EQ(loaded.tracks()[0].pluginIds.size(), 1);
    ASSERT_EQ(loaded.tracks()[1].clips.size(), 1);
    EXPECT_EQ(loaded.tracks()[1].clips[0].notes.size(), 2);
    EXPECT_EQ(loaded.tracks()[1].clips[0].notes[1].note, 64);

    fs::remove_all(dir);
}

TEST(Project, LoadRejectsGarbage) {
    const fs::path dir = uniqueTempDir("garbage");
    const fs::path file = dir / "bad.aura";
    std::ofstream(file) << "{ this is not json";
    Aura::Project::Project project;
    EXPECT_FALSE(project.load(file.generic_string()).empty());

    std::ofstream(file) << "{\"format\":\"other\"}";
    EXPECT_FALSE(project.load(file.generic_string()).empty());
    fs::remove_all(dir);
}

TEST(Project, AutosaveWritesBackup) {
    Aura::Project::Project project;
    project.createNew("Autosave Song");
    project.addTrack("Vox", Aura::Project::TrackType::Audio);

    const fs::path dir = uniqueTempDir("autosave");
    EXPECT_TRUE(project.autosave(dir.generic_string()).empty());

    int auraFiles = 0;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() == ".aura") {
            ++auraFiles;
        }
    }
    EXPECT_EQ(auraFiles, 1);
    fs::remove_all(dir);
}

TEST(Project, TrackManagement) {
    Aura::Project::Project project;
    project.createNew("Tracks");
    const std::string id = project.addTrack("Bus 1", Aura::Project::TrackType::Bus);
    EXPECT_EQ(project.findTrack(id)->type, Aura::Project::TrackType::Bus);
    EXPECT_TRUE(project.removeTrack(id));
    EXPECT_EQ(project.findTrack(id), nullptr);
    EXPECT_FALSE(project.removeTrack("missing"));
}

TEST(Project, AddAudioClipValidatesAndRoundTrips) {
    Aura::Project::Project project;
    project.createNew("Audio import");
    const std::string trackId = project.addTrack("Audio 1", Aura::Project::TrackType::Audio);

    Aura::Project::ClipState clip;
    clip.name = "Intro.wav";
    clip.startBar = 2.0;
    clip.lengthBars = 3.5;
    clip.sourcePath = "Samples/Intro.wav";
    std::string error;
    ASSERT_TRUE(project.addClip(trackId, clip, error)) << error;
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(project.findTrack(trackId)->clips.front().id, "clip-1");

    Aura::Project::TrackMixerState mixer;
    mixer.volumeDb = -6.0;
    mixer.pan = 0.25;
    ASSERT_TRUE(project.setTrackMixerState(trackId, mixer));
    EXPECT_FALSE(project.setTrackMixerState("missing", mixer));
    mixer.pan = 2.0;
    EXPECT_FALSE(project.setTrackMixerState(trackId, mixer));

    const fs::path dir = uniqueTempDir("audio_clip");
    const fs::path file = dir / "song.aura";
    ASSERT_TRUE(project.save(file.generic_string()).empty());

    Aura::Project::Project loaded;
    ASSERT_TRUE(loaded.load(file.generic_string()).empty());
    ASSERT_EQ(loaded.tracks().size(), 1);
    ASSERT_EQ(loaded.tracks().front().clips.size(), 1);
    EXPECT_EQ(loaded.tracks().front().clips.front().sourcePath, "Samples/Intro.wav");
    EXPECT_DOUBLE_EQ(loaded.tracks().front().clips.front().lengthBars, 3.5);
    EXPECT_DOUBLE_EQ(loaded.tracks().front().mixer.volumeDb, -6.0);
    EXPECT_DOUBLE_EQ(loaded.tracks().front().mixer.pan, 0.25);
    fs::remove_all(dir);
}

TEST(Project, FailedLoadPreservesCurrentSession) {
    const fs::path dir = uniqueTempDir("transactional_load");
    const fs::path goodFile = dir / "good.aura";
    const fs::path badFile = dir / "bad.aura";

    Aura::Project::Project project;
    project.createNew("Keep this session");
    const std::string trackId =
        project.addTrack("Keep this track", Aura::Project::TrackType::Audio);
    ASSERT_TRUE(project.save(goodFile.generic_string()).empty());
    const std::string originalPath = project.filePath();

    std::ofstream(badFile)
        << R"({"format":"aura-project","version":1,"name":"Broken","tempo":1000,"tracks":[]})";
    const std::string error = project.load(badFile.generic_string());
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(project.name(), "Keep this session");
    ASSERT_EQ(project.tracks().size(), 1);
    EXPECT_EQ(project.tracks().front().id, trackId);
    EXPECT_EQ(project.filePath(), originalPath);
    EXPECT_FALSE(project.isDirty());
    fs::remove_all(dir);
}

TEST(Project, FailedAtomicSaveLeavesExistingProjectFileUntouched) {
    const fs::path dir = uniqueTempDir("atomic_save");
    const fs::path goodFile = dir / "good.aura";

    Aura::Project::Project project;
    project.createNew("Atomic");
    project.addTrack("Before", Aura::Project::TrackType::Audio);
    ASSERT_TRUE(project.save(goodFile.generic_string()).empty());
    const std::string originalPath = project.filePath();
    std::ifstream originalInput(goodFile, std::ios::binary);
    const std::string originalContents((std::istreambuf_iterator<char>(originalInput)),
                                       std::istreambuf_iterator<char>());
    originalInput.close();

    project.addTrack("After", Aura::Project::TrackType::Audio);
    const fs::path occupiedTarget = dir / "occupied.aura";
    fs::create_directory(occupiedTarget);
    EXPECT_FALSE(project.save(occupiedTarget.generic_string()).empty());
    EXPECT_EQ(project.filePath(), originalPath);
    EXPECT_TRUE(project.isDirty());

    std::ifstream afterInput(goodFile, std::ios::binary);
    const std::string afterContents((std::istreambuf_iterator<char>(afterInput)),
                                    std::istreambuf_iterator<char>());
    afterInput.close();
    EXPECT_EQ(afterContents, originalContents);
    fs::remove_all(dir);
}

TEST(Project, RejectsInvalidAudioClipAndEmptySavePath) {
    Aura::Project::Project project;
    project.createNew("Invalid input");
    const std::string trackId = project.addTrack("Audio", Aura::Project::TrackType::Audio);

    Aura::Project::ClipState clip;
    clip.name = "Missing asset";
    clip.lengthBars = 1.0;
    std::string error;
    EXPECT_FALSE(project.addClip(trackId, clip, error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(project.findTrack(trackId)->clips.empty());
    EXPECT_FALSE(project.save("").empty());
}
