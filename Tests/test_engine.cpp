/// @file test_engine.cpp
/// @brief Unit tests for the audio engine and transport.

#include <chrono>
#include <thread>
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
    EXPECT_EQ(Aura::Audio::fileExtension(Aura::Audio::AudioFileFormat::FLAC), std::string("flac"));
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
    std::string configureDeviceFormat(double sampleRate, int numInputs, int numOutputs) override {
        preparedSampleRate = sampleRate;
        preparedInputs = numInputs;
        preparedOutputs = numOutputs;
        return {};
    }

    int blocks = 0;
    double preparedSampleRate = 0.0;
    int preparedInputs = -1;
    int preparedOutputs = -1;
};

class SlowCallback : public Aura::Audio::IAudioCallback {
  public:
    void processBlock(const double* const* /*inputs*/, double* const* /*outputs*/,
                      int /*numInputs*/, int /*numOutputs*/, int /*numSamples*/) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
};
} // namespace

TEST(Engine, ConfiguresCallbackWithNegotiatedDummyFormat) {
    Aura::Audio::AudioEngine engine;
    CountingCallback callback;
    engine.setCallback(&callback);

    Aura::Audio::AudioDeviceConfig config;
    config.driver = Aura::Audio::DriverType::Dummy;
    config.sampleRate = 44100.0;
    config.numOutputChannels = 1;
    ASSERT_TRUE(engine.setConfig(config).empty());
    ASSERT_TRUE(engine.start().empty());

    EXPECT_DOUBLE_EQ(callback.preparedSampleRate, 44100.0);
    EXPECT_EQ(callback.preparedInputs, 0);
    EXPECT_EQ(callback.preparedOutputs, 1);
    EXPECT_DOUBLE_EQ(engine.negotiatedSampleRate(), 44100.0);
    EXPECT_EQ(engine.negotiatedOutputChannels(), 1);
    engine.stop();
}

TEST(Engine, WasapiDriverFactoryIsAvailableOnWindows) {
#if defined(_WIN32)
    const auto driver = Aura::Audio::createDriver(Aura::Audio::DriverType::WASAPI);
    ASSERT_NE(driver, nullptr);
    EXPECT_EQ(driver->type(), Aura::Audio::DriverType::WASAPI);
#else
    GTEST_SKIP() << "WASAPI is only compiled on Windows.";
#endif
}

TEST(Engine, UnsupportedHardwareDriversFailExplicitly) {
#if defined(_WIN32)
    GTEST_SKIP()
        << "WASAPI is compiled on Windows; hardware initialization is covered by device tests.";
#else
    Aura::Audio::AudioEngine engine;
    Aura::Audio::AudioDeviceConfig config;
    config.driver = Aura::Audio::DriverType::WASAPI;

    const std::string error = engine.setConfig(config);
    EXPECT_NE(error.find("WASAPI backend is not available"), std::string::npos);
    EXPECT_FALSE(engine.start().empty());
    EXPECT_FALSE(engine.isRunning());

    const auto devices = engine.enumerateDevices();
    ASSERT_EQ(devices.size(), 1);
    EXPECT_EQ(devices.front().driver, Aura::Audio::DriverType::Dummy);
#endif
}

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

TEST(Engine, CountsCallbacksThatExceedOneBufferPeriod) {
    Aura::Audio::AudioEngine engine;
    SlowCallback callback;
    engine.setCallback(&callback);

    Aura::Audio::AudioDeviceConfig config;
    config.driver = Aura::Audio::DriverType::Dummy;
    config.sampleRate = 48000.0;
    config.bufferSize = 16;
    ASSERT_TRUE(engine.setConfig(config).empty());
    EXPECT_EQ(engine.underrunCount(), 0U);

    EXPECT_EQ(engine.renderOffline(1), 1);
    EXPECT_EQ(engine.underrunCount(), 1U);
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
