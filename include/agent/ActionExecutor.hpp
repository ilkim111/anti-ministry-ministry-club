#pragma once
#include "llm/ActionSchema.hpp"
#include "console/IConsoleAdapter.hpp"
#include "console/ConsoleModel.hpp"
#include <spdlog/spdlog.h>
#include <cmath>
#include <thread>
#include <chrono>

// Executes validated MixActions on the console with safe ramping.
// Fader moves are ramped over multiple steps to avoid audible jumps.
class ActionExecutor {
public:
    ActionExecutor(IConsoleAdapter& adapter, ConsoleModel& model)
        : adapter_(adapter), model_(model) {}

    struct ExecutionResult {
        bool  success;
        float actualValue;   // what was actually set after ramping
        std::string error;
    };

    ExecutionResult execute(const MixAction& action) {
        switch (action.type) {
            case ActionType::SetFader:
                return executeFader(action);
            case ActionType::SetPan:
                return executePan(action);
            case ActionType::SetEqBand:
                return executeEq(action);
            case ActionType::SetCompressor:
                return executeComp(action);
            case ActionType::SetGate:
                return executeGate(action);
            case ActionType::SetHighPass:
                return executeHpf(action);
            case ActionType::SetSendLevel:
                return executeSend(action);
            case ActionType::MuteChannel:
                adapter_.setChannelParam(action.channel,
                    ChannelParam::Mute, true);
                spdlog::info("Executed: mute ch{}", action.channel);
                return {true, 1.0f, ""};
            case ActionType::UnmuteChannel:
                adapter_.setChannelParam(action.channel,
                    ChannelParam::Mute, false);
                spdlog::info("Executed: unmute ch{}", action.channel);
                return {true, 0.0f, ""};
            case ActionType::NoAction:
            case ActionType::Observation:
                return {true, 0.0f, ""};
        }
        return {false, 0.0f, "Unknown action type"};
    }

private:
    // Ramp fader over multiple steps to avoid audible jumps
    ExecutionResult executeFader(const MixAction& action) {
        float current = model_.channel(action.channel).fader;
        float target  = action.value;
        float delta   = target - current;

        // If delta is small enough, just set it directly
        if (std::abs(delta) < 0.02f) {
            adapter_.setChannelParam(action.channel,
                ChannelParam::Fader, target);
            spdlog::info("Executed: ch{} fader {:.2f} -> {:.2f}",
                         action.channel, current, target);
            return {true, target, ""};
        }

        // Ramp over ~200ms in 10 steps
        int steps = 10;
        float stepSize = delta / steps;
        float val = current;

        for (int i = 0; i < steps; i++) {
            val += stepSize;
            adapter_.setChannelParam(action.channel,
                ChannelParam::Fader, val);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        // Final set to exact target
        adapter_.setChannelParam(action.channel,
            ChannelParam::Fader, target);

        spdlog::info("Executed: ch{} fader {:.2f} -> {:.2f} (ramped)",
                     action.channel, current, target);
        return {true, target, ""};
    }

    ExecutionResult executePan(const MixAction& action) {
        adapter_.setChannelParam(action.channel,
            ChannelParam::Pan, action.value);
        spdlog::info("Executed: ch{} pan -> {:.2f}",
                     action.channel, action.value);
        return {true, action.value, ""};
    }

    ExecutionResult executeEq(const MixAction& action) {
        int band = action.bandIndex;
        float freq = action.value;
        float gain = action.value2;
        float q    = action.value3;

        // Map band index to ChannelParam
        ChannelParam freqParam, gainParam, qParam;
        switch (band) {
            case 1: freqParam = ChannelParam::EqBand1Freq;
                    gainParam = ChannelParam::EqBand1Gain;
                    qParam    = ChannelParam::EqBand1Q; break;
            case 2: freqParam = ChannelParam::EqBand2Freq;
                    gainParam = ChannelParam::EqBand2Gain;
                    qParam    = ChannelParam::EqBand2Q; break;
            case 3: freqParam = ChannelParam::EqBand3Freq;
                    gainParam = ChannelParam::EqBand3Gain;
                    qParam    = ChannelParam::EqBand3Q; break;
            case 4: freqParam = ChannelParam::EqBand4Freq;
                    gainParam = ChannelParam::EqBand4Gain;
                    qParam    = ChannelParam::EqBand4Q; break;
            default:
                return {false, 0, "Invalid EQ band " + std::to_string(band)};
        }

        adapter_.setChannelParam(action.channel, freqParam, freq);
        adapter_.setChannelParam(action.channel, gainParam, gain);
        adapter_.setChannelParam(action.channel, qParam, q);

        spdlog::info("Executed: ch{} EQ band{} {:.0f}Hz {:.1f}dB Q={:.1f}",
                     action.channel, band, freq, gain, q);
        return {true, gain, ""};
    }

    ExecutionResult executeComp(const MixAction& action) {
        adapter_.setChannelParam(action.channel,
            ChannelParam::CompThreshold, action.value);
        adapter_.setChannelParam(action.channel,
            ChannelParam::CompRatio, action.value2);
        adapter_.setChannelParam(action.channel,
            ChannelParam::CompOn, true);

        spdlog::info("Executed: ch{} comp thresh={:.1f}dB ratio={:.1f}:1",
                     action.channel, action.value, action.value2);
        return {true, action.value, ""};
    }

    ExecutionResult executeGate(const MixAction& action) {
        adapter_.setChannelParam(action.channel,
            ChannelParam::GateThreshold, action.value);
        adapter_.setChannelParam(action.channel,
            ChannelParam::GateOn, true);

        spdlog::info("Executed: ch{} gate thresh={:.1f}dB",
                     action.channel, action.value);
        return {true, action.value, ""};
    }

    ExecutionResult executeHpf(const MixAction& action) {
        adapter_.setChannelParam(action.channel,
            ChannelParam::HighPassFreq, action.value);
        adapter_.setChannelParam(action.channel,
            ChannelParam::HighPassOn, true);

        spdlog::info("Executed: ch{} HPF -> {:.0f}Hz",
                     action.channel, action.value);
        return {true, action.value, ""};
    }

    ExecutionResult executeSend(const MixAction& action) {
        adapter_.setSendLevel(action.channel, action.auxIndex, action.value);

        spdlog::info("Executed: ch{} send to bus{} -> {:.2f}",
                     action.channel, action.auxIndex, action.value);
        return {true, action.value, ""};
    }

    IConsoleAdapter& adapter_;
    ConsoleModel&    model_;
};
