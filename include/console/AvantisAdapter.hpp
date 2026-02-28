#pragma once
#include "IConsoleAdapter.hpp"
#include <string>
#include <thread>
#include <atomic>

// Allen & Heath Avantis adapter — TCP-based protocol
class AvantisAdapter : public IConsoleAdapter {
public:
    AvantisAdapter();
    ~AvantisAdapter() override;

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
    // Avantis uses a proprietary TCP protocol
    void sendCommand(uint16_t msgType, const std::vector<uint8_t>& payload);
    std::vector<uint8_t> buildSetParam(int ch, uint16_t paramId, float value);

    // A&H parameter ID mapping
    uint16_t paramToAvantisId(ChannelParam param) const;

    void receiveLoop();
    void parseMessage(const uint8_t* data, size_t len);
    void sendKeepalive();

    int  sockFd_ = -1;
    std::string ip_;
    int  port_ = 51325;    // Avantis default TCP port
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    std::thread recvThread_;

    std::chrono::steady_clock::time_point lastKeepalive_;
    bool metering_ = false;
};
