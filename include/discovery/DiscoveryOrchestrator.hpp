#pragma once
#include "ChannelProfile.hpp"
#include "DynamicChannelMap.hpp"
#include "ConsoleDiscovery.hpp"
#include "NameClassifier.hpp"
#include "SpectralClassifier.hpp"
#include "StereoPairDetector.hpp"
#include "LLMDiscoveryReview.hpp"
#include "console/IConsoleAdapter.hpp"
#include "console/ConsoleModel.hpp"
#include "llm/LLMDecisionEngine.hpp"
#include <spdlog/spdlog.h>
#include <thread>
#include <chrono>
#include <optional>
#include <functional>

class DiscoveryOrchestrator {
    IConsoleAdapter&   adapter_;
    ConsoleModel&      model_;
    DynamicChannelMap& channelMap_;
    NameClassifier     nameClassifier_;
    SpectralClassifier spectralClassifier_;
    StereoPairDetector pairDetector_;
    LLMDecisionEngine& llm_;

public:
    // Callback to ask the engineer to clarify an unidentified channel.
    // The orchestrator provides a human-readable question; the UI should
    // display it via the chat panel. When the engineer responds, call
    // handleClarification() with the channel and their answer.
    std::function<void(int channel, const std::string& question)>
        onClarificationNeeded;

    DiscoveryOrchestrator(IConsoleAdapter& a, ConsoleModel& m,
                          DynamicChannelMap& cm, LLMDecisionEngine& llm)
        : adapter_(a), model_(m), channelMap_(cm), llm_(llm) {}

    // Called when the engineer responds to a clarification request.
    void handleClarification(int channel, const std::string& answer) {
        auto result = nameClassifier_.classify(answer);
        auto profile = channelMap_.getProfile(channel);
        if (result.role != InstrumentRole::Unknown) {
            profile.consoleName = answer;
            profile.role        = result.role;
            profile.group       = result.group;
            profile.confidence  = DiscoveryConfidence::High;
            profile.manuallyOverridden = true;
            profile.lastUpdated = std::chrono::steady_clock::now();
            channelMap_.updateProfile(profile);
            spdlog::info("ch{} clarified by engineer: {} ({})",
                         channel, answer, roleToString(result.role));
        } else {
            // If classification still fails, set the name and mark as manual
            profile.consoleName = answer;
            profile.manuallyOverridden = true;
            profile.lastUpdated = std::chrono::steady_clock::now();
            channelMap_.updateProfile(profile);
            spdlog::info("ch{} labeled by engineer: '{}'", channel, answer);
        }
    }

    void run() {
        auto caps = adapter_.capabilities();
        spdlog::info("=== Starting Channel Discovery ===");
        spdlog::info("Console: {} ({} channels, {} buses)",
                     caps.model, caps.channelCount, caps.busCount);

        // 1. Full state sync
        ConsoleDiscovery discovery(adapter_, model_);
        bool syncOk = discovery.performFullSync(30000);
        if (!syncOk)
            spdlog::warn("Partial sync — some channels may be missing data");

        // 2. Wait briefly for audio to settle
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // 3. Capture spectral fingerprints
        auto fingerprints = captureFingerprints(caps.channelCount);

        // 4. Build initial profiles (no classification yet)
        std::vector<ChannelProfile> profiles;
        for (int ch = 1; ch <= caps.channelCount; ch++) {
            auto snapshot = model_.channel(ch);
            ChannelProfile profile;
            profile.index        = ch;
            profile.consoleName  = snapshot.name;
            profile.faderNorm    = snapshot.fader;
            profile.muted        = snapshot.muted;
            profile.phantomPower = snapshot.phantom;
            profile.phaseInvert  = snapshot.phase;
            profile.highPassHz   = snapshot.hpfFreq;
            profile.fingerprint  = fingerprints[ch - 1];
            profile.discoveredAt = std::chrono::steady_clock::now();
            profiles.push_back(profile);
        }

        // 5. LLM-primary classification — send all channel names to LLM
        bool llmClassified = false;
        spdlog::info("Starting LLM channel classification...");
        try {
            LLMDiscoveryReview review(llm_);
            profiles = review.review(std::move(profiles));
            llmClassified = true;
            spdlog::info("LLM channel classification complete");
        } catch (const std::exception& e) {
            spdlog::warn("LLM classification failed: {} — falling back to regex",
                         e.what());
        }

        // 6. Regex fallback for any channels the LLM didn't classify
        for (auto& profile : profiles) {
            if (profile.role == InstrumentRole::Unknown &&
                !profile.consoleName.empty()) {
                auto nameResult = nameClassifier_.classify(profile.consoleName);
                profile.role       = nameResult.role;
                profile.group      = nameResult.group;
                profile.confidence = nameResult.confidence;
            }

            // 7. Spectral override if still unknown
            if (profile.confidence <= DiscoveryConfidence::Low &&
                profile.fingerprint.hasSignal) {
                auto spectralResult =
                    spectralClassifier_.classify(
                        fingerprints[profile.index - 1]);
                if (spectralResult.matchScore > 0.6f) {
                    profile.role       = spectralResult.role;
                    profile.group      = spectralResult.group;
                    profile.confidence = DiscoveryConfidence::Medium;
                    spdlog::debug("ch{} '{}': spectral -> {} ({:.0f}%)",
                                  profile.index, profile.consoleName,
                                  roleToString(spectralResult.role),
                                  spectralResult.matchScore * 100);
                }
            }
        }

        // 8. Stereo pair detection
        auto pairs = pairDetector_.detect(profiles);
        for (auto& pair : pairs) {
            profiles[pair.left  - 1].stereoPair = pair.right;
            profiles[pair.right - 1].stereoPair = pair.left;
            spdlog::info("Detected stereo pair: ch{} / ch{} ({:.0f}%)",
                         pair.left, pair.right, pair.confidence * 100);
        }

        // 9. Apply classifications
        for (auto& p : profiles)
            channelMap_.updateProfile(p);

        spdlog::info("=== Discovery Complete ===");
        logChannelMap();

        // 10. Ask engineer about unidentified channels with signal
        if (onClarificationNeeded) {
            for (auto& p : profiles) {
                if (p.fingerprint.hasSignal &&
                    p.role == InstrumentRole::Unknown &&
                    p.confidence <= DiscoveryConfidence::Low) {
                    std::string question =
                        "ch" + std::to_string(p.index);
                    if (!p.consoleName.empty())
                        question += " ('" + p.consoleName + "')";
                    question += " has signal but I can't identify it. "
                                "What instrument is on this channel?";
                    onClarificationNeeded(p.index, question);
                }
            }
        }
    }

private:
    std::vector<ChannelProfile::Fingerprint>
    captureFingerprints(int count) {
        std::vector<ChannelProfile::Fingerprint> fingerprints(count);
        for (int ch = 1; ch <= count; ch++) {
            auto snap = model_.channel(ch);
            auto& fp  = fingerprints[ch - 1];

            fp.averageRMS       = snap.rmsDB;
            fp.hasSignal        = (snap.rmsDB > -60.0f);
            fp.bassEnergy       = snap.spectral.bass;
            fp.midEnergy        = snap.spectral.mid;
            fp.presenceEnergy   = snap.spectral.presence;
            fp.highEnergy       = snap.spectral.presence;
            fp.crestFactor      = snap.spectral.crestFactor;
            fp.isPercussive     = snap.spectral.crestFactor > 10.0f;
            fp.dominantFreqHz   = snap.spectral.spectralCentroid;
            fp.spectralCentroid = snap.spectral.spectralCentroid;
        }
        return fingerprints;
    }

    void logChannelMap() {
        spdlog::info("Channel Map:");
        for (auto& p : channelMap_.all()) {
            if (p.consoleName.empty() && !p.fingerprint.hasSignal)
                continue;
            std::string pairStr = p.stereoPair
                ? " -> pair ch" + std::to_string(*p.stereoPair) : "";
            spdlog::info("  ch{:02d}  {:<12}  {:<20}  {}{}",
                p.index,
                p.consoleName.empty() ? "(unnamed)" : p.consoleName,
                roleToString(p.role),
                confidenceToString(p.confidence),
                pairStr);
        }
    }
};
