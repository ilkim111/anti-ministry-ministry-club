#pragma once
#include "console/ConsoleModel.hpp"
#include "discovery/DynamicChannelMap.hpp"
#include <nlohmann/json.hpp>
#include <vector>
#include <string>

// Builds JSON snapshots of the current mix state for LLM consumption.
// This is the bridge between raw meter data and structured LLM context.
class MeterBridge {
public:
    MeterBridge(const ConsoleModel& model, const DynamicChannelMap& channelMap)
        : model_(model), channelMap_(channelMap) {}

    // Build full mix state JSON for LLM decision engine
    nlohmann::json buildMixState() const {
        nlohmann::json state;
        state["channels"] = nlohmann::json::array();

        for (auto& profile : channelMap_.all()) {
            if (!profile.fingerprint.hasSignal && profile.consoleName.empty())
                continue;

            auto snap = model_.channel(profile.index);

            nlohmann::json ch = {
                {"index",       profile.index},
                {"name",        profile.consoleName},
                {"role",        roleToString(profile.role)},
                {"group",       profile.group},
                {"fader",       snap.fader},
                {"muted",       snap.muted},
                {"pan",         snap.pan},
                {"rms_db",      roundTo(snap.rmsDB, 1)},
                {"peak_db",     roundTo(snap.peakDB, 1)},
                {"has_signal",  snap.rmsDB > -60.0f}
            };

            // Include stereo pair info
            if (profile.stereoPair)
                ch["stereo_pair"] = *profile.stereoPair;

            // EQ state (only if active)
            if (snap.eqOn) {
                nlohmann::json eq = nlohmann::json::array();
                for (int b = 0; b < 4; b++) {
                    if (std::abs(snap.eq[b].gain) > 0.1f) {
                        eq.push_back({
                            {"band",  b + 1},
                            {"freq",  snap.eq[b].freq},
                            {"gain",  roundTo(snap.eq[b].gain, 1)},
                            {"q",     roundTo(snap.eq[b].q, 2)}
                        });
                    }
                }
                if (!eq.empty())
                    ch["eq"] = eq;
            }

            // HPF
            if (snap.hpfOn && snap.hpfFreq > 20.0f)
                ch["hpf_hz"] = roundTo(snap.hpfFreq, 0);

            // Compressor (only if active)
            if (snap.comp.on) {
                ch["comp"] = {
                    {"threshold", roundTo(snap.comp.threshold, 1)},
                    {"ratio",     roundTo(snap.comp.ratio, 1)},
                    {"attack",    roundTo(snap.comp.attack, 1)},
                    {"release",   roundTo(snap.comp.release, 0)}
                };
            }

            // Gate (only if active)
            if (snap.gate.on) {
                ch["gate"] = {
                    {"threshold", roundTo(snap.gate.threshold, 1)},
                    {"range",     roundTo(snap.gate.range, 1)}
                };
            }

            state["channels"].push_back(ch);
        }

        return state;
    }

    // Compact summary for frequent LLM calls (smaller token count)
    nlohmann::json buildCompactState() const {
        nlohmann::json state;
        state["ch"] = nlohmann::json::array();

        for (auto& profile : channelMap_.active()) {
            auto snap = model_.channel(profile.index);
            state["ch"].push_back({
                {"i",  profile.index},
                {"r",  roleToString(profile.role)},
                {"f",  roundTo(snap.fader, 2)},
                {"db", roundTo(snap.rmsDB, 0)},
                {"pk", roundTo(snap.peakDB, 0)}
            });
        }

        return state;
    }

private:
    static float roundTo(float val, int decimals) {
        float mult = std::pow(10.0f, decimals);
        return std::round(val * mult) / mult;
    }

    const ConsoleModel&      model_;
    const DynamicChannelMap& channelMap_;
};
