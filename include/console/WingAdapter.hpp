#pragma once
#include "IConsoleAdapter.hpp"
#include <string>
#include <thread>
#include <atomic>
#include <chrono>

// Behringer Wing adapter — OSC on port 2223, different address scheme from X32
// Wing uses /ch/N/fdr (dB values), /ch/N/mute (1=muted), etc.
class WingAdapter : public IConsoleAdapter {
public:
    WingAdapter();
    ~WingAdapter() override;

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
    void sendOsc(const std::string& address, float value);
    void sendOsc(const std::string& address, int value);
    void sendOsc(const std::string& address, const std::string& value);
    void sendOscQuery(const std::string& address);
    void sendRaw(const std::vector<uint8_t>& data);

    // Wing uses /ch/N/... (1-based, no zero-padding)
    std::string channelPath(int ch, const std::string& suffix);
    std::string busPath(int bus, const std::string& suffix);

    void receiveLoop();
    void parseOscMessage(const uint8_t* data, size_t len);
    void handleParameterMessage(const std::string& address, const ParamValue& value);
    void sendKeepalive();

    int  sockFd_ = -1;
    std::string ip_;
    int  port_ = 2223;    // Wing OSC port (2222=native, 2223=OSC)
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    std::thread recvThread_;

    std::chrono::steady_clock::time_point lastKeepalive_;
    std::chrono::steady_clock::time_point lastMeterRenew_;
    bool metering_ = false;
};
