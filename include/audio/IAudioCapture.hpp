#pragma once
#include <functional>
#include <string>
#include <vector>

// Abstract audio capture interface.
// Implementations: PortAudioCapture (ASIO/CoreAudio), NullAudioCapture (no-op).
class IAudioCapture {
public:
    virtual ~IAudioCapture() = default;

    struct DeviceInfo {
        int         id;
        std::string name;
        int         maxInputChannels;
        double      defaultSampleRate;
    };

    struct Config {
        int    deviceId      = -1;    // -1 = default device
        int    channelCount  = 32;
        double sampleRate    = 48000;
        int    framesPerBlock = 1024; // FFT size
    };

    // Lifecycle
    virtual bool open(const Config& config) = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;

    // Called from DSP thread to consume captured audio.
    // Returns the number of frames available per channel.
    // Buffer is interleaved: [ch0_s0, ch1_s0, ..., ch0_s1, ch1_s1, ...]
    // Or per-channel blocks depending on implementation.
    using AudioCallback = std::function<void(const float* const* channelData,
                                             int channelCount,
                                             int frameCount)>;
    virtual void setCallback(AudioCallback cb) = 0;

    // Device enumeration
    virtual std::vector<DeviceInfo> listDevices() const = 0;

    // Name for logging
    virtual std::string backendName() const = 0;
};
