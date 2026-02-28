#include <gtest/gtest.h>
#include "llm/ActionSchema.hpp"
#include <nlohmann/json.hpp>

// These tests verify that MixAction::fromJson handles malformed,
// partial, and edge-case LLM responses gracefully without crashing.

TEST(LLMParseResilience, ValidActionParsesCorrectly) {
    nlohmann::json j = {
        {"action",  "set_fader"},
        {"channel", 3},
        {"role",    "LeadVocal"},
        {"value",   0.8},
        {"urgency", "normal"},
        {"reason",  "vocal is buried"}
    };

    auto action = MixAction::fromJson(j);
    EXPECT_EQ(action.type, ActionType::SetFader);
    EXPECT_EQ(action.channel, 3);
    EXPECT_EQ(action.roleName, "LeadVocal");
    EXPECT_FLOAT_EQ(action.value, 0.8f);
    EXPECT_EQ(action.urgency, MixAction::Urgency::Normal);
    EXPECT_EQ(action.reason, "vocal is buried");
}

TEST(LLMParseResilience, MissingFieldsGetDefaults) {
    nlohmann::json j = {
        {"action", "set_fader"}
    };

    auto action = MixAction::fromJson(j);
    EXPECT_EQ(action.type, ActionType::SetFader);
    EXPECT_EQ(action.channel, 0);     // default
    EXPECT_FLOAT_EQ(action.value, 0.0f);
    EXPECT_EQ(action.urgency, MixAction::Urgency::Normal);
    EXPECT_EQ(action.reason, "");
    EXPECT_EQ(action.roleName, "");
}

TEST(LLMParseResilience, UnknownActionTypeBecomesNoAction) {
    nlohmann::json j = {
        {"action", "do_something_weird"},
        {"channel", 1}
    };

    auto action = MixAction::fromJson(j);
    EXPECT_EQ(action.type, ActionType::NoAction);
}

TEST(LLMParseResilience, EmptyObjectBecomesNoAction) {
    nlohmann::json j = nlohmann::json::object();

    auto action = MixAction::fromJson(j);
    EXPECT_EQ(action.type, ActionType::NoAction);
}

TEST(LLMParseResilience, AllActionTypesParsable) {
    std::vector<std::pair<std::string, ActionType>> types = {
        {"set_fader",    ActionType::SetFader},
        {"set_pan",      ActionType::SetPan},
        {"set_eq",       ActionType::SetEqBand},
        {"set_comp",     ActionType::SetCompressor},
        {"set_gate",     ActionType::SetGate},
        {"set_hpf",      ActionType::SetHighPass},
        {"set_send",     ActionType::SetSendLevel},
        {"mute",         ActionType::MuteChannel},
        {"unmute",       ActionType::UnmuteChannel},
        {"no_action",    ActionType::NoAction},
        {"observation",  ActionType::Observation},
    };

    for (auto& [str, expected] : types) {
        nlohmann::json j = {{"action", str}};
        auto action = MixAction::fromJson(j);
        EXPECT_EQ(action.type, expected) << "Failed for: " << str;
    }
}

TEST(LLMParseResilience, AllUrgencyLevelsParsable) {
    std::vector<std::pair<std::string, MixAction::Urgency>> levels = {
        {"immediate", MixAction::Urgency::Immediate},
        {"fast",      MixAction::Urgency::Fast},
        {"normal",    MixAction::Urgency::Normal},
        {"low",       MixAction::Urgency::Low},
    };

    for (auto& [str, expected] : levels) {
        nlohmann::json j = {{"action", "set_fader"}, {"urgency", str}};
        auto action = MixAction::fromJson(j);
        EXPECT_EQ(action.urgency, expected) << "Failed for: " << str;
    }
}

TEST(LLMParseResilience, UnknownUrgencyDefaultsToNormal) {
    nlohmann::json j = {
        {"action", "set_fader"},
        {"urgency", "super_urgent_please"}
    };

    auto action = MixAction::fromJson(j);
    EXPECT_EQ(action.urgency, MixAction::Urgency::Normal);
}

TEST(LLMParseResilience, EqActionParsesAllFields) {
    nlohmann::json j = {
        {"action",  "set_eq"},
        {"channel", 5},
        {"value",   2500.0},    // freq
        {"value2",  -3.0},      // gain
        {"value3",  2.0},       // Q
        {"band",    3},
        {"reason",  "cut mud"}
    };

    auto action = MixAction::fromJson(j);
    EXPECT_EQ(action.type, ActionType::SetEqBand);
    EXPECT_EQ(action.channel, 5);
    EXPECT_FLOAT_EQ(action.value, 2500.0f);
    EXPECT_FLOAT_EQ(action.value2, -3.0f);
    EXPECT_FLOAT_EQ(action.value3, 2.0f);
    EXPECT_EQ(action.bandIndex, 3);
}

TEST(LLMParseResilience, SendActionParsesAuxIndex) {
    nlohmann::json j = {
        {"action",  "set_send"},
        {"channel", 1},
        {"aux",     4},
        {"value",   0.6}
    };

    auto action = MixAction::fromJson(j);
    EXPECT_EQ(action.type, ActionType::SetSendLevel);
    EXPECT_EQ(action.auxIndex, 4);
    EXPECT_FLOAT_EQ(action.value, 0.6f);
}

TEST(LLMParseResilience, DescribeNeverCrashes) {
    // Verify describe() works for every action type, even with missing fields
    std::vector<ActionType> types = {
        ActionType::SetFader, ActionType::SetPan, ActionType::SetEqBand,
        ActionType::SetCompressor, ActionType::SetGate, ActionType::SetHighPass,
        ActionType::SetSendLevel, ActionType::MuteChannel,
        ActionType::UnmuteChannel, ActionType::NoAction, ActionType::Observation
    };

    for (auto type : types) {
        MixAction a;
        a.type = type;
        a.channel = 1;
        a.reason = "test";
        // Should not throw
        std::string desc = a.describe();
        EXPECT_FALSE(desc.empty()) << "Empty description for type " << (int)type;
    }
}

TEST(LLMParseResilience, ToJsonRoundTripsCleanly) {
    MixAction original;
    original.type      = ActionType::SetFader;
    original.channel   = 7;
    original.value     = 0.65f;
    original.urgency   = MixAction::Urgency::Fast;
    original.reason    = "vocal needs boost";
    original.roleName  = "LeadVocal";

    auto j = original.toJson();

    // Verify JSON fields exist
    EXPECT_EQ(j["channel"], 7);
    EXPECT_FLOAT_EQ(j["value"].get<float>(), 0.65f);
    EXPECT_EQ(j["reason"], "vocal needs boost");
    EXPECT_EQ(j["role"], "LeadVocal");
    EXPECT_FALSE(j["description"].get<std::string>().empty());
}

// Simulates the LLM returning garbage wrapped in a valid JSON array
TEST(LLMParseResilience, ArrayWithMixedValidAndInvalidActions) {
    nlohmann::json arr = nlohmann::json::array();

    // Valid action
    arr.push_back({
        {"action", "set_fader"}, {"channel", 1}, {"value", 0.7}
    });

    // Nonsense action type — should become NoAction
    arr.push_back({
        {"action", "wiggle_the_fader"}, {"channel", 2}
    });

    // Valid observation
    arr.push_back({
        {"action", "observation"}, {"reason", "bass is boomy"}
    });

    std::vector<MixAction> actions;
    for (auto& item : arr) {
        actions.push_back(MixAction::fromJson(item));
    }

    ASSERT_EQ(actions.size(), 3);
    EXPECT_EQ(actions[0].type, ActionType::SetFader);
    EXPECT_EQ(actions[1].type, ActionType::NoAction);
    EXPECT_EQ(actions[2].type, ActionType::Observation);
}

// Test that numeric strings in channel field are handled
TEST(LLMParseResilience, NumericFieldAsInt) {
    nlohmann::json j = {
        {"action", "set_fader"},
        {"channel", 5},
        {"value", 0.5}
    };

    auto action = MixAction::fromJson(j);
    EXPECT_EQ(action.channel, 5);
}

// Negative and extreme values
TEST(LLMParseResilience, ExtremeValues) {
    nlohmann::json j = {
        {"action",  "set_fader"},
        {"channel", 999},
        {"value",   -50.0},
        {"value2",  99999.0}
    };

    auto action = MixAction::fromJson(j);
    // Should parse without crashing — validation happens later
    EXPECT_EQ(action.channel, 999);
    EXPECT_FLOAT_EQ(action.value, -50.0f);
    EXPECT_FLOAT_EQ(action.value2, 99999.0f);
}
