#pragma once
#include "IAudioCapture.hpp"

// No-op audio capture — used when no audio device is available.
// The system falls back to console meter data only.
class NullAudioCapture : public IAudioCapture {
public:
    bool open(const Config&) override { return true; }
    bool start() override { return true; }
    void stop() override {}
    bool isRunning() const override { return false; }
    void setCallback(AudioCallback) override {}
    std::vector<DeviceInfo> listDevices() const override { return {}; }
    std::string backendName() const override { return "null"; }
};
