#pragma once
#include "IConsoleAdapter.hpp"
#include <string>
#include <thread>
#include <atomic>

// Behringer Wing adapter — same OSC transport, different address scheme
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
    void sendRaw(const std::vector<uint8_t>& data);

    // Wing uses /ch/XX/... with 1-based indexing and different parameter paths
    std::string channelPath(int ch, const std::string& suffix);
    std::string busPath(int bus, const std::string& suffix);

    void receiveLoop();
    void parseOscMessage(const uint8_t* data, size_t len);
    void sendKeepalive();

    int  sockFd_ = -1;
    std::string ip_;
    int  port_ = 2222;    // Wing default OSC port
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    std::thread recvThread_;

    std::chrono::steady_clock::time_point lastKeepalive_;
    bool metering_ = false;
};
