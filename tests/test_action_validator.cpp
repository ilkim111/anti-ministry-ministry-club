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
