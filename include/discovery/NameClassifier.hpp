#pragma once
#include "ChannelProfile.hpp"
#include <regex>
#include <vector>
#include <string>
#include <algorithm>
#include <cctype>

class NameClassifier {
    struct RuleEntry {
        std::regex          pattern;
        InstrumentRole      role;
        std::string         group;
        DiscoveryConfidence confidence;
    };

    std::vector<RuleEntry> rules_;

public:
    NameClassifier() {
        auto add = [&](const std::string& pattern, InstrumentRole role,
                       const std::string& group, DiscoveryConfidence conf) {
            rules_.push_back({
                std::regex(pattern, std::regex::icase),
                role, group, conf
            });
        };

        // ── Drums ─────────────────────────────────────────────────────
        add(R"(^k(ic)?k$|bd|bass.?drum)",
            InstrumentRole::Kick, "drums", DiscoveryConfidence::High);
        add(R"(^sn(are)?$|snr)",
            InstrumentRole::Snare, "drums", DiscoveryConfidence::High);
        add(R"(h\.?h|hi.?hat|hihat|hh)",
            InstrumentRole::HiHat, "drums", DiscoveryConfidence::High);
        add(R"(^tom\s*[1-4]?$|t[1-4]$|rack.?tom|floor.?tom)",
            InstrumentRole::Tom, "drums", DiscoveryConfidence::High);
        add(R"(^oh$|over.?head|cym(bal)?)",
            InstrumentRole::Overhead, "drums", DiscoveryConfidence::High);
        add(R"(room|amb(ience)?|kit.?mic)",
            InstrumentRole::RoomMic, "drums", DiscoveryConfidence::High);

        // ── Bass ──────────────────────────────────────────────────────
        add(R"(^bass?\s*(d\.?i\.?|direct)?$|b\.d\.i\.?|bgtr)",
            InstrumentRole::BassGuitar, "bass", DiscoveryConfidence::High);
        add(R"(bass.?amp|b\.?amp)",
            InstrumentRole::BassAmp, "bass", DiscoveryConfidence::High);

        // ── Guitars ───────────────────────────────────────────────────
        add(R"(^e\.?gtr|elec.?git|e\.?guitar|gtr\s*[lr12]?$)",
            InstrumentRole::ElectricGuitar, "guitars", DiscoveryConfidence::High);
        add(R"(ac.?git|acoustic|a\.?gtr)",
            InstrumentRole::AcousticGuitar, "guitars", DiscoveryConfidence::High);

        // ── Keys ──────────────────────────────────────────────────────
        add(R"(^pno$|piano|grand)",
            InstrumentRole::Piano, "keys", DiscoveryConfidence::High);
        add(R"(^keys?\s*[lr12]?$|keyboard)",
            InstrumentRole::Keys, "keys", DiscoveryConfidence::High);
        add(R"(organ|b3|hammond)",
            InstrumentRole::Organ, "keys", DiscoveryConfidence::High);
        add(R"(synth|moog|arp|poly|pad|seq)",
            InstrumentRole::Synth, "keys", DiscoveryConfidence::High);

        // ── Vocals ────────────────────────────────────────────────────
        add(R"(^(lead\s*)?vox\s*(l|r|lr|1|2)?$|^(lead\s*)?vocal|^lv$|^ld\.?vx)",
            InstrumentRole::LeadVocal, "vocals", DiscoveryConfidence::High);
        add(R"(bv\s*[1-4lr]?|b\.?v\.|back.?voc|backing|harmony|bg\.?voc)",
            InstrumentRole::BackingVocal, "vocals", DiscoveryConfidence::High);
        add(R"(choir|chorus)",
            InstrumentRole::Choir, "vocals", DiscoveryConfidence::High);
        add(R"(presenter|speaker|announce|mc$|host)",
            InstrumentRole::Presenter, "vocals", DiscoveryConfidence::High);
        add(R"(talk.?back|tb$|comm)",
            InstrumentRole::Talkback, "talkback", DiscoveryConfidence::High);

        // ── Brass / Strings ───────────────────────────────────────────
        add(R"(tpt|trumpet|trp)",
            InstrumentRole::Trumpet, "brass", DiscoveryConfidence::High);
        add(R"(sax|alto|tenor|bari)",
            InstrumentRole::Saxophone, "brass", DiscoveryConfidence::High);
        add(R"(vln|violin|fiddle)",
            InstrumentRole::Violin, "strings", DiscoveryConfidence::High);

        // ── Playback / FX ─────────────────────────────────────────────
        add(R"(playback|track[s]?|click|backing.?track|bt$)",
            InstrumentRole::Playback, "playback", DiscoveryConfidence::High);
        add(R"(^fx\s*ret|return|rev.?return|delay.?ret)",
            InstrumentRole::FXReturn, "fx", DiscoveryConfidence::High);
        add(R"(^d\.?i\.?$|direct)",
            InstrumentRole::DI, "misc", DiscoveryConfidence::Medium);

        // ── Low confidence fallbacks ──────────────────────────────────
        add(R"(^ch\s*\d+$|^input\s*\d+$|^mic\s*\d+$|^\d+$)",
            InstrumentRole::Unknown, "unknown", DiscoveryConfidence::Low);
    }

    struct ClassificationResult {
        InstrumentRole      role;
        std::string         group;
        DiscoveryConfidence confidence;
    };

    ClassificationResult classify(const std::string& name) const {
        std::string trimmed = trim(name);
        if (trimmed.empty())
            return {InstrumentRole::Unknown, "unknown", DiscoveryConfidence::Unknown};

        for (auto& rule : rules_) {
            if (std::regex_search(trimmed, rule.pattern))
                return {rule.role, rule.group, rule.confidence};
        }

        // No match — unknown but has a custom name
        return {InstrumentRole::Unknown, "unknown", DiscoveryConfidence::Low};
    }

private:
    static std::string trim(std::string s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(),
            [](unsigned char c){ return !std::isspace(c); }));
        s.erase(std::find_if(s.rbegin(), s.rend(),
            [](unsigned char c){ return !std::isspace(c); }).base(), s.end());
        return s;
    }
};
