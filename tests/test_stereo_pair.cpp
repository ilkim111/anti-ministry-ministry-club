#include <gtest/gtest.h>
#include "discovery/StereoPairDetector.hpp"

class StereoPairDetectorTest : public ::testing::Test {
protected:
    StereoPairDetector detector;

    std::vector<ChannelProfile> makeChannels(
        const std::vector<std::pair<std::string, InstrumentRole>>& defs)
    {
        std::vector<ChannelProfile> channels;
        for (int i = 0; i < (int)defs.size(); i++) {
            ChannelProfile p;
            p.index = i + 1;
            p.consoleName = defs[i].first;
            p.role = defs[i].second;
            p.fingerprint.hasSignal = true;
            p.fingerprint.dominantFreqHz = 1000.0f;
            channels.push_back(p);
        }
        return channels;
    }
};

TEST_F(StereoPairDetectorTest, DetectsLRPair) {
    auto channels = makeChannels({
        {"OH L", InstrumentRole::Overhead},
        {"OH R", InstrumentRole::Overhead},
    });

    auto pairs = detector.detect(channels);
    EXPECT_EQ(pairs.size(), 1);
    EXPECT_EQ(pairs[0].left, 1);
    EXPECT_EQ(pairs[0].right, 2);
}

TEST_F(StereoPairDetectorTest, Detects12Pair) {
    auto channels = makeChannels({
        {"Gtr 1", InstrumentRole::ElectricGuitar},
        {"Gtr 2", InstrumentRole::ElectricGuitar},
    });

    auto pairs = detector.detect(channels);
    EXPECT_EQ(pairs.size(), 1);
}

TEST_F(StereoPairDetectorTest, DoesNotPairDifferentRoles) {
    auto channels = makeChannels({
        {"Kick", InstrumentRole::Kick},
        {"Snare", InstrumentRole::Snare},
    });

    auto pairs = detector.detect(channels);
    EXPECT_EQ(pairs.size(), 0);
}

TEST_F(StereoPairDetectorTest, DoesNotPairNonAdjacentChannels) {
    auto channels = makeChannels({
        {"Gtr L", InstrumentRole::ElectricGuitar},
        {"Kick",  InstrumentRole::Kick},
        {"Gtr R", InstrumentRole::ElectricGuitar},
    });

    // Gtr L is ch1, Gtr R is ch3 — not adjacent
    auto pairs = detector.detect(channels);
    // Should not pair ch1 with ch3 since they're not adjacent
    for (auto& p : pairs) {
        EXPECT_NE(p.left, 1);  // ch1 should not be paired with ch3
    }
}

TEST_F(StereoPairDetectorTest, MultiplesPairs) {
    auto channels = makeChannels({
        {"OH L",  InstrumentRole::Overhead},
        {"OH R",  InstrumentRole::Overhead},
        {"Gtr L", InstrumentRole::ElectricGuitar},
        {"Gtr R", InstrumentRole::ElectricGuitar},
    });

    auto pairs = detector.detect(channels);
    EXPECT_EQ(pairs.size(), 2);
}
