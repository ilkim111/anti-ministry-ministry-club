#pragma once
#include "ChannelProfile.hpp"
#include "llm/LLMDecisionEngine.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <vector>
#include <string>
#include <unordered_map>

class LLMDiscoveryReview {
    LLMDecisionEngine& llm_;

public:
    explicit LLMDiscoveryReview(LLMDecisionEngine& l) : llm_(l) {}

    // Returns corrected profiles for any channels the LLM disagrees with
    std::vector<ChannelProfile> review(std::vector<ChannelProfile> profiles) {
        auto prompt = buildDiscoveryPrompt(profiles);
        auto response = llm_.callRaw(discoverySystemPrompt(),
                                      prompt.dump());
        return parseReviewResponse(response, std::move(profiles));
    }

private:
    std::string discoverySystemPrompt() {
        return R"(You are an experienced live sound engineer reviewing a channel map
that was automatically detected from a mixing console.

Your job is to:
1. Identify any channels that are probably misclassified
2. Spot likely stereo pairs that weren't detected
3. Identify the overall band/show type from the channel layout
4. Flag any channels with suspicious settings (e.g. phantom on a dynamic mic)

Respond ONLY with valid JSON:
{
  "show_type": "rock_band|jazz_quartet|musical_theatre|conference|dj_set|...",
  "show_confidence": 0.85,
  "observations": "brief overall assessment",
  "corrections": [
    {
      "channel": 5,
      "current_role": "Unknown",
      "suggested_role": "ElectricGuitar",
      "reason": "named 'GTR1', spectral profile matches guitar",
      "confidence": 0.9
    }
  ],
  "stereo_pairs": [
    { "left": 15, "right": 16, "reason": "named GTR L/R, same role" }
  ],
  "concerns": [
    {
      "channel": 3,
      "issue": "phantom_48v_on_dynamic",
      "detail": "channel named 'Snare' has 48V phantom — likely a mistake"
    }
  ]
})";
    }

    nlohmann::json buildDiscoveryPrompt(
        const std::vector<ChannelProfile>& profiles)
    {
        nlohmann::json channels = nlohmann::json::array();
        for (auto& p : profiles) {
            if (!p.fingerprint.hasSignal && p.consoleName.empty())
                continue;

            channels.push_back({
                {"channel",       p.index},
                {"name",          p.consoleName},
                {"inferred_role", roleToString(p.role)},
                {"confidence",    confidenceToString(p.confidence)},
                {"has_signal",    p.fingerprint.hasSignal},
                {"fader_norm",    p.faderNorm},
                {"muted",         p.muted},
                {"phantom_48v",   p.phantomPower},
                {"phase_invert",  p.phaseInvert},
                {"hpf_hz",        p.highPassHz},
                {"spectral", {
                    {"dominant_hz",   p.fingerprint.dominantFreqHz},
                    {"bass_energy",   p.fingerprint.bassEnergy},
                    {"mid_energy",    p.fingerprint.midEnergy},
                    {"high_energy",   p.fingerprint.highEnergy},
                    {"crest_factor",  p.fingerprint.crestFactor},
                    {"is_percussive", p.fingerprint.isPercussive}
                }}
            });
        }
        return {{"channels", channels}};
    }

    std::vector<ChannelProfile> parseReviewResponse(
        const std::string& response,
        std::vector<ChannelProfile> profiles)
    {
        try {
            auto j = nlohmann::json::parse(response);

            spdlog::info("LLM identified show type: {} (confidence: {:.2f})",
                         j.value("show_type", "unknown"),
                         j.value("show_confidence", 0.0f));

            if (j.contains("observations"))
                spdlog::info("LLM observations: {}",
                             j["observations"].get<std::string>());

            // Apply corrections
            if (j.contains("corrections")) {
                for (auto& correction : j["corrections"]) {
                    int ch = correction["channel"].get<int>();
                    if (ch < 1 || ch > (int)profiles.size()) continue;

                    auto& profile = profiles[ch - 1];
                    if (!profile.manuallyOverridden) {
                        profile.role = roleFromString(
                            correction["suggested_role"].get<std::string>());
                        profile.confidence = DiscoveryConfidence::Medium;
                        profile.llmNotes   = correction.value("reason", "");
                        spdlog::info("LLM corrected ch{} ({}) -> {}",
                                     ch, profile.consoleName,
                                     correction["suggested_role"].get<std::string>());
                    }
                }
            }

            // Apply stereo pairs
            if (j.contains("stereo_pairs")) {
                for (auto& pair : j["stereo_pairs"]) {
                    int l = pair["left"].get<int>();
                    int r = pair["right"].get<int>();
                    if (l < 1 || l > (int)profiles.size()) continue;
                    if (r < 1 || r > (int)profiles.size()) continue;
                    profiles[l - 1].stereoPair = r;
                    profiles[r - 1].stereoPair = l;
                    spdlog::info("LLM detected stereo pair: ch{} / ch{}", l, r);
                }
            }

            // Log concerns
            if (j.contains("concerns")) {
                for (auto& concern : j["concerns"]) {
                    spdlog::warn("Discovery concern on ch{}: {}",
                                 concern["channel"].get<int>(),
                                 concern.value("detail", "unknown issue"));
                }
            }

        } catch (const std::exception& e) {
            spdlog::error("Failed to parse LLM discovery response: {}",
                          e.what());
        }

        return profiles;
    }
};
