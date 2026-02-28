#pragma once
#include "discovery/ChannelProfile.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>

// Genre-specific mix targets that guide the LLM's decisions.
// Each preset defines target RMS levels (relative to main bus),
// EQ character hints, and dynamics guidance per instrument role.
//
// These are injected into the LLM context as "mix_references" —
// the LLM uses them as a target to mix toward, not as hard rules.
struct RoleMixTarget {
    InstrumentRole role;
    float targetRmsRelative = 0.0f;  // dB relative to mix bus (0 = same as bus)
    float panTarget         = 0.0f;  // -1.0 to 1.0, 0 = center
    std::string eqCharacter;         // e.g. "warm", "bright", "punchy", "smooth"
    std::string dynamicsHint;        // e.g. "moderate compression 4:1", "light gate"
    std::string notes;               // freeform guidance for this role
};

struct GenrePreset {
    std::string name;           // "rock", "jazz", "worship", "edm", etc.
    std::string description;    // human-readable description
    std::vector<RoleMixTarget> targets;

    // Serialize to JSON for LLM context
    nlohmann::json toJson() const {
        nlohmann::json j;
        j["genre"] = name;
        j["description"] = description;
        j["targets"] = nlohmann::json::array();
        for (auto& t : targets) {
            nlohmann::json tj;
            tj["role"] = roleToString(t.role);
            tj["target_db_relative"] = t.targetRmsRelative;
            if (t.panTarget != 0.0f)
                tj["pan"] = t.panTarget;
            if (!t.eqCharacter.empty())
                tj["eq_character"] = t.eqCharacter;
            if (!t.dynamicsHint.empty())
                tj["dynamics"] = t.dynamicsHint;
            if (!t.notes.empty())
                tj["notes"] = t.notes;
            j["targets"].push_back(tj);
        }
        return j;
    }

    // Look up target for a specific role
    const RoleMixTarget* targetForRole(InstrumentRole role) const {
        for (auto& t : targets) {
            if (t.role == role) return &t;
        }
        return nullptr;
    }
};

// Built-in genre presets
class GenrePresetLibrary {
public:
    GenrePresetLibrary() {
        buildDefaults();
    }

    const GenrePreset* get(const std::string& name) const {
        auto it = presets_.find(name);
        return (it != presets_.end()) ? &it->second : nullptr;
    }

    std::vector<std::string> available() const {
        std::vector<std::string> names;
        for (auto& [k, _] : presets_) names.push_back(k);
        return names;
    }

    // Load custom preset from JSON file
    bool loadFromFile(const std::string& path) {
        std::ifstream f(path);
        if (!f.is_open()) return false;
        try {
            nlohmann::json j;
            f >> j;
            GenrePreset p;
            p.name = j.value("genre", "custom");
            p.description = j.value("description", "");
            if (j.contains("targets")) {
                for (auto& tj : j["targets"]) {
                    RoleMixTarget t;
                    t.role = roleFromString(tj.value("role", "Unknown"));
                    t.targetRmsRelative = tj.value("target_db_relative", 0.0f);
                    t.panTarget = tj.value("pan", 0.0f);
                    t.eqCharacter = tj.value("eq_character", "");
                    t.dynamicsHint = tj.value("dynamics", "");
                    t.notes = tj.value("notes", "");
                    p.targets.push_back(t);
                }
            }
            presets_[p.name] = p;
            return true;
        } catch (...) {
            return false;
        }
    }

private:
    void buildDefaults() {
        // ── Rock ─────────────────────────────────────────────────
        presets_["rock"] = {
            "rock",
            "Punchy drums, driving guitars, vocals above the band",
            {
                {InstrumentRole::Kick,          -6,  0,  "punchy, tight low-end", "moderate compression 4:1, fast attack", "HPF around 50Hz, cut boxiness at 300-400Hz"},
                {InstrumentRole::Snare,          -4,  0,  "crack with body", "medium compression 3:1", "boost attack at 2-5kHz, body at 200Hz"},
                {InstrumentRole::HiHat,        -14,  0.3f, "crisp not harsh", "", "HPF at 300Hz, tame harshness at 3-4kHz"},
                {InstrumentRole::Tom,            -8,  0,  "full, round attack", "light compression", "cut mud at 300-500Hz"},
                {InstrumentRole::Overhead,      -10,  0,  "natural cymbals, room", "", "HPF at 200Hz"},
                {InstrumentRole::BassGuitar,     -6,  0,  "warm and defined", "moderate compression 4:1", "separate from kick in low-mid, DI+amp blend"},
                {InstrumentRole::ElectricGuitar,  -8, -0.3f, "mid-forward, biting", "light compression", "don't compete with vocal 2-4kHz range"},
                {InstrumentRole::AcousticGuitar, -10,  0.3f, "open, strummy", "", "HPF at 100Hz, presence boost"},
                {InstrumentRole::LeadVocal,       0,  0,  "clear, upfront, present", "moderate compression 3:1", "this is the star — sits above everything, de-ess if sibilant"},
                {InstrumentRole::BackingVocal,   -6,  0,  "supportive, blended", "medium compression", "4-6dB below lead vocal"},
                {InstrumentRole::Keys,          -10,  0.2f, "pad underneath", "", "stay out of vocal range"},
            }
        };

        // ── Jazz ─────────────────────────────────────────────────
        presets_["jazz"] = {
            "jazz",
            "Natural, dynamic, piano/bass/drums trio feel, minimal processing",
            {
                {InstrumentRole::Kick,          -10,  0,  "warm, natural", "very light or none", "let dynamics breathe, no heavy gating"},
                {InstrumentRole::Snare,          -8,  0,  "warm brush or stick", "very light", "no harsh processing"},
                {InstrumentRole::HiHat,        -14,  0.3f, "natural sizzle", "", ""},
                {InstrumentRole::Overhead,       -6,  0,  "primary drum image", "", "these carry the kit sound in jazz"},
                {InstrumentRole::BassGuitar,     -4,  0,  "warm, full, walking", "very light", "upright bass needs body, HPF only at 30Hz"},
                {InstrumentRole::Piano,           0,  0,  "full, dynamic, rich", "none or very light", "often the lead — let it breathe"},
                {InstrumentRole::Keys,           -4,  0,  "natural, dynamic", "", ""},
                {InstrumentRole::ElectricGuitar,  -6,  0.3f, "clean, warm", "", "jazz guitar sits behind piano"},
                {InstrumentRole::LeadVocal,      -2,  0,  "intimate, warm", "very light 2:1", "jazz vocals are conversational, not arena"},
                {InstrumentRole::Saxophone,      -2,  0,  "rich, honky character", "", "don't over-EQ, natural is better"},
                {InstrumentRole::Trumpet,        -4,  0,  "bright but not harsh", "", "watch for harshness in upper register"},
            }
        };

        // ── Worship / Contemporary ──────────────────────────────
        presets_["worship"] = {
            "worship",
            "Big pads, clear vocals, emotional dynamics, atmospheric",
            {
                {InstrumentRole::Kick,           -8,  0,  "modern click + sub", "moderate 4:1", "tight, controlled low-end, sub emphasis"},
                {InstrumentRole::Snare,           -6,  0,  "fat, reverbed", "moderate 3:1", "generous reverb, big snare sound"},
                {InstrumentRole::BassGuitar,      -6,  0,  "sub-heavy, smooth", "moderate compression", "stay below 200Hz primarily"},
                {InstrumentRole::ElectricGuitar, -10,  0.4f, "ambient, washed", "", "lots of delay/reverb, textural not rhythmic"},
                {InstrumentRole::AcousticGuitar,  -8,  0.3f, "bright, rhythmic", "", "drives the rhythm in quieter sections"},
                {InstrumentRole::Keys,            -6,  0,  "big pads, atmospheric", "", "synth pads are foundational — warm and wide"},
                {InstrumentRole::Piano,           -6,  0,  "emotional, dynamic", "light", "let it lead in quiet moments"},
                {InstrumentRole::LeadVocal,        0,  0,  "clear, emotional, present", "moderate 3:1", "the most important element — always intelligible"},
                {InstrumentRole::BackingVocal,    -6,  0,  "blended, lush", "moderate", "tight harmonies, cohesive with lead"},
                {InstrumentRole::Choir,           -8,  0,  "full, blended wash", "", "congregation feel, not individual voices"},
            }
        };

        // ── EDM / Electronic ────────────────────────────────────
        presets_["edm"] = {
            "edm",
            "Loud, punchy, bass-heavy, everything compressed and controlled",
            {
                {InstrumentRole::Kick,           -2,  0,  "huge sub + transient click", "heavy compression 8:1", "sidechain everything to this"},
                {InstrumentRole::Snare,           -4,  0,  "layered, big clap/snare", "heavy compression", "reverb tail adds size"},
                {InstrumentRole::HiHat,         -12,  0.3f, "crisp, cutting", "", "precise, mechanical feel"},
                {InstrumentRole::BassGuitar,      -2,  0,  "massive sub, distorted mid", "heavy compression", "sidechain to kick, dominate the low-end"},
                {InstrumentRole::Synth,           -6,  0,  "leads bright, pads wide", "moderate", "automate filter sweeps"},
                {InstrumentRole::Keys,            -8,  0.4f, "pads: warm stereo, stabs: mono punch", "", ""},
                {InstrumentRole::LeadVocal,       -2,  0,  "processed, effected, upfront", "heavy compression 6:1", "autotune/vocoder acceptable, always audible"},
                {InstrumentRole::Playback,        -4,  0,  "full, matched to live elements", "", "blend seamlessly with live instruments"},
            }
        };

        // ── Acoustic / Singer-Songwriter ────────────────────────
        presets_["acoustic"] = {
            "acoustic",
            "Intimate, natural, vocal-forward with minimal instrumentation",
            {
                {InstrumentRole::AcousticGuitar,  -4,  0,  "natural, warm, body", "light compression 2:1", "primary instrument — full range"},
                {InstrumentRole::LeadVocal,        0,  0,  "intimate, clear, present", "light compression 2:1", "the whole show — above everything else"},
                {InstrumentRole::Piano,            -4,  0,  "natural, unprocessed", "none or very light", "pair with voice naturally"},
                {InstrumentRole::BassGuitar,       -8,  0,  "warm support", "light", "subtle foundation"},
                {InstrumentRole::Violin,           -6,  0.2f, "singing, expressive", "", "complement the vocal"},
                {InstrumentRole::BackingVocal,     -8,  0,  "gentle harmony", "light", "well behind the lead"},
            }
        };
    }

    std::unordered_map<std::string, GenrePreset> presets_;
};
