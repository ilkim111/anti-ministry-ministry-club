#pragma once
#include "ActionSchema.hpp"
#include <nlohmann/json.hpp>
#include <deque>
#include <string>
#include <shared_mutex>
#include <chrono>

// Rolling session memory — provides context for LLM decisions.
// Tracks recent actions, their outcomes, and mix state snapshots.
class SessionMemory {
public:
    struct MemoryEntry {
        std::chrono::steady_clock::time_point timestamp;
        enum class Type {
            ActionTaken,     // we changed something
            ActionRejected,  // approval queue rejected it
            Observation,     // LLM noted something
            EngOverride,     // engineer manually changed something
            MixSnapshot      // periodic mix state dump
        } type;

        MixAction action;             // the action (if applicable)
        nlohmann::json mixState;      // context at time of entry
        std::string note;             // human-readable note
    };

    explicit SessionMemory(size_t maxEntries = 100)
        : maxEntries_(maxEntries) {}

    void recordAction(const MixAction& action, const nlohmann::json& context) {
        std::unique_lock lock(mtx_);
        entries_.push_back({
            std::chrono::steady_clock::now(),
            MemoryEntry::Type::ActionTaken,
            action, context,
            action.describe()
        });
        trimLocked();
    }

    void recordRejection(const MixAction& action, const std::string& reason) {
        std::unique_lock lock(mtx_);
        entries_.push_back({
            std::chrono::steady_clock::now(),
            MemoryEntry::Type::ActionRejected,
            action, {},
            "Rejected: " + reason
        });
        trimLocked();
    }

    void recordObservation(const std::string& note) {
        std::unique_lock lock(mtx_);
        MixAction obs;
        obs.type = ActionType::Observation;
        obs.reason = note;
        entries_.push_back({
            std::chrono::steady_clock::now(),
            MemoryEntry::Type::Observation,
            obs, {}, note
        });
        trimLocked();
    }

    void recordEngineerOverride(int channel, const std::string& what) {
        std::unique_lock lock(mtx_);
        MixAction a;
        a.channel = channel;
        a.reason = what;
        entries_.push_back({
            std::chrono::steady_clock::now(),
            MemoryEntry::Type::EngOverride,
            a, {},
            "Engineer override ch" + std::to_string(channel) + ": " + what
        });
        trimLocked();
    }

    void recordSnapshot(const nlohmann::json& mixState) {
        std::unique_lock lock(mtx_);
        entries_.push_back({
            std::chrono::steady_clock::now(),
            MemoryEntry::Type::MixSnapshot,
            {}, mixState,
            "Mix snapshot"
        });
        trimLocked();
    }

    // Build context string for LLM prompt
    nlohmann::json buildContext(int maxRecent = 20) const {
        std::shared_lock lock(mtx_);

        nlohmann::json ctx = nlohmann::json::array();
        int start = std::max(0, (int)entries_.size() - maxRecent);

        for (int i = start; i < (int)entries_.size(); i++) {
            auto& e = entries_[i];
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - e.timestamp).count();

            nlohmann::json entry = {
                {"seconds_ago", elapsed},
                {"note",        e.note}
            };

            switch (e.type) {
                case MemoryEntry::Type::ActionTaken:
                    entry["type"] = "action_taken";
                    entry["action"] = e.action.toJson();
                    break;
                case MemoryEntry::Type::ActionRejected:
                    entry["type"] = "action_rejected";
                    entry["action"] = e.action.toJson();
                    break;
                case MemoryEntry::Type::Observation:
                    entry["type"] = "observation";
                    break;
                case MemoryEntry::Type::EngOverride:
                    entry["type"] = "engineer_override";
                    entry["channel"] = e.action.channel;
                    break;
                case MemoryEntry::Type::MixSnapshot:
                    entry["type"] = "snapshot";
                    break;
            }
            ctx.push_back(entry);
        }
        return ctx;
    }

    size_t size() const {
        std::shared_lock lock(mtx_);
        return entries_.size();
    }

private:
    void trimLocked() {
        while (entries_.size() > maxEntries_)
            entries_.pop_front();
    }

    size_t maxEntries_;
    std::deque<MemoryEntry> entries_;
    mutable std::shared_mutex mtx_;
};
