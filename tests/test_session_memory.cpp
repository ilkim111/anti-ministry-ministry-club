#include <gtest/gtest.h>
#include "llm/SessionMemory.hpp"

class SessionMemoryTest : public ::testing::Test {
protected:
    SessionMemory memory{50};
};

TEST_F(SessionMemoryTest, RecordAndRetrieveAction) {
    MixAction action;
    action.type    = ActionType::SetFader;
    action.channel = 3;
    action.value   = 0.8f;
    action.reason  = "boost vocal";

    nlohmann::json ctx = {{"test", true}};
    memory.recordAction(action, ctx);

    EXPECT_EQ(memory.size(), 1);

    auto context = memory.buildContext(10);
    ASSERT_EQ(context.size(), 1);
    EXPECT_EQ(context[0]["type"], "action_taken");
}

TEST_F(SessionMemoryTest, RecordRejection) {
    MixAction action;
    action.type    = ActionType::SetFader;
    action.channel = 1;

    memory.recordRejection(action, "too aggressive");

    auto context = memory.buildContext(10);
    ASSERT_EQ(context.size(), 1);
    EXPECT_EQ(context[0]["type"], "action_rejected");
    EXPECT_NE(context[0]["note"].get<std::string>().find("too aggressive"),
              std::string::npos);
}

TEST_F(SessionMemoryTest, RecordObservation) {
    memory.recordObservation("snare is getting lost in the mix");

    auto context = memory.buildContext(10);
    ASSERT_EQ(context.size(), 1);
    EXPECT_EQ(context[0]["type"], "observation");
}

TEST_F(SessionMemoryTest, RecordEngineerOverride) {
    memory.recordEngineerOverride(5, "fader moved from 0.6 to 0.9");

    auto context = memory.buildContext(10);
    ASSERT_EQ(context.size(), 1);
    EXPECT_EQ(context[0]["type"], "engineer_override");
    EXPECT_EQ(context[0]["channel"], 5);
}

TEST_F(SessionMemoryTest, RecordInstruction) {
    memory.recordInstruction("bring up the vocals");

    auto context = memory.buildContext(10);
    ASSERT_EQ(context.size(), 1);
    EXPECT_EQ(context[0]["type"], "engineer_instruction");
    EXPECT_EQ(context[0]["instruction"], "bring up the vocals");
}

TEST_F(SessionMemoryTest, ActiveInstructionsReturnsRecentInstructions) {
    memory.recordInstruction("more reverb on snare");
    memory.recordObservation("mix sounding good");
    memory.recordInstruction("leave drums alone");
    memory.recordAction({}, {});
    memory.recordInstruction("bring up keys");

    auto instructions = memory.activeInstructions(10);
    ASSERT_EQ(instructions.size(), 3);
    EXPECT_EQ(instructions[0], "more reverb on snare");
    EXPECT_EQ(instructions[1], "leave drums alone");
    EXPECT_EQ(instructions[2], "bring up keys");
}

TEST_F(SessionMemoryTest, ActiveInstructionsRespectsMaxCount) {
    for (int i = 0; i < 20; i++) {
        memory.recordInstruction("instruction " + std::to_string(i));
    }

    auto instructions = memory.activeInstructions(5);
    EXPECT_EQ(instructions.size(), 5);
    // Should be the 5 most recent
    EXPECT_EQ(instructions[0], "instruction 15");
    EXPECT_EQ(instructions[4], "instruction 19");
}

TEST_F(SessionMemoryTest, TrimsToMaxEntries) {
    SessionMemory small(5);

    for (int i = 0; i < 20; i++) {
        small.recordObservation("note " + std::to_string(i));
    }

    EXPECT_EQ(small.size(), 5);

    auto context = small.buildContext(100);
    EXPECT_EQ(context.size(), 5);
    // Should contain only the last 5
    EXPECT_EQ(context[0]["note"], "note 15");
    EXPECT_EQ(context[4]["note"], "note 19");
}

TEST_F(SessionMemoryTest, BuildContextRespectsMaxRecent) {
    for (int i = 0; i < 30; i++) {
        memory.recordObservation("note " + std::to_string(i));
    }

    auto context = memory.buildContext(5);
    EXPECT_EQ(context.size(), 5);
}

TEST_F(SessionMemoryTest, MixSnapshotRecorded) {
    nlohmann::json state = {{"channels", 32}, {"rms", -12.0}};
    memory.recordSnapshot(state);

    auto context = memory.buildContext(10);
    ASSERT_EQ(context.size(), 1);
    EXPECT_EQ(context[0]["type"], "snapshot");
}

TEST_F(SessionMemoryTest, TimestampsAreReasonable) {
    memory.recordObservation("test");

    auto context = memory.buildContext(10);
    ASSERT_EQ(context.size(), 1);
    // seconds_ago should be 0 or very small (just recorded)
    int secondsAgo = context[0]["seconds_ago"].get<int>();
    EXPECT_LT(secondsAgo, 2);
}

TEST_F(SessionMemoryTest, MixedEntryTypesOrderPreserved) {
    memory.recordAction({}, {});
    memory.recordInstruction("more bass");
    memory.recordObservation("feedback risk on ch5");
    memory.recordEngineerOverride(5, "fader");

    auto context = memory.buildContext(10);
    ASSERT_EQ(context.size(), 4);
    EXPECT_EQ(context[0]["type"], "action_taken");
    EXPECT_EQ(context[1]["type"], "engineer_instruction");
    EXPECT_EQ(context[2]["type"], "observation");
    EXPECT_EQ(context[3]["type"], "engineer_override");
}

TEST_F(SessionMemoryTest, EmptyMemoryReturnsEmptyContext) {
    auto context = memory.buildContext(10);
    EXPECT_TRUE(context.empty());

    auto instructions = memory.activeInstructions(10);
    EXPECT_TRUE(instructions.empty());
}
