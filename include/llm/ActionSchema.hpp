#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// Typed action schema — every LLM decision maps to one of these
enum class ActionType {
    SetFader,
    SetPan,
    SetEqBand,
    SetCompressor,
    SetGate,
    SetHighPass,
    SetSendLevel,
    MuteChannel,
    UnmuteChannel,
    NoAction,       // LLM decided no change needed
    Observation     // LLM notes something but takes no action
};

struct MixAction {
    ActionType  type;
    int         channel;      // 1-based
    int         auxIndex = 0; // for sends

    // Values depend on type
    float       value = 0;    // fader position, gain, freq, etc.
    float       value2 = 0;   // secondary (e.g. Q for EQ)
    float       value3 = 0;   // tertiary
    int         bandIndex = 0; // EQ band number

    // Urgency: how quickly this should be applied
    enum class Urgency {
        Immediate,    // feedback, clipping — apply NOW
        Fast,         // audible issue — apply within 1 tick
        Normal,       // optimization — can wait for approval
        Low           // suggestion — apply when convenient
    } urgency = Urgency::Normal;

    // Safety: maximum allowed change magnitude
    float       maxDelta = 0;    // 0 = use global default

    // Human-readable explanation
    std::string reason;
    std::string roleName;   // "LeadVocal", "Kick", etc.

    // For approval UI
    std::string describe() const {
        switch (type) {
            case ActionType::SetFader:
                return "Set ch" + std::to_string(channel) +
                       " (" + roleName + ") fader to " +
                       std::to_string(int(value * 100)) + "%";
            case ActionType::SetPan:
                return "Set ch" + std::to_string(channel) +
                       " pan to " + std::to_string(int(value * 100));
            case ActionType::SetEqBand:
                return "Set ch" + std::to_string(channel) +
                       " EQ band " + std::to_string(bandIndex) +
                       ": " + std::to_string(int(value)) + "Hz @ " +
                       std::to_string(value2) + "dB Q=" +
                       std::to_string(value3);
            case ActionType::SetCompressor:
                return "Set ch" + std::to_string(channel) +
                       " comp threshold=" + std::to_string(int(value)) +
                       "dB ratio=" + std::to_string(value2) + ":1";
            case ActionType::SetGate:
                return "Set ch" + std::to_string(channel) +
                       " gate threshold=" + std::to_string(int(value)) + "dB";
            case ActionType::SetHighPass:
                return "Set ch" + std::to_string(channel) +
                       " HPF to " + std::to_string(int(value)) + "Hz";
            case ActionType::SetSendLevel:
                return "Set ch" + std::to_string(channel) +
                       " send to bus " + std::to_string(auxIndex) +
                       " level=" + std::to_string(int(value * 100)) + "%";
            case ActionType::MuteChannel:
                return "Mute ch" + std::to_string(channel) +
                       " (" + roleName + ")";
            case ActionType::UnmuteChannel:
                return "Unmute ch" + std::to_string(channel) +
                       " (" + roleName + ")";
            case ActionType::NoAction:
                return "No action needed: " + reason;
            case ActionType::Observation:
                return "Note: " + reason;
        }
        return "Unknown action";
    }

    // Serialize to JSON for logging/display
    nlohmann::json toJson() const {
        return {
            {"type",      static_cast<int>(type)},
            {"channel",   channel},
            {"value",     value},
            {"value2",    value2},
            {"value3",    value3},
            {"band",      bandIndex},
            {"urgency",   static_cast<int>(urgency)},
            {"reason",    reason},
            {"role",      roleName},
            {"description", describe()}
        };
    }

    // Parse from LLM JSON response
    static MixAction fromJson(const nlohmann::json& j) {
        MixAction a;
        std::string typeStr = j.value("action", "no_action");

        if (typeStr == "set_fader")       a.type = ActionType::SetFader;
        else if (typeStr == "set_pan")    a.type = ActionType::SetPan;
        else if (typeStr == "set_eq")     a.type = ActionType::SetEqBand;
        else if (typeStr == "set_comp")   a.type = ActionType::SetCompressor;
        else if (typeStr == "set_gate")   a.type = ActionType::SetGate;
        else if (typeStr == "set_hpf")    a.type = ActionType::SetHighPass;
        else if (typeStr == "set_send")   a.type = ActionType::SetSendLevel;
        else if (typeStr == "mute")       a.type = ActionType::MuteChannel;
        else if (typeStr == "unmute")     a.type = ActionType::UnmuteChannel;
        else if (typeStr == "observation")a.type = ActionType::Observation;
        else                              a.type = ActionType::NoAction;

        a.channel   = j.value("channel", 0);
        a.value     = j.value("value", 0.0f);
        a.value2    = j.value("value2", 0.0f);
        a.value3    = j.value("value3", 1.0f);
        a.bandIndex = j.value("band", 1);
        a.auxIndex  = j.value("aux", 0);
        a.reason    = j.value("reason", "");
        a.roleName  = j.value("role", "");

        std::string urg = j.value("urgency", "normal");
        if (urg == "immediate")     a.urgency = MixAction::Urgency::Immediate;
        else if (urg == "fast")     a.urgency = MixAction::Urgency::Fast;
        else if (urg == "low")      a.urgency = MixAction::Urgency::Low;
        else                        a.urgency = MixAction::Urgency::Normal;

        return a;
    }
};
