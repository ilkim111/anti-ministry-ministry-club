#pragma once
#include "ChannelProfile.hpp"
#include <vector>
#include <string>
#include <regex>
#include <algorithm>
#include <cmath>

class StereoPairDetector {
public:
    struct Pair {
        int   left, right;
        float confidence;
    };

    std::vector<Pair> detect(const std::vector<ChannelProfile>& channels) {
        std::vector<Pair> pairs;

        for (int i = 0; i < (int)channels.size() - 1; i++) {
            auto& a = channels[i];
            auto& b = channels[i + 1];

            // Only pair adjacent channels
            if (b.index != a.index + 1) continue;

            float score = 0;

            // Name-based matching
            score += nameImpliesPair(a.consoleName, b.consoleName) ? 0.6f : 0;

            // Same role
            score += (a.role == b.role && a.role != InstrumentRole::Unknown)
                     ? 0.2f : 0;

            // Similar spectral content
            score += spectralSimilarity(a.fingerprint, b.fingerprint) * 0.2f;

            if (score > 0.5f)
                pairs.push_back({a.index, b.index, score});
        }
        return pairs;
    }

private:
    bool nameImpliesPair(const std::string& a, const std::string& b) {
        auto normA = toLower(a), normB = toLower(b);
        if (normA.empty() || normB.empty()) return false;

        // Strip trailing L/R/1/2 and compare roots
        auto rootA = stripSuffix(normA);
        auto rootB = stripSuffix(normB);

        if (rootA == rootB && !rootA.empty())
            return true;

        return false;
    }

    std::string stripSuffix(std::string s) {
        // Remove trailing whitespace/separators + L/R/1/2
        while (!s.empty() && (s.back() == ' ' || s.back() == '-' ||
                              s.back() == '/' || s.back() == '_'))
            s.pop_back();

        if (!s.empty()) {
            char last = s.back();
            if (last == 'l' || last == 'r' || last == '1' || last == '2') {
                s.pop_back();
                // Trim trailing separators again
                while (!s.empty() && (s.back() == ' ' || s.back() == '-' ||
                                      s.back() == '/' || s.back() == '_'))
                    s.pop_back();
            }
        }
        return s;
    }

    float spectralSimilarity(const ChannelProfile::Fingerprint& a,
                              const ChannelProfile::Fingerprint& b) {
        if (!a.hasSignal || !b.hasSignal) return 0;
        float maxFreq = std::max(a.dominantFreqHz, b.dominantFreqHz);
        if (maxFreq < 1.0f) return 0;
        float diff = std::abs(a.dominantFreqHz - b.dominantFreqHz) / maxFreq;
        return std::max(0.0f, 1.0f - diff);
    }

    static std::string toLower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c){ return std::tolower(c); });
        return s;
    }
};
