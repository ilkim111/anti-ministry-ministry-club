#pragma once
#include <string>
#include <variant>
#include <functional>

// All channel parameters the system can read/write
enum class ChannelParam {
    Fader,          // 0.0–1.0 normalized
    Mute,           // bool
    Pan,            // -1.0 (L) to +1.0 (R)
    Name,           // string
    Gain,           // dB
    PhantomPower,   // bool (48V)
    PhaseInvert,    // bool
    // EQ
    EqOn,
    EqBand1Freq, EqBand1Gain, EqBand1Q, EqBand1Type,
    EqBand2Freq, EqBand2Gain, EqBand2Q, EqBand2Type,
    EqBand3Freq, EqBand3Gain, EqBand3Q, EqBand3Type,
    EqBand4Freq, EqBand4Gain, EqBand4Q, EqBand4Type,
    EqBand5Freq, EqBand5Gain, EqBand5Q,
    EqBand6Freq, EqBand6Gain, EqBand6Q,
    HighPassFreq,   // HPF frequency in Hz
    HighPassOn,     // HPF enabled
    // Dynamics
    CompThreshold, CompRatio, CompAttack, CompRelease, CompMakeup, CompOn,
    GateThreshold, GateRange, GateAttack, GateHold, GateRelease, GateOn,
    // Sends
    SendLevel,      // requires auxIndex
    SendPan,
    SendOn,
    // DCA assignment
    DCAAssign
};

// Bus/aux parameters
enum class BusParam {
    Fader, Mute, Pan, Name,
    EqOn,
    EqBand1Freq, EqBand1Gain, EqBand1Q,
    EqBand2Freq, EqBand2Gain, EqBand2Q,
    EqBand3Freq, EqBand3Gain, EqBand3Q,
    EqBand4Freq, EqBand4Gain, EqBand4Q,
    CompThreshold, CompRatio, CompAttack, CompRelease, CompOn
};

using ParamValue = std::variant<float, bool, int, std::string>;

struct ParameterUpdate {
    enum class Target { Channel, Bus, Main, DCA } target;
    int         index;     // 1-based channel/bus number
    int         auxIndex;  // for sends: which aux/bus
    ChannelParam param;    // which parameter changed
    ParamValue   value;    // new value
    std::string  strValue; // convenience for Name updates

    // Helper to get float value
    float floatVal() const {
        return std::holds_alternative<float>(value)
            ? std::get<float>(value) : 0.0f;
    }
    bool boolVal() const {
        return std::holds_alternative<bool>(value)
            ? std::get<bool>(value) : false;
    }
};

// Console capability descriptor
struct ConsoleCapabilities {
    std::string model;       // "X32", "Wing", "Avantis"
    std::string firmware;
    int  channelCount;       // input channels
    int  busCount;           // aux/mix buses
    int  matrixCount;
    int  dcaCount;
    int  fxSlots;
    int  eqBands;            // per channel
    bool hasMotorizedFaders;
    bool hasDynamicEq;
    bool hasMultibandComp;
    int  meterUpdateRateMs;  // how often meters refresh
};
