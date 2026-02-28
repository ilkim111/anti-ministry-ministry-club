#include <gtest/gtest.h>
#include "llm/GenrePreset.hpp"
#include <fstream>
#include <filesystem>

TEST(GenrePresetTest, LibraryHasAllBuiltInPresets) {
    GenrePresetLibrary lib;
    EXPECT_NE(lib.get("rock"), nullptr);
    EXPECT_NE(lib.get("jazz"), nullptr);
    EXPECT_NE(lib.get("worship"), nullptr);
    EXPECT_NE(lib.get("edm"), nullptr);
    EXPECT_NE(lib.get("acoustic"), nullptr);
}

TEST(GenrePresetTest, UnknownPresetReturnsNull) {
    GenrePresetLibrary lib;
    EXPECT_EQ(lib.get("polka"), nullptr);
    EXPECT_EQ(lib.get(""), nullptr);
}

TEST(GenrePresetTest, RockPresetHasExpectedRoles) {
    GenrePresetLibrary lib;
    auto* rock = lib.get("rock");
    ASSERT_NE(rock, nullptr);
    EXPECT_EQ(rock->name, "rock");
    EXPECT_FALSE(rock->description.empty());

    // Rock should have kick, snare, lead vocal at minimum
    EXPECT_NE(rock->targetForRole(InstrumentRole::Kick), nullptr);
    EXPECT_NE(rock->targetForRole(InstrumentRole::Snare), nullptr);
    EXPECT_NE(rock->targetForRole(InstrumentRole::LeadVocal), nullptr);
}

TEST(GenrePresetTest, LeadVocalIsLoudestInRock) {
    GenrePresetLibrary lib;
    auto* rock = lib.get("rock");
    ASSERT_NE(rock, nullptr);

    auto* vocal = rock->targetForRole(InstrumentRole::LeadVocal);
    auto* kick  = rock->targetForRole(InstrumentRole::Kick);
    ASSERT_NE(vocal, nullptr);
    ASSERT_NE(kick, nullptr);

    // Lead vocal should be louder than kick
    EXPECT_GT(vocal->targetRmsRelative, kick->targetRmsRelative);
}

TEST(GenrePresetTest, JazzIsLessDynamic) {
    GenrePresetLibrary lib;
    auto* jazz = lib.get("jazz");
    ASSERT_NE(jazz, nullptr);

    auto* kick = jazz->targetForRole(InstrumentRole::Kick);
    ASSERT_NE(kick, nullptr);
    // Jazz kick dynamics should mention light or none
    EXPECT_TRUE(kick->dynamicsHint.find("light") != std::string::npos ||
                kick->dynamicsHint.find("none") != std::string::npos);
}

TEST(GenrePresetTest, TargetForRoleMissReturnsNull) {
    GenrePresetLibrary lib;
    auto* acoustic = lib.get("acoustic");
    ASSERT_NE(acoustic, nullptr);

    // Acoustic preset probably doesn't have a Synth target
    EXPECT_EQ(acoustic->targetForRole(InstrumentRole::Synth), nullptr);
}

TEST(GenrePresetTest, ToJsonProducesValidStructure) {
    GenrePresetLibrary lib;
    auto* rock = lib.get("rock");
    ASSERT_NE(rock, nullptr);

    auto j = rock->toJson();
    EXPECT_EQ(j["genre"], "rock");
    EXPECT_TRUE(j.contains("description"));
    EXPECT_TRUE(j.contains("targets"));
    EXPECT_TRUE(j["targets"].is_array());
    EXPECT_GT(j["targets"].size(), 0u);

    // Check first target has required fields
    auto& t = j["targets"][0];
    EXPECT_TRUE(t.contains("role"));
    EXPECT_TRUE(t.contains("target_db_relative"));
}

TEST(GenrePresetTest, ToJsonIncludesOptionalFields) {
    GenrePresetLibrary lib;
    auto* rock = lib.get("rock");
    ASSERT_NE(rock, nullptr);

    auto j = rock->toJson();
    // Find a target with pan != 0 (e.g. HiHat at 0.3)
    bool foundPan = false;
    for (auto& t : j["targets"]) {
        if (t.contains("pan")) {
            foundPan = true;
            break;
        }
    }
    EXPECT_TRUE(foundPan);
}

TEST(GenrePresetTest, AvailableListsAllPresets) {
    GenrePresetLibrary lib;
    auto names = lib.available();
    EXPECT_GE(names.size(), 5u);
}

TEST(GenrePresetTest, LoadFromFile) {
    // Create a temp JSON preset file
    std::string path = "/tmp/test_genre_preset.json";
    {
        nlohmann::json j = {
            {"genre", "custom_test"},
            {"description", "Test preset"},
            {"targets", {
                {
                    {"role", "Kick"},
                    {"target_db_relative", -5.0},
                    {"eq_character", "boomy"},
                    {"dynamics", "heavy 8:1"}
                },
                {
                    {"role", "LeadVocal"},
                    {"target_db_relative", 0.0},
                    {"pan", 0.0},
                    {"eq_character", "bright"}
                }
            }}
        };
        std::ofstream f(path);
        f << j.dump(2);
    }

    GenrePresetLibrary lib;
    EXPECT_TRUE(lib.loadFromFile(path));
    auto* custom = lib.get("custom_test");
    ASSERT_NE(custom, nullptr);
    EXPECT_EQ(custom->name, "custom_test");
    EXPECT_EQ(custom->targets.size(), 2u);

    auto* kick = custom->targetForRole(InstrumentRole::Kick);
    ASSERT_NE(kick, nullptr);
    EXPECT_FLOAT_EQ(kick->targetRmsRelative, -5.0f);
    EXPECT_EQ(kick->eqCharacter, "boomy");

    std::filesystem::remove(path);
}

TEST(GenrePresetTest, LoadFromInvalidFileReturnsFalse) {
    GenrePresetLibrary lib;
    EXPECT_FALSE(lib.loadFromFile("/tmp/nonexistent_preset_12345.json"));
}

TEST(GenrePresetTest, EDMKickIsLoud) {
    GenrePresetLibrary lib;
    auto* edm = lib.get("edm");
    ASSERT_NE(edm, nullptr);

    auto* kick = edm->targetForRole(InstrumentRole::Kick);
    ASSERT_NE(kick, nullptr);
    // EDM kick should be very prominent (-2 dB relative)
    EXPECT_GE(kick->targetRmsRelative, -4.0f);
}
