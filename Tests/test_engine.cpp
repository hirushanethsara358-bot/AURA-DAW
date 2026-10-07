/// @file test_engine.cpp
/// @brief Unit tests for the audio engine and transport.

#include <vector>

#include <gtest/gtest.h>

#include "Aura/AudioEngine.hpp"
#include "Aura/Transport.hpp"

TEST(Engine, ConfigValidation) {
    Aura::Audio::AudioDeviceConfig good;
    EXPECT_TRUE(good.validate().empty());

    Aura::Audio::AudioDeviceConfig badRate = good;
    badRate.sampleRate = 12345.0;
    EXPECT_FALSE(badRate.validate().empty());

    Aura::Audio::AudioDeviceConfig badBuffer = good;
    badBuffer.bufferSize = 4;
    EXPECT_FALSE(badBuffer.validate().empty());

    Aura::Audio::AudioDeviceConfig badChannels = good;
    badChannels.numOutputChannels = 0;
    EXPECT_FALSE(badChannels.validate().empty());
}

TEST(Engine, DriverNames) {
    EXPECT_STREQ(Aura::Audio::toString(Aura::Audio::DriverType::ASIO), "ASIO");
    EXPECT_STREQ(Aura::Audio::toString(Aura::Audio::DriverType::WASAPI), "WASAPI");
    EXPECT_EQ(Aura::Audio::fileExtension(Aura::Audio::AudioFileFormat::FLAC),
              std::string("flac"));
}

namespace {
class CountingCallback : public Aura::Audio::IAudioCallback {
public:
    void processBlock(const double* const* /*inputs*/, double* const* outputs, int /*numInputs*/,
                      int numOutputs, int numSamples) override {
        ++blocks;
        for (int ch = 0; ch < numOutputs; ++ch) {
            for (int i = 0; i < numSamples; ++i) {
                outputs[ch][i] = 0.125;
            }
        }
    }
    int blocks = 0;
};
} // namespace

TEST(Engine, StartStopAndOfflineRender) {
    Aura::Audio::AudioEngine engine;
    CountingCallback callback;
    engine.setCallback(&callback);

    Aura::Audio::AudioDeviceConfig config;
    config.driver = Aura::Audio::DriverType::Dummy;
    config.sampleRate = 48000.0;
    config.bufferSize = 256;
    EXPECT_TRUE(engine.setConfig(config).empty());

    EXPECT_TRUE(engine.start().empty());
    EXPECT_TRUE(engine.isRunning());
    EXPECT_EQ(engine.latencySamples(), 256); // low-latency mode: single buffer

    EXPECT_EQ(engine.renderOffline(4), 4);
    EXPECT_EQ(callback.blocks, 4);
    EXPECT_GE(engine.cpuLoad(), 0.0);

    engine.stop();
    EXPECT_FALSE(engine.isRunning());

    const auto devices = engine.enumerateDevices();
    EXPECT_FALSE(devices.empty());
}

TEST(Transport, PlayAdvanceAndSeek) {
    Aura::Transport::Transport transport(48000.0);
    EXPECT_EQ(transport.state(), Aura::Transport::TransportState::Stopped);

    transport.play();
    transport.advance(48000); // 1 second at 120 BPM, 4/4
    EXPECT_EQ(transport.positionSamples(), 48000);
    EXPECT_DOUBLE_EQ(transport.positionSeconds(), 1.0);
    EXPECT_DOUBLE_EQ(transport.positionBeats(), 2.0);
    EXPECT_DOUBLE_EQ(transport.positionBars(), 0.5);

    transport.seekBars(1.0);
    EXPECT_EQ(transport.positionSamples(), 96000);

    transport.stop();
    transport.advance(48000); // no movement while stopped
    EXPECT_EQ(transport.positionSamples(), 96000);

    transport.record();
    EXPECT_EQ(transport.state(), Aura::Transport::TransportState::Recording);
}

TEST(Transport, LoopWraps) {
    Aura::Transport::Transport transport(48000.0);
    transport.setLoop(true, 0.0, 1.0); // one bar loop
    transport.play();
    transport.advance(96000 * 3 + 100); // 3+ bars
    EXPECT_LT(transport.positionSamples(), 96000);
    EXPECT_GE(transport.positionSamples(), 0);
}

TEST(Transport, TempoAndSignature) {
    Aura::Transport::Transport transport(48000.0);
    transport.setTempo(90.0);
    EXPECT_DOUBLE_EQ(transport.tempo(), 90.0);
    transport.setTempo(9999.0); // clamps
    EXPECT_DOUBLE_EQ(transport.tempo(), 999.0);
    transport.setTimeSignature(3, 4);
    transport.seekBars(1.0);
    EXPECT_NEAR(transport.positionBeats(), 3.0, 1e-2);
}
