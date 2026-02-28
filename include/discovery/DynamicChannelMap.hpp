#pragma once
#include "ChannelProfile.hpp"
#include <shared_mutex>
#include <vector>
#include <string>

class DynamicChannelMap {
    std::vector<ChannelProfile>  channels_;
    mutable std::shared_mutex    mtx_;

public:
    explicit DynamicChannelMap(int count = 0) : channels_(count) {
        for (int i = 0; i < count; i++)
            channels_[i].index = i + 1;
    }

    void resize(int count) {
        std::unique_lock lock(mtx_);
        channels_.resize(count);
        for (int i = 0; i < count; i++)
            channels_[i].index = i + 1;
    }

    void updateProfile(const ChannelProfile& p) {
        std::unique_lock lock(mtx_);
        if (p.index < 1 || p.index > (int)channels_.size()) return;
        channels_[p.index - 1] = p;
    }

    ChannelProfile getProfile(int ch) const {
        std::shared_lock lock(mtx_);
        return channels_.at(ch - 1);
    }

    // Query by role — returns all matching channels
    std::vector<ChannelProfile> byRole(InstrumentRole role) const {
        std::shared_lock lock(mtx_);
        std::vector<ChannelProfile> result;
        for (auto& c : channels_)
            if (c.role == role) result.push_back(c);
        return result;
    }

    // Query by group
    std::vector<ChannelProfile> byGroup(const std::string& group) const {
        std::shared_lock lock(mtx_);
        std::vector<ChannelProfile> result;
        for (auto& c : channels_)
            if (c.group == group) result.push_back(c);
        return result;
    }

    // All channels with signal
    std::vector<ChannelProfile> active() const {
        std::shared_lock lock(mtx_);
        std::vector<ChannelProfile> result;
        for (auto& c : channels_)
            if (c.fingerprint.hasSignal && !c.muted) result.push_back(c);
        return result;
    }

    // All channels
    std::vector<ChannelProfile> all() const {
        std::shared_lock lock(mtx_);
        return channels_;
    }

    int count() const {
        std::shared_lock lock(mtx_);
        return static_cast<int>(channels_.size());
    }
};
