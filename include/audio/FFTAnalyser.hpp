#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Lightweight FFT analyser using a radix-2 Cooley-Tukey implementation.
// No external dependencies — self-contained for portability.
// Operates on real-valued audio signals and extracts band energies,
// spectral centroid, dominant frequency, and crest factor.
class FFTAnalyser {
public:
    struct BandEnergy {
        float subBass  = -96.0f;  // 20–80 Hz
        float bass     = -96.0f;  // 80–250 Hz
        float lowMid   = -96.0f;  // 250–500 Hz
        float mid      = -96.0f;  // 500–2k Hz
        float upperMid = -96.0f;  // 2k–6k Hz
        float presence = -96.0f;  // 6k–10k Hz
        float air      = -96.0f;  // 10k–20k Hz
    };

    struct Result {
        BandEnergy bands;
        float spectralCentroid = 0.0f;  // Hz
        float dominantFreqHz   = 0.0f;  // Hz
        float rmsDB            = -96.0f;
        float peakDB           = -96.0f;
        float crestFactor      = 0.0f;  // peak - rms in dB
        bool  hasSignal        = false;
    };

    explicit FFTAnalyser(int fftSize = 1024)
        : fftSize_(fftSize)
        , window_(fftSize)
        , timeBuf_(fftSize)
        , realBuf_(fftSize)
        , imagBuf_(fftSize)
    {
        // Pre-compute Hann window
        for (int i = 0; i < fftSize; i++) {
            window_[i] = 0.5f * (1.0f - std::cos(2.0f * (float)M_PI * i / (fftSize - 1)));
        }
    }

    // Analyse a block of samples. Returns spectral analysis result.
    Result analyse(const float* samples, int sampleCount, float sampleRate) {
        Result r;

        if (sampleCount < fftSize_ || sampleRate <= 0)
            return r;

        // Compute RMS and peak from time domain
        float sumSq = 0.0f;
        float peak = 0.0f;
        for (int i = 0; i < sampleCount; i++) {
            float s = samples[i];
            sumSq += s * s;
            float a = std::abs(s);
            if (a > peak) peak = a;
        }
        float rms = std::sqrt(sumSq / sampleCount);
        r.rmsDB  = toDBFS(rms);
        r.peakDB = toDBFS(peak);
        r.crestFactor = r.peakDB - r.rmsDB;
        r.hasSignal = r.rmsDB > -60.0f;

        if (!r.hasSignal)
            return r;

        // Apply window and copy to work buffer
        for (int i = 0; i < fftSize_; i++) {
            timeBuf_[i] = samples[i] * window_[i];
            realBuf_[i] = timeBuf_[i];
            imagBuf_[i] = 0.0f;
        }

        // In-place FFT
        fft(realBuf_.data(), imagBuf_.data(), fftSize_);

        // Compute magnitude spectrum (only first half — Nyquist)
        int halfN = fftSize_ / 2;
        std::vector<float> magnitude(halfN);
        float binWidth = sampleRate / fftSize_;

        for (int i = 0; i < halfN; i++) {
            float re = realBuf_[i];
            float im = imagBuf_[i];
            magnitude[i] = std::sqrt(re * re + im * im) / halfN;
        }

        // Band energies
        r.bands.subBass  = bandEnergyDB(magnitude, binWidth, 20.0f, 80.0f);
        r.bands.bass     = bandEnergyDB(magnitude, binWidth, 80.0f, 250.0f);
        r.bands.lowMid   = bandEnergyDB(magnitude, binWidth, 250.0f, 500.0f);
        r.bands.mid      = bandEnergyDB(magnitude, binWidth, 500.0f, 2000.0f);
        r.bands.upperMid = bandEnergyDB(magnitude, binWidth, 2000.0f, 6000.0f);
        r.bands.presence = bandEnergyDB(magnitude, binWidth, 6000.0f, 10000.0f);
        r.bands.air      = bandEnergyDB(magnitude, binWidth, 10000.0f, sampleRate / 2);

        // Spectral centroid
        float weightedSum = 0.0f;
        float totalMag = 0.0f;
        for (int i = 1; i < halfN; i++) {
            float freq = i * binWidth;
            weightedSum += freq * magnitude[i];
            totalMag += magnitude[i];
        }
        r.spectralCentroid = (totalMag > 1e-12f)
            ? weightedSum / totalMag : 0.0f;

        // Dominant frequency (peak bin)
        auto maxIt = std::max_element(magnitude.begin() + 1, magnitude.end());
        int peakBin = static_cast<int>(std::distance(magnitude.begin(), maxIt));
        r.dominantFreqHz = peakBin * binWidth;

        return r;
    }

    int fftSize() const { return fftSize_; }

private:
    static float toDBFS(float linear) {
        if (linear < 1e-10f) return -96.0f;
        return 20.0f * std::log10(linear);
    }

    float bandEnergyDB(const std::vector<float>& mag, float binWidth,
                        float loHz, float hiHz) const {
        int loBin = std::max(1, (int)(loHz / binWidth));
        int hiBin = std::min((int)mag.size() - 1, (int)(hiHz / binWidth));

        if (loBin > hiBin) return -96.0f;

        float sumSq = 0.0f;
        for (int i = loBin; i <= hiBin; i++)
            sumSq += mag[i] * mag[i];

        float rms = std::sqrt(sumSq / (hiBin - loBin + 1));
        return toDBFS(rms);
    }

    // Radix-2 Cooley-Tukey FFT (in-place)
    static void fft(float* real, float* imag, int n) {
        // Bit-reversal permutation
        int j = 0;
        for (int i = 0; i < n - 1; i++) {
            if (i < j) {
                std::swap(real[i], real[j]);
                std::swap(imag[i], imag[j]);
            }
            int m = n >> 1;
            while (m >= 1 && j >= m) {
                j -= m;
                m >>= 1;
            }
            j += m;
        }

        // Butterfly computation
        for (int step = 2; step <= n; step <<= 1) {
            int halfStep = step >> 1;
            float angle = -(float)(2.0 * M_PI / step);

            for (int group = 0; group < n; group += step) {
                for (int pair = 0; pair < halfStep; pair++) {
                    float twiddleReal = std::cos(angle * pair);
                    float twiddleImag = std::sin(angle * pair);

                    int even = group + pair;
                    int odd  = even + halfStep;

                    float tReal = twiddleReal * real[odd] - twiddleImag * imag[odd];
                    float tImag = twiddleReal * imag[odd] + twiddleImag * real[odd];

                    real[odd]  = real[even] - tReal;
                    imag[odd]  = imag[even] - tImag;
                    real[even] += tReal;
                    imag[even] += tImag;
                }
            }
        }
    }

    int fftSize_;
    std::vector<float> window_;
    std::vector<float> timeBuf_;
    std::vector<float> realBuf_;
    std::vector<float> imagBuf_;
};
