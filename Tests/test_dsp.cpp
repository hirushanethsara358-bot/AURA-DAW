/// @file test_dsp.cpp
/// @brief Unit tests for the AURA DSP suite.

#include <cmath>
#include <vector>

#include <gtest/gtest.h>

#include "Aura/DSP.hpp"

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 512;

std::vector<double> makeSine(double freq, int count, double sampleRate = kSampleRate) {
    std::vector<double> out(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        out[static_cast<std::size_t>(i)] =
            std::sin(2.0 * 3.141592653589793 * freq * i / sampleRate);
    }
    return out;
}

double rms(const std::vector<double>& v) {
    double sum = 0.0;
    for (double x : v) {
        sum += x * x;
    }
    return std::sqrt(sum / v.size());
}

double peakOf(const std::vector<double>& v) {
    double peak = 0.0;
    for (double x : v) {
        peak = std::max(peak, std::abs(x));
    }
    return peak;
}

} // namespace

TEST(Dsp, BiquadLowPassFiltersHighs) {
    Aura::Dsp::Biquad filter;
    filter.setType(Aura::Dsp::Biquad::Type::LowPass);
    filter.setParams(1000.0, 0.7071);
    filter.prepare(kSampleRate, 1);

    auto low = makeSine(100.0, kBlock * 4);
    double* ch[1] = {low.data()};
    filter.processBlock(ch, 1, kBlock * 4);
    EXPECT_GT(rms(low) / 0.7071, 0.9); // passes lows

    filter.reset();
    auto high = makeSine(10000.0, kBlock * 4);
    double* chH[1] = {high.data()};
    filter.processBlock(chH, 1, kBlock * 4);
    EXPECT_LT(rms(high) / 0.7071, 0.2); // attenuates highs
}

TEST(Dsp, BiquadPeakingBoosts) {
    Aura::Dsp::Biquad filter;
    filter.setType(Aura::Dsp::Biquad::Type::Peaking);
    filter.setParams(1000.0, 1.0, 12.0);
    filter.prepare(kSampleRate, 1);

    auto tone = makeSine(1000.0, kBlock * 8);
    double* ch[1] = {tone.data()};
    filter.processBlock(ch, 1, kBlock * 8);
    // +12 dB boost at center (allow settling tolerance).
    EXPECT_GT(rms(tone) / 0.7071, 2.5);
}

TEST(Dsp, ParametricEqDefaultIsFlat) {
    Aura::Dsp::ParametricEQ eq;
    eq.prepare(kSampleRate, 2);
    // Neutralize HPF/LPF corners so the default chain is transparent.
    eq.band(0).setParams(5.0, 0.7071);
    eq.band(5).setParams(23000.0, 0.7071);

    auto left = makeSine(440.0, kBlock * 4);
    auto right = makeSine(440.0, kBlock * 4);
    double* ch[2] = {left.data(), right.data()};
    eq.processBlock(ch, 2, kBlock * 4);
    EXPECT_NEAR(rms(left) / 0.7071, 1.0, 0.1);
    EXPECT_NEAR(rms(right) / 0.7071, 1.0, 0.1);
}

TEST(Dsp, CompressorReducesLoudSignal) {
    Aura::Dsp::Compressor comp;
    comp.setThresholdDb(-20.0);
    comp.setRatio(10.0);
    comp.setAttackMs(1.0);
    comp.setReleaseMs(50.0);
    comp.prepare(kSampleRate, 1);

    std::vector<double> loud(kBlock * 8, 0.9);
    double* ch[1] = {loud.data()};
    comp.processBlock(ch, 1, kBlock * 8); // warm up the envelope
    std::fill(loud.begin(), loud.end(), 0.9);
    comp.processBlock(ch, 1, kBlock * 8); // steady state
    EXPECT_LT(peakOf(loud), 0.6);
    EXPECT_GT(comp.gainReductionDb(), 1.0);
}

TEST(Dsp, LimiterEnforcesCeiling) {
    Aura::Dsp::Limiter limiter;
    limiter.setCeilingDb(-6.0);
    limiter.prepare(kSampleRate, 1);

    std::vector<double> hot(kBlock * 4, 2.0);
    double* ch[1] = {hot.data()};
    limiter.processBlock(ch, 1, kBlock * 4);
    EXPECT_LE(peakOf(hot), 0.55); // -6 dBFS ~= 0.501
}

TEST(Dsp, GateClosesOnSilence) {
    Aura::Dsp::Gate gate;
    gate.setThresholdDb(-40.0);
    gate.prepare(kSampleRate, 1);

    std::vector<double> silence(kBlock * 8, 1e-6);
    double* ch[1] = {silence.data()};
    gate.processBlock(ch, 1, kBlock * 8);
    EXPECT_LT(peakOf(silence), 1e-6);

    std::vector<double> loud(kBlock * 8, 0.5);
    double* chL[1] = {loud.data()};
    gate.processBlock(chL, 1, kBlock * 8);
    EXPECT_GT(peakOf(loud), 0.3); // opens for loud signal
}

TEST(Dsp, DelayProducesEcho) {
    Aura::Dsp::Delay delay;
    delay.setDelayMs(100.0);
    delay.setFeedback(0.0);
    delay.setMix(1.0);
    delay.prepare(kSampleRate, 1);

    const int total = kBlock * 16;
    std::vector<double> buf(static_cast<std::size_t>(total), 0.0);
    buf[0] = 1.0; // impulse
    double* ch[1] = {buf.data()};
    delay.processBlock(ch, 1, total);

    const int echoPos = static_cast<int>(0.1 * kSampleRate);
    EXPECT_GT(std::abs(buf[static_cast<std::size_t>(echoPos)]), 0.5);
}

TEST(Dsp, ReverbProducesTail) {
    Aura::Dsp::Reverb reverb;
    reverb.setRoomSize(0.8);
    reverb.setMix(0.5);
    reverb.prepare(kSampleRate, 1);

    const int total = kBlock * 16;
    std::vector<double> buf(static_cast<std::size_t>(total), 0.0);
    buf[0] = 1.0;
    double* ch[1] = {buf.data()};
    reverb.processBlock(ch, 1, total);

    // Energy well after the impulse indicates a reverb tail.
    double tailEnergy = 0.0;
    for (int i = kBlock * 4; i < total; ++i) {
        tailEnergy += buf[static_cast<std::size_t>(i)] * buf[static_cast<std::size_t>(i)];
    }
    EXPECT_GT(tailEnergy, 1e-6);
}

TEST(Dsp, ChorusAndFlangerModulate) {
    auto dry = makeSine(440.0, kBlock * 4);

    Aura::Dsp::Chorus chorus;
    chorus.prepare(kSampleRate, 1);
    auto wet = dry;
    double* ch[1] = {wet.data()};
    chorus.processBlock(ch, 1, kBlock * 4);
    double diff = 0.0;
    for (std::size_t i = 0; i < dry.size(); ++i) {
        diff += std::abs(dry[i] - wet[i]);
    }
    EXPECT_GT(diff, 1e-3);
    EXPECT_LT(peakOf(wet), 2.0);

    Aura::Dsp::Flanger flanger;
    flanger.prepare(kSampleRate, 1);
    auto wetF = dry;
    double* chF[1] = {wetF.data()};
    flanger.processBlock(chF, 1, kBlock * 4);
    EXPECT_LT(peakOf(wetF), 3.0);
}

TEST(Dsp, DistortionAddsHarmonicsAndBounds) {
    Aura::Dsp::Distortion dist;
    dist.setDrive(30.0);
    dist.setTone(0.9);
    dist.setMix(1.0);
    dist.prepare(kSampleRate, 1);

    auto tone = makeSine(220.0, kBlock * 4);
    const double dryPeak = peakOf(tone);
    double* ch[1] = {tone.data()};
    dist.processBlock(ch, 1, kBlock * 4);
    EXPECT_LE(peakOf(tone), 1.01);
    // Clipped wave differs from the pure sine.
    auto clean = makeSine(220.0, kBlock * 4);
    double diff = 0.0;
    for (std::size_t i = 0; i < tone.size(); ++i) {
        diff += std::abs(tone[i] - clean[i] * (peakOf(tone) / dryPeak));
    }
    EXPECT_GT(diff, 1.0);
}

TEST(Dsp, DbConversions) {
    EXPECT_NEAR(Aura::Dsp::dbToGain(0.0), 1.0, 1e-9);
    EXPECT_NEAR(Aura::Dsp::dbToGain(-6.0), 0.501187, 1e-4);
    EXPECT_NEAR(Aura::Dsp::gainToDb(1.0), 0.0, 1e-9);
    EXPECT_EQ(Aura::Dsp::gainToDb(0.0), -120.0);
}
