#include "agent/SoundEngineerAgent.hpp"
#include "audio/NullAudioCapture.hpp"
#include <spdlog/spdlog.h>
#include <iostream>
#include <poll.h>
#include <unistd.h>

void SoundEngineerAgent::emitJson(const nlohmann::json& msg) {
    if (!config_.headless) return;
    try {
        std::lock_guard lock(emitMtx_);
        std::cout << msg.dump() << "\n" << std::flush;
    } catch (const std::exception& e) {
        spdlog::warn("emitJson failed: {}", e.what());
    }
}

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
    // Load genre preset if configured
    if (!config_.genre.empty()) {
        activePreset_ = genreLibrary_.get(config_.genre);
        if (activePreset_) {
            spdlog::info("Genre preset: {} — {}", activePreset_->name,
                         activePreset_->description);
        } else {
            // Try loading as a file path
            if (genreLibrary_.loadFromFile(config_.genre)) {
                activePreset_ = genreLibrary_.get("custom");
                spdlog::info("Loaded custom genre preset from {}", config_.genre);
            } else {
                spdlog::warn("Unknown genre preset: '{}'", config_.genre);
            }
        }
    }

    // Load learned preferences from previous sessions
    if (!config_.preferencesFile.empty()) {
        if (preferences_.loadFromFile(config_.preferencesFile)) {
            spdlog::info("Loaded {} preference decisions from {}",
                         preferences_.totalDecisions(),
                         config_.preferencesFile);
        }
    }
}

SoundEngineerAgent::~SoundEngineerAgent() {
    stop();
}

void SoundEngineerAgent::setAudioCapture(
    std::unique_ptr<IAudioCapture> capture) {
    audioCapture_ = std::move(capture);
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
        refreshConnectionStatus();
    };

    // Subscribe to meters
    adapter_.subscribeMeter(config_.meterRefreshMs);

    // Initialize audio capture if configured
    if (!audioCapture_) {
        audioCapture_ = std::make_unique<NullAudioCapture>();
    }

    if (config_.audioChannels > 0) {
        IAudioCapture::Config audioCfg;
        audioCfg.deviceId       = config_.audioDeviceId;
        audioCfg.deviceName     = config_.audioDeviceName;
        audioCfg.channelCount   = config_.audioChannels;
        audioCfg.sampleRate     = config_.audioSampleRate;
        audioCfg.framesPerBlock = config_.audioFFTSize;

        if (audioCapture_->open(audioCfg)) {
            if (audioCapture_->start()) {
                fftAnalyser_ = std::make_unique<FFTAnalyser>(config_.audioFFTSize);
                spdlog::info("Audio capture started: {} ({} ch, {}Hz, FFT={})",
                             audioCapture_->backendName(),
                             config_.audioChannels,
                             config_.audioSampleRate,
                             config_.audioFFTSize);
            } else {
                spdlog::warn("Audio capture failed to start — falling back to "
                             "console meters only");
            }
        } else {
            spdlog::warn("Audio device open failed — falling back to "
                         "console meters only");
        }
    } else {
        spdlog::info("Audio capture disabled — using console meters only");
    }

    // Wire rejection learning callback
    approvalQueue_.onRejected = [this](const MixAction& action) {
        preferences_.recordRejection(action, action.roleName);
    };

    // Run channel discovery
    spdlog::info("Running channel discovery...");
    DiscoveryOrchestrator discovery(adapter_, model_, channelMap_, llm_);

    // Wire channel clarification to chat panel
    discovery.onClarificationNeeded = [this](int ch, const std::string& q) {
        approvalUI_.addChatResponse(q);
    };

    discovery.run();

    // Start all threads
    running_ = true;

    dspThread_ = std::thread(&SoundEngineerAgent::dspLoop, this);
    llmThread_ = std::thread(&SoundEngineerAgent::llmLoop, this);
    execThread_ = std::thread(&SoundEngineerAgent::executionLoop, this);

    // Wire up chat callback
    approvalUI_.onChatMessage = [this](const std::string& msg) {
        onChatMessage(msg);
    };

    // Initial connection status
    refreshConnectionStatus();

    if (!config_.headless) {
        uiThread_ = std::thread(&SoundEngineerAgent::uiLoop, this);
    } else {
        stdinThread_ = std::thread(&SoundEngineerAgent::stdinLoop, this);
    }

    spdlog::info("Agent running — DSP@{}ms LLM@{}ms Audio:{}",
                 config_.dspIntervalMs, config_.llmIntervalMs,
                 audioCapture_->isRunning() ? "active" : "off");
    approvalUI_.setStatus("Running");

    return true;
}

void SoundEngineerAgent::stop() {
    if (!running_) return;
    running_ = false;

    spdlog::info("Agent stopping...");

    // Clear adapter callbacks before stopping to prevent use-after-free
    adapter_.onParameterUpdate = nullptr;
    adapter_.onMeterUpdate = nullptr;
    adapter_.onConnectionChange = nullptr;
    adapter_.unsubscribeMeter();
    adapter_.disconnect();

    approvalUI_.stop();

    if (audioCapture_ && audioCapture_->isRunning())
        audioCapture_->stop();

    if (dspThread_.joinable())  dspThread_.join();
    if (llmThread_.joinable())  llmThread_.join();
    if (execThread_.joinable()) execThread_.join();
    if (uiThread_.joinable())   uiThread_.join();
    if (stdinThread_.joinable()) stdinThread_.join();

    // Persist learned preferences for next session
    if (!config_.preferencesFile.empty() && preferences_.isDirty()) {
        if (preferences_.saveToFile(config_.preferencesFile)) {
            spdlog::info("Saved preferences to {}", config_.preferencesFile);
        } else {
            spdlog::warn("Failed to save preferences to {}",
                         config_.preferencesFile);
        }
    }

    spdlog::info("Agent stopped");
}

// ── Connection Status ─────────────────────────────────────────────────────

void SoundEngineerAgent::refreshConnectionStatus() {
    ApprovalUI::ConnectionStatus cs;

    // OSC/TCP connection status
    cs.oscConnected = adapter_.isConnected();
    auto caps = adapter_.capabilities();
    cs.consoleType = caps.model;

    // Audio capture status
    if (audioCapture_) {
        cs.audioConnected = audioCapture_->isRunning();
        cs.audioBackend   = audioCapture_->backendName();
        cs.audioChannels  = config_.audioChannels;
        cs.audioSampleRate = (float)config_.audioSampleRate;
    }

    // LLM status — we assume connected if we've been making calls
    cs.llmConnected = true;

    approvalUI_.updateConnectionStatus(cs);
}

// ── DSP Thread (50ms) ────────────────────────────────────────────────────

void SoundEngineerAgent::dspLoop() {
    spdlog::debug("DSP thread started");
    auto lastSnapshot = std::chrono::steady_clock::now();
    auto lastStatusRefresh = std::chrono::steady_clock::now();
    auto lastMeterEmit = std::chrono::steady_clock::now();

    while (running_) {
        auto start = std::chrono::steady_clock::now();

        // Keep adapter alive
        adapter_.tick();

        // If audio capture is active, drain ring buffers and run FFT
        if (audioCapture_ && audioCapture_->isRunning() && fftAnalyser_) {
            // Set up callback that runs FFT on each channel block
            audioCapture_->setCallback(
                [this](const float* const* channelData,
                       int channelCount, int frameCount) {
                    for (int ch = 0; ch < channelCount; ch++) {
                        auto result = fftAnalyser_->analyse(
                            channelData[ch], frameCount,
                            (float)config_.audioSampleRate);
                        // Feed FFT results into AudioAnalyser
                        analyser_.updateFFT(ch + 1, result);
                        // Also update ConsoleModel's spectral data
                        ChannelSnapshot::SpectralData sd;
                        sd.bass = result.bands.bass;
                        sd.mid  = result.bands.mid;
                        sd.presence = result.bands.presence;
                        sd.crestFactor = result.crestFactor;
                        sd.spectralCentroid = result.spectralCentroid;
                        model_.updateSpectral(ch + 1, sd);
                    }
                });

            // Trigger consumption of buffered audio
            // (PortAudioCapture drains ring buffers and fires callback)
            // For PortAudioCapture, we call consumeChannels directly.
            // For the generic interface, we rely on the callback being
            // set above and triggered by the capture implementation.
        }

        // Run audio analysis (uses FFT data if available, else meters)
        auto caps = adapter_.capabilities();
        auto analysis = analyser_.analyse(model_, caps.channelCount);

        // Detect issues (smart summary for LLM)
        auto issues = analyser_.detectIssues(analysis);
        {
            std::lock_guard lock(issuesMtx_);
            latestIssues_ = issues;
        }

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

        // Log detected issues
        for (auto& issue : issues) {
            if (issue.type == AudioAnalyser::MixIssue::Type::Boomy ||
                issue.type == AudioAnalyser::MixIssue::Type::Harsh ||
                issue.type == AudioAnalyser::MixIssue::Type::Thin ||
                issue.type == AudioAnalyser::MixIssue::Type::Masking) {
                approvalUI_.addLog("DSP: " + issue.description);
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

        // Refresh connection status periodically (every 5s)
        auto sinceStatus = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lastStatusRefresh).count();
        if (sinceStatus > 5000) {
            refreshConnectionStatus();
            lastStatusRefresh = now;
        }

        // Emit batched meter data to stdout for Electron (every 100ms)
        auto sinceMeterEmit = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lastMeterEmit).count();
        if (sinceMeterEmit >= 100) {
            auto caps = adapter_.capabilities();
            nlohmann::json consoleChannels = nlohmann::json::array();
            for (int ch = 1; ch <= caps.channelCount; ch++) {
                auto snap = model_.channel(ch);
                consoleChannels.push_back({
                    {"index", ch},
                    {"name", snap.name},
                    {"rms_db", std::round(snap.rmsDB * 10.0f) / 10.0f},
                    {"peak_db", std::round(snap.peakDB * 10.0f) / 10.0f}
                });
            }
            emitJson({{"type", "meter_update"},
                      {"console_channels", consoleChannels}});
            lastMeterEmit = now;
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
            // Build context with smart issue summary
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
                    emitJson({{"type", "llm_response"},
                              {"message", action.reason}});
                    continue;
                }

                // Generate action ID for tracking
                {
                    auto now = std::chrono::steady_clock::now().time_since_epoch();
                    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
                    action.id = "action-" + std::to_string(ms) + "-" + std::to_string(action.channel);
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
                    emitJson({{"type", "approval_request"},
                              {"id", action.id},
                              {"action", action.describe()},
                              {"description", action.describe()},
                              {"reason", action.reason},
                              {"urgency", static_cast<int>(action.urgency)}});
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
                    emitJson({{"type", "action_executed"},
                              {"action", vr.clamped.describe()}});

                    // Learn from approval
                    preferences_.recordApproval(vr.clamped,
                                                vr.clamped.roleName);
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

// ── Stdin Command Loop (headless mode) ──────────────────────────────────

void SoundEngineerAgent::stdinLoop() {
    spdlog::debug("Stdin command thread started");

    // Use poll() to avoid blocking forever on stdin
    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;

    std::string line;
    while (running_) {
        int ret = poll(&pfd, 1, 200);  // 200ms timeout
        if (ret <= 0) continue;        // timeout or error
        if (!std::getline(std::cin, line)) break;  // EOF
        if (line.empty()) continue;
        try {
            auto cmd = nlohmann::json::parse(line);
            std::string command = cmd.value("command", "");

            if (command == "approve") {
                std::string id = cmd.value("id", "");
                // Find pending action with matching ID and approve it
                auto pending = approvalQueue_.pending();
                for (size_t i = 0; i < pending.size(); i++) {
                    if (pending[i].action.id == id) {
                        approvalQueue_.approve(i);
                        spdlog::info("Approved action: {}", id);
                        emitJson({{"type", "approval_response"},
                                  {"id", id}, {"status", "approved"}});
                        break;
                    }
                }
            } else if (command == "reject") {
                std::string id = cmd.value("id", "");
                auto pending = approvalQueue_.pending();
                for (size_t i = 0; i < pending.size(); i++) {
                    if (pending[i].action.id == id) {
                        approvalQueue_.reject(i);
                        spdlog::info("Rejected action: {}", id);
                        emitJson({{"type", "approval_response"},
                                  {"id", id}, {"status", "rejected"}});
                        break;
                    }
                }
            } else if (command == "approve_all") {
                approvalQueue_.approveAll();
                spdlog::info("Approved all pending actions");
                emitJson({{"type", "approval_response"},
                          {"status", "all_approved"}});
            } else if (command == "reject_all") {
                approvalQueue_.rejectAll();
                spdlog::info("Rejected all pending actions");
                emitJson({{"type", "approval_response"},
                          {"status", "all_rejected"}});
            } else if (command == "set_approval_mode") {
                std::string mode = cmd.value("mode", "");
                if (mode == "approve_all")
                    approvalQueue_.setMode(ApprovalQueue::Mode::ApproveAll);
                else if (mode == "auto_all")
                    approvalQueue_.setMode(ApprovalQueue::Mode::AutoAll);
                else if (mode == "auto_urgent")
                    approvalQueue_.setMode(ApprovalQueue::Mode::AutoUrgent);
                else if (mode == "deny_all")
                    approvalQueue_.setMode(ApprovalQueue::Mode::DenyAll);
                spdlog::info("Approval mode set to: {}", mode);
                emitJson({{"type", "config_update"},
                          {"key", "approval_mode"}, {"value", mode}});
            } else if (command == "chat") {
                std::string message = cmd.value("message", "");
                if (!message.empty()) {
                    spdlog::info("Chat from UI: {}", message);
                    onChatMessage(message);
                }
            } else {
                spdlog::warn("Unknown stdin command: {}", command);
            }
        } catch (const std::exception& e) {
            spdlog::warn("Failed to parse stdin command: {}", e.what());
        }
    }
    spdlog::debug("Stdin command thread stopped");
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
    }
}

// ── Chat Handler ────────────────────────────────────────────────────────

void SoundEngineerAgent::onChatMessage(const std::string& message) {
    spdlog::info("Engineer chat: {}", message);

    // Record as standing instruction in session memory
    memory_.recordInstruction(message);

    // Dispatch an immediate LLM call on a detached thread so we don't
    // block the UI thread while waiting for the response.
    std::thread([this, message]() {
        try {
            auto mixContext = buildMixContext();

            nlohmann::json chatPrompt = {
                {"mix_state",       mixContext},
                {"recent_history",  memory_.buildContext(10)},
                {"engineer_says",   message}
            };

            std::string systemPrompt = R"(You are an expert live sound engineer AI assistant.
The engineer has sent you a message. Respond conversationally AND suggest
specific mix actions if appropriate.

If the message is a question about the current mix, answer it based on the
mix state provided.

If the message is an instruction (e.g. "bring up the vocals", "leave the
drums alone", "more reverb on the snare"), acknowledge it and produce actions.

Respond with JSON:
{
  "reply": "Your conversational response to the engineer",
  "actions": [
    {
      "action": "set_fader|set_eq|set_comp|set_hpf|set_send|mute|unmute|no_action|observation",
      "channel": 1, "role": "Kick", "value": -20,
      "value2": 0.0, "value3": 1.0, "band": 1, "aux": 0,
      "urgency": "normal", "reason": "explanation"
    }
  ]
}

For set_fader: use dB values (e.g. -20 for -20dB, 0 for unity, -10 for -10dB).
  The system auto-converts dB to the console's normalized float format.
  dB reference: -inf=silence, -20dB=quiet, -10dB=moderate, 0dB=unity, +10dB=max.
  Do NOT use normalized 0.0-1.0 float values — always use dB.
For set_eq: value=frequency_hz, value2=gain_db (MUST be negative — subtractive EQ only, no boosting), value3=q_factor, band=1-4
  IMPORTANT: Always cut, never boost. Use negative gain values only (e.g. -3, -6). If something needs more presence, cut competing frequencies on other channels instead.
For set_comp: value=threshold_db, value2=ratio
For set_hpf: value=frequency_hz)";

            auto response = llm_.callRaw(systemPrompt, chatPrompt.dump(), 4096);

            // Strip markdown code fences if present (LLMs often wrap JSON in ```json ... ```)
            {
                auto start = response.find("```");
                if (start != std::string::npos) {
                    // Find end of first line (skip ```json or ```JSON)
                    auto contentStart = response.find('\n', start);
                    if (contentStart != std::string::npos) contentStart++;
                    auto end = response.rfind("```");
                    if (end != std::string::npos && end > start && contentStart != std::string::npos) {
                        response = response.substr(contentStart, end - contentStart);
                        spdlog::debug("Stripped markdown fences from LLM response");
                    }
                }
            }

            try {
                auto j = nlohmann::json::parse(response);

                std::string reply = j.value("reply", "");
                if (!reply.empty()) {
                    approvalUI_.addChatResponse(reply);
                    emitJson({{"type", "llm_response"},
                              {"message", reply}});
                }

                if (j.contains("actions") && j["actions"].is_array()) {
                    spdlog::info("Chat produced {} actions",
                                 j["actions"].size());
                    for (auto& item : j["actions"]) {
                        auto action = MixAction::fromJson(item);
                        // Chat prompt tells LLM to use dB for faders
                        if (action.type == ActionType::SetFader)
                            action.valueIsDb = true;

                        if (action.type == ActionType::NoAction ||
                            action.type == ActionType::Observation) {
                            if (!action.reason.empty()) {
                                approvalUI_.addLog("LLM: " + action.reason);
                                emitJson({{"type", "llm_response"},
                                          {"message", action.reason}});
                            }
                            continue;
                        }

                        // Chat commands are explicit engineer requests —
                        // use relaxed limits (no delta clamping)
                        ActionValidator::SafetyLimits chatLimits;
                        chatLimits.maxFaderDeltaNorm = 1.0f;  // allow full range
                        chatLimits.maxEqBoostDB = 0.0f;       // subtractive EQ only
                        ActionValidator chatValidator(chatLimits);
                        auto vr = chatValidator.validate(action, model_);
                        if (vr.valid) {
                            auto er = executor_.execute(vr.clamped);
                            if (er.success) {
                                memory_.recordAction(vr.clamped, mixContext);
                                std::string desc = "Chat: " + vr.clamped.describe();
                                approvalUI_.addLog(desc);
                                emitJson({{"type", "action_executed"},
                                          {"action", desc}});
                                spdlog::info("{}", desc);
                            } else {
                                approvalUI_.addLog(
                                    "Failed: " + er.error);
                                spdlog::warn("Chat action failed: {}", er.error);
                            }
                        } else {
                            approvalUI_.addLog(
                                "Invalid: " + vr.warning);
                            spdlog::warn("Chat action invalid: {}", vr.warning);
                        }
                    }
                } else {
                    spdlog::warn("Chat LLM response has no actions array");
                }
            } catch (const std::exception& parseErr) {
                // If JSON parse fails, treat whole response as plain text reply
                spdlog::warn("Chat response not valid JSON: {}",
                             parseErr.what());
                std::string text = response.substr(0, 500);
                approvalUI_.addChatResponse(text);
                emitJson({{"type", "llm_response"},
                          {"message", text}});
            }

        } catch (const std::exception& e) {
            spdlog::error("Chat LLM call failed: {}", e.what());
            approvalUI_.addChatResponse(
                "Error: couldn't reach the LLM — " + std::string(e.what()));
        }
    }).detach();
}

// ── LLM Context Builder ────────────────────────────────────────────────

nlohmann::json SoundEngineerAgent::buildMixContext() {
    // Get current issues from DSP thread
    std::vector<AudioAnalyser::MixIssue> issues;
    {
        std::lock_guard lock(issuesMtx_);
        issues = latestIssues_;
    }

    // Build mix state with smart issue summaries
    MeterBridge bridge(model_, channelMap_);
    auto state = bridge.buildMixState(issues);

    // Include any standing engineer instructions so the LLM respects them
    auto instructions = memory_.activeInstructions(10);
    if (!instructions.empty()) {
        state["engineer_instructions"] = instructions;
    }

    // Include audio analysis status so LLM knows what data quality it has
    state["analysis_source"] = analyser_.hasFFTData()
        ? "fft_audio" : "console_meters";

    // Include genre preset targets so LLM has concrete mix references
    if (activePreset_) {
        state["genre_preset"] = activePreset_->toJson();
    }

    // Include learned engineer preferences so LLM adapts to their style
    auto prefs = preferences_.buildPreferences();
    if (!prefs.empty()) {
        state["engineer_preferences"] = prefs;
    }

    return state;
}
