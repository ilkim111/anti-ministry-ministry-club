#pragma once
#include "llm/ActionSchema.hpp"
#include <deque>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <functional>

// Queues MixActions for human approval before execution.
// Actions above a certain urgency level bypass the queue.
class ApprovalQueue {
public:
    struct QueuedAction {
        MixAction action;
        std::chrono::steady_clock::time_point queued;
        int  timeoutMs;     // auto-approve if no response within this time
        bool approved = false;
        bool rejected = false;
        bool expired  = false;
    };

    enum class Mode {
        ApproveAll,     // every action needs approval
        AutoUrgent,     // auto-approve Immediate/Fast urgency
        AutoAll,        // auto-approve everything (demo/testing)
        DenyAll         // reject everything (safe mode)
    };

    explicit ApprovalQueue(Mode mode = Mode::AutoUrgent)
        : mode_(mode) {}

    void setMode(Mode m) {
        std::lock_guard lock(mtx_);
        mode_ = m;
    }

    Mode mode() const {
        std::lock_guard lock(mtx_);
        return mode_;
    }

    // Submit an action for approval. Returns true if auto-approved.
    bool submit(const MixAction& action) {
        std::lock_guard lock(mtx_);

        // Check auto-approve rules
        if (mode_ == Mode::AutoAll)
            return true;
        if (mode_ == Mode::DenyAll) {
            rejected_.push_back({action, std::chrono::steady_clock::now(),
                                 0, false, true, false});
            return false;
        }
        if (mode_ == Mode::AutoUrgent &&
            (action.urgency == MixAction::Urgency::Immediate ||
             action.urgency == MixAction::Urgency::Fast))
            return true;

        // Queue for manual approval
        int timeout = timeoutForUrgency(action.urgency);
        pending_.push_back({action, std::chrono::steady_clock::now(),
                            timeout, false, false, false});
        cv_.notify_all();
        return false;
    }

    // Get pending actions for UI display
    std::vector<QueuedAction> pending() const {
        std::lock_guard lock(mtx_);
        return {pending_.begin(), pending_.end()};
    }

    // Approve action at index
    bool approve(size_t index) {
        std::lock_guard lock(mtx_);
        if (index >= pending_.size()) return false;
        pending_[index].approved = true;
        approved_.push_back(pending_[index]);
        pending_.erase(pending_.begin() + index);
        cv_.notify_all();
        return true;
    }

    // Reject action at index
    bool reject(size_t index) {
        std::lock_guard lock(mtx_);
        if (index >= pending_.size()) return false;
        pending_[index].rejected = true;
        rejected_.push_back(pending_[index]);
        pending_.erase(pending_.begin() + index);
        return true;
    }

    // Approve all pending
    void approveAll() {
        std::lock_guard lock(mtx_);
        for (auto& a : pending_) {
            a.approved = true;
            approved_.push_back(a);
        }
        pending_.clear();
        cv_.notify_all();
    }

    // Reject all pending
    void rejectAll() {
        std::lock_guard lock(mtx_);
        for (auto& a : pending_) {
            a.rejected = true;
            rejected_.push_back(a);
        }
        pending_.clear();
    }

    // Pop next approved action (blocks until available or timeout)
    bool popApproved(MixAction& out, int timeoutMs = 100) {
        std::unique_lock lock(mtx_);

        // Check for expired pending actions
        expireOldLocked();

        if (!approved_.empty()) {
            out = approved_.front().action;
            approved_.pop_front();
            return true;
        }

        cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs));

        if (!approved_.empty()) {
            out = approved_.front().action;
            approved_.pop_front();
            return true;
        }
        return false;
    }

    size_t pendingCount() const {
        std::lock_guard lock(mtx_);
        return pending_.size();
    }

private:
    int timeoutForUrgency(MixAction::Urgency u) const {
        switch (u) {
            case MixAction::Urgency::Immediate: return 500;
            case MixAction::Urgency::Fast:      return 2000;
            case MixAction::Urgency::Normal:    return 10000;
            case MixAction::Urgency::Low:       return 30000;
        }
        return 10000;
    }

    void expireOldLocked() {
        auto now = std::chrono::steady_clock::now();
        auto it = pending_.begin();
        while (it != pending_.end()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - it->queued).count();
            if (elapsed > it->timeoutMs) {
                // Auto-approve expired actions (they had their chance)
                it->approved = true;
                it->expired  = true;
                approved_.push_back(*it);
                it = pending_.erase(it);
            } else {
                ++it;
            }
        }
    }

    Mode mode_;
    std::deque<QueuedAction> pending_;
    std::deque<QueuedAction> approved_;
    std::deque<QueuedAction> rejected_;
    mutable std::mutex mtx_;
    std::condition_variable cv_;
};
