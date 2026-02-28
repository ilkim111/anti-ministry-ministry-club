#pragma once
#include "IAudioCapture.hpp"
#include "RingBuffer.hpp"
#include <vector>
#include <memory>
#include <string>
#include <atomic>

// PortAudio-based audio capture. Supports ASIO (Windows), Core Audio (macOS),
// ALSA/PulseAudio (Linux). Link with -lportaudio.
//
// Audio data flows:
//   PortAudio callback (real-time thread)
//       → per-channel RingBuffer
//           → DSP thread reads via consumeChannels()
//
// The callback writes deinterleaved samples into per-channel ring buffers.
// The DSP thread periodically drains the buffers for FFT analysis.

// Forward declare PortAudio types to avoid including portaudio.h in header
typedef void PaStream;

class PortAudioCapture : public IAudioCapture {
public:
    PortAudioCapture();
    ~PortAudioCapture() override;

    bool open(const Config& config) override;
    bool start() override;
    void stop() override;
    bool isRunning() const override { return running_; }

    void setCallback(AudioCallback cb) override { callback_ = cb; }

    std::vector<DeviceInfo> listDevices() const override;
    std::string backendName() const override { return "PortAudio"; }

    // Called from DSP thread: drain ring buffers and invoke callback
    // with per-channel data blocks.
    void consumeChannels(int framesPerBlock);

private:
    // PortAudio stream callback (static → forwards to instance)
    static int paCallback(const void* input, void* output,
                          unsigned long frameCount,
                          const void* timeInfo,
                          unsigned long statusFlags,
                          void* userData);

    int handleAudio(const float* input, int frameCount);

    Config              config_;
    PaStream*           stream_    = nullptr;
    std::atomic<bool>   running_{false};
    AudioCallback       callback_;

    // Per-channel ring buffers (written by audio callback, read by DSP thread)
    std::vector<std::unique_ptr<RingBuffer>> channelBuffers_;

    // Scratch buffer for consumeChannels
    std::vector<std::vector<float>> readBufs_;
    std::vector<const float*>       readPtrs_;

    bool paInitialized_ = false;
};
