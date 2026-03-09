#pragma once
#include "IConsoleAdapter.hpp"
#include <string>
#include <thread>
#include <atomic>

// Behringer X32 / Midas M32 adapter — communicates via OSC over UDP
class X32Adapter : public IConsoleAdapter {
public:
    X32Adapter();
    ~X32Adapter() override;

    bool connect(const std::string& ip, int port) override;
    void disconnect() override;
    bool isConnected() const override;

    ConsoleCapabilities capabilities() const override;
    void requestFullSync() override;

    void setChannelParam(int ch, ChannelParam param, float value) override;
    void setChannelParam(int ch, ChannelParam param, bool value) override;
    void setChannelParam(int ch, ChannelParam param, const std::string& value) override;
    void setSendLevel(int ch, int bus, float value) override;
    void setBusParam(int bus, BusParam param, float value) override;

    void subscribeMeter(int refreshMs) override;
    void unsubscribeMeter() override;

    void tick() override;

private:
    // OSC message construction
    std::vector<uint8_t> buildOscMessage(const std::string& address,
                                          const std::vector<ParamValue>& args);
    void sendOscQuery(const std::string& address);  // no-arg query
    void sendOsc(const std::string& address, float value);
    void sendOsc(const std::string& address, int value);
    void sendOsc(const std::string& address, const std::string& value);
    void sendRaw(const std::vector<uint8_t>& data);

    // OSC address builders for X32 paths
    std::string channelPath(int ch, const std::string& suffix);
    std::string busPath(int bus, const std::string& suffix);

    // Receive thread
    void receiveLoop();
    void parseOscMessage(const uint8_t* data, size_t len);
    void handleParameterMessage(const std::string& address, const ParamValue& value);
    void handleMeterMessage(const uint8_t* data, size_t len);

    // X32-specific: keepalive via /xremote and meter renewals
    void sendKeepalive();
    void renewMeterSubscription();

    // X32 parameter normalization (human-readable → 0.0-1.0 OSC float)
    // All X32 float params are sent as normalized [0.0, 1.0] over OSC.
    static float eqFreqToNorm(float hz);      // logf [20, 20000, 201]
    static float eqGainToNorm(float db);       // linf [-15, 15, 0.250]
    static float eqQToNorm(float q);           // logf [10.0, 0.3, 72]
    static float hpfFreqToNorm(float hz);      // logf [20, 400, 101]
    static float compThreshToNorm(float db);   // linf [-60, 0, 0.500]
    static int   compRatioToEnum(float ratio);  // enum [0..11]
    static float gateThreshToNorm(float db);   // linf [-80, 0, 0.500]

    // Reverse: normalized 0.0-1.0 → human-readable
    static float normToEqFreq(float norm);
    static float normToEqGain(float norm);
    static float normToEqQ(float norm);
    static float normToHpfFreq(float norm);

    int  sockFd_ = -1;
    std::string ip_;
    int  port_ = 10023;   // X32 default OSC port
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    std::thread recvThread_;

    // Keepalive timer
    std::chrono::steady_clock::time_point lastKeepalive_;
    std::chrono::steady_clock::time_point lastMeterRenew_;
    bool metering_ = false;
    int  meterRefreshMs_ = 50;
};
