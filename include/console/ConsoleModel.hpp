#pragma once
#include "ParameterTypes.hpp"
#include <string>
#include <vector>
#include <shared_mutex>
#include <cmath>

// Thread-safe snapshot of a single channel's state
struct ChannelSnapshot {
    int         index = 0;
    std::string name;
    float       fader    = 0.75f;   // 0.0–1.0 normalized
    bool        muted    = false;
    float       pan      = 0.0f;    // -1.0 (L) to +1.0 (R)
    float       gainDB   = 0.0f;
    bool        phantom  = false;
    bool        phase    = false;

    // EQ state
    struct EqBand {
        float freq = 1000.0f;
        float gain = 0.0f;
        float q    = 1.0f;
        int   type = 0;  // 0=bell, 1=shelf, 2=hpf, 3=lpf
    };
    bool    eqOn = true;
    EqBand  eq[6] = {};
    float   hpfFreq = 0.0f;
    bool    hpfOn   = false;

    // Dynamics
    struct Compressor {
        float threshold = 0.0f;
        float ratio     = 1.0f;
        float attack    = 10.0f;
        float release   = 100.0f;
        float makeup    = 0.0f;
        bool  on        = false;
    } comp;

    struct Gate {
        float threshold = -80.0f;
        float range     = -80.0f;
        float attack    = 0.5f;
        float hold      = 50.0f;
        float release   = 200.0f;
        bool  on        = false;
    } gate;

    // Metering (updated by meter subscription)
    float rmsDB  = -96.0f;
    float peakDB = -96.0f;

    // Spectral analysis data (updated by DSP thread)
    struct SpectralData {
        float bass       = -96.0f;
        float mid        = -96.0f;
        float presence   = -96.0f;
        float crestFactor    = 0.0f;
        float spectralCentroid = 0.0f;
    } spectral;

    // Send levels to buses
    std::vector<float> sends;
};

struct BusSnapshot {
    int         index = 0;
    std::string name;
    float       fader = 0.75f;
    bool        muted = false;
    float       pan   = 0.0f;
};

// Central state model — single source of truth for all console state.
// Updated by the adapter callbacks, queried by agent and UI threads.
class ConsoleModel {
    std::vector<ChannelSnapshot> channels_;
    std::vector<BusSnapshot>     buses_;
    mutable std::shared_mutex    mtx_;

public:
    void init(int channelCount, int busCount) {
        std::unique_lock lock(mtx_);
        channels_.resize(channelCount);
        buses_.resize(busCount);
        for (int i = 0; i < channelCount; i++) {
            channels_[i].index = i + 1;
            channels_[i].sends.resize(busCount, 0.0f);
        }
        for (int i = 0; i < busCount; i++)
            buses_[i].index = i + 1;
    }

    ChannelSnapshot channel(int ch) const {
        std::shared_lock lock(mtx_);
        return channels_.at(ch - 1);
    }

    BusSnapshot bus(int b) const {
        std::shared_lock lock(mtx_);
        return buses_.at(b - 1);
    }

    int channelCount() const {
        std::shared_lock lock(mtx_);
        return static_cast<int>(channels_.size());
    }

    int busCount() const {
        std::shared_lock lock(mtx_);
        return static_cast<int>(buses_.size());
    }

    // Apply an incoming parameter update from the console adapter
    void applyUpdate(const ParameterUpdate& u) {
        std::unique_lock lock(mtx_);
        if (u.target == ParameterUpdate::Target::Channel) {
            if (u.index < 1 || u.index > (int)channels_.size()) return;
            auto& ch = channels_[u.index - 1];
            applyChannelParam(ch, u);
        } else if (u.target == ParameterUpdate::Target::Bus) {
            if (u.index < 1 || u.index > (int)buses_.size()) return;
            auto& bus = buses_[u.index - 1];
            applyBusParam(bus, u);
        }
    }

    // Update meter values from metering callback
    void updateMeter(int ch, float rmsDB, float peakDB) {
        std::unique_lock lock(mtx_);
        if (ch < 1 || ch > (int)channels_.size()) return;
        channels_[ch - 1].rmsDB  = rmsDB;
        channels_[ch - 1].peakDB = peakDB;
    }

    // Update spectral data from DSP analysis thread
    void updateSpectral(int ch, const ChannelSnapshot::SpectralData& data) {
        std::unique_lock lock(mtx_);
        if (ch < 1 || ch > (int)channels_.size()) return;
        channels_[ch - 1].spectral = data;
    }

    // Snapshot all channels (for LLM context building)
    std::vector<ChannelSnapshot> allChannels() const {
        std::shared_lock lock(mtx_);
        return channels_;
    }

private:
    void applyChannelParam(ChannelSnapshot& ch, const ParameterUpdate& u) {
        switch (u.param) {
            case ChannelParam::Fader:       ch.fader    = u.floatVal(); break;
            case ChannelParam::Mute:        ch.muted    = u.boolVal();  break;
            case ChannelParam::Pan:         ch.pan      = u.floatVal(); break;
            case ChannelParam::Name:        ch.name     = u.strValue;   break;
            case ChannelParam::Gain:        ch.gainDB   = u.floatVal(); break;
            case ChannelParam::PhantomPower:ch.phantom  = u.boolVal();  break;
            case ChannelParam::PhaseInvert: ch.phase    = u.boolVal();  break;
            case ChannelParam::EqOn:        ch.eqOn     = u.boolVal();  break;
            case ChannelParam::HighPassFreq:ch.hpfFreq  = u.floatVal(); break;
            case ChannelParam::HighPassOn:  ch.hpfOn    = u.boolVal();  break;
            case ChannelParam::EqBand1Freq: ch.eq[0].freq = u.floatVal(); break;
            case ChannelParam::EqBand1Gain: ch.eq[0].gain = u.floatVal(); break;
            case ChannelParam::EqBand1Q:    ch.eq[0].q    = u.floatVal(); break;
            case ChannelParam::EqBand2Freq: ch.eq[1].freq = u.floatVal(); break;
            case ChannelParam::EqBand2Gain: ch.eq[1].gain = u.floatVal(); break;
            case ChannelParam::EqBand2Q:    ch.eq[1].q    = u.floatVal(); break;
            case ChannelParam::EqBand3Freq: ch.eq[2].freq = u.floatVal(); break;
            case ChannelParam::EqBand3Gain: ch.eq[2].gain = u.floatVal(); break;
            case ChannelParam::EqBand3Q:    ch.eq[2].q    = u.floatVal(); break;
            case ChannelParam::EqBand4Freq: ch.eq[3].freq = u.floatVal(); break;
            case ChannelParam::EqBand4Gain: ch.eq[3].gain = u.floatVal(); break;
            case ChannelParam::EqBand4Q:    ch.eq[3].q    = u.floatVal(); break;
            case ChannelParam::CompThreshold: ch.comp.threshold = u.floatVal(); break;
            case ChannelParam::CompRatio:     ch.comp.ratio     = u.floatVal(); break;
            case ChannelParam::CompAttack:    ch.comp.attack    = u.floatVal(); break;
            case ChannelParam::CompRelease:   ch.comp.release   = u.floatVal(); break;
            case ChannelParam::CompMakeup:    ch.comp.makeup    = u.floatVal(); break;
            case ChannelParam::CompOn:        ch.comp.on        = u.boolVal();  break;
            case ChannelParam::GateThreshold: ch.gate.threshold = u.floatVal(); break;
            case ChannelParam::GateRange:     ch.gate.range     = u.floatVal(); break;
            case ChannelParam::GateAttack:    ch.gate.attack    = u.floatVal(); break;
            case ChannelParam::GateHold:      ch.gate.hold      = u.floatVal(); break;
            case ChannelParam::GateRelease:   ch.gate.release   = u.floatVal(); break;
            case ChannelParam::GateOn:        ch.gate.on        = u.boolVal();  break;
            case ChannelParam::SendLevel:
                if (u.auxIndex >= 1 && u.auxIndex <= (int)ch.sends.size())
                    ch.sends[u.auxIndex - 1] = u.floatVal();
                break;
            default: break;
        }
    }

    void applyBusParam(BusSnapshot& bus, const ParameterUpdate& u) {
        // Bus params use the same ChannelParam enum for simplicity
        switch (u.param) {
            case ChannelParam::Fader: bus.fader = u.floatVal(); break;
            case ChannelParam::Mute:  bus.muted = u.boolVal();  break;
            case ChannelParam::Pan:   bus.pan   = u.floatVal(); break;
            case ChannelParam::Name:  bus.name  = u.strValue;   break;
            default: break;
        }
    }
};
