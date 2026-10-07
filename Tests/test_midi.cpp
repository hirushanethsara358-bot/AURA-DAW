/// @file test_midi.cpp
/// @brief Unit tests for MIDI utilities, messages and clip editing.

#include <gtest/gtest.h>

#include "Aura/Midi.hpp"

TEST(Midi, NoteNamesAndFrequencies) {
    EXPECT_EQ(Aura::Midi::noteName(60), "C4");
    EXPECT_EQ(Aura::Midi::noteName(69), "A4");
    EXPECT_EQ(Aura::Midi::noteName(61), "C#4");
    EXPECT_NEAR(Aura::Midi::noteFrequency(69), 440.0, 1e-6);
    EXPECT_NEAR(Aura::Midi::noteFrequency(60), 261.6256, 1e-2);
    EXPECT_TRUE(Aura::Midi::isBlackKey(61));
    EXPECT_FALSE(Aura::Midi::isBlackKey(60));
}

TEST(Midi, ClipOrdering) {
    Aura::Midi::Clip clip;
    clip.addMessage(Aura::Midi::Message::noteOn(64, 100, 2.0));
    clip.addMessage(Aura::Midi::Message::noteOn(60, 100, 0.0));
    clip.addMessage(Aura::Midi::Message::noteOn(67, 100, 1.0));
    ASSERT_EQ(clip.size(), 3);
    EXPECT_EQ(clip.messages()[0].note, 60);
    EXPECT_EQ(clip.messages()[1].note, 67);
    EXPECT_EQ(clip.messages()[2].note, 64);
}

TEST(Midi, QuantizeSnapsToGrid) {
    Aura::Midi::Clip clip;
    clip.addMessage(Aura::Midi::Message::noteOn(60, 100, 0.51));
    clip.addMessage(Aura::Midi::Message::noteOn(62, 100, 1.49));
    clip.quantize(0.5, 1.0);
    EXPECT_DOUBLE_EQ(clip.messages()[0].beat, 0.5);
    EXPECT_DOUBLE_EQ(clip.messages()[1].beat, 1.5);
}

TEST(Midi, QuantizeStrengthIsPartial) {
    Aura::Midi::Clip clip;
    clip.addMessage(Aura::Midi::Message::noteOn(60, 100, 0.6));
    clip.quantize(0.5, 0.5);
    EXPECT_DOUBLE_EQ(clip.messages()[0].beat, 0.55);
}

TEST(Midi, HumanizeIsDeterministic) {
    Aura::Midi::Clip a;
    a.addMessage(Aura::Midi::Message::noteOn(60, 100, 1.0));
    a.addMessage(Aura::Midi::Message::noteOn(64, 100, 2.0));
    Aura::Midi::Clip b = a;
    a.humanize(0.02, 8, 1234);
    b.humanize(0.02, 8, 1234);
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_DOUBLE_EQ(a.messages()[i].beat, b.messages()[i].beat);
        EXPECT_EQ(a.messages()[i].velocity, b.messages()[i].velocity);
    }
    // And it actually changes something with a nonzero amount.
    Aura::Midi::Clip c;
    c.addMessage(Aura::Midi::Message::noteOn(60, 100, 1.0));
    c.humanize(0.05, 20, 7);
    const bool changed = (c.messages()[0].beat != 1.0) || (c.messages()[0].velocity != 100);
    EXPECT_TRUE(changed);
}

TEST(Midi, TransposeAndVelocity) {
    Aura::Midi::Clip clip;
    clip.addMessage(Aura::Midi::Message::noteOn(60, 100, 0.0));
    clip.transpose(12);
    EXPECT_EQ(clip.messages()[0].note, 72);
    clip.transpose(-200); // clamps
    EXPECT_EQ(clip.messages()[0].note, 0);
    clip.scaleVelocity(0.5);
    EXPECT_EQ(clip.messages()[0].velocity, 50);
}
