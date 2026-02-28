#pragma once
#include "ActionSchema.hpp"
#include <nlohmann/json.hpp>
#include <deque>
#include <string>
#include <mutex>
#include <shared_mutex>
#include <chrono>

// Rolling session memory — provides context for LLM decisions.
// Tracks recent actions, their outcomes, and mix state snapshots.
class SessionMemory {
public:
    struct MemoryEntry {
        std::chrono::steady_clock::time_point timestamp;
        enum class Type {
            ActionTaken,         // we changed something
            ActionRejected,      // approval queue rejected it
            Observation,         // LLM noted something
            EngOverride,         // engineer manually changed something
            EngInstruction,      // engineer typed a chat instruction
            MixSnapshot          // periodic mix state dump
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

    void recordInstruction(const std::string& instruction) {
        std::unique_lock lock(mtx_);
        MixAction a;
        a.type = ActionType::Observation;
        a.reason = instruction;
        entries_.push_back({
            std::chrono::steady_clock::now(),
            MemoryEntry::Type::EngInstruction,
            a, {},
            instruction
        });
        trimLocked();
    }

    // Get active standing instructions (last N EngInstruction entries)
    std::vector<std::string> activeInstructions(int maxCount = 10) const {
        std::shared_lock lock(mtx_);
        std::vector<std::string> result;
        for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
            if (it->type == MemoryEntry::Type::EngInstruction) {
                result.push_back(it->note);
                if ((int)result.size() >= maxCount) break;
            }
        }
        std::reverse(result.begin(), result.end());
        return result;
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
                case MemoryEntry::Type::EngInstruction:
                    entry["type"] = "engineer_instruction";
                    entry["instruction"] = e.note;
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
