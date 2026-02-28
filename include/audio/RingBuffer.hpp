#pragma once
#include <vector>
#include <atomic>
#include <cstring>
#include <algorithm>

// Lock-free single-producer single-consumer ring buffer.
// Producer: audio callback thread (real-time safe — no allocs, no locks).
// Consumer: DSP analysis thread.
class RingBuffer {
public:
    explicit RingBuffer(size_t capacity = 8192)
        : buf_(capacity), capacity_(capacity) {}

    // Producer: write samples (called from audio callback — must be RT-safe)
    size_t write(const float* data, size_t count) {
        size_t wr = writePos_.load(std::memory_order_relaxed);
        size_t rd = readPos_.load(std::memory_order_acquire);

        size_t available = capacity_ - (wr - rd);
        size_t toWrite = std::min(count, available);
        if (toWrite == 0) return 0;

        size_t wrIdx = wr % capacity_;
        size_t firstChunk = std::min(toWrite, capacity_ - wrIdx);
        std::memcpy(&buf_[wrIdx], data, firstChunk * sizeof(float));

        if (toWrite > firstChunk) {
            std::memcpy(&buf_[0], data + firstChunk,
                        (toWrite - firstChunk) * sizeof(float));
        }

        writePos_.store(wr + toWrite, std::memory_order_release);
        return toWrite;
    }

    // Consumer: read samples into output buffer
    size_t read(float* out, size_t count) {
        size_t rd = readPos_.load(std::memory_order_relaxed);
        size_t wr = writePos_.load(std::memory_order_acquire);

        size_t available = wr - rd;
        size_t toRead = std::min(count, available);
        if (toRead == 0) return 0;

        size_t rdIdx = rd % capacity_;
        size_t firstChunk = std::min(toRead, capacity_ - rdIdx);
        std::memcpy(out, &buf_[rdIdx], firstChunk * sizeof(float));

        if (toRead > firstChunk) {
            std::memcpy(out + firstChunk, &buf_[0],
                        (toRead - firstChunk) * sizeof(float));
        }

        readPos_.store(rd + toRead, std::memory_order_release);
        return toRead;
    }

    // How many samples are available to read
    size_t available() const {
        return writePos_.load(std::memory_order_acquire)
             - readPos_.load(std::memory_order_relaxed);
    }

    void reset() {
        writePos_.store(0, std::memory_order_relaxed);
        readPos_.store(0, std::memory_order_relaxed);
    }

private:
    std::vector<float> buf_;
    size_t capacity_;
    std::atomic<size_t> writePos_{0};
    std::atomic<size_t> readPos_{0};
};
