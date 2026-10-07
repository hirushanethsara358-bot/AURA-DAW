/// @file test_project.cpp
/// @brief Unit tests for the .aura project format and autosave.

#include <filesystem>
#include <fstream>

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
