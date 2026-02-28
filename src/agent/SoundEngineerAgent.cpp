#include "agent/SoundEngineerAgent.hpp"
#include <spdlog/spdlog.h>

SoundEngineerAgent::SoundEngineerAgent(
    IConsoleAdapter& adapter,
    const LLMConfig& llmConfig,
    const AgentConfig& agentConfig)
    : adapter_(adapter)
    , channelMap_(0)
    , llm_(llmConfig)
    , memory_(200)
    , executor_(adapter, model_)
    , approvalQueue_(agentConfig.approvalMode)
    , approvalUI_(approvalQueue_)
    , config_(agentConfig)
{
}

SoundEngineerAgent::~SoundEngineerAgent() {
    stop();
}

bool SoundEngineerAgent::start() {
    auto caps = adapter_.capabilities();

    // Initialize model and channel map
    model_.init(caps.channelCount, caps.busCount);
    channelMap_.resize(caps.channelCount);

    spdlog::info("Agent starting — {} ({} ch, {} bus)",
                 caps.model, caps.channelCount, caps.busCount);

    // Wire up adapter callbacks
    adapter_.onParameterUpdate = [this](const ParameterUpdate& u) {
        model_.applyUpdate(u);
        onParameterUpdate(u);
    };

    adapter_.onMeterUpdate = [this](int ch, float rms, float peak) {
        model_.updateMeter(ch, rms, peak);
    };

    adapter_.onConnectionChange = [this](bool connected) {
        if (!connected) {
            spdlog::error("Console disconnected!");
            approvalUI_.setStatus("DISCONNECTED");
        } else {
            approvalUI_.setStatus("Connected");
        }
    };

    // Subscribe to meters
    adapter_.subscribeMeter(config_.meterRefreshMs);

    // Run channel discovery
    spdlog::info("Running channel discovery...");
    DiscoveryOrchestrator discovery(adapter_, model_, channelMap_, llm_);
    discovery.run();

    // Start all threads
    running_ = true;

    dspThread_ = std::thread(&SoundEngineerAgent::dspLoop, this);
    llmThread_ = std::thread(&SoundEngineerAgent::llmLoop, this);
    execThread_ = std::thread(&SoundEngineerAgent::executionLoop, this);

    if (!config_.headless) {
        uiThread_ = std::thread(&SoundEngineerAgent::uiLoop, this);
    }

    spdlog::info("Agent running — DSP@{}ms LLM@{}ms",
                 config_.dspIntervalMs, config_.llmIntervalMs);
    approvalUI_.setStatus("Running");

    return true;
}

void SoundEngineerAgent::stop() {
    if (!running_) return;
    running_ = false;

    spdlog::info("Agent stopping...");
    approvalUI_.stop();

    adapter_.unsubscribeMeter();

    if (dspThread_.joinable())  dspThread_.join();
    if (llmThread_.joinable())  llmThread_.join();
    if (execThread_.joinable()) execThread_.join();
    if (uiThread_.joinable())   uiThread_.join();

    spdlog::info("Agent stopped");
}

// ── DSP Thread (50ms) ────────────────────────────────────────────────────

void SoundEngineerAgent::dspLoop() {
    spdlog::debug("DSP thread started");
    auto lastSnapshot = std::chrono::steady_clock::now();

    while (running_) {
        auto start = std::chrono::steady_clock::now();

        // Keep adapter alive
        adapter_.tick();

        // Run audio analysis
        auto caps = adapter_.capabilities();
        auto analysis = analyser_.analyse(model_, caps.channelCount);

        // Handle immediate issues (bypass LLM for speed)
        if (analysis.hasClipping) {
            MixAction fix;
            fix.type    = ActionType::SetFader;
            fix.channel = analysis.clippingChannel;
            fix.urgency = MixAction::Urgency::Immediate;
            fix.reason  = "Clipping detected — reducing level";

            auto snap = model_.channel(analysis.clippingChannel);
            fix.value = snap.fader * 0.9f;  // -1dB roughly

            if (approvalQueue_.submit(fix)) {
                auto vr = validator_.validate(fix, model_);
                if (vr.valid)
                    executor_.execute(vr.clamped);
            }
        }

        if (analysis.hasFeedbackRisk) {
            for (auto& warning : analysis.warnings) {
                approvalUI_.addLog("!! " + warning);
            }
        }

        // Periodic mix state snapshot for session memory
        auto now = std::chrono::steady_clock::now();
        auto sinceLast = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lastSnapshot).count();
        if (sinceLast > config_.snapshotIntervalMs) {
            MeterBridge bridge(model_, channelMap_);
            memory_.recordSnapshot(bridge.buildCompactState());
            lastSnapshot = now;
        }

        // Sleep for remainder of interval
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        int sleepMs = config_.dspIntervalMs - (int)elapsed;
        if (sleepMs > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
    spdlog::debug("DSP thread stopped");
}

// ── LLM Thread (5s) ─────────────────────────────────────────────────────

void SoundEngineerAgent::llmLoop() {
    spdlog::debug("LLM thread started");

    // Wait for initial discovery to complete
    std::this_thread::sleep_for(std::chrono::seconds(2));

    while (running_) {
        auto start = std::chrono::steady_clock::now();

        try {
            // Build context
            auto mixContext = buildMixContext();
            auto sessionContext = memory_.buildContext(20);

            // Ask LLM for decisions
            auto actions = llm_.decideMixActions(mixContext, sessionContext);

            spdlog::debug("LLM returned {} actions", actions.size());

            for (auto& action : actions) {
                if (action.type == ActionType::NoAction) {
                    spdlog::debug("LLM: no action needed — {}",
                                  action.reason);
                    continue;
                }

                if (action.type == ActionType::Observation) {
                    memory_.recordObservation(action.reason);
                    approvalUI_.addLog("LLM: " + action.reason);
                    continue;
                }

                // Submit to approval queue
                bool autoApproved = approvalQueue_.submit(action);

                if (autoApproved) {
                    // Validate and execute immediately
                    auto vr = validator_.validate(action, model_);
                    if (vr.valid) {
                        auto er = executor_.execute(vr.clamped);
                        if (er.success) {
                            memory_.recordAction(vr.clamped, mixContext);
                            approvalUI_.addLog("Auto: " +
                                               vr.clamped.describe());
                        }
                    } else {
                        spdlog::warn("Validation failed: {}", vr.warning);
                    }
                } else {
                    approvalUI_.addLog("Queued: " + action.describe());
                }
            }

        } catch (const std::exception& e) {
            spdlog::error("LLM loop error: {}", e.what());
        }

        // Sleep for remainder of interval
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        int sleepMs = config_.llmIntervalMs - (int)elapsed;
        if (sleepMs > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
    spdlog::debug("LLM thread stopped");
}

// ── Execution Thread ─────────────────────────────────────────────────────

void SoundEngineerAgent::executionLoop() {
    spdlog::debug("Execution thread started");

    while (running_) {
        MixAction action;
        if (approvalQueue_.popApproved(action, 200)) {
            auto vr = validator_.validate(action, model_);
            if (vr.valid) {
                auto er = executor_.execute(vr.clamped);
                if (er.success) {
                    MeterBridge bridge(model_, channelMap_);
                    memory_.recordAction(vr.clamped,
                                         bridge.buildCompactState());
                    approvalUI_.addLog("Approved: " + vr.clamped.describe());
                } else {
                    spdlog::warn("Execution failed: {}", er.error);
                    approvalUI_.addLog("Failed: " + er.error);
                }
            } else {
                spdlog::warn("Validation failed for approved action: {}",
                             vr.warning);
                memory_.recordRejection(action, vr.warning);
            }
        }
    }
    spdlog::debug("Execution thread stopped");
}

// ── UI Thread ────────────────────────────────────────────────────────────

void SoundEngineerAgent::uiLoop() {
    spdlog::debug("UI thread started");
    approvalUI_.run();
    // If UI exits, stop the agent
    if (running_) {
        spdlog::info("UI exited — stopping agent");
        running_ = false;
    }
}

// ── Live reclassification ────────────────────────────────────────────────

void SoundEngineerAgent::onParameterUpdate(const ParameterUpdate& u) {
    if (u.param == ChannelParam::Name &&
        u.target == ParameterUpdate::Target::Channel) {
        auto profile = channelMap_.getProfile(u.index);

        if (!profile.manuallyOverridden) {
            auto result = nameClassifier_.classify(u.strValue);
            profile.consoleName = u.strValue;
            profile.role        = result.role;
            profile.group       = result.group;
            profile.confidence  = result.confidence;
            profile.lastUpdated = std::chrono::steady_clock::now();
            channelMap_.updateProfile(profile);

            spdlog::info("ch{} renamed to '{}' — reclassified as {}",
                         u.index, u.strValue, roleToString(result.role));
            approvalUI_.addLog("Reclassified ch" + std::to_string(u.index) +
                               " -> " + roleToString(result.role));
        }
    }

    // Detect engineer overrides (fader moves not initiated by us)
    if (u.param == ChannelParam::Fader &&
        u.target == ParameterUpdate::Target::Channel) {
        // TODO: track which fader moves we initiated vs engineer
        // For now, just log significant moves
    }
}

// ── LLM Context Builder ─────────────────────────────────────────────────

nlohmann::json SoundEngineerAgent::buildMixContext() {
    MeterBridge bridge(model_, channelMap_);
    return bridge.buildMixState();
}
