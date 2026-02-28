#pragma once
#include "ActionValidator.hpp"
#include "ActionExecutor.hpp"
#include "console/IConsoleAdapter.hpp"
#include "console/ConsoleModel.hpp"
#include "discovery/DynamicChannelMap.hpp"
#include "discovery/DiscoveryOrchestrator.hpp"
#include "discovery/NameClassifier.hpp"
#include "analysis/AudioAnalyser.hpp"
#include "analysis/MeterBridge.hpp"
#include "audio/IAudioCapture.hpp"
#include "audio/FFTAnalyser.hpp"
#include "llm/LLMDecisionEngine.hpp"
#include "llm/SessionMemory.hpp"
#include "approval/ApprovalQueue.hpp"
#include "approval/ApprovalUI.hpp"
#include <nlohmann/json.hpp>
#include <atomic>
#include <thread>
#include <chrono>
#include <memory>
#include <mutex>

struct AgentConfig {
    int  dspIntervalMs    = 50;    // DSP analysis rate
    int  llmIntervalMs    = 5000;  // LLM decision rate
    int  snapshotIntervalMs = 60000; // session memory snapshot rate
    int  meterRefreshMs   = 50;
    bool headless         = false;  // no UI

    // Audio capture config
    int  audioDeviceId    = -1;    // -1 = default, or specific PortAudio device ID
    int  audioChannels    = 0;     // 0 = disable audio capture
    double audioSampleRate = 48000;
    int  audioFFTSize     = 1024;

    ApprovalQueue::Mode approvalMode = ApprovalQueue::Mode::AutoUrgent;
};

class SoundEngineerAgent {
public:
    SoundEngineerAgent(IConsoleAdapter& adapter,
                       const LLMConfig& llmConfig,
                       const AgentConfig& agentConfig);
    ~SoundEngineerAgent();

    // Full lifecycle
    bool start();
    void stop();
    bool isRunning() const { return running_; }

    // Optionally inject an audio capture backend (call before start())
    void setAudioCapture(std::unique_ptr<IAudioCapture> capture);

    // Access for external control
    ApprovalQueue&    approvalQueue()  { return approvalQueue_; }
    DynamicChannelMap& channelMap()    { return channelMap_; }
    SessionMemory&    sessionMemory()  { return memory_; }

private:
    // Thread entry points
    void dspLoop();          // fast: meter reading + analysis + FFT (50ms)
    void llmLoop();          // slow: LLM decisions (5s)
    void executionLoop();    // approval queue consumer
    void uiLoop();           // terminal UI

    // Live channel reclassification on name change
    void onParameterUpdate(const ParameterUpdate& u);

    // Chat message handler — called from UI thread
    void onChatMessage(const std::string& message);

    // Build LLM context from current state
    nlohmann::json buildMixContext();

    // Update connection status indicators in the UI
    void refreshConnectionStatus();

    // Components
    IConsoleAdapter&   adapter_;
    ConsoleModel       model_;
    DynamicChannelMap  channelMap_;
    LLMDecisionEngine  llm_;
    SessionMemory      memory_;
    AudioAnalyser      analyser_;
    ActionValidator    validator_;
    ActionExecutor     executor_;
    ApprovalQueue      approvalQueue_;
    ApprovalUI         approvalUI_;
    NameClassifier     nameClassifier_;

    // Audio capture (optional)
    std::unique_ptr<IAudioCapture> audioCapture_;
    std::unique_ptr<FFTAnalyser>   fftAnalyser_;

    // Latest detected issues (shared between DSP and LLM threads)
    std::vector<AudioAnalyser::MixIssue> latestIssues_;
    std::mutex issuesMtx_;

    // Config
    AgentConfig config_;

    // Threads
    std::thread dspThread_;
    std::thread llmThread_;
    std::thread execThread_;
    std::thread uiThread_;
    std::atomic<bool> running_{false};
};
