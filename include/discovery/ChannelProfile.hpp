#pragma once
#include <string>
#include <optional>
#include <chrono>
#include <vector>
#include <shared_mutex>
#include <algorithm>

enum class InstrumentRole {
    Unknown,
    // Drums
    Kick, Snare, HiHat, Tom, Overhead, RoomMic,
    // Bass
    BassGuitar, BassAmp,
    // Guitars
    ElectricGuitar, AcousticGuitar,
    // Keys
    Piano, Keys, Organ, Synth,
    // Vocals
    LeadVocal, BackingVocal, Choir, Presenter, Announcer,
    // Brass / Strings
    Trumpet, Saxophone, Violin, Cello,
    // Other
    DI, Playback, Talkback, FXReturn,
    // Special
    Muted, NoSignal
};

enum class DiscoveryConfidence {
    High,      // name + spectral match perfectly
    Medium,    // name or spectral, not both
    Low,       // guessing from spectral only
    Unknown    // no signal, generic name
};

inline std::string roleToString(InstrumentRole role) {
    switch (role) {
        case InstrumentRole::Unknown:        return "Unknown";
        case InstrumentRole::Kick:           return "Kick";
        case InstrumentRole::Snare:          return "Snare";
        case InstrumentRole::HiHat:          return "HiHat";
        case InstrumentRole::Tom:            return "Tom";
        case InstrumentRole::Overhead:       return "Overhead";
        case InstrumentRole::RoomMic:        return "RoomMic";
        case InstrumentRole::BassGuitar:     return "BassGuitar";
        case InstrumentRole::BassAmp:        return "BassAmp";
        case InstrumentRole::ElectricGuitar: return "ElectricGuitar";
        case InstrumentRole::AcousticGuitar: return "AcousticGuitar";
        case InstrumentRole::Piano:          return "Piano";
        case InstrumentRole::Keys:           return "Keys";
        case InstrumentRole::Organ:          return "Organ";
        case InstrumentRole::Synth:          return "Synth";
        case InstrumentRole::LeadVocal:      return "LeadVocal";
        case InstrumentRole::BackingVocal:   return "BackingVocal";
        case InstrumentRole::Choir:          return "Choir";
        case InstrumentRole::Presenter:      return "Presenter";
        case InstrumentRole::Announcer:      return "Announcer";
        case InstrumentRole::Trumpet:        return "Trumpet";
        case InstrumentRole::Saxophone:      return "Saxophone";
        case InstrumentRole::Violin:         return "Violin";
        case InstrumentRole::Cello:          return "Cello";
        case InstrumentRole::DI:             return "DI";
        case InstrumentRole::Playback:       return "Playback";
        case InstrumentRole::Talkback:       return "Talkback";
        case InstrumentRole::FXReturn:       return "FXReturn";
        case InstrumentRole::Muted:          return "Muted";
        case InstrumentRole::NoSignal:       return "NoSignal";
    }
    return "Unknown";
}

inline InstrumentRole roleFromString(const std::string& s) {
    static const std::unordered_map<std::string, InstrumentRole> map = {
        {"Unknown", InstrumentRole::Unknown},
        {"Kick", InstrumentRole::Kick},
        {"Snare", InstrumentRole::Snare},
        {"HiHat", InstrumentRole::HiHat},
        {"Tom", InstrumentRole::Tom},
        {"Overhead", InstrumentRole::Overhead},
        {"RoomMic", InstrumentRole::RoomMic},
        {"BassGuitar", InstrumentRole::BassGuitar},
        {"BassAmp", InstrumentRole::BassAmp},
        {"ElectricGuitar", InstrumentRole::ElectricGuitar},
        {"AcousticGuitar", InstrumentRole::AcousticGuitar},
        {"Piano", InstrumentRole::Piano},
        {"Keys", InstrumentRole::Keys},
        {"Organ", InstrumentRole::Organ},
        {"Synth", InstrumentRole::Synth},
        {"LeadVocal", InstrumentRole::LeadVocal},
        {"BackingVocal", InstrumentRole::BackingVocal},
        {"Choir", InstrumentRole::Choir},
        {"Presenter", InstrumentRole::Presenter},
        {"Announcer", InstrumentRole::Announcer},
        {"Trumpet", InstrumentRole::Trumpet},
        {"Saxophone", InstrumentRole::Saxophone},
        {"Violin", InstrumentRole::Violin},
        {"Cello", InstrumentRole::Cello},
        {"DI", InstrumentRole::DI},
        {"Playback", InstrumentRole::Playback},
        {"Talkback", InstrumentRole::Talkback},
        {"FXReturn", InstrumentRole::FXReturn},
        {"Muted", InstrumentRole::Muted},
        {"NoSignal", InstrumentRole::NoSignal},
    };
    auto it = map.find(s);
    return it != map.end() ? it->second : InstrumentRole::Unknown;
}

inline std::string confidenceToString(DiscoveryConfidence c) {
    switch (c) {
        case DiscoveryConfidence::High:    return "High";
        case DiscoveryConfidence::Medium:  return "Medium";
        case DiscoveryConfidence::Low:     return "Low";
        case DiscoveryConfidence::Unknown: return "Unknown";
    }
    return "Unknown";
}

struct ChannelProfile {
    int         index = 0;          // 1-based, physical channel number
    std::string consoleName;        // raw name from console ("Kick", "CH 01", "")
    std::string normalisedName;     // cleaned up ("kick", "ch01", "")

    InstrumentRole      role       = InstrumentRole::Unknown;
    DiscoveryConfidence confidence = DiscoveryConfidence::Unknown;

    // Spectral fingerprint captured at discovery time
    struct Fingerprint {
        float dominantFreqHz    = 0;
        float spectralCentroid  = 0;
        float subBassEnergy     = -96;   // 20–80 Hz
        float bassEnergy        = -96;   // 80–250 Hz
        float lowMidEnergy      = -96;   // 250–500 Hz
        float midEnergy         = -96;   // 500–2k Hz
        float upperMidEnergy    = -96;   // 2k–6k Hz
        float presenceEnergy    = -96;   // 6k–10k Hz
        float airEnergy         = -96;   // 10k–20k Hz
        float highEnergy        = -96;   // alias for presenceEnergy
        float crestFactor       = 0;
        float averageRMS        = -96;
        bool  hasSignal         = false;
        bool  isPercussive      = false;  // high crest factor
        bool  isBroadband       = false;  // energy across all bands
        bool  isNarrowband      = false;  // energy concentrated in one region
    } fingerprint;

    // Current console state at discovery
    float   faderNorm    = 0.75f;
    bool    muted        = false;
    float   gainDB       = 0;
    bool    phantomPower = false;
    bool    phaseInvert  = false;
    float   highPassHz   = 0;    // current HPF setting (0 = off)

    // Group assignment (inferred from role)
    std::string group;           // "drums", "bass", "guitars", "vocals", etc.

    // Relationship data
    std::vector<int> likelyMaskingWith;  // channels we conflict with
    std::optional<int> stereoPair;       // if this is one of a L/R pair

    // Metadata
    std::chrono::steady_clock::time_point discoveredAt;
    std::chrono::steady_clock::time_point lastUpdated;
    std::string llmNotes;                // LLM's free-text observations
    bool manuallyOverridden = false;     // engineer corrected the inferred role
};
