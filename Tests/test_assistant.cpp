/// @file test_assistant.cpp
/// @brief Unit tests for the AURA AI assistant.

#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Aura/Assistant.hpp"

TEST(Assistant, TheoryParsesKeys) {
    int root = -1;
    bool minor = false;
    EXPECT_TRUE(Aura::Ai::MusicTheory::parseKey("A minor", root, minor));
    EXPECT_EQ(root, 9);
    EXPECT_TRUE(minor);
    EXPECT_TRUE(Aura::Ai::MusicTheory::parseKey("F# major", root, minor));
    EXPECT_EQ(root, 6);
    EXPECT_FALSE(minor);
    EXPECT_FALSE(Aura::Ai::MusicTheory::parseKey("H major", root, minor));
    EXPECT_EQ(Aura::Ai::MusicTheory::formatKey(9, true), "A minor");
}

TEST(Assistant, ChordSuggestionsAreDiatonic) {
    Aura::Ai::ChordSuggester suggester;
    const auto chords = suggester.suggest("A minor", 8);
    ASSERT_EQ(chords.size(), 7);     // seven diatonic triads
    EXPECT_EQ(chords[0].name, "Am"); // tonic first
    EXPECT_EQ(chords[0].function, "i");
    for (const auto& chord : chords) {
        EXPECT_EQ(chord.midiNotes.size(), 3);
    }

    const auto prog = suggester.progression("A minor");
    ASSERT_EQ(prog.size(), 4);
    EXPECT_EQ(prog[0].function, "i");
    EXPECT_EQ(prog[1].function, "VI");
    EXPECT_EQ(prog[2].function, "III");
    EXPECT_EQ(prog[3].function, "VII");
}

TEST(Assistant, MelodyIsDeterministicAndInKey) {
    Aura::Ai::MelodyGenerator generator;
    const auto a = generator.generate("C major", 2, 42);
    const auto b = generator.generate("C major", 2, 42);
    ASSERT_EQ(a.size(), b.size());
    ASSERT_FALSE(a.empty());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i].midi, b[i].midi);
        EXPECT_DOUBLE_EQ(a[i].beat, b[i].beat);
    }
    // C major scale pitch classes only.
    const std::vector<int> allowed = {0, 2, 4, 5, 7, 9, 11};
    for (const auto& note : a) {
        EXPECT_NE(std::find(allowed.begin(), allowed.end(), note.midi % 12), allowed.end());
        EXPECT_LE(note.beat, 8.0);
    }
}

TEST(Assistant, MixAdvisorFlagsClipping) {
    Aura::Ai::MixAdvisor advisor;
    const auto hot = advisor.advise(-0.1, -8.0);
    ASSERT_FALSE(hot.empty());
    EXPECT_EQ(hot[0].category, "Level");

    const auto healthy = advisor.advise(-6.0, -18.0);
    ASSERT_FALSE(healthy.empty());
    EXPECT_EQ(healthy[0].message.find("healthy") != std::string::npos, true);
}

TEST(Assistant, MasteringTargetsPlatform) {
    Aura::Ai::MasteringAssistant mastering;
    const auto streaming = mastering.suggest(-3.0, -16.0, "streaming");
    EXPECT_DOUBLE_EQ(streaming.targetLufs, -14.0);
    EXPECT_DOUBLE_EQ(streaming.limiterCeilingDb, -1.0);

    const auto club = mastering.suggest(-3.0, -16.0, "club");
    EXPECT_DOUBLE_EQ(club.targetLufs, -9.0);
}
