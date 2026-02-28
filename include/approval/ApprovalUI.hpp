#pragma once
#include "ApprovalQueue.hpp"
#include <string>
#include <vector>
#include <functional>
#include <mutex>

// Terminal UI for displaying and interacting with the approval queue.
// Uses ftxui for rendering. Includes a chat input bar for engineer feedback.
class ApprovalUI {
public:
    explicit ApprovalUI(ApprovalQueue& queue);
    ~ApprovalUI();

    // Callback when engineer sends a chat message
    std::function<void(const std::string&)> onChatMessage;

    // Render one frame (called from UI thread)
    void render();

    // Start interactive UI loop (blocks)
    void run();

    // Stop the UI
    void stop();

    // Add a log line to the activity feed
    void addLog(const std::string& msg);

    // Add a chat response (from LLM or system)
    void addChatResponse(const std::string& msg);

    // Update status line
    void setStatus(const std::string& status);

private:
    ApprovalQueue& queue_;
    bool running_ = false;
    std::string status_;
    std::vector<std::string> logs_;
    static constexpr int maxLogs_ = 50;

    // Chat state
    std::vector<std::string> chatHistory_;
    static constexpr int maxChatHistory_ = 100;
    std::mutex chatMtx_;

    enum class UIMode {
        Approval,   // navigating the approval queue (default)
        Chat        // typing in the chat input
    };
    UIMode uiMode_ = UIMode::Approval;
};
