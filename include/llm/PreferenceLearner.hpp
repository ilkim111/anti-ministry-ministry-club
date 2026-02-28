#pragma once
#include "ActionSchema.hpp"
#include "discovery/ChannelProfile.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <fstream>
#include <cmath>

// Learns engineer preferences from their approve/reject decisions
// and chat instructions over time.
//
// The learner tracks patterns:
// - Which action types get approved vs rejected per role
// - Preferred fader ranges per role
// - EQ tendency (does the engineer prefer cuts or boosts?)
// - How aggressive the engineer likes compression
// - Specific repeated instructions ("always keep vocals above X dB")
//
// These preferences are serialized to JSON and included in the LLM context
// as "engineer_preferences" so the LLM adapts to the engineer's style.
class PreferenceLearner {
public:
    // Record that an action was approved (engineer agreed with the LLM)
    void recordApproval(const MixAction& action, const std::string& role) {
        std::lock_guard lock(mtx_);
        auto& stats = roleStats_[role];
        stats.totalApproved++;

        switch (action.type) {
            case ActionType::SetFader:
                stats.faderApprovals.push_back(action.value);
                stats.faderAdjustDirection += (action.value > 0.5f) ? 1 : -1;
                break;
            case ActionType::SetEqBand:
                if (action.value2 > 0)
                    stats.eqBoostApprovals++;
                else
                    stats.eqCutApprovals++;
                break;
            case ActionType::SetCompressor:
                stats.compApprovals++;
                stats.compRatioSum += action.value2;
                break;
            case ActionType::SetHighPass:
                stats.hpfApprovals.push_back(action.value);
                break;
            default:
                break;
        }
        dirty_ = true;
    }

    // Record that an action was rejected (engineer disagreed with the LLM)
    void recordRejection(const MixAction& action, const std::string& role) {
        std::lock_guard lock(mtx_);
        auto& stats = roleStats_[role];
        stats.totalRejected++;

        switch (action.type) {
            case ActionType::SetFader:
                stats.faderRejections.push_back(action.value);
                break;
            case ActionType::SetEqBand:
                if (action.value2 > 0)
                    stats.eqBoostRejections++;
                else
                    stats.eqCutRejections++;
                break;
            case ActionType::SetCompressor:
                stats.compRejections++;
                break;
            default:
                break;
        }
        dirty_ = true;
    }

    // Record a standing instruction from the engineer
    void recordInstruction(const std::string& instruction) {
        std::lock_guard lock(mtx_);
        standingInstructions_.push_back(instruction);
        if (standingInstructions_.size() > 20)
            standingInstructions_.erase(standingInstructions_.begin());
        dirty_ = true;
    }

    // Build preferences JSON for LLM context
    nlohmann::json buildPreferences() const {
        std::lock_guard lock(mtx_);

        if (roleStats_.empty() && standingInstructions_.empty())
            return {};

        nlohmann::json prefs;

        // Overall tendencies
        int totalApproved = 0, totalRejected = 0;
        int totalEqBoostApproved = 0, totalEqCutApproved = 0;
        int totalEqBoostRejected = 0, totalEqCutRejected = 0;

        for (auto& [role, stats] : roleStats_) {
            totalApproved += stats.totalApproved;
            totalRejected += stats.totalRejected;
            totalEqBoostApproved += stats.eqBoostApprovals;
            totalEqCutApproved += stats.eqCutApprovals;
            totalEqBoostRejected += stats.eqBoostRejections;
            totalEqCutRejected += stats.eqCutRejections;
        }

        if (totalApproved + totalRejected > 5) {
            float approvalRate = (float)totalApproved /
                                 (totalApproved + totalRejected);
            prefs["overall_approval_rate"] = roundTo(approvalRate, 2);

            if (approvalRate < 0.4f) {
                prefs["note"] = "Engineer rejects many suggestions — "
                                "be more conservative";
            } else if (approvalRate > 0.8f) {
                prefs["note"] = "Engineer trusts AI suggestions — "
                                "confidence is appropriate";
            }
        }

        // EQ tendency
        int totalEqApproved = totalEqBoostApproved + totalEqCutApproved;
        int totalEqRejected = totalEqBoostRejected + totalEqCutRejected;
        if (totalEqApproved + totalEqRejected > 3) {
            if (totalEqBoostRejected > totalEqBoostApproved * 2) {
                prefs["eq_tendency"] = "Engineer prefers cuts over boosts — "
                                       "use subtractive EQ";
            } else if (totalEqBoostApproved > totalEqCutApproved) {
                prefs["eq_tendency"] = "Engineer is comfortable with EQ boosts";
            }
        }

        // Per-role preferences
        nlohmann::json rolePrefs = nlohmann::json::object();
        for (auto& [role, stats] : roleStats_) {
            if (stats.totalApproved + stats.totalRejected < 3)
                continue; // not enough data

            nlohmann::json rp;
            float roleApprovalRate = (float)stats.totalApproved /
                (stats.totalApproved + stats.totalRejected);
            rp["approval_rate"] = roundTo(roleApprovalRate, 2);

            // Fader preference
            if (!stats.faderApprovals.empty()) {
                float avgFader = average(stats.faderApprovals);
                rp["preferred_fader_range"] = roundTo(avgFader, 2);
            }

            // Compression preference
            if (stats.compApprovals + stats.compRejections > 2) {
                if (stats.compRejections > stats.compApprovals) {
                    rp["dynamics"] = "engineer prefers less compression on this";
                } else if (stats.compApprovals > 0) {
                    float avgRatio = stats.compRatioSum / stats.compApprovals;
                    rp["preferred_comp_ratio"] = roundTo(avgRatio, 1);
                }
            }

            // HPF preference
            if (!stats.hpfApprovals.empty()) {
                float avgHpf = average(stats.hpfApprovals);
                rp["preferred_hpf_hz"] = (int)avgHpf;
            }

            if (roleApprovalRate < 0.3f) {
                rp["warning"] = "engineer frequently rejects changes to this — "
                                "leave it alone unless asked";
            }

            rolePrefs[role] = rp;
        }

        if (!rolePrefs.empty())
            prefs["role_preferences"] = rolePrefs;

        return prefs;
    }

    // Persistence — save/load to disk so preferences carry across sessions
    bool saveToFile(const std::string& path) const {
        std::lock_guard lock(mtx_);
        try {
            nlohmann::json j;
            j["preferences"] = buildPreferencesUnlocked();
            j["instructions"] = standingInstructions_;
            j["role_stats"] = nlohmann::json::object();
            for (auto& [role, stats] : roleStats_) {
                j["role_stats"][role] = {
                    {"approved", stats.totalApproved},
                    {"rejected", stats.totalRejected},
                    {"eq_boost_approved", stats.eqBoostApprovals},
                    {"eq_cut_approved", stats.eqCutApprovals},
                    {"eq_boost_rejected", stats.eqBoostRejections},
                    {"eq_cut_rejected", stats.eqCutRejections},
                    {"comp_approved", stats.compApprovals},
                    {"comp_rejected", stats.compRejections},
                    {"comp_ratio_sum", stats.compRatioSum},
                    {"fader_approvals", stats.faderApprovals},
                    {"fader_rejections", stats.faderRejections},
                    {"hpf_approvals", stats.hpfApprovals},
                    {"fader_direction", stats.faderAdjustDirection}
                };
            }

            std::ofstream f(path);
            if (!f.is_open()) return false;
            f << j.dump(2);
            dirty_ = false;
            return true;
        } catch (...) {
            return false;
        }
    }

    bool loadFromFile(const std::string& path) {
        std::lock_guard lock(mtx_);
        try {
            std::ifstream f(path);
            if (!f.is_open()) return false;
            nlohmann::json j;
            f >> j;

            if (j.contains("instructions"))
                standingInstructions_ = j["instructions"]
                    .get<std::vector<std::string>>();

            if (j.contains("role_stats")) {
                for (auto& [role, sj] : j["role_stats"].items()) {
                    RoleStats stats;
                    stats.totalApproved = sj.value("approved", 0);
                    stats.totalRejected = sj.value("rejected", 0);
                    stats.eqBoostApprovals = sj.value("eq_boost_approved", 0);
                    stats.eqCutApprovals = sj.value("eq_cut_approved", 0);
                    stats.eqBoostRejections = sj.value("eq_boost_rejected", 0);
                    stats.eqCutRejections = sj.value("eq_cut_rejected", 0);
                    stats.compApprovals = sj.value("comp_approved", 0);
                    stats.compRejections = sj.value("comp_rejected", 0);
                    stats.compRatioSum = sj.value("comp_ratio_sum", 0.0f);
                    if (sj.contains("fader_approvals"))
                        stats.faderApprovals = sj["fader_approvals"]
                            .get<std::vector<float>>();
                    if (sj.contains("fader_rejections"))
                        stats.faderRejections = sj["fader_rejections"]
                            .get<std::vector<float>>();
                    if (sj.contains("hpf_approvals"))
                        stats.hpfApprovals = sj["hpf_approvals"]
                            .get<std::vector<float>>();
                    stats.faderAdjustDirection = sj.value("fader_direction", 0);
                    roleStats_[role] = stats;
                }
            }

            dirty_ = false;
            return true;
        } catch (...) {
            return false;
        }
    }

    bool isDirty() const { return dirty_; }
    void clearDirty() { dirty_ = false; }

    int totalDecisions() const {
        std::lock_guard lock(mtx_);
        int total = 0;
        for (auto& [_, s] : roleStats_)
            total += s.totalApproved + s.totalRejected;
        return total;
    }

private:
    struct RoleStats {
        int totalApproved = 0;
        int totalRejected = 0;

        // EQ preferences
        int eqBoostApprovals  = 0;
        int eqCutApprovals    = 0;
        int eqBoostRejections = 0;
        int eqCutRejections   = 0;

        // Compression preferences
        int   compApprovals  = 0;
        int   compRejections = 0;
        float compRatioSum   = 0.0f;

        // Fader preferences
        std::vector<float> faderApprovals;
        std::vector<float> faderRejections;
        int faderAdjustDirection = 0; // positive = tends to push up

        // HPF preferences
        std::vector<float> hpfApprovals;
    };

    nlohmann::json buildPreferencesUnlocked() const {
        // Same as buildPreferences but without locking (called while locked)
        nlohmann::json prefs;
        // ... simplified for internal use
        return prefs;
    }

    static float average(const std::vector<float>& v) {
        if (v.empty()) return 0;
        float sum = 0;
        for (float f : v) sum += f;
        return sum / v.size();
    }

    static float roundTo(float val, int decimals) {
        float mult = std::pow(10.0f, decimals);
        return std::round(val * mult) / mult;
    }

    std::unordered_map<std::string, RoleStats> roleStats_;
    std::vector<std::string> standingInstructions_;
    mutable std::mutex mtx_;
    mutable bool dirty_ = false;
};
