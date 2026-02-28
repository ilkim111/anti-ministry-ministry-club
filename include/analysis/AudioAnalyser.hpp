#pragma once
#include "console/ConsoleModel.hpp"
#include <vector>
#include <cmath>
#include <algorithm>

// Analyses meter and spectral data from the console model.
// Runs on the DSP thread at ~50ms intervals.
class AudioAnalyser {
public:
    struct ChannelAnalysis {
        int   channel;
        float rmsDB;
        float peakDB;
        float crestFactor;       // peak - rms (dB)
        bool  isClipping;        // peak > -0.5 dBFS
        bool  isFeedbackRisk;    // sustained narrow-band energy spike
        float dominantFreqHz;
        float spectralCentroid;

        // Band energies (from console metering or FFT)
        float subBass;     // 20–80 Hz
        float bass;        // 80–250 Hz
        float lowMid;      // 250–500 Hz
        float mid;         // 500–2k Hz
        float upperMid;    // 2k–6k Hz
        float presence;    // 6k–10k Hz
        float air;         // 10k–20k Hz
    };

    struct MixAnalysis {
        std::vector<ChannelAnalysis> channels;

        // Mix bus
        float mainRmsDB     = -96;
        float mainPeakDB    = -96;
        bool  mainClipping  = false;

        // Issues detected
        std::vector<std::string> warnings;
        bool hasFeedbackRisk = false;
        bool hasClipping     = false;
        int  clippingChannel = 0;
    };

    MixAnalysis analyse(const ConsoleModel& model, int channelCount) {
        MixAnalysis result;

        for (int ch = 1; ch <= channelCount; ch++) {
            auto snap = model.channel(ch);
            ChannelAnalysis ca;
            ca.channel         = ch;
            ca.rmsDB           = snap.rmsDB;
            ca.peakDB          = snap.peakDB;
            ca.crestFactor     = snap.peakDB - snap.rmsDB;
            ca.isClipping      = snap.peakDB > -0.5f;
            ca.dominantFreqHz  = snap.spectral.spectralCentroid;
            ca.spectralCentroid = snap.spectral.spectralCentroid;
            ca.bass            = snap.spectral.bass;
            ca.mid             = snap.spectral.mid;
            ca.presence        = snap.spectral.presence;

            // Feedback detection heuristic:
            // Sustained narrow-band energy > -6dB with low crest factor
            ca.isFeedbackRisk = false;
            if (snap.rmsDB > -10.0f && ca.crestFactor < 3.0f) {
                // Very low crest factor + high level = possible feedback
                ca.isFeedbackRisk = true;
                result.hasFeedbackRisk = true;
                result.warnings.push_back(
                    "Possible feedback on ch" + std::to_string(ch) +
                    " at " + std::to_string(int(ca.dominantFreqHz)) + "Hz");
            }

            if (ca.isClipping) {
                result.hasClipping = true;
                result.clippingChannel = ch;
                result.warnings.push_back(
                    "Clipping on ch" + std::to_string(ch) +
                    " (peak=" + std::to_string(int(ca.peakDB)) + "dBFS)");
            }

            result.channels.push_back(ca);
        }

        return result;
    }

    // Check for masking between two channels in a specific frequency range
    struct MaskingResult {
        bool  isMasking;
        float overlapDB;      // how much energy overlaps
        float suggestedCutHz; // where to cut on the less important channel
        float suggestedCutDB; // how much to cut
    };

    MaskingResult checkMasking(const ChannelAnalysis& a,
                                const ChannelAnalysis& b) {
        MaskingResult r{false, 0, 0, 0};

        // Compare bass energy overlap (kick vs bass guitar problem)
        float bassOverlap = std::min(a.bass, b.bass);
        if (bassOverlap > -15.0f && std::abs(a.bass - b.bass) < 6.0f) {
            r.isMasking = true;
            r.overlapDB = bassOverlap;
            // Suggest cutting the channel with less bass role
            r.suggestedCutHz = 200.0f;
            r.suggestedCutDB = -3.0f;
        }

        // Compare mid energy overlap (guitar vs vocal)
        float midOverlap = std::min(a.mid, b.mid);
        if (midOverlap > -12.0f && std::abs(a.mid - b.mid) < 4.0f) {
            r.isMasking = true;
            r.overlapDB = midOverlap;
            r.suggestedCutHz = 2000.0f;
            r.suggestedCutDB = -2.0f;
        }

        return r;
    }
};
