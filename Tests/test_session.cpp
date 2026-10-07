/// @file test_session.cpp
/// @brief Offline integration tests for one project WAV clip through the MVP callback.

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Aura/ProjectPlaybackSession.hpp"

namespace {

void writeU16(std::ofstream& output, std::uint16_t value) {
    const char bytes[] = {static_cast<char>(value & 0xffU),
                          static_cast<char>((value >> 8U) & 0xffU)};
    output.write(bytes, 2);
}

void writeU32(std::ofstream& output, std::uint32_t value) {
    const char bytes[] = {
        static_cast<char>(value & 0xffU), static_cast<char>((value >> 8U) & 0xffU),
        static_cast<char>((value >> 16U) & 0xffU), static_cast<char>((value >> 24U) & 0xffU)};
    output.write(bytes, 4);
}

std::filesystem::path writeTestWav() {
    static std::uint64_t counter = 0;
    const auto path = std::filesystem::temp_directory_path() /
                      ("aura_session_" + std::to_string(counter++) + ".wav");
    std::ofstream output(path, std::ios::binary);
    output.write("RIFF", 4);
    writeU32(output, 42);
    output.write("WAVEfmt ", 8);
    writeU32(output, 16);
    writeU16(output, 1); // PCM
    writeU16(output, 1); // Mono
    writeU32(output, 48000);
    writeU32(output, 96000);
    writeU16(output, 2);
    writeU16(output, 16);
    output.write("data", 4);
    writeU32(output, 6);
    writeU16(output, 0x4000); // +0.5
    writeU16(output, 0x2000); // +0.25
    writeU16(output, 0xc000); // -0.5
    return path;
}

Aura::Project::ClipState makeClip(const std::string& path, double startBar, double lengthBars) {
    Aura::Project::ClipState clip;
    clip.name = "Test WAV";
    clip.sourcePath = path;
    clip.startBar = startBar;
    clip.lengthBars = lengthBars;
    return clip;
}

void addAudioClip(Aura::Project::Project& project, const std::string& path, double startBar,
                  double lengthBars) {
    const std::string trackId = project.tracks().empty()
                                    ? project.addTrack("Audio", Aura::Project::TrackType::Audio)
                                    : project.tracks().front().id;
    std::string error;
    EXPECT_TRUE(project.addClip(trackId, makeClip(path, startBar, lengthBars), error)) << error;
}

} // namespace

TEST(ProjectPlaybackSession, LoadsAndRendersSingleClipAtProjectTimelineOffset) {
    const auto path = writeTestWav();
    Aura::Project::Project project;
    project.createNew("Session test");
    project.setTempo(120.0);
    addAudioClip(project, path.string(), 2.0 / 96000.0, 3.0 / 96000.0);

    Aura::Session::ProjectPlaybackSession session;
    std::string error;
    ASSERT_TRUE(session.loadProject(project, 48000.0, error)) << error;
    EXPECT_TRUE(session.isLoaded());
    EXPECT_DOUBLE_EQ(session.positionBars(), 0.0);
    session.play();

    std::vector<double> left(5, -1.0);
    std::vector<double> right(5, -1.0);
    double* outputs[] = {left.data(), right.data()};
    session.processBlock(nullptr, outputs, 0, 2, 5);

    constexpr double kCenterGain = 0.7071067811865475;
    EXPECT_DOUBLE_EQ(left[0], 0.0);
    EXPECT_DOUBLE_EQ(left[1], 0.0);
    EXPECT_NEAR(left[2], 0.5 * kCenterGain, 1.0e-7);
    EXPECT_NEAR(left[3], 0.25 * kCenterGain, 1.0e-7);
    EXPECT_NEAR(left[4], -0.5 * kCenterGain, 1.0e-7);
    EXPECT_EQ(session.positionFrames(), 5U);
    EXPECT_EQ(session.transport().positionSamples(), 5);
    EXPECT_EQ(session.transport().state(), Aura::Transport::TransportState::Stopped);
    EXPECT_FALSE(session.isPlaying());
    std::filesystem::remove(path);
}

TEST(ProjectPlaybackSession, BarSeekUsesSampleFramesAndStopSilences) {
    const auto path = writeTestWav();
    Aura::Project::Project project;
    project.createNew("Seek test");
    addAudioClip(project, path.string(), 2.0 / 96000.0, 3.0 / 96000.0);

    Aura::Session::ProjectPlaybackSession session;
    std::string error;
    ASSERT_TRUE(session.loadProject(project, 48000.0, error)) << error;
    ASSERT_TRUE(session.seekBars(4.0 / 96000.0));
    EXPECT_EQ(session.positionFrames(),
              4U); // The transport exposes the requested seek immediately.
    session.play();

    std::vector<double> left(1, 0.0);
    std::vector<double> right(1, 0.0);
    double* outputs[] = {left.data(), right.data()};
    session.processBlock(nullptr, outputs, 0, 2, 1);
    constexpr double kCenterGain = 0.7071067811865475;
    EXPECT_NEAR(left[0], -0.5 * kCenterGain, 1.0e-7);
    EXPECT_EQ(session.positionFrames(), 5U);
    EXPECT_FALSE(session.isPlaying());

    session.stop();
    left[0] = 99.0;
    right[0] = 99.0;
    session.processBlock(nullptr, outputs, 0, 2, 1);
    EXPECT_DOUBLE_EQ(left[0], 0.0);
    EXPECT_DOUBLE_EQ(right[0], 0.0);
    std::filesystem::remove(path);
}

TEST(ProjectPlaybackSession, NegotiatedSampleRateReschedulesAndPreservesBarPosition) {
    const auto path = writeTestWav();
    Aura::Project::Project project;
    project.createNew("Negotiation test");
    project.setTempo(120.0);
    addAudioClip(project, path.string(), 2.0 / 96000.0, 3.0 / 96000.0);

    Aura::Session::ProjectPlaybackSession session;
    std::string error;
    ASSERT_TRUE(session.loadProject(project, 48000.0, error)) << error;
    ASSERT_TRUE(session.seekBars(4.0 / 96000.0));
    session.play();

    ASSERT_TRUE(session.configureDeviceFormat(44100.0, 0, 2).empty());
    EXPECT_NEAR(session.transport().sampleRate(), 44100.0, 0.0);
    EXPECT_EQ(session.positionFrames(), 4U);
    EXPECT_TRUE(session.isPlaying());

    std::vector<double> left(1, 0.0);
    std::vector<double> right(1, 0.0);
    double* outputs[] = {left.data(), right.data()};
    session.processBlock(nullptr, outputs, 0, 2, 1);
    EXPECT_NEAR(left[0], -0.5 * 0.7071067811865475, 1.0e-7);
    EXPECT_EQ(session.positionFrames(), 5U);
    EXPECT_FALSE(session.isPlaying());
    std::filesystem::remove(path);
}

TEST(ProjectPlaybackSession, TempoChangeReschedulesClipAndPreservesMusicalPosition) {
    const auto path = writeTestWav();
    Aura::Project::Project project;
    project.createNew("Tempo change test");
    project.setTempo(120.0);
    addAudioClip(project, path.string(), 2.0 / 96000.0, 3.0 / 96000.0);

    Aura::Session::ProjectPlaybackSession session;
    std::string error;
    ASSERT_TRUE(session.loadProject(project, 48000.0, error)) << error;
    ASSERT_TRUE(session.seekBars(4.0 / 96000.0));
    session.play();

    ASSERT_TRUE(session.setProjectTempo(240.0).empty());
    EXPECT_DOUBLE_EQ(session.transport().tempo(), 240.0);
    EXPECT_EQ(session.positionFrames(), 2U);
    EXPECT_TRUE(session.isPlaying());

    std::vector<double> left(1, 0.0);
    std::vector<double> right(1, 0.0);
    double* outputs[] = {left.data(), right.data()};
    session.processBlock(nullptr, outputs, 0, 2, 1);
    EXPECT_NEAR(left[0], 0.25 * 0.7071067811865475, 1.0e-7);
    EXPECT_EQ(session.positionFrames(), 3U);
    EXPECT_FALSE(session.isPlaying());
    std::filesystem::remove(path);
}

TEST(ProjectPlaybackSession, RunsThroughOfflineEngineCallbackAndPublishesTransport) {
    const auto path = writeTestWav();
    Aura::Project::Project project;
    project.createNew("Engine callback test");
    addAudioClip(project, path.string(), 0.0, 1.0);

    Aura::Session::ProjectPlaybackSession session;
    std::string error;
    ASSERT_TRUE(session.loadProject(project, 48000.0, error)) << error;

    Aura::Audio::AudioEngine engine;
    Aura::Audio::AudioDeviceConfig config;
    config.driver = Aura::Audio::DriverType::Dummy;
    config.sampleRate = 48000.0;
    ASSERT_TRUE(engine.setConfig(config).empty());
    engine.setCallback(&session);
    ASSERT_TRUE(engine.start().empty());
    session.play();
    ASSERT_EQ(engine.renderOffline(1), 1);
    EXPECT_EQ(session.positionFrames(), static_cast<std::uint64_t>(engine.config().bufferSize));
    EXPECT_EQ(session.transport().positionSamples(), engine.config().bufferSize);
    EXPECT_EQ(session.transport().state(), Aura::Transport::TransportState::Playing);

    session.stop();
    engine.stop();
    std::filesystem::remove(path);
}

TEST(ProjectPlaybackSession, AppliesTrackFaderPanAndMasterGainInCallback) {
    const auto path = writeTestWav();
    Aura::Project::Project project;
    project.createNew("Mixer path test");
    addAudioClip(project, path.string(), 0.0, 1.0);

    Aura::Session::ProjectPlaybackSession session;
    std::string error;
    ASSERT_TRUE(session.loadProject(project, 48000.0, error)) << error;
    ASSERT_TRUE(session.setGainPan(-6.0, 0.0));
    ASSERT_TRUE(session.setMasterGainDb(-6.020599913279624)); // 0.5 linear master gain.
    session.play();

    std::vector<double> left(1, 0.0);
    std::vector<double> right(1, 0.0);
    double* outputs[] = {left.data(), right.data()};
    session.processBlock(nullptr, outputs, 0, 2, 1);

    constexpr double kCenterGain = 0.7071067811865475;
    const double expected = 0.5 * std::pow(10.0, -6.0 / 20.0) * kCenterGain * 0.5;
    EXPECT_NEAR(left[0], expected, 1.0e-7);
    EXPECT_NEAR(right[0], expected, 1.0e-7);
    EXPECT_NEAR(session.masterPeak(), expected, 1.0e-7);
    std::filesystem::remove(path);
}

TEST(ProjectPlaybackSession, RejectsMultipleClipsWithoutDestroyingCurrentSession) {
    const auto path = writeTestWav();
    Aura::Project::Project project;
    project.createNew("Single clip");
    addAudioClip(project, path.string(), 0.0, 1.0);

    Aura::Session::ProjectPlaybackSession session;
    std::string error;
    ASSERT_TRUE(session.loadProject(project, 48000.0, error)) << error;

    Aura::Project::Project multiple;
    multiple.createNew("Multiple clips");
    addAudioClip(multiple, path.string(), 0.0, 1.0);
    addAudioClip(multiple, path.string(), 1.0, 1.0);
    EXPECT_FALSE(session.loadProject(multiple, 48000.0, error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(session.isLoaded());

    ASSERT_TRUE(session.seekBars(0.0));
    session.play();
    std::vector<double> left(1, 0.0);
    std::vector<double> right(1, 0.0);
    double* outputs[] = {left.data(), right.data()};
    session.processBlock(nullptr, outputs, 0, 2, 1);
    EXPECT_GT(left[0], 0.0);
    std::filesystem::remove(path);
}

TEST(ProjectPlaybackSession, EnforcesDecodedMemoryBudget) {
    const auto path = writeTestWav();
    Aura::Project::Project project;
    project.createNew("Memory limit");
    addAudioClip(project, path.string(), 0.0, 1.0);

    Aura::Session::ProjectPlaybackSession session(1);
    std::string error;
    EXPECT_FALSE(session.loadProject(project, 48000.0, error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(session.isLoaded());
    std::filesystem::remove(path);
}
