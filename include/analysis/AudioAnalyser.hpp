#pragma once
#include "console/ConsoleModel.hpp"
#include "audio/FFTAnalyser.hpp"
#include <vector>
#include <cmath>
#include <algorithm>
#include <string>
#include <mutex>

// Analyses meter and spectral data from the console model.
// Runs on the DSP thread at ~50ms intervals.
//
// When audio capture is available, it receives real FFT results
// and produces detailed spectral analysis. Without audio capture,
// it falls back to console meter data (RMS/peak only).
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

        // Band energies (from FFT or console metering)
        float subBass  = -96.0f;  // 20–80 Hz
        float bass     = -96.0f;  // 80–250 Hz
        float lowMid   = -96.0f;  // 250–500 Hz
        float mid      = -96.0f;  // 500–2k Hz
        float upperMid = -96.0f;  // 2k–6k Hz
        float presence = -96.0f;  // 6k–10k Hz
        float air      = -96.0f;  // 10k–20k Hz

        bool  hasFFTData = false; // true if populated from real FFT
    };

    struct MixAnalysis {
        std::vector<ChannelAnalysis> channels;

        // Mix bus
        float mainRmsDB     = -96;
        float mainPeakDB    = -96;
        bool  mainClipping  = false;

        // Issues detected — these become the smart summary for LLM
        std::vector<std::string> warnings;
        bool hasFeedbackRisk = false;
        bool hasClipping     = false;
        int  clippingChannel = 0;
    };

    // High-level issue for LLM consumption (concise, actionable)
    struct MixIssue {
        enum class Type {
            Clipping,
            FeedbackRisk,
            Masking,
            Boomy,       // excess low-mid energy
            Harsh,       // excess upper-mid energy
            Thin,        // lacking mid/presence
            Muddy,       // excess bass buildup across mix
            NoHeadroom   // main bus close to clipping
        };
        Type type;
        int  channel    = 0;
        int  channel2   = 0;    // second channel for masking
        float freqHz    = 0.0f; // relevant frequency
        float severity  = 0.0f; // 0-1, how bad
        std::string description;
    };

    // Feed real FFT result for a channel (called from DSP thread after FFT)
    void updateFFT(int channel, const FFTAnalyser::Result& fftResult) {
        std::lock_guard lock(fftMtx_);
        if (channel < 1) return;
        if (channel > (int)fftResults_.size())
            fftResults_.resize(channel);
        fftResults_[channel - 1] = fftResult;
        fftResults_[channel - 1].hasSignal = true;
        hasFFTData_ = true;
    }

    bool hasFFTData() const { return hasFFTData_; }

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

            // If we have real FFT data for this channel, use it
            FFTAnalyser::Result fft;
            bool haveFFT = false;
            {
                std::lock_guard lock(fftMtx_);
                if (ch <= (int)fftResults_.size() &&
                    fftResults_[ch - 1].hasSignal) {
                    fft = fftResults_[ch - 1];
                    haveFFT = true;
                }
            }

            if (haveFFT) {
                ca.hasFFTData       = true;
                ca.dominantFreqHz   = fft.dominantFreqHz;
                ca.spectralCentroid = fft.spectralCentroid;
                ca.subBass          = fft.bands.subBass;
                ca.bass             = fft.bands.bass;
                ca.lowMid           = fft.bands.lowMid;
                ca.mid              = fft.bands.mid;
                ca.upperMid         = fft.bands.upperMid;
                ca.presence         = fft.bands.presence;
                ca.air              = fft.bands.air;
                // Use FFT-derived levels if they're more accurate
                if (fft.rmsDB > -95.0f) {
                    ca.rmsDB = fft.rmsDB;
                    ca.peakDB = fft.peakDB;
                    ca.crestFactor = fft.crestFactor;
                    ca.isClipping = fft.peakDB > -0.5f;
                }
            } else {
                // Fall back to console model's spectral data (if any)
                ca.dominantFreqHz   = snap.spectral.spectralCentroid;
                ca.spectralCentroid = snap.spectral.spectralCentroid;
                ca.bass             = snap.spectral.bass;
                ca.mid              = snap.spectral.mid;
                ca.presence         = snap.spectral.presence;
            }

            // Feedback detection — much better with FFT
            ca.isFeedbackRisk = false;
            if (haveFFT) {
                // Feedback = sustained narrow peak well above surrounding energy
                // Low crest factor (peak ≈ RMS) means a pure sinusoidal tone
                if (ca.rmsDB > -12.0f && ca.crestFactor < 3.0f) {
                    ca.isFeedbackRisk = true;
                    result.hasFeedbackRisk = true;
                    result.warnings.push_back(
                        "Feedback risk ch" + std::to_string(ch) +
                        " @" + std::to_string(int(ca.dominantFreqHz)) + "Hz"
                        " (crest=" + std::to_string(int(ca.crestFactor)) + "dB)");
                }
            } else {
                // Heuristic fallback from meter data
                if (snap.rmsDB > -10.0f && ca.crestFactor < 3.0f) {
                    ca.isFeedbackRisk = true;
                    result.hasFeedbackRisk = true;
                    result.warnings.push_back(
                        "Possible feedback ch" + std::to_string(ch));
                }
            }

            if (ca.isClipping) {
                result.hasClipping = true;
                result.clippingChannel = ch;
                result.warnings.push_back(
                    "Clipping ch" + std::to_string(ch) +
                    " (peak=" + std::to_string(int(ca.peakDB)) + "dBFS)");
            }

            result.channels.push_back(ca);
        }

        return result;
    }

    // Generate concise, actionable issues for LLM consumption.
    // This is the "smart summary" — only includes things the LLM should act on.
    std::vector<MixIssue> detectIssues(const MixAnalysis& analysis) {
        std::vector<MixIssue> issues;

        for (auto& ch : analysis.channels) {
            if (ch.rmsDB < -60.0f) continue; // skip silent channels

            // Clipping
            if (ch.isClipping) {
                issues.push_back({
                    MixIssue::Type::Clipping, ch.channel, 0,
                    0, std::min(1.0f, (ch.peakDB + 3.0f) / 3.0f),
                    "ch" + std::to_string(ch.channel) +
                    " clipping (peak " + fmtDB(ch.peakDB) + ")"
                });
            }

            // Feedback risk
            if (ch.isFeedbackRisk) {
                issues.push_back({
                    MixIssue::Type::FeedbackRisk, ch.channel, 0,
                    ch.dominantFreqHz,
                    std::min(1.0f, (-ch.crestFactor + 6.0f) / 6.0f),
                    "ch" + std::to_string(ch.channel) +
                    " feedback risk @" + std::to_string(int(ch.dominantFreqHz)) + "Hz"
                });
            }

            if (!ch.hasFFTData) continue; // below needs real FFT

            // Boomy: excess low-mid energy
            if (ch.lowMid > -12.0f && ch.lowMid > ch.mid + 6.0f) {
                issues.push_back({
                    MixIssue::Type::Boomy, ch.channel, 0,
                    350.0f, std::min(1.0f, (ch.lowMid + 6.0f) / 12.0f),
                    "ch" + std::to_string(ch.channel) +
                    " boomy (low-mid " + fmtDB(ch.lowMid) + ")"
                });
            }

            // Harsh: excess upper-mid (2-6kHz) energy
            if (ch.upperMid > -10.0f && ch.upperMid > ch.mid + 4.0f) {
                issues.push_back({
                    MixIssue::Type::Harsh, ch.channel, 0,
                    3500.0f, std::min(1.0f, (ch.upperMid + 6.0f) / 12.0f),
                    "ch" + std::to_string(ch.channel) +
                    " harsh (upper-mid " + fmtDB(ch.upperMid) + ")"
                });
            }

            // Thin: lacking mid/presence energy relative to bass
            if (ch.presence < -30.0f && ch.bass > -15.0f &&
                ch.bass - ch.presence > 15.0f) {
                issues.push_back({
                    MixIssue::Type::Thin, ch.channel, 0,
                    5000.0f, std::min(1.0f, (ch.bass - ch.presence) / 20.0f),
                    "ch" + std::to_string(ch.channel) +
                    " thin (presence " + fmtDB(ch.presence) + ")"
                });
            }
        }

        // Masking detection: compare all active channel pairs
        for (size_t i = 0; i < analysis.channels.size(); i++) {
            auto& a = analysis.channels[i];
            if (a.rmsDB < -40.0f || !a.hasFFTData) continue;

            for (size_t j = i + 1; j < analysis.channels.size(); j++) {
                auto& b = analysis.channels[j];
                if (b.rmsDB < -40.0f || !b.hasFFTData) continue;

                auto masking = checkMasking(a, b);
                if (masking.isMasking) {
                    issues.push_back({
                        MixIssue::Type::Masking,
                        a.channel, b.channel,
                        masking.suggestedCutHz,
                        std::min(1.0f, (masking.overlapDB + 12.0f) / 12.0f),
                        "ch" + std::to_string(a.channel) + " & ch" +
                        std::to_string(b.channel) + " masking @" +
                        std::to_string(int(masking.suggestedCutHz)) + "Hz"
                    });
                }
            }
        }

        return issues;
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
            r.suggestedCutHz = 200.0f;
            r.suggestedCutDB = -3.0f;
        }

        // Compare low-mid overlap (guitar vs keys)
        float lowMidOverlap = std::min(a.lowMid, b.lowMid);
        if (lowMidOverlap > -12.0f && std::abs(a.lowMid - b.lowMid) < 5.0f) {
            r.isMasking = true;
            r.overlapDB = lowMidOverlap;
            r.suggestedCutHz = 400.0f;
            r.suggestedCutDB = -2.5f;
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

private:
    static std::string fmtDB(float db) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.1fdB", db);
        return buf;
    }

    std::mutex fftMtx_;
    std::vector<FFTAnalyser::Result> fftResults_;
    bool hasFFTData_ = false;
};
