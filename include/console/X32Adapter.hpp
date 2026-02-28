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
    void sendOsc(const std::string& address, const std::vector<ParamValue>& args);
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
