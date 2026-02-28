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

class DiscoveryOrchestrator {
    IConsoleAdapter&   adapter_;
    ConsoleModel&      model_;
    DynamicChannelMap& channelMap_;
    NameClassifier     nameClassifier_;
    SpectralClassifier spectralClassifier_;
    StereoPairDetector pairDetector_;
    LLMDecisionEngine& llm_;

public:
    DiscoveryOrchestrator(IConsoleAdapter& a, ConsoleModel& m,
                          DynamicChannelMap& cm, LLMDecisionEngine& llm)
        : adapter_(a), model_(m), channelMap_(cm), llm_(llm) {}

    void run() {
        auto caps = adapter_.capabilities();
        spdlog::info("=== Starting Channel Discovery ===");
        spdlog::info("Console: {} ({} channels, {} buses)",
                     caps.model, caps.channelCount, caps.busCount);

        // 1. Full state sync
        ConsoleDiscovery discovery(adapter_, model_);
        bool syncOk = discovery.performFullSync(10000);
        if (!syncOk)
            spdlog::warn("Partial sync — some channels may be missing data");

        // 2. Wait briefly for audio to settle
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // 3. Capture spectral fingerprints
        auto fingerprints = captureFingerprints(caps.channelCount);

        // 4. Build initial profiles
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

            // 5. Name classification (fast, local)
            auto nameResult = nameClassifier_.classify(snapshot.name);
            profile.role       = nameResult.role;
            profile.group      = nameResult.group;
            profile.confidence = nameResult.confidence;

            // 6. Spectral override if name was generic/unknown
            if (profile.confidence <= DiscoveryConfidence::Low &&
                profile.fingerprint.hasSignal) {
                auto spectralResult =
                    spectralClassifier_.classify(fingerprints[ch - 1]);
                if (spectralResult.matchScore > 0.6f) {
                    profile.role       = spectralResult.role;
                    profile.group      = spectralResult.group;
                    profile.confidence = DiscoveryConfidence::Medium;
                    spdlog::debug("ch{} '{}': spectral -> {} ({:.0f}%)",
                                  ch, snapshot.name,
                                  roleToString(spectralResult.role),
                                  spectralResult.matchScore * 100);
                }
            }

            profiles.push_back(profile);
        }

        // 7. Stereo pair detection
        auto pairs = pairDetector_.detect(profiles);
        for (auto& pair : pairs) {
            profiles[pair.left  - 1].stereoPair = pair.right;
            profiles[pair.right - 1].stereoPair = pair.left;
            spdlog::info("Detected stereo pair: ch{} / ch{} ({:.0f}%)",
                         pair.left, pair.right, pair.confidence * 100);
        }

        // 8. Apply local classifications immediately
        for (auto& p : profiles)
            channelMap_.updateProfile(p);

        spdlog::info("=== Discovery Complete (local) ===");
        logChannelMap();

        // 9. LLM review pass (async — don't block the show)
        std::thread([this, profiles]() mutable {
            spdlog::info("Starting LLM discovery review...");
            try {
                LLMDiscoveryReview review(llm_);
                profiles = review.review(std::move(profiles));
                for (auto& p : profiles)
                    channelMap_.updateProfile(p);
                spdlog::info("LLM discovery review complete");
                logChannelMap();
            } catch (const std::exception& e) {
                spdlog::warn("LLM discovery review failed: {} "
                             "— proceeding with local classification",
                             e.what());
            }
        }).detach();
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
