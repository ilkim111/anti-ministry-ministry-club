#include <gtest/gtest.h>
#include "agent/ActionValidator.hpp"

class ActionValidatorTest : public ::testing::Test {
protected:
    ConsoleModel model;
    ActionValidator validator;

    void SetUp() override {
        model.init(32, 16);
    }
};

TEST_F(ActionValidatorTest, ValidFaderAction) {
    MixAction action;
    action.type    = ActionType::SetFader;
    action.channel = 1;
    action.value   = 0.80f;

    auto result = validator.validate(action, model);
    EXPECT_TRUE(result.valid);
}

TEST_F(ActionValidatorTest, ClampsFaderDelta) {
    // Set current fader to 0.5
    ParameterUpdate u{};
    u.target = ParameterUpdate::Target::Channel;
    u.index  = 1;
    u.param  = ChannelParam::Fader;
    u.value  = 0.5f;
    model.applyUpdate(u);

    // Try to move to 1.0 (delta = 0.5, exceeds max 0.15)
    MixAction action;
    action.type    = ActionType::SetFader;
    action.channel = 1;
    action.value   = 1.0f;

    auto result = validator.validate(action, model);
    EXPECT_TRUE(result.valid);
    EXPECT_NEAR(result.clamped.value, 0.65f, 0.01f);
    EXPECT_FALSE(result.warning.empty());
}

TEST_F(ActionValidatorTest, InvalidChannelRejected) {
    MixAction action;
    action.type    = ActionType::SetFader;
    action.channel = 0;  // invalid
    action.value   = 0.5f;

    auto result = validator.validate(action, model);
    EXPECT_FALSE(result.valid);
}

TEST_F(ActionValidatorTest, EqBoostClamped) {
    MixAction action;
    action.type      = ActionType::SetEqBand;
    action.channel   = 1;
    action.bandIndex = 1;
    action.value     = 1000.0f;  // freq
    action.value2    = 10.0f;    // gain — too high
    action.value3    = 2.0f;     // Q

    auto result = validator.validate(action, model);
    EXPECT_TRUE(result.valid);
    EXPECT_LE(result.clamped.value2, 3.0f);  // clamped to max boost
}

TEST_F(ActionValidatorTest, EqCutAllowed) {
    MixAction action;
    action.type      = ActionType::SetEqBand;
    action.channel   = 1;
    action.bandIndex = 1;
    action.value     = 300.0f;
    action.value2    = -6.0f;    // reasonable cut
    action.value3    = 2.0f;

    auto result = validator.validate(action, model);
    EXPECT_TRUE(result.valid);
    EXPECT_FLOAT_EQ(result.clamped.value2, -6.0f);
}

TEST_F(ActionValidatorTest, HpfClampedToMax) {
    MixAction action;
    action.type    = ActionType::SetHighPass;
    action.channel = 1;
    action.value   = 800.0f;  // way too high

    auto result = validator.validate(action, model);
    EXPECT_TRUE(result.valid);
    EXPECT_LE(result.clamped.value, 400.0f);
}

TEST_F(ActionValidatorTest, NoActionAlwaysValid) {
    MixAction action;
    action.type = ActionType::NoAction;
    action.reason = "everything sounds fine";

    auto result = validator.validate(action, model);
    EXPECT_TRUE(result.valid);
}

// ── dB to Fader Float Conversion ─────────────────────────────────────────
// X32 "level" type: 4 piecewise linear dB ranges
//   0.0    → 0.0625  =  -∞   to -60 dB
//   0.0625 → 0.25    =  -60  to -30 dB
//   0.25   → 0.5     =  -30  to -10 dB
//   0.5    → 1.0     =  -10  to +10 dB

TEST_F(ActionValidatorTest, DbToFader_NegativeInfinity) {
    EXPECT_FLOAT_EQ(ActionValidator::dbToFaderFloat(-90.0f), 0.0f);
    EXPECT_FLOAT_EQ(ActionValidator::dbToFaderFloat(-100.0f), 0.0f);
}

TEST_F(ActionValidatorTest, DbToFader_MaxValue) {
    EXPECT_FLOAT_EQ(ActionValidator::dbToFaderFloat(10.0f), 1.0f);
    EXPECT_FLOAT_EQ(ActionValidator::dbToFaderFloat(15.0f), 1.0f);
}

TEST_F(ActionValidatorTest, DbToFader_BoundaryPoints) {
    EXPECT_NEAR(ActionValidator::dbToFaderFloat(-60.0f), 0.0625f, 0.001f);
    EXPECT_NEAR(ActionValidator::dbToFaderFloat(-30.0f), 0.25f, 0.001f);
    EXPECT_NEAR(ActionValidator::dbToFaderFloat(-10.0f), 0.5f, 0.001f);
    EXPECT_NEAR(ActionValidator::dbToFaderFloat(10.0f), 1.0f, 0.001f);
}

TEST_F(ActionValidatorTest, DbToFader_ZeroDB) {
    // 0 dB → 0.5 + 10/20 * 0.5 = 0.75
    EXPECT_NEAR(ActionValidator::dbToFaderFloat(0.0f), 0.75f, 0.001f);
}

TEST_F(ActionValidatorTest, DbToFader_Minus20dB) {
    // -20 dB → 0.25 + 10/20 * 0.25 = 0.375
    EXPECT_NEAR(ActionValidator::dbToFaderFloat(-20.0f), 0.375f, 0.001f);
}

TEST_F(ActionValidatorTest, DbToFader_Minus5dB) {
    // -5 dB → 0.5 + 5/20 * 0.5 = 0.625
    EXPECT_NEAR(ActionValidator::dbToFaderFloat(-5.0f), 0.625f, 0.001f);
}

TEST_F(ActionValidatorTest, DbToFader_Plus5dB) {
    // +5 dB → 0.5 + 15/20 * 0.5 = 0.875
    EXPECT_NEAR(ActionValidator::dbToFaderFloat(5.0f), 0.875f, 0.001f);
}

TEST_F(ActionValidatorTest, DbToFader_Minus45dB) {
    // -45 dB → 0.0625 + 15/30 * 0.1875 = 0.15625
    EXPECT_NEAR(ActionValidator::dbToFaderFloat(-45.0f), 0.15625f, 0.001f);
}

TEST_F(ActionValidatorTest, DbToFader_Minus75dB) {
    // -75 dB → 0 + 15/30 * 0.0625 = 0.03125
    EXPECT_NEAR(ActionValidator::dbToFaderFloat(-75.0f), 0.03125f, 0.001f);
}

TEST_F(ActionValidatorTest, DbToFader_MonotonicIncrease) {
    float prev = 0.0f;
    for (float db = -89.0f; db <= 10.0f; db += 1.0f) {
        float val = ActionValidator::dbToFaderFloat(db);
        EXPECT_GE(val, prev) << "Not monotonic at " << db << " dB";
        prev = val;
    }
}

// ── Validator auto-detects dB values in fader actions ────────────────────

TEST_F(ActionValidatorTest, FaderNegativeValueTreatedAsDb) {
    // When LLM sends -20 (dB), validator should convert to normalized float
    MixAction action;
    action.type = ActionType::SetFader;
    action.channel = 1;
    action.value = -20.0f;

    auto result = validator.validate(action, model);
    EXPECT_TRUE(result.valid);
    // -20 dB → ~0.375, but may be delta-clamped from 0.0
    EXPECT_GT(result.clamped.value, 0.0f);
    EXPECT_LE(result.clamped.value, 1.0f);
}

TEST_F(ActionValidatorTest, FaderValueAboveOneTreatedAsDb) {
    // When LLM sends 5 (dB), it's > 1.0 so should be treated as dB
    MixAction action;
    action.type = ActionType::SetFader;
    action.channel = 1;
    action.value = 5.0f;

    auto result = validator.validate(action, model);
    EXPECT_TRUE(result.valid);
    EXPECT_GT(result.clamped.value, 0.0f);
    EXPECT_LE(result.clamped.value, 1.0f);
}

TEST_F(ActionValidatorTest, FaderNormalizedValueNotConverted) {
    // A value already in [0, 1] should NOT be treated as dB
    // Set current fader to 0.4 first
    ParameterUpdate u{};
    u.target = ParameterUpdate::Target::Channel;
    u.index  = 2;
    u.param  = ChannelParam::Fader;
    u.value  = 0.4f;
    model.applyUpdate(u);

    MixAction action;
    action.type = ActionType::SetFader;
    action.channel = 2;
    action.value = 0.5f;  // small delta from 0.4 — should not be treated as dB

    auto result = validator.validate(action, model);
    EXPECT_TRUE(result.valid);
    // 0.5 is in [0,1] so stays as 0.5 (delta 0.1 < maxDelta 0.15)
    EXPECT_NEAR(result.clamped.value, 0.5f, 0.01f);
}

TEST_F(ActionValidatorTest, MuteUnmuteValid) {
    MixAction mute;
    mute.type    = ActionType::MuteChannel;
    mute.channel = 5;

    auto r1 = validator.validate(mute, model);
    EXPECT_TRUE(r1.valid);

    MixAction unmute;
    unmute.type    = ActionType::UnmuteChannel;
    unmute.channel = 5;

    auto r2 = validator.validate(unmute, model);
    EXPECT_TRUE(r2.valid);
}
