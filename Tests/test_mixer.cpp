/// @file test_mixer.cpp
/// @brief Unit tests for the mixer (strips, buses, mute/solo, master).

#include <algorithm>
#include <array>
#include <vector>

#include <gtest/gtest.h>

#include "Aura/Mixer.hpp"

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 256;

/// @brief Builds per-strip stereo inputs filled with a DC value.
struct StripInputSet {
    std::vector<std::vector<double>> storage; // [strip][L...R...]
    std::vector<std::array<const double*, 2>> ptrs;
    std::vector<const double* const*> top;

    explicit StripInputSet(int numStrips, double value = 0.5) {
        storage.assign(static_cast<std::size_t>(numStrips),
                       std::vector<double>(static_cast<std::size_t>(kBlock * 2), value));
        ptrs.resize(static_cast<std::size_t>(numStrips));
        top.resize(static_cast<std::size_t>(numStrips));
        for (int s = 0; s < numStrips; ++s) {
            ptrs[static_cast<std::size_t>(s)] = {storage[static_cast<std::size_t>(s)].data(),
                                                 storage[static_cast<std::size_t>(s)].data() + kBlock};
            top[static_cast<std::size_t>(s)] = ptrs[static_cast<std::size_t>(s)].data();
        }
    }
};

double bufferPeak(const std::vector<double>& v) {
    double peak = 0.0;
    for (double x : v) {
        peak = std::max(peak, std::abs(x));
    }
    return peak;
}

} // namespace

TEST(Mixer, BasicSumAndMasterPeak) {
    Aura::Mixer::Mixer mixer;
    mixer.prepare(kSampleRate, 512);
    mixer.addStrip("Drums");
    mixer.addStrip("Bass");
    EXPECT_EQ(mixer.stripCount(), 2);

    StripInputSet inputs(2, 0.25);
    std::vector<double> outL(kBlock, 0.0), outR(kBlock, 0.0);
    double* outs[2] = {outL.data(), outR.data()};
    mixer.processBlock(inputs.top.data(), outs, 2, kBlock);

    // Two strips at unity, center pan (-3 dB per side): 2 * 0.25 * cos(pi/4).
    EXPECT_NEAR(outL[0], 2 * 0.25 * 0.7071067, 1e-6);
    EXPECT_NEAR(outR[0], 2 * 0.25 * 0.7071067, 1e-6);
    EXPECT_GT(mixer.masterPeak(), 0.0);
}

TEST(Mixer, MuteAndSolo) {
    Aura::Mixer::Mixer mixer;
    mixer.prepare(kSampleRate, 512);
    mixer.addStrip("A");
    mixer.addStrip("B");

    StripInputSet inputs(2, 0.5);
    std::vector<double> outL(kBlock), outR(kBlock);
    double* outs[2] = {outL.data(), outR.data()};

    mixer.strip(0).setMute(true);
    mixer.processBlock(inputs.top.data(), outs, 2, kBlock);
    EXPECT_NEAR(outL[0], 0.5 * 0.7071067, 1e-6); // only strip B audible

    mixer.strip(0).setMute(false);
    mixer.strip(1).setSolo(true);
    mixer.processBlock(inputs.top.data(), outs, 2, kBlock);
    EXPECT_NEAR(outL[0], 0.5 * 0.7071067, 1e-6); // solo isolates strip B
}

TEST(Mixer, PanLaw) {
    Aura::Mixer::Mixer mixer;
    mixer.prepare(kSampleRate, 512);
    mixer.addStrip("Lead");

    StripInputSet inputs(1, 1.0);
    std::vector<double> outL(kBlock), outR(kBlock);
    double* outs[2] = {outL.data(), outR.data()};

    mixer.strip(0).setPan(-1.0); // hard left
    mixer.processBlock(inputs.top.data(), outs, 2, kBlock);
    EXPECT_NEAR(outL[0], 1.0, 1e-6);
    EXPECT_NEAR(outR[0], 0.0, 1e-6);

    mixer.strip(0).setPan(1.0); // hard right
    mixer.processBlock(inputs.top.data(), outs, 2, kBlock);
    EXPECT_NEAR(outL[0], 0.0, 1e-6);
    EXPECT_NEAR(outR[0], 1.0, 1e-6);
}

TEST(Mixer, FaderAndMasterGain) {
    Aura::Mixer::Mixer mixer;
    mixer.prepare(kSampleRate, 512);
    mixer.addStrip("Pad");
    mixer.strip(0).setVolumeDb(-6.0);
    mixer.setMasterVolumeDb(-6.0);

    StripInputSet inputs(1, 1.0);
    std::vector<double> outL(kBlock), outR(kBlock);
    double* outs[2] = {outL.data(), outR.data()};
    mixer.processBlock(inputs.top.data(), outs, 2, kBlock);
    // -12 dB total plus -3 dB center pan.
    EXPECT_NEAR(outL[0], 0.251187 * 0.7071067, 1e-4);
}

TEST(Mixer, SendsRouteToBuses) {
    Aura::Mixer::Mixer mixer;
    mixer.prepare(kSampleRate, 512);
    mixer.setNumBuses(1);
    mixer.addStrip("Vox");

    Aura::Mixer::Send send;
    send.busIndex = 0;
    send.levelDb = 0.0;
    mixer.strip(0).setSends({send});
    mixer.strip(0).setVolumeDb(-96.0); // strip itself silent, send still feeds bus

    StripInputSet inputs(1, 0.5);
    std::vector<double> outL(kBlock), outR(kBlock);
    double* outs[2] = {outL.data(), outR.data()};
    mixer.processBlock(inputs.top.data(), outs, 2, kBlock);
    // Bus return at unity adds the send back into the mix.
    EXPECT_GT(bufferPeak(outL), 0.4);
}

TEST(Mixer, RemoveStrip) {
    Aura::Mixer::Mixer mixer;
    mixer.prepare(kSampleRate, 512);
    mixer.addStrip("A");
    mixer.addStrip("B");
    mixer.removeStrip(0);
    EXPECT_EQ(mixer.stripCount(), 1);
    EXPECT_EQ(mixer.strip(0).name(), "B");
}
