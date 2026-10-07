/// @file test_synth.cpp
/// @brief Unit tests for AURA Synth (oscillator, ADSR, voices).

#include <cmath>
#include <vector>

#include <gtest/gtest.h>

#include "Aura/Synth.hpp"

namespace {
double bufferRms(const std::vector<double>& v) {
    double sum = 0.0;
    for (double x : v) {
        sum += x * x;
    }
    return std::sqrt(sum / v.size());
}
} // namespace

TEST(Synth, MidiToFrequency) {
    EXPECT_NEAR(Aura::Instrument::midiToFrequency(69), 440.0, 1e-6);
    EXPECT_NEAR(Aura::Instrument::midiToFrequency(60), 261.6256, 1e-2);
}

TEST(Synth, OscillatorSineRms) {
    Aura::Instrument::Oscillator osc;
    osc.setType(Aura::Instrument::OscillatorType::Sine);
    osc.setFrequency(440.0);
    osc.setSampleRate(48000.0);
    std::vector<double> buf(4800);
    for (auto& x : buf) {
        x = osc.next();
    }
    EXPECT_NEAR(bufferRms(buf), 0.7071, 0.01);
}

TEST(Synth, AdsrEnvelopeShape) {
    Aura::Instrument::Adsr adsr;
    adsr.setSampleRate(1000.0);
    adsr.setParams({0.1, 0.1, 0.5, 0.1});
    adsr.noteOn();
    double peak = 0.0;
    for (int i = 0; i < 100; ++i) { // attack phase
        peak = std::max(peak, adsr.next());
    }
    EXPECT_NEAR(peak, 1.0, 0.05);
    for (int i = 0; i < 2000; ++i) { // decay to sustain
        adsr.next();
    }
    EXPECT_NEAR(adsr.next(), 0.5, 0.05);
    adsr.noteOff();
    for (int i = 0; i < 2000; ++i) {
        adsr.next();
    }
    EXPECT_FALSE(adsr.isActive());
}

TEST(Synth, NoteOnRendersAudio) {
    Aura::Instrument::Synth synth;
    synth.prepare(48000.0);
    synth.noteOn(69, 100);
    EXPECT_EQ(synth.activeVoiceCount(), 1);

    std::vector<double> left(4800, 0.0), right(4800, 0.0);
    synth.renderBlock(left.data(), right.data(), 4800);
    EXPECT_GT(bufferRms(left), 0.01);
    EXPECT_GT(bufferRms(right), 0.01);

    synth.noteOff(69);
    // Render past the default release tail.
    for (int b = 0; b < 20; ++b) {
        std::fill(left.begin(), left.end(), 0.0);
        std::fill(right.begin(), right.end(), 0.0);
        synth.renderBlock(left.data(), right.data(), 4800);
    }
    EXPECT_EQ(synth.activeVoiceCount(), 0);
}

TEST(Synth, VoiceStealingCapsPolyphony) {
    Aura::Instrument::Synth synth(2);
    synth.prepare(48000.0);
    synth.noteOn(60, 100);
    synth.noteOn(64, 100);
    synth.noteOn(67, 100); // steals oldest
    EXPECT_LE(synth.activeVoiceCount(), 2);
    synth.allNotesOff();
    EXPECT_EQ(synth.activeVoiceCount(), 0);
}
