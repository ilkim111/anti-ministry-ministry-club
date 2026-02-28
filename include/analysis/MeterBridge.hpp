#pragma once
#include "console/ConsoleModel.hpp"
#include "discovery/DynamicChannelMap.hpp"
#include "AudioAnalyser.hpp"
#include <nlohmann/json.hpp>
#include <vector>
#include <string>

// Builds JSON snapshots of the current mix state for LLM consumption.
// This is the bridge between raw meter/spectral data and structured LLM context.
//
// Key design principle: the LLM never sees raw spectral data.
// The AudioAnalyser does the heavy DSP work locally, and this class
// only includes concise, actionable summaries in the JSON.
class MeterBridge {
public:
    MeterBridge(const ConsoleModel& model, const DynamicChannelMap& channelMap)
        : model_(model), channelMap_(channelMap) {}

    // Build full mix state JSON for LLM decision engine.
    // If issues are provided (from AudioAnalyser::detectIssues), they are
    // included as a compact "issues" array — this is the smart summary.
    nlohmann::json buildMixState(
        const std::vector<AudioAnalyser::MixIssue>& issues = {}) const
    {
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
                {"fader",       roundTo(snap.fader, 2)},
                {"muted",       snap.muted},
                {"pan",         roundTo(snap.pan, 2)},
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

        // Smart issue summary — concise actionable items from DSP analysis.
        // This is the key token-efficient approach: the FFT runs locally,
        // and only the conclusions reach the LLM.
        if (!issues.empty()) {
            state["issues"] = nlohmann::json::array();
            for (auto& issue : issues) {
                nlohmann::json ij;
                ij["type"] = issueTypeToString(issue.type);
                ij["channel"] = issue.channel;
                if (issue.channel2 > 0)
                    ij["channel2"] = issue.channel2;
                if (issue.freqHz > 0)
                    ij["freq_hz"] = (int)issue.freqHz;
                ij["severity"] = roundTo(issue.severity, 2);
                ij["description"] = issue.description;
                state["issues"].push_back(ij);
            }
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
    static std::string issueTypeToString(AudioAnalyser::MixIssue::Type t) {
        switch (t) {
            case AudioAnalyser::MixIssue::Type::Clipping:     return "clipping";
            case AudioAnalyser::MixIssue::Type::FeedbackRisk: return "feedback_risk";
            case AudioAnalyser::MixIssue::Type::Masking:      return "masking";
            case AudioAnalyser::MixIssue::Type::Boomy:        return "boomy";
            case AudioAnalyser::MixIssue::Type::Harsh:        return "harsh";
            case AudioAnalyser::MixIssue::Type::Thin:         return "thin";
            case AudioAnalyser::MixIssue::Type::Muddy:        return "muddy";
            case AudioAnalyser::MixIssue::Type::NoHeadroom:   return "no_headroom";
        }
        return "unknown";
    }

    static float roundTo(float val, int decimals) {
        float mult = std::pow(10.0f, decimals);
        return std::round(val * mult) / mult;
    }

    const ConsoleModel&      model_;
    const DynamicChannelMap& channelMap_;
};
