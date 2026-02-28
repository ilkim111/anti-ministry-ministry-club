#pragma once
#include "llm/ActionSchema.hpp"
#include "console/ConsoleModel.hpp"
#include <spdlog/spdlog.h>
#include <string>
#include <cmath>

// Validates and clamps MixActions before they reach the console.
// This is the safety layer — no action bypasses it.
class ActionValidator {
public:
    struct ValidationResult {
        bool        valid;
        MixAction   clamped;      // the action after safety clamping
        std::string warning;      // if we had to clamp
    };

    struct SafetyLimits {
        float maxFaderDeltaNorm   = 0.15f;   // ~6dB max fader move per step
        float maxEqBoostDB        = 3.0f;    // max EQ boost per step
        float maxEqCutDB          = -12.0f;  // max EQ cut per step
        float maxCompThresholdDB  = -50.0f;  // don't go below this
        float minCompRatio        = 1.0f;
        float maxCompRatio        = 20.0f;
        float maxHpfHz            = 400.0f;  // don't HPF above this
        float minHpfHz            = 20.0f;
        float maxSendDelta        = 0.2f;    // max send level change
    };

    ActionValidator() = default;
    explicit ActionValidator(const SafetyLimits& limits) : limits_(limits) {}

    ValidationResult validate(const MixAction& action,
                               const ConsoleModel& model) const {
        ValidationResult result{true, action, ""};

        switch (action.type) {
            case ActionType::SetFader:
                result = validateFader(action, model);
                break;
            case ActionType::SetEqBand:
                result = validateEq(action);
                break;
            case ActionType::SetCompressor:
                result = validateComp(action);
                break;
            case ActionType::SetHighPass:
                result = validateHpf(action);
                break;
            case ActionType::SetSendLevel:
                result = validateSend(action, model);
                break;
            case ActionType::MuteChannel:
            case ActionType::UnmuteChannel:
                // Always valid but logged
                spdlog::info("Validator: {} ch{}",
                    action.type == ActionType::MuteChannel ? "mute" : "unmute",
                    action.channel);
                break;
            case ActionType::NoAction:
            case ActionType::Observation:
                break;
            default:
                break;
        }

        return result;
    }

private:
    ValidationResult validateFader(const MixAction& action,
                                    const ConsoleModel& model) const {
        ValidationResult r{true, action, ""};

        if (action.channel < 1 || action.channel > model.channelCount()) {
            r.valid = false;
            r.warning = "Invalid channel " + std::to_string(action.channel);
            return r;
        }

        float current = model.channel(action.channel).fader;
        float target  = action.value;

        // Clamp to valid range
        target = std::clamp(target, 0.0f, 1.0f);

        // Limit step size
        float delta = target - current;
        if (std::abs(delta) > limits_.maxFaderDeltaNorm) {
            float sign = delta > 0 ? 1.0f : -1.0f;
            target = current + sign * limits_.maxFaderDeltaNorm;
            r.warning = "Fader clamped: requested " +
                        std::to_string(action.value) +
                        " -> clamped to " + std::to_string(target);
            spdlog::warn("Validator: {}", r.warning);
        }

        r.clamped.value = target;
        return r;
    }

    ValidationResult validateEq(const MixAction& action) const {
        ValidationResult r{true, action, ""};

        // Clamp gain
        float gain = action.value2;
        if (gain > limits_.maxEqBoostDB) {
            gain = limits_.maxEqBoostDB;
            r.warning = "EQ boost clamped to " +
                        std::to_string(limits_.maxEqBoostDB) + "dB";
            spdlog::warn("Validator: {}", r.warning);
        }
        if (gain < limits_.maxEqCutDB) {
            gain = limits_.maxEqCutDB;
            r.warning = "EQ cut clamped to " +
                        std::to_string(limits_.maxEqCutDB) + "dB";
        }

        // Clamp Q
        float q = std::clamp(action.value3, 0.1f, 20.0f);

        // Clamp frequency
        float freq = std::clamp(action.value, 20.0f, 20000.0f);

        r.clamped.value  = freq;
        r.clamped.value2 = gain;
        r.clamped.value3 = q;
        return r;
    }

    ValidationResult validateComp(const MixAction& action) const {
        ValidationResult r{true, action, ""};

        float thresh = std::clamp(action.value,
                                   limits_.maxCompThresholdDB, 0.0f);
        float ratio  = std::clamp(action.value2,
                                   limits_.minCompRatio,
                                   limits_.maxCompRatio);

        r.clamped.value  = thresh;
        r.clamped.value2 = ratio;
        return r;
    }

    ValidationResult validateHpf(const MixAction& action) const {
        ValidationResult r{true, action, ""};

        float freq = std::clamp(action.value,
                                 limits_.minHpfHz, limits_.maxHpfHz);
        if (freq != action.value) {
            r.warning = "HPF clamped: " + std::to_string(int(action.value)) +
                        "Hz -> " + std::to_string(int(freq)) + "Hz";
            spdlog::warn("Validator: {}", r.warning);
        }

        r.clamped.value = freq;
        return r;
    }

    ValidationResult validateSend(const MixAction& action,
                                   const ConsoleModel& model) const {
        ValidationResult r{true, action, ""};

        if (action.channel < 1 || action.channel > model.channelCount()) {
            r.valid = false;
            r.warning = "Invalid channel";
            return r;
        }

        float target = std::clamp(action.value, 0.0f, 1.0f);
        r.clamped.value = target;
        return r;
    }

    SafetyLimits limits_;
};
