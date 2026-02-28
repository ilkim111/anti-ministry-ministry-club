#pragma once
#include "discovery/DynamicChannelMap.hpp"
#include "console/ConsoleModel.hpp"
#include "llm/LLMDecisionEngine.hpp"
#include "llm/SessionMemory.hpp"
#include <string>

// Top-level terminal dashboard combining channel map view,
// meter bridge, and approval queue into a single ftxui layout.
class TerminalUI {
public:
    struct Dependencies {
        const DynamicChannelMap* channelMap;
        const ConsoleModel*     model;
        const LLMDecisionEngine* llm;
        const SessionMemory*    memory;
    };

    explicit TerminalUI(const Dependencies& deps);
    ~TerminalUI();

    // Non-interactive single frame render (for headless logging)
    std::string renderFrame() const;

    // Format a channel strip for display
    std::string formatChannelStrip(int ch) const;

    // Format the meter bridge (all channels, compact)
    std::string formatMeterBridge() const;

private:
    Dependencies deps_;

    static std::string meterBar(float dbFS, int width = 30);
    static std::string faderBar(float norm, int width = 15);
};
