#pragma once
#include "ParameterTypes.hpp"
#include <functional>
#include <string>
#include <vector>

// Abstract interface — every console adapter implements this
class IConsoleAdapter {
public:
    virtual ~IConsoleAdapter() = default;

    // Connection lifecycle
    virtual bool connect(const std::string& ip, int port) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;

    // Console info
    virtual ConsoleCapabilities capabilities() const = 0;

    // Full state dump — requests all channel/bus parameters
    virtual void requestFullSync() = 0;

    // Channel parameters
    virtual void setChannelParam(int ch, ChannelParam param, float value) = 0;
    virtual void setChannelParam(int ch, ChannelParam param, bool value) = 0;
    virtual void setChannelParam(int ch, ChannelParam param, const std::string& value) = 0;

    // Send levels (channel -> bus)
    virtual void setSendLevel(int ch, int bus, float value) = 0;

    // Bus parameters
    virtual void setBusParam(int bus, BusParam param, float value) = 0;

    // Metering — subscribe to meter updates
    virtual void subscribeMeter(int refreshMs) = 0;
    virtual void unsubscribeMeter() = 0;

    // Callbacks
    std::function<void(const ParameterUpdate&)> onParameterUpdate;
    std::function<void(int ch, float rmsDB, float peakDB)> onMeterUpdate;
    std::function<void(bool connected)> onConnectionChange;

    // Keepalive — must be called periodically to maintain connection
    virtual void tick() = 0;
};
