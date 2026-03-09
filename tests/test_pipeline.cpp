#include <gtest/gtest.h>
#include "llm/ActionSchema.hpp"
#include "approval/ApprovalQueue.hpp"
#include "agent/ActionValidator.hpp"
#include "agent/ActionExecutor.hpp"
#include "console/ConsoleModel.hpp"

// ── Mock Console Adapter ─────────────────────────────────────────────────
// Records all setChannelParam / setSendLevel calls for verification.

class MockAdapter : public IConsoleAdapter {
public:
    struct Call {
        int channel;
        ChannelParam param;
        float floatVal;
        bool  boolVal;
        bool  isFloat;
    };
    struct SendCall {
        int channel;
        int aux;
        float level;
    };

    std::vector<Call> calls;
    std::vector<SendCall> sendCalls;

    bool connect(const std::string&, int) override { return true; }
    void disconnect() override {}
    bool isConnected() const override { return true; }
    void tick() override {}

    void setChannelParam(int ch, ChannelParam p, float val) override {
        calls.push_back({ch, p, val, false, true});
    }
    void setChannelParam(int ch, ChannelParam p, bool val) override {
        calls.push_back({ch, p, 0, val, false});
    }
    void setChannelParam(int, ChannelParam, const std::string&) override {}
    void setSendLevel(int ch, int aux, float level) override {
        sendCalls.push_back({ch, aux, level});
    }
    void setBusParam(int, BusParam, float) override {}

    void subscribeMeter(int) override {}
    void unsubscribeMeter() override {}
    void requestFullSync() override {}

    ConsoleCapabilities capabilities() const override {
        return {"Mock", "MockConsole", 32, 16, 6};
    }

    void clear() { calls.clear(); sendCalls.clear(); }

    // Find the last call for a specific channel+param
    const Call* findCall(int ch, ChannelParam p) const {
        for (auto it = calls.rbegin(); it != calls.rend(); ++it) {
            if (it->channel == ch && it->param == p) return &(*it);
        }
        return nullptr;
    }
};

// ── Pipeline Test Fixture ────────────────────────────────────────────────

class PipelineTest : public ::testing::Test {
protected:
    MockAdapter adapter;
    ConsoleModel model;
    ActionValidator validator;

    void SetUp() override {
        model.init(32, 16);
    }

    // Full pipeline: parse JSON -> validate -> execute
    ActionExecutor::ExecutionResult runPipeline(
        const nlohmann::json& actionJson)
    {
        auto action = MixAction::fromJson(actionJson);
        auto vr = validator.validate(action, model);
        if (!vr.valid) {
            return {false, 0, vr.warning};
        }
        ActionExecutor executor(adapter, model);
        return executor.execute(vr.clamped);
    }

    // Full pipeline with approval queue
    ActionExecutor::ExecutionResult runPipelineWithApproval(
        const nlohmann::json& actionJson,
        ApprovalQueue::Mode mode)
    {
        ApprovalQueue queue(mode);
        auto action = MixAction::fromJson(actionJson);
        bool autoApproved = queue.submit(action);

        if (!autoApproved) {
            // Not auto-approved — check if it's queued
            if (queue.pendingCount() == 0) {
                return {false, 0, "denied by queue"};
            }
            queue.approve(0);
            MixAction out;
            if (!queue.popApproved(out, 100)) {
                return {false, 0, "pop failed after approval"};
            }
            action = out;
        }

        auto vr = validator.validate(action, model);
        if (!vr.valid) {
            return {false, 0, vr.warning};
        }
        ActionExecutor executor(adapter, model);
        return executor.execute(vr.clamped);
    }
};

// ── Path 1: LLM JSON → Validator → Executor (all action types) ──────────

TEST_F(PipelineTest, FaderPipeline) {
    // Default fader is 0.75 (0dB unity). Setting to 0.5 is delta -0.25,
    // exceeds max delta 0.15, so gets clamped to 0.75 - 0.15 = 0.60
    auto result = runPipeline({
        {"action", "set_fader"}, {"channel", 1}, {"value", 0.5}
    });
    EXPECT_TRUE(result.success);
    auto* call = adapter.findCall(1, ChannelParam::Fader);
    ASSERT_NE(call, nullptr);
    EXPECT_NEAR(call->floatVal, 0.60f, 0.01f);
}

TEST_F(PipelineTest, FaderPipeline_SmallDelta) {
    // Small delta within limit should not be clamped
    auto result = runPipeline({
        {"action", "set_fader"}, {"channel", 1}, {"value", 0.8}
    });
    EXPECT_TRUE(result.success);
    auto* call = adapter.findCall(1, ChannelParam::Fader);
    ASSERT_NE(call, nullptr);
    EXPECT_NEAR(call->floatVal, 0.8f, 0.01f);
}

TEST_F(PipelineTest, FaderPipeline_DbConversion) {
    // LLM sends -20 dB, should be converted to ~0.375 normalized
    // Default fader is 0.75, so delta is -0.375, clamped to -0.15 = 0.60
    auto result = runPipeline({
        {"action", "set_fader"}, {"channel", 1}, {"value", -20.0}
    });
    EXPECT_TRUE(result.success);
    auto* call = adapter.findCall(1, ChannelParam::Fader);
    ASSERT_NE(call, nullptr);
    EXPECT_NEAR(call->floatVal, 0.60f, 0.01f);
}

TEST_F(PipelineTest, FaderPipeline_AboveOneTreatedAsDb) {
    auto result = runPipeline({
        {"action", "set_fader"}, {"channel", 1}, {"value", 5.0}
    });
    EXPECT_TRUE(result.success);
    auto* call = adapter.findCall(1, ChannelParam::Fader);
    ASSERT_NE(call, nullptr);
    EXPECT_GT(call->floatVal, 0.0f);
    EXPECT_LE(call->floatVal, 1.0f);
}

TEST_F(PipelineTest, MutePipeline) {
    auto result = runPipeline({
        {"action", "mute"}, {"channel", 5}
    });
    EXPECT_TRUE(result.success);
    auto* call = adapter.findCall(5, ChannelParam::Mute);
    ASSERT_NE(call, nullptr);
    EXPECT_TRUE(call->boolVal); // mute = true
}

TEST_F(PipelineTest, UnmutePipeline) {
    auto result = runPipeline({
        {"action", "unmute"}, {"channel", 5}
    });
    EXPECT_TRUE(result.success);
    auto* call = adapter.findCall(5, ChannelParam::Mute);
    ASSERT_NE(call, nullptr);
    EXPECT_FALSE(call->boolVal); // unmute = false
}

TEST_F(PipelineTest, PanPipeline) {
    auto result = runPipeline({
        {"action", "set_pan"}, {"channel", 3}, {"value", 0.3}
    });
    EXPECT_TRUE(result.success);
    auto* call = adapter.findCall(3, ChannelParam::Pan);
    ASSERT_NE(call, nullptr);
    EXPECT_NEAR(call->floatVal, 0.3f, 0.01f);
}

TEST_F(PipelineTest, EqPipeline) {
    // Subtractive EQ only — use negative gain (cut)
    auto result = runPipeline({
        {"action", "set_eq"}, {"channel", 2}, {"band", 1},
        {"value", 1000.0}, {"value2", -2.5}, {"value3", 1.5}
    });
    EXPECT_TRUE(result.success);
    // Should have set freq, gain, Q for band 1
    auto* freq = adapter.findCall(2, ChannelParam::EqBand1Freq);
    auto* gain = adapter.findCall(2, ChannelParam::EqBand1Gain);
    auto* q    = adapter.findCall(2, ChannelParam::EqBand1Q);
    ASSERT_NE(freq, nullptr);
    ASSERT_NE(gain, nullptr);
    ASSERT_NE(q, nullptr);
    EXPECT_NEAR(freq->floatVal, 1000.0f, 1.0f);
    EXPECT_NEAR(gain->floatVal, -2.5f, 0.01f);
    EXPECT_NEAR(q->floatVal, 1.5f, 0.01f);
}

TEST_F(PipelineTest, EqPipeline_BoostClampedToZero) {
    // Any boost should be clamped to 0dB (subtractive EQ only)
    auto result = runPipeline({
        {"action", "set_eq"}, {"channel", 1}, {"band", 2},
        {"value", 500.0}, {"value2", 10.0}, {"value3", 2.0}
    });
    EXPECT_TRUE(result.success);
    auto* gain = adapter.findCall(1, ChannelParam::EqBand2Gain);
    ASSERT_NE(gain, nullptr);
    EXPECT_LE(gain->floatVal, 0.0f);
}

TEST_F(PipelineTest, EqPipeline_AllBands) {
    for (int band = 1; band <= 4; band++) {
        adapter.clear();
        auto result = runPipeline({
            {"action", "set_eq"}, {"channel", 1}, {"band", band},
            {"value", 500.0}, {"value2", -2.0}, {"value3", 1.0}
        });
        EXPECT_TRUE(result.success) << "Failed for band " << band;
        EXPECT_FALSE(adapter.calls.empty()) << "No calls for band " << band;
    }
}

TEST_F(PipelineTest, CompressorPipeline) {
    auto result = runPipeline({
        {"action", "set_comp"}, {"channel", 4},
        {"value", -20.0}, {"value2", 4.0}
    });
    EXPECT_TRUE(result.success);
    auto* thresh = adapter.findCall(4, ChannelParam::CompThreshold);
    auto* ratio  = adapter.findCall(4, ChannelParam::CompRatio);
    auto* on     = adapter.findCall(4, ChannelParam::CompOn);
    ASSERT_NE(thresh, nullptr);
    ASSERT_NE(ratio, nullptr);
    ASSERT_NE(on, nullptr);
    EXPECT_NEAR(thresh->floatVal, -20.0f, 0.01f);
    EXPECT_NEAR(ratio->floatVal, 4.0f, 0.01f);
    EXPECT_TRUE(on->boolVal);
}

TEST_F(PipelineTest, CompressorPipeline_RatioClamped) {
    auto result = runPipeline({
        {"action", "set_comp"}, {"channel", 1},
        {"value", -10.0}, {"value2", 100.0}  // ratio way too high
    });
    EXPECT_TRUE(result.success);
    auto* ratio = adapter.findCall(1, ChannelParam::CompRatio);
    ASSERT_NE(ratio, nullptr);
    EXPECT_LE(ratio->floatVal, 20.0f); // clamped to max
}

TEST_F(PipelineTest, HpfPipeline) {
    auto result = runPipeline({
        {"action", "set_hpf"}, {"channel", 7}, {"value", 120.0}
    });
    EXPECT_TRUE(result.success);
    auto* freq = adapter.findCall(7, ChannelParam::HighPassFreq);
    auto* on   = adapter.findCall(7, ChannelParam::HighPassOn);
    ASSERT_NE(freq, nullptr);
    ASSERT_NE(on, nullptr);
    EXPECT_NEAR(freq->floatVal, 120.0f, 0.01f);
    EXPECT_TRUE(on->boolVal);
}

TEST_F(PipelineTest, HpfPipeline_Clamped) {
    auto result = runPipeline({
        {"action", "set_hpf"}, {"channel", 1}, {"value", 800.0}
    });
    EXPECT_TRUE(result.success);
    auto* freq = adapter.findCall(1, ChannelParam::HighPassFreq);
    ASSERT_NE(freq, nullptr);
    EXPECT_LE(freq->floatVal, 400.0f); // clamped to max
}

TEST_F(PipelineTest, SendPipeline) {
    auto result = runPipeline({
        {"action", "set_send"}, {"channel", 3}, {"aux", 2}, {"value", 0.6}
    });
    EXPECT_TRUE(result.success);
    ASSERT_EQ(adapter.sendCalls.size(), 1u);
    EXPECT_EQ(adapter.sendCalls[0].channel, 3);
    EXPECT_EQ(adapter.sendCalls[0].aux, 2);
    EXPECT_NEAR(adapter.sendCalls[0].level, 0.6f, 0.01f);
}

TEST_F(PipelineTest, GatePipeline) {
    auto result = runPipeline({
        {"action", "set_gate"}, {"channel", 2}, {"value", -30.0}
    });
    EXPECT_TRUE(result.success);
    auto* thresh = adapter.findCall(2, ChannelParam::GateThreshold);
    auto* on     = adapter.findCall(2, ChannelParam::GateOn);
    ASSERT_NE(thresh, nullptr);
    ASSERT_NE(on, nullptr);
    EXPECT_NEAR(thresh->floatVal, -30.0f, 0.01f);
    EXPECT_TRUE(on->boolVal);
}

TEST_F(PipelineTest, NoActionPipeline) {
    auto result = runPipeline({
        {"action", "no_action"}, {"reason", "sounds great"}
    });
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(adapter.calls.empty());
}

TEST_F(PipelineTest, ObservationPipeline) {
    auto result = runPipeline({
        {"action", "observation"}, {"reason", "vocals are sitting well"}
    });
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(adapter.calls.empty());
}

// ── Path 2: Invalid actions rejected by validator ────────────────────────

TEST_F(PipelineTest, InvalidChannel_Rejected) {
    auto result = runPipeline({
        {"action", "set_fader"}, {"channel", 0}, {"value", 0.5}
    });
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(adapter.calls.empty());
}

TEST_F(PipelineTest, ChannelOutOfRange_Rejected) {
    auto result = runPipeline({
        {"action", "set_fader"}, {"channel", 99}, {"value", 0.5}
    });
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(adapter.calls.empty());
}

// ── Path 3: Approval queue modes ─────────────────────────────────────────

TEST_F(PipelineTest, AutoAll_ExecutesImmediately) {
    auto result = runPipelineWithApproval(
        {{"action", "set_fader"}, {"channel", 1}, {"value", 0.5}},
        ApprovalQueue::Mode::AutoAll);
    EXPECT_TRUE(result.success);
    EXPECT_FALSE(adapter.calls.empty());
}

TEST_F(PipelineTest, DenyAll_Blocked) {
    auto result = runPipelineWithApproval(
        {{"action", "set_fader"}, {"channel", 1}, {"value", 0.5}},
        ApprovalQueue::Mode::DenyAll);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(adapter.calls.empty());
}

TEST_F(PipelineTest, ManualApprove_ExecutesAfterApproval) {
    auto result = runPipelineWithApproval(
        {{"action", "set_fader"}, {"channel", 1}, {"value", 0.5}},
        ApprovalQueue::Mode::ApproveAll);
    EXPECT_TRUE(result.success);
    EXPECT_FALSE(adapter.calls.empty());
}

TEST_F(PipelineTest, AutoUrgent_ImmediateExecutes) {
    auto result = runPipelineWithApproval(
        {{"action", "set_fader"}, {"channel", 1}, {"value", 0.5},
         {"urgency", "immediate"}},
        ApprovalQueue::Mode::AutoUrgent);
    EXPECT_TRUE(result.success);
    EXPECT_FALSE(adapter.calls.empty());
}

TEST_F(PipelineTest, AutoUrgent_FastExecutes) {
    auto result = runPipelineWithApproval(
        {{"action", "set_fader"}, {"channel", 1}, {"value", 0.5},
         {"urgency", "fast"}},
        ApprovalQueue::Mode::AutoUrgent);
    EXPECT_TRUE(result.success);
    EXPECT_FALSE(adapter.calls.empty());
}

TEST_F(PipelineTest, AutoUrgent_NormalQueued) {
    auto result = runPipelineWithApproval(
        {{"action", "mute"}, {"channel", 3}, {"urgency", "normal"}},
        ApprovalQueue::Mode::AutoUrgent);
    // Should succeed because our helper manually approves
    EXPECT_TRUE(result.success);
}

TEST_F(PipelineTest, AutoUrgent_LowQueued) {
    auto result = runPipelineWithApproval(
        {{"action", "set_hpf"}, {"channel", 1}, {"value", 80.0},
         {"urgency", "low"}},
        ApprovalQueue::Mode::AutoUrgent);
    EXPECT_TRUE(result.success);
}

// ── Path 4: Fader delta clamping through full pipeline ───────────────────

TEST_F(PipelineTest, FaderDeltaClamped_FullPipeline) {
    // Set channel 1 fader to 0.3 first
    ParameterUpdate u{};
    u.target = ParameterUpdate::Target::Channel;
    u.index  = 1;
    u.param  = ChannelParam::Fader;
    u.value  = 0.3f;
    model.applyUpdate(u);

    // Try to jump to 1.0 (delta 0.7, max is 0.15)
    auto result = runPipeline({
        {"action", "set_fader"}, {"channel", 1}, {"value", 1.0}
    });
    EXPECT_TRUE(result.success);
    auto* call = adapter.findCall(1, ChannelParam::Fader);
    ASSERT_NE(call, nullptr);
    // Should be clamped to 0.3 + 0.15 = 0.45
    EXPECT_NEAR(call->floatVal, 0.45f, 0.02f);
}

// ── Path 5: Multi-action LLM response ───────────────────────────────────

TEST_F(PipelineTest, MultipleActionsFromArray) {
    nlohmann::json arr = nlohmann::json::array();
    arr.push_back({{"action", "mute"}, {"channel", 1}});
    arr.push_back({{"action", "mute"}, {"channel", 2}});
    arr.push_back({{"action", "set_hpf"}, {"channel", 3}, {"value", 100.0}});
    arr.push_back({{"action", "no_action"}, {"reason", "ch4 sounds good"}});

    int successCount = 0;
    for (auto& item : arr) {
        auto action = MixAction::fromJson(item);
        auto vr = validator.validate(action, model);
        if (vr.valid) {
            ActionExecutor executor(adapter, model);
            auto er = executor.execute(vr.clamped);
            if (er.success) successCount++;
        }
    }
    EXPECT_EQ(successCount, 4); // all should succeed (including no_action)

    // Verify mute calls happened
    int muteCount = 0;
    for (auto& c : adapter.calls) {
        if (c.param == ChannelParam::Mute && c.boolVal) muteCount++;
    }
    EXPECT_EQ(muteCount, 2);
}

// ── Path 6: Chat path (direct execute, bypass approval) ─────────────────

TEST_F(PipelineTest, ChatPath_DirectExecution) {
    // Simulates the chat handler: parse → validate → execute (no approval queue)
    nlohmann::json chatResponse = {
        {"reply", "I'll mute channels 1 and 2 for you."},
        {"actions", nlohmann::json::array({
            {{"action", "mute"}, {"channel", 1}, {"reason", "engineer requested"}},
            {{"action", "mute"}, {"channel", 2}, {"reason", "engineer requested"}}
        })}
    };

    std::string reply = chatResponse.value("reply", "");
    EXPECT_FALSE(reply.empty());

    auto& actions = chatResponse["actions"];
    EXPECT_EQ(actions.size(), 2u);

    for (auto& item : actions) {
        auto action = MixAction::fromJson(item);
        auto vr = validator.validate(action, model);
        EXPECT_TRUE(vr.valid);
        ActionExecutor executor(adapter, model);
        auto er = executor.execute(vr.clamped);
        EXPECT_TRUE(er.success);
    }

    // Both channels should be muted
    int muteCount = 0;
    for (auto& c : adapter.calls) {
        if (c.param == ChannelParam::Mute && c.boolVal) muteCount++;
    }
    EXPECT_EQ(muteCount, 2);
}

TEST_F(PipelineTest, ChatPath_MixedActionsAndObservations) {
    nlohmann::json chatResponse = {
        {"reply", "Adjusting the mix as requested."},
        {"actions", nlohmann::json::array({
            {{"action", "set_fader"}, {"channel", 1}, {"value", 0.8}},
            {{"action", "observation"}, {"reason", "bass sounds good"}},
            {{"action", "set_hpf"}, {"channel", 3}, {"value", 80.0}},
            {{"action", "no_action"}, {"reason", "drums are fine"}}
        })}
    };

    int executed = 0;
    for (auto& item : chatResponse["actions"]) {
        auto action = MixAction::fromJson(item);
        if (action.type == ActionType::NoAction ||
            action.type == ActionType::Observation) {
            executed++;
            continue;
        }
        auto vr = validator.validate(action, model);
        EXPECT_TRUE(vr.valid);
        ActionExecutor executor(adapter, model);
        auto er = executor.execute(vr.clamped);
        EXPECT_TRUE(er.success);
        executed++;
    }
    EXPECT_EQ(executed, 4);
}

// ── Path 7: LLM response format variants ────────────────────────────────

TEST_F(PipelineTest, ParseActions_PlainArray) {
    std::string response = R"([
        {"action": "mute", "channel": 1},
        {"action": "set_fader", "channel": 2, "value": 0.7}
    ])";

    auto j = nlohmann::json::parse(response);
    EXPECT_TRUE(j.is_array());
    EXPECT_EQ(j.size(), 2u);

    auto a0 = MixAction::fromJson(j[0]);
    auto a1 = MixAction::fromJson(j[1]);
    EXPECT_EQ(a0.type, ActionType::MuteChannel);
    EXPECT_EQ(a1.type, ActionType::SetFader);
}

TEST_F(PipelineTest, ParseActions_WrappedInObject) {
    // Some models wrap actions in an object with "actions" key
    std::string response = R"({
        "actions": [
            {"action": "unmute", "channel": 3}
        ]
    })";

    auto j = nlohmann::json::parse(response);
    ASSERT_TRUE(j.contains("actions"));
    ASSERT_TRUE(j["actions"].is_array());

    auto action = MixAction::fromJson(j["actions"][0]);
    EXPECT_EQ(action.type, ActionType::UnmuteChannel);
    EXPECT_EQ(action.channel, 3);
}

TEST_F(PipelineTest, ParseActions_ChatFormat) {
    // Chat response format with reply + actions
    std::string response = R"({
        "reply": "Done, I've boosted the vocals.",
        "actions": [
            {"action": "set_fader", "channel": 13, "role": "LeadVocal",
             "value": 0.8, "urgency": "normal", "reason": "boost vocals"}
        ]
    })";

    auto j = nlohmann::json::parse(response);
    EXPECT_EQ(j["reply"], "Done, I've boosted the vocals.");
    ASSERT_TRUE(j["actions"].is_array());
    ASSERT_EQ(j["actions"].size(), 1u);

    auto action = MixAction::fromJson(j["actions"][0]);
    EXPECT_EQ(action.type, ActionType::SetFader);
    EXPECT_EQ(action.channel, 13);
    EXPECT_EQ(action.roleName, "LeadVocal");
    EXPECT_NEAR(action.value, 0.8f, 0.01f);
}

// ── Path 8: Rejection callback fires on queue reject ─────────────────────

TEST_F(PipelineTest, RejectionCallbackFires) {
    ApprovalQueue queue(ApprovalQueue::Mode::ApproveAll);

    bool callbackFired = false;
    int rejectedChannel = 0;
    queue.onRejected = [&](const MixAction& a) {
        callbackFired = true;
        rejectedChannel = a.channel;
    };

    MixAction action;
    action.type = ActionType::SetFader;
    action.channel = 7;
    action.urgency = MixAction::Urgency::Normal;
    queue.submit(action);

    queue.reject(0);
    EXPECT_TRUE(callbackFired);
    EXPECT_EQ(rejectedChannel, 7);
}

// ── Path 9: Mode switching mid-stream ────────────────────────────────────

TEST_F(PipelineTest, ModeSwitchFromManualToAutoAll) {
    ApprovalQueue queue(ApprovalQueue::Mode::ApproveAll);

    MixAction a1;
    a1.type = ActionType::SetFader;
    a1.channel = 1;
    a1.urgency = MixAction::Urgency::Normal;

    // Manual mode: queued
    EXPECT_FALSE(queue.submit(a1));
    EXPECT_EQ(queue.pendingCount(), 1u);

    // Switch to auto-all
    queue.setMode(ApprovalQueue::Mode::AutoAll);

    MixAction a2;
    a2.type = ActionType::MuteChannel;
    a2.channel = 2;
    a2.urgency = MixAction::Urgency::Normal;

    // Now auto-approved
    EXPECT_TRUE(queue.submit(a2));
    // First action still pending from before mode switch
    EXPECT_EQ(queue.pendingCount(), 1u);
}

// ── Path 10: End-to-end with all action types through approval ───────────

TEST_F(PipelineTest, AllActionTypes_ThroughAutoAllApproval) {
    std::vector<nlohmann::json> actions = {
        {{"action", "set_fader"}, {"channel", 1}, {"value", 0.5}},
        {{"action", "set_pan"}, {"channel", 2}, {"value", 0.3}},
        {{"action", "set_eq"}, {"channel", 3}, {"band", 1},
         {"value", 1000.0}, {"value2", -2.0}, {"value3", 2.0}},
        {{"action", "set_comp"}, {"channel", 4},
         {"value", -20.0}, {"value2", 3.0}},
        {{"action", "set_gate"}, {"channel", 5}, {"value", -40.0}},
        {{"action", "set_hpf"}, {"channel", 6}, {"value", 100.0}},
        {{"action", "set_send"}, {"channel", 7}, {"aux", 1}, {"value", 0.5}},
        {{"action", "mute"}, {"channel", 8}},
        {{"action", "unmute"}, {"channel", 9}},
        {{"action", "no_action"}, {"reason", "fine"}},
        {{"action", "observation"}, {"reason", "noted"}},
    };

    for (auto& aj : actions) {
        adapter.clear();
        auto result = runPipelineWithApproval(aj, ApprovalQueue::Mode::AutoAll);
        std::string actionStr = aj.value("action", "unknown");
        EXPECT_TRUE(result.success)
            << "Failed for action: " << actionStr;
    }
}
