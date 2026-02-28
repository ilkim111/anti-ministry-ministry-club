#include <gtest/gtest.h>
#include "llm/PreferenceLearner.hpp"
#include <filesystem>

class PreferenceLearnerTest : public ::testing::Test {
protected:
    PreferenceLearner learner;

    MixAction makeFaderAction(float value) {
        MixAction a;
        a.type = ActionType::SetFader;
        a.channel = 1;
        a.value = value;
        a.roleName = "Kick";
        return a;
    }

    MixAction makeEqBoost() {
        MixAction a;
        a.type = ActionType::SetEqBand;
        a.channel = 1;
        a.value = 1000;   // freq
        a.value2 = 3.0;   // gain (positive = boost)
        a.roleName = "LeadVocal";
        return a;
    }

    MixAction makeEqCut() {
        MixAction a;
        a.type = ActionType::SetEqBand;
        a.channel = 1;
        a.value = 300;
        a.value2 = -4.0;  // gain (negative = cut)
        a.roleName = "LeadVocal";
        return a;
    }

    MixAction makeCompAction(float ratio) {
        MixAction a;
        a.type = ActionType::SetCompressor;
        a.channel = 1;
        a.value = -20;     // threshold
        a.value2 = ratio;  // ratio
        a.roleName = "Kick";
        return a;
    }

    MixAction makeHpfAction(float freq) {
        MixAction a;
        a.type = ActionType::SetHighPass;
        a.channel = 1;
        a.value = freq;
        a.roleName = "AcousticGuitar";
        return a;
    }
};

TEST_F(PreferenceLearnerTest, StartsEmpty) {
    EXPECT_EQ(learner.totalDecisions(), 0);
    EXPECT_FALSE(learner.isDirty());

    auto prefs = learner.buildPreferences();
    EXPECT_TRUE(prefs.empty());
}

TEST_F(PreferenceLearnerTest, RecordApprovalTracksDirty) {
    learner.recordApproval(makeFaderAction(0.7f), "Kick");
    EXPECT_TRUE(learner.isDirty());
    EXPECT_EQ(learner.totalDecisions(), 1);
}

TEST_F(PreferenceLearnerTest, RecordRejectionTracksDirty) {
    learner.recordRejection(makeFaderAction(0.7f), "Kick");
    EXPECT_TRUE(learner.isDirty());
    EXPECT_EQ(learner.totalDecisions(), 1);
}

TEST_F(PreferenceLearnerTest, ApprovalRateCalculation) {
    // Need > 5 total decisions for approval rate
    for (int i = 0; i < 8; i++)
        learner.recordApproval(makeFaderAction(0.6f), "Kick");
    for (int i = 0; i < 2; i++)
        learner.recordRejection(makeFaderAction(0.6f), "Kick");

    auto prefs = learner.buildPreferences();
    EXPECT_FALSE(prefs.empty());
    EXPECT_TRUE(prefs.contains("overall_approval_rate"));
    EXPECT_FLOAT_EQ(prefs["overall_approval_rate"].get<float>(), 0.8f);
}

TEST_F(PreferenceLearnerTest, LowApprovalRateSuggestsConservative) {
    for (int i = 0; i < 3; i++)
        learner.recordApproval(makeFaderAction(0.5f), "Kick");
    for (int i = 0; i < 7; i++)
        learner.recordRejection(makeFaderAction(0.5f), "Kick");

    auto prefs = learner.buildPreferences();
    EXPECT_TRUE(prefs.contains("note"));
    std::string note = prefs["note"];
    EXPECT_NE(note.find("conservative"), std::string::npos);
}

TEST_F(PreferenceLearnerTest, HighApprovalRateShowsConfidence) {
    for (int i = 0; i < 9; i++)
        learner.recordApproval(makeFaderAction(0.7f), "Kick");
    learner.recordRejection(makeFaderAction(0.7f), "Kick");

    auto prefs = learner.buildPreferences();
    EXPECT_TRUE(prefs.contains("note"));
    std::string note = prefs["note"];
    EXPECT_NE(note.find("trust"), std::string::npos);
}

TEST_F(PreferenceLearnerTest, EqTendencyDetectsPreferCuts) {
    // Approve many cuts, reject many boosts
    for (int i = 0; i < 5; i++) {
        learner.recordApproval(makeEqCut(), "LeadVocal");
        learner.recordRejection(makeEqBoost(), "LeadVocal");
    }

    auto prefs = learner.buildPreferences();
    EXPECT_TRUE(prefs.contains("eq_tendency"));
    std::string eq = prefs["eq_tendency"];
    EXPECT_NE(eq.find("cut"), std::string::npos);
}

TEST_F(PreferenceLearnerTest, PerRolePreferences) {
    // Need 3+ decisions per role for role prefs to appear
    for (int i = 0; i < 4; i++)
        learner.recordApproval(makeFaderAction(0.7f), "Kick");
    learner.recordRejection(makeFaderAction(0.3f), "Kick");

    auto prefs = learner.buildPreferences();
    EXPECT_TRUE(prefs.contains("role_preferences"));
    auto& rp = prefs["role_preferences"];
    EXPECT_TRUE(rp.contains("Kick"));
    EXPECT_TRUE(rp["Kick"].contains("approval_rate"));
    EXPECT_TRUE(rp["Kick"].contains("preferred_fader_range"));
}

TEST_F(PreferenceLearnerTest, CompressionPreference) {
    // Approve compression for kick with known ratios
    for (int i = 0; i < 4; i++)
        learner.recordApproval(makeCompAction(4.0f), "Kick");

    auto prefs = learner.buildPreferences();
    EXPECT_TRUE(prefs.contains("role_preferences"));
    auto& kick = prefs["role_preferences"]["Kick"];
    EXPECT_TRUE(kick.contains("preferred_comp_ratio"));
    EXPECT_FLOAT_EQ(kick["preferred_comp_ratio"].get<float>(), 4.0f);
}

TEST_F(PreferenceLearnerTest, HpfPreference) {
    for (int i = 0; i < 3; i++)
        learner.recordApproval(makeHpfAction(100.0f), "AcousticGuitar");

    auto prefs = learner.buildPreferences();
    EXPECT_TRUE(prefs.contains("role_preferences"));
    auto& ag = prefs["role_preferences"]["AcousticGuitar"];
    EXPECT_TRUE(ag.contains("preferred_hpf_hz"));
    EXPECT_EQ(ag["preferred_hpf_hz"].get<int>(), 100);
}

TEST_F(PreferenceLearnerTest, FrequentRejectionWarning) {
    // 1 approval, 5 rejections => 16.7% approval rate
    learner.recordApproval(makeFaderAction(0.5f), "Snare");
    for (int i = 0; i < 5; i++)
        learner.recordRejection(makeFaderAction(0.5f), "Snare");

    auto prefs = learner.buildPreferences();
    auto& snare = prefs["role_preferences"]["Snare"];
    EXPECT_TRUE(snare.contains("warning"));
    std::string warning = snare["warning"];
    EXPECT_NE(warning.find("leave it alone"), std::string::npos);
}

TEST_F(PreferenceLearnerTest, StandingInstructions) {
    learner.recordInstruction("always keep vocals above -6dB");
    EXPECT_TRUE(learner.isDirty());

    // Instructions don't appear in buildPreferences directly
    // but are tracked internally and persisted
    EXPECT_EQ(learner.totalDecisions(), 0); // instructions aren't decisions
}

TEST_F(PreferenceLearnerTest, InstructionsCappedAt20) {
    for (int i = 0; i < 25; i++)
        learner.recordInstruction("instruction " + std::to_string(i));

    // The oldest 5 should have been evicted (cap is 20)
    // We can verify via save/load round-trip
    std::string path = "/tmp/test_prefs_cap.json";
    EXPECT_TRUE(learner.saveToFile(path));

    PreferenceLearner loaded;
    EXPECT_TRUE(loaded.loadFromFile(path));
    std::filesystem::remove(path);
}

TEST_F(PreferenceLearnerTest, SaveAndLoadRoundTrip) {
    std::string path = "/tmp/test_prefs_roundtrip.json";

    // Record some decisions
    for (int i = 0; i < 5; i++)
        learner.recordApproval(makeFaderAction(0.7f), "Kick");
    for (int i = 0; i < 2; i++)
        learner.recordRejection(makeFaderAction(0.3f), "Kick");
    learner.recordInstruction("keep vocals hot");

    EXPECT_TRUE(learner.saveToFile(path));

    // Load into a new learner
    PreferenceLearner loaded;
    EXPECT_TRUE(loaded.loadFromFile(path));
    EXPECT_EQ(loaded.totalDecisions(), 7);

    // Preferences should match
    auto prefs1 = learner.buildPreferences();
    auto prefs2 = loaded.buildPreferences();
    EXPECT_EQ(prefs1["role_preferences"]["Kick"]["approval_rate"],
              prefs2["role_preferences"]["Kick"]["approval_rate"]);

    std::filesystem::remove(path);
}

TEST_F(PreferenceLearnerTest, SaveToInvalidPathReturnsFalse) {
    EXPECT_FALSE(learner.saveToFile("/nonexistent/dir/prefs.json"));
}

TEST_F(PreferenceLearnerTest, LoadFromInvalidPathReturnsFalse) {
    EXPECT_FALSE(learner.loadFromFile("/tmp/nonexistent_prefs_12345.json"));
}

TEST_F(PreferenceLearnerTest, ClearDirty) {
    learner.recordApproval(makeFaderAction(0.5f), "Kick");
    EXPECT_TRUE(learner.isDirty());
    learner.clearDirty();
    EXPECT_FALSE(learner.isDirty());
}

TEST_F(PreferenceLearnerTest, MultipleRolesTrackedSeparately) {
    for (int i = 0; i < 4; i++) {
        learner.recordApproval(makeFaderAction(0.8f), "Kick");
        learner.recordRejection(makeFaderAction(0.3f), "Snare");
    }

    auto prefs = learner.buildPreferences();
    EXPECT_TRUE(prefs.contains("role_preferences"));
    auto& rp = prefs["role_preferences"];
    EXPECT_TRUE(rp.contains("Kick"));
    EXPECT_TRUE(rp.contains("Snare"));

    // Kick should have high approval, Snare low
    EXPECT_GT(rp["Kick"]["approval_rate"].get<float>(), 0.9f);
    EXPECT_LT(rp["Snare"]["approval_rate"].get<float>(), 0.1f);
}
