#include "audio/PortAudioCapture.hpp"
#include <spdlog/spdlog.h>

#ifdef HAS_PORTAUDIO
#include <portaudio.h>
#endif

PortAudioCapture::PortAudioCapture() {
#ifdef HAS_PORTAUDIO
    PaError err = Pa_Initialize();
    if (err == paNoError) {
        paInitialized_ = true;
    } else {
        spdlog::error("PortAudio init failed: {}", Pa_GetErrorText(err));
    }
#endif
}

PortAudioCapture::~PortAudioCapture() {
    stop();
#ifdef HAS_PORTAUDIO
    if (paInitialized_)
        Pa_Terminate();
#endif
}

bool PortAudioCapture::open(const Config& config) {
#ifdef HAS_PORTAUDIO
    if (!paInitialized_) return false;

    config_ = config;

    // Create per-channel ring buffers
    // 2 seconds of buffer at the configured sample rate
    size_t bufSize = (size_t)(config.sampleRate * 2);
    channelBuffers_.clear();
    for (int i = 0; i < config.channelCount; i++) {
        channelBuffers_.push_back(std::make_unique<RingBuffer>(bufSize));
    }

    readBufs_.resize(config.channelCount);
    readPtrs_.resize(config.channelCount);

    PaStreamParameters inputParams;
    inputParams.channelCount = config.channelCount;
    inputParams.sampleFormat = paFloat32 | paNonInterleaved;
    inputParams.suggestedLatency = 0.020;  // 20ms — reasonable for analysis
    inputParams.hostApiSpecificStreamInfo = nullptr;

    if (config.deviceId >= 0) {
        inputParams.device = config.deviceId;
    } else {
        inputParams.device = Pa_GetDefaultInputDevice();
        if (inputParams.device == paNoDevice) {
            spdlog::error("No default audio input device");
            return false;
        }
    }

    // Check device has enough channels
    const PaDeviceInfo* devInfo = Pa_GetDeviceInfo(inputParams.device);
    if (!devInfo) {
        spdlog::error("Invalid audio device ID {}", inputParams.device);
        return false;
    }

    if (devInfo->maxInputChannels < config.channelCount) {
        spdlog::warn("Device '{}' has {} inputs, requested {} — clamping",
                     devInfo->name, devInfo->maxInputChannels, config.channelCount);
        config_.channelCount = devInfo->maxInputChannels;
        inputParams.channelCount = config_.channelCount;
    }

    spdlog::info("Opening audio: device='{}', {} ch, {}Hz, {} frames/block",
                 devInfo->name, config_.channelCount,
                 config_.sampleRate, config_.framesPerBlock);

    PaError err = Pa_OpenStream(
        &stream_,
        &inputParams,
        nullptr,  // no output
        config.sampleRate,
        config.framesPerBlock,
        paClipOff,
        &PortAudioCapture::paCallback,
        this
    );

    if (err != paNoError) {
        spdlog::error("Pa_OpenStream failed: {}", Pa_GetErrorText(err));
        return false;
    }

    return true;
#else
    (void)config;
    spdlog::warn("PortAudio not available — built without HAS_PORTAUDIO");
    return false;
#endif
}

bool PortAudioCapture::start() {
#ifdef HAS_PORTAUDIO
    if (!stream_) return false;
    PaError err = Pa_StartStream(stream_);
    if (err != paNoError) {
        spdlog::error("Pa_StartStream failed: {}", Pa_GetErrorText(err));
        return false;
    }
    running_ = true;
    spdlog::info("Audio capture started");
    return true;
#else
    return false;
#endif
}

void PortAudioCapture::stop() {
#ifdef HAS_PORTAUDIO
    running_ = false;
    if (stream_) {
        Pa_StopStream(stream_);
        Pa_CloseStream(stream_);
        stream_ = nullptr;
    }
    spdlog::info("Audio capture stopped");
#endif
}

std::vector<IAudioCapture::DeviceInfo> PortAudioCapture::listDevices() const {
    std::vector<DeviceInfo> result;
#ifdef HAS_PORTAUDIO
    if (!paInitialized_) return result;

    int count = Pa_GetDeviceCount();
    for (int i = 0; i < count; i++) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info && info->maxInputChannels > 0) {
            result.push_back({
                i,
                info->name,
                info->maxInputChannels,
                info->defaultSampleRate
            });
        }
    }
#endif
    return result;
}

int PortAudioCapture::paCallback(
    const void* input, void* /*output*/,
    unsigned long frameCount,
    const void* /*timeInfo*/,
    unsigned long /*statusFlags*/,
    void* userData)
{
    auto* self = static_cast<PortAudioCapture*>(userData);
    return self->handleAudio(static_cast<const float*>(input),
                              static_cast<int>(frameCount));
}

int PortAudioCapture::handleAudio(const float* input, int frameCount) {
    if (!input) return 0;  // paContinue

#ifdef HAS_PORTAUDIO
    // Non-interleaved: input is an array of channel pointers
    auto** channelPtrs = reinterpret_cast<const float* const*>(
        reinterpret_cast<const void*>(&input));

    // Write each channel into its ring buffer
    for (int ch = 0; ch < config_.channelCount; ch++) {
        if (ch < (int)channelBuffers_.size()) {
            // For non-interleaved PortAudio, the input pointer
            // is actually a pointer to an array of float* (one per channel)
            const float* chData = reinterpret_cast<const float* const*>(input)[ch];
            channelBuffers_[ch]->write(chData, frameCount);
        }
    }
#else
    (void)input;
    (void)frameCount;
#endif

    return 0;  // paContinue
}

void PortAudioCapture::consumeChannels(int framesPerBlock) {
    if (!running_ || channelBuffers_.empty()) return;

    // Check if we have enough data in all channels
    size_t minAvail = channelBuffers_[0]->available();
    for (int ch = 1; ch < (int)channelBuffers_.size(); ch++) {
        minAvail = std::min(minAvail, channelBuffers_[ch]->available());
    }

    if ((int)minAvail < framesPerBlock) return;

    // Read from each channel buffer
    for (int ch = 0; ch < (int)channelBuffers_.size(); ch++) {
        readBufs_[ch].resize(framesPerBlock);
        channelBuffers_[ch]->read(readBufs_[ch].data(), framesPerBlock);
        readPtrs_[ch] = readBufs_[ch].data();
    }

    // Fire the callback with per-channel data
    if (callback_) {
        callback_(readPtrs_.data(), config_.channelCount, framesPerBlock);
    }
}
