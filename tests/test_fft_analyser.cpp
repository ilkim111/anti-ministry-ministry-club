#include <gtest/gtest.h>
#include "audio/FFTAnalyser.hpp"
#include <cmath>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class FFTAnalyserTest : public ::testing::Test {
protected:
    FFTAnalyser analyser{1024};

    // Generate a sine wave at a given frequency
    std::vector<float> generateSine(float freqHz, float sampleRate,
                                     int samples, float amplitude = 0.5f) {
        std::vector<float> buf(samples);
        for (int i = 0; i < samples; i++) {
            buf[i] = amplitude * std::sin(2.0f * (float)M_PI * freqHz * i / sampleRate);
        }
        return buf;
    }

    // Generate white noise
    std::vector<float> generateNoise(int samples, float amplitude = 0.1f) {
        std::vector<float> buf(samples);
        for (int i = 0; i < samples; i++) {
            buf[i] = amplitude * (2.0f * (float)rand() / RAND_MAX - 1.0f);
        }
        return buf;
    }
};

TEST_F(FFTAnalyserTest, SilenceReturnsMinus96) {
    std::vector<float> silence(1024, 0.0f);
    auto r = analyser.analyse(silence.data(), 1024, 48000);
    EXPECT_LE(r.rmsDB, -90.0f);
    EXPECT_LE(r.peakDB, -90.0f);
    EXPECT_FALSE(r.hasSignal);
}

TEST_F(FFTAnalyserTest, SineWaveDetectsDominantFrequency) {
    auto sine = generateSine(1000.0f, 48000, 1024);
    auto r = analyser.analyse(sine.data(), 1024, 48000);

    EXPECT_TRUE(r.hasSignal);
    // Dominant frequency should be close to 1000Hz (within one FFT bin)
    float binWidth = 48000.0f / 1024;
    EXPECT_NEAR(r.dominantFreqHz, 1000.0f, binWidth * 2);
}

TEST_F(FFTAnalyserTest, LowFreqSineHasBassEnergy) {
    auto sine = generateSine(100.0f, 48000, 1024, 0.8f);
    auto r = analyser.analyse(sine.data(), 1024, 48000);

    EXPECT_TRUE(r.hasSignal);
    // Bass (80-250Hz) should have the most energy
    EXPECT_GT(r.bands.bass, r.bands.mid);
    EXPECT_GT(r.bands.bass, r.bands.presence);
}

TEST_F(FFTAnalyserTest, HighFreqSineHasPresenceEnergy) {
    auto sine = generateSine(8000.0f, 48000, 1024, 0.5f);
    auto r = analyser.analyse(sine.data(), 1024, 48000);

    EXPECT_TRUE(r.hasSignal);
    // Presence (6k-10kHz) should have the most energy
    EXPECT_GT(r.bands.presence, r.bands.bass);
    EXPECT_GT(r.bands.presence, r.bands.subBass);
}

TEST_F(FFTAnalyserTest, RmsAndPeakCorrectForSine) {
    auto sine = generateSine(440.0f, 48000, 1024, 0.5f);
    auto r = analyser.analyse(sine.data(), 1024, 48000);

    // RMS of a sine wave = amplitude / sqrt(2) ≈ 0.354 -> ~-9dB
    // Peak = amplitude = 0.5 -> ~-6dB
    EXPECT_NEAR(r.peakDB, -6.0f, 1.0f);
    EXPECT_NEAR(r.rmsDB, -9.0f, 1.5f);
}

TEST_F(FFTAnalyserTest, CrestFactorLowForPureTone) {
    // Pure sine has low crest factor (~3dB)
    auto sine = generateSine(1000.0f, 48000, 1024, 0.5f);
    auto r = analyser.analyse(sine.data(), 1024, 48000);

    EXPECT_GT(r.crestFactor, 2.0f);
    EXPECT_LT(r.crestFactor, 4.0f);
}

TEST_F(FFTAnalyserTest, SpectralCentroidInRange) {
    auto sine = generateSine(2000.0f, 48000, 1024, 0.5f);
    auto r = analyser.analyse(sine.data(), 1024, 48000);

    // Spectral centroid should be near the sine frequency
    EXPECT_NEAR(r.spectralCentroid, 2000.0f, 200.0f);
}

TEST_F(FFTAnalyserTest, TooFewSamplesReturnsDefault) {
    std::vector<float> short_buf(100, 0.5f);
    auto r = analyser.analyse(short_buf.data(), 100, 48000);

    // Not enough samples for the 1024-point FFT
    EXPECT_LE(r.rmsDB, -90.0f);
    EXPECT_FALSE(r.hasSignal);
}

TEST_F(FFTAnalyserTest, MidFrequencyDetectedCorrectly) {
    auto sine = generateSine(1000.0f, 48000, 1024, 0.5f);
    auto r = analyser.analyse(sine.data(), 1024, 48000);

    // Mid band (500-2kHz) should dominate
    EXPECT_GT(r.bands.mid, r.bands.bass);
    EXPECT_GT(r.bands.mid, r.bands.air);
}

TEST_F(FFTAnalyserTest, FullScaleClipsCorrectly) {
    // Signal at 0 dBFS
    auto sine = generateSine(1000.0f, 48000, 1024, 1.0f);
    auto r = analyser.analyse(sine.data(), 1024, 48000);

    EXPECT_NEAR(r.peakDB, 0.0f, 0.5f);
}
