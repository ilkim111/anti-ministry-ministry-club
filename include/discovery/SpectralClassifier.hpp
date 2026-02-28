#pragma once
#include "ChannelProfile.hpp"
#include <vector>
#include <cmath>
#include <algorithm>

class SpectralClassifier {
    struct SpectralProfile {
        InstrumentRole role;
        std::string    group;
        struct BandExpectation {
            float minDB, maxDB;
            float weight;
        };
        BandExpectation sub;        // 20–80Hz
        BandExpectation bass;       // 80–250Hz
        BandExpectation mid;        // 500Hz–2kHz
        BandExpectation presence;   // 6–10kHz
        float minCrestFactor;
        float maxCrestFactor;
    };

    std::vector<SpectralProfile> profiles_;

public:
    SpectralClassifier() { buildProfiles(); }

    struct Result {
        InstrumentRole role;
        std::string    group;
        float          matchScore;  // 0.0–1.0
    };

    Result classify(const ChannelProfile::Fingerprint& fp) const {
        if (!fp.hasSignal)
            return {InstrumentRole::NoSignal, "inactive", 0.0f};

        float bestScore = 0;
        const SpectralProfile* bestMatch = nullptr;

        for (auto& profile : profiles_) {
            float score = matchScore(fp, profile);
            if (score > bestScore) {
                bestScore = score;
                bestMatch = &profile;
            }
        }

        if (!bestMatch || bestScore < 0.4f)
            return {InstrumentRole::Unknown, "unknown", bestScore};

        return {bestMatch->role, bestMatch->group, bestScore};
    }

private:
    float matchScore(const ChannelProfile::Fingerprint& fp,
                     const SpectralProfile& profile) const {
        float totalWeight = 0, weightedScore = 0;

        auto scoreBand = [&](float energy,
                             const SpectralProfile::BandExpectation& exp) {
            if (exp.weight == 0) return;
            float score;
            if (energy >= exp.minDB && energy <= exp.maxDB) {
                score = 1.0f;
            } else {
                float dist = std::min(std::abs(energy - exp.minDB),
                                      std::abs(energy - exp.maxDB));
                score = std::max(0.0f, 1.0f - (dist / 12.0f));
            }
            weightedScore += score * exp.weight;
            totalWeight   += exp.weight;
        };

        scoreBand(fp.subBassEnergy,    profile.sub);
        scoreBand(fp.bassEnergy,       profile.bass);
        scoreBand(fp.midEnergy,        profile.mid);
        scoreBand(fp.presenceEnergy,   profile.presence);

        // Crest factor scoring
        if (fp.crestFactor >= profile.minCrestFactor &&
            fp.crestFactor <= profile.maxCrestFactor) {
            weightedScore += 2.0f;
        }
        totalWeight += 2.0f;

        return totalWeight > 0 ? weightedScore / totalWeight : 0;
    }

    void buildProfiles() {
        // Kick: strong sub/bass, percussive, minimal high end
        profiles_.push_back({
            InstrumentRole::Kick, "drums",
            {-10, 0, 2.0f},     // sub: strong
            {-10, 0, 2.0f},     // bass: strong
            {-30,-10,1.0f},     // mid: low
            {-40,-15,0.5f},     // presence: minimal
            8.0f, 30.0f
        });

        // Snare: strong mid, percussive
        profiles_.push_back({
            InstrumentRole::Snare, "drums",
            {-40,-20,1.0f},     // sub: low
            {-20,-5, 1.0f},     // bass: moderate
            {-10, 2, 2.0f},     // mid: strong
            {-20,-5, 1.5f},     // presence: wires
            10.0f, 35.0f
        });

        // Hi-hat: mostly high frequency energy
        profiles_.push_back({
            InstrumentRole::HiHat, "drums",
            {-70,-40,1.0f},     // sub: none
            {-60,-30,1.0f},     // bass: none
            {-30,-10,1.0f},     // mid: low
            {-5,  5, 2.5f},    // presence: very strong
            15.0f, 40.0f
        });

        // Bass guitar: strong bass/low-mid, sustained
        profiles_.push_back({
            InstrumentRole::BassGuitar, "bass",
            {-5,  5, 1.5f},    // sub: very strong
            {-5,  5, 2.0f},    // bass: dominant
            {-20,-5, 1.0f},    // mid: some presence
            {-45,-20,0.5f},    // presence: minimal
            2.0f, 8.0f
        });

        // Lead vocal: concentrated mid/upper-mid
        profiles_.push_back({
            InstrumentRole::LeadVocal, "vocals",
            {-50,-25,0.5f},    // sub: very low
            {-25,-5, 1.0f},    // bass: some warmth
            {-10, 3, 2.0f},    // mid: dominant
            {-20,-5, 1.5f},    // presence: clarity
            4.0f, 12.0f
        });

        // Electric guitar: mid-heavy
        profiles_.push_back({
            InstrumentRole::ElectricGuitar, "guitars",
            {-60,-30,1.0f},    // sub: very low
            {-30,-10,1.0f},    // bass: low
            {-5,  5, 2.0f},    // mid: dominant
            {-20,-5, 1.0f},    // presence: moderate
            3.0f, 10.0f
        });

        // Acoustic guitar: broad midrange
        profiles_.push_back({
            InstrumentRole::AcousticGuitar, "guitars",
            {-50,-30,1.0f},    // sub: minimal
            {-20,-5, 1.5f},    // bass: body
            {-10, 3, 2.0f},    // mid: strong
            {-15, 0, 1.5f},    // presence: string attack
            4.0f, 12.0f
        });

        // Piano: broad, full range
        profiles_.push_back({
            InstrumentRole::Piano, "keys",
            {-30,-10,1.0f},    // sub: some
            {-15,-5, 1.5f},    // bass: present
            {-10, 0, 2.0f},    // mid: strong
            {-15,-5, 1.5f},    // presence: brightness
            5.0f, 15.0f
        });

        // Overheads: broadband, lots of high end
        profiles_.push_back({
            InstrumentRole::Overhead, "drums",
            {-30,-10,1.0f},    // sub: some bleed
            {-25,-10,1.0f},    // bass: bleed
            {-15,-5, 1.5f},    // mid: moderate
            {-5,  5, 2.0f},    // presence: cymbals
            6.0f, 20.0f
        });

        // Tom: similar to kick but more mid
        profiles_.push_back({
            InstrumentRole::Tom, "drums",
            {-15,-5, 1.5f},    // sub: moderate
            {-10, 0, 2.0f},    // bass: strong body
            {-15, 0, 1.5f},    // mid: attack
            {-30,-10,0.5f},    // presence: minimal
            8.0f, 25.0f
        });
    }
};
