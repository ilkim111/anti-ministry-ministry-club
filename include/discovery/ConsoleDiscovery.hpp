#pragma once
#include "console/IConsoleAdapter.hpp"
#include "console/ConsoleModel.hpp"
#include <spdlog/spdlog.h>
#include <future>
#include <atomic>
#include <chrono>

class ConsoleDiscovery {
    IConsoleAdapter& adapter_;
    ConsoleModel&    model_;

    std::atomic<int>   syncedChannels_{0};
    std::promise<void> syncComplete_;

public:
    ConsoleDiscovery(IConsoleAdapter& a, ConsoleModel& m)
        : adapter_(a), model_(m) {}

    // Blocks until full state received or timeout
    bool performFullSync(int timeoutMs = 10000) {
        spdlog::info("Starting full console sync...");

        auto caps = adapter_.capabilities();
        int expected = caps.channelCount + caps.busCount;
        syncedChannels_ = 0;

        // Hook into parameter updates to count completions
        auto prevCb = adapter_.onParameterUpdate;
        adapter_.onParameterUpdate = [&, prevCb](const ParameterUpdate& u) {
            model_.applyUpdate(u);
            if (prevCb) prevCb(u);

            if (u.param == ChannelParam::Name) {
                int completed = ++syncedChannels_;
                spdlog::debug("Sync progress: {}/{}", completed, expected);
                if (completed >= expected)
                    syncComplete_.set_value();
            }
        };

        // Request full dump
        adapter_.requestFullSync();

        // Wait with timeout
        auto future = syncComplete_.get_future();
        auto status = future.wait_for(
            std::chrono::milliseconds(timeoutMs));

        // Restore original callback
        adapter_.onParameterUpdate = prevCb;

        if (status == std::future_status::timeout) {
            spdlog::warn("Full sync timed out after {}ms — "
                         "proceeding with partial state ({}/{})",
                         timeoutMs, syncedChannels_.load(), expected);
            return false;
        }

        spdlog::info("Full sync complete — {} channels received",
                     syncedChannels_.load());
        return true;
    }
};
