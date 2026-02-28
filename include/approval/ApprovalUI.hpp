#pragma once
#include "ApprovalQueue.hpp"
#include <string>
#include <vector>

// Terminal UI for displaying and interacting with the approval queue.
// Uses ftxui for rendering.
class ApprovalUI {
public:
    explicit ApprovalUI(ApprovalQueue& queue);
    ~ApprovalUI();

    // Render one frame (called from UI thread)
    void render();

    // Start interactive UI loop (blocks)
    void run();

    // Stop the UI
    void stop();

    // Add a log line to the activity feed
    void addLog(const std::string& msg);

    // Update status line
    void setStatus(const std::string& status);

private:
    ApprovalQueue& queue_;
    bool running_ = false;
    std::string status_;
    std::vector<std::string> logs_;
    static constexpr int maxLogs_ = 50;
};
