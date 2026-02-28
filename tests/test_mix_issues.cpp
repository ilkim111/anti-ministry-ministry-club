#include <gtest/gtest.h>
#include "analysis/AudioAnalyser.hpp"
#include "analysis/MeterBridge.hpp"
#include "console/ConsoleModel.hpp"
#include "discovery/DynamicChannelMap.hpp"

class MixIssueTest : public ::testing::Test {
protected:
    AudioAnalyser analyser;

    // Helper to create a channel analysis with FFT data
    AudioAnalyser::ChannelAnalysis makeChannel(
        int ch, float rmsDB, float peakDB, float bass, float lowMid,
        float mid, float upperMid, float presence, bool hasFFT = true)
    {
        AudioAnalyser::ChannelAnalysis ca;
        ca.channel         = ch;
        ca.rmsDB           = rmsDB;
        ca.peakDB          = peakDB;
        ca.crestFactor     = peakDB - rmsDB;
        ca.isClipping      = peakDB > -0.5f;
        ca.isFeedbackRisk  = false;
        ca.dominantFreqHz  = 1000;
        ca.spectralCentroid = 1000;
        ca.bass            = bass;
        ca.lowMid          = lowMid;
        ca.mid             = mid;
        ca.upperMid        = upperMid;
        ca.presence        = presence;
        ca.hasFFTData      = hasFFT;
        return ca;
    }
};

TEST_F(MixIssueTest, DetectsClipping) {
    AudioAnalyser::MixAnalysis analysis;
    analysis.channels.push_back(makeChannel(1, -3.0f, 0.0f, -20, -20, -10, -15, -20));
    analysis.channels[0].isClipping = true;

    auto issues = analyser.detectIssues(analysis);
    ASSERT_GE(issues.size(), 1u);
    EXPECT_EQ(issues[0].type, AudioAnalyser::MixIssue::Type::Clipping);
    EXPECT_EQ(issues[0].channel, 1);
}

TEST_F(MixIssueTest, DetectsBoomy) {
    AudioAnalyser::MixAnalysis analysis;
    // Low-mid heavy, mid recessed → boomy
    analysis.channels.push_back(makeChannel(1, -10, -4, -8, -6, -18, -20, -25));

    auto issues = analyser.detectIssues(analysis);
    bool foundBoomy = false;
    for (auto& i : issues) {
        if (i.type == AudioAnalyser::MixIssue::Type::Boomy && i.channel == 1)
            foundBoomy = true;
    }
    EXPECT_TRUE(foundBoomy);
}

TEST_F(MixIssueTest, DetectsHarsh) {
    AudioAnalyser::MixAnalysis analysis;
    // Upper-mid way above mid → harsh
    analysis.channels.push_back(makeChannel(1, -10, -4, -20, -15, -15, -6, -20));

    auto issues = analyser.detectIssues(analysis);
    bool foundHarsh = false;
    for (auto& i : issues) {
        if (i.type == AudioAnalyser::MixIssue::Type::Harsh && i.channel == 1)
            foundHarsh = true;
    }
    EXPECT_TRUE(foundHarsh);
}

TEST_F(MixIssueTest, DetectsThin) {
    AudioAnalyser::MixAnalysis analysis;
    // Strong bass, very weak presence → thin
    analysis.channels.push_back(makeChannel(1, -10, -4, -8, -12, -18, -25, -40));

    auto issues = analyser.detectIssues(analysis);
    bool foundThin = false;
    for (auto& i : issues) {
        if (i.type == AudioAnalyser::MixIssue::Type::Thin && i.channel == 1)
            foundThin = true;
    }
    EXPECT_TRUE(foundThin);
}

TEST_F(MixIssueTest, DetectsBassMasking) {
    AudioAnalyser::MixAnalysis analysis;
    // Two channels with similar strong bass energy → masking
    analysis.channels.push_back(makeChannel(1, -10, -4, -8, -15, -20, -25, -30));
    analysis.channels.push_back(makeChannel(2, -10, -4, -9, -16, -20, -25, -30));

    auto issues = analyser.detectIssues(analysis);
    bool foundMasking = false;
    for (auto& i : issues) {
        if (i.type == AudioAnalyser::MixIssue::Type::Masking)
            foundMasking = true;
    }
    EXPECT_TRUE(foundMasking);
}

TEST_F(MixIssueTest, NoIssuesForBalancedChannel) {
    AudioAnalyser::MixAnalysis analysis;
    // Well-balanced channel — no issues expected
    analysis.channels.push_back(makeChannel(1, -18, -12, -20, -18, -16, -18, -20));

    auto issues = analyser.detectIssues(analysis);
    // Should only have issues if there's actually something wrong
    for (auto& i : issues) {
        EXPECT_NE(i.type, AudioAnalyser::MixIssue::Type::Boomy);
        EXPECT_NE(i.type, AudioAnalyser::MixIssue::Type::Harsh);
        EXPECT_NE(i.type, AudioAnalyser::MixIssue::Type::Thin);
    }
}

TEST_F(MixIssueTest, SilentChannelNoIssues) {
    AudioAnalyser::MixAnalysis analysis;
    analysis.channels.push_back(makeChannel(1, -80, -80, -90, -90, -90, -90, -90));

    auto issues = analyser.detectIssues(analysis);
    EXPECT_TRUE(issues.empty());
}

TEST_F(MixIssueTest, NoFFTDataSkipsSpectralIssues) {
    AudioAnalyser::MixAnalysis analysis;
    // Has signal but no FFT data — should not detect boomy/harsh/thin
    auto ch = makeChannel(1, -10, -4, -6, -6, -20, -6, -40, false);
    analysis.channels.push_back(ch);

    auto issues = analyser.detectIssues(analysis);
    for (auto& i : issues) {
        EXPECT_NE(i.type, AudioAnalyser::MixIssue::Type::Boomy);
        EXPECT_NE(i.type, AudioAnalyser::MixIssue::Type::Harsh);
        EXPECT_NE(i.type, AudioAnalyser::MixIssue::Type::Thin);
        EXPECT_NE(i.type, AudioAnalyser::MixIssue::Type::Masking);
    }
}

// ── MeterBridge Smart Summary Tests ─────────────────────────────────────

TEST(MeterBridgeTest, IssuesIncludedInMixState) {
    ConsoleModel model;
    model.init(2, 0);
    DynamicChannelMap channelMap(2);

    // Set up a channel with a name so it's included
    ParameterUpdate u;
    u.target = ParameterUpdate::Target::Channel;
    u.index = 1;
    u.param = ChannelParam::Name;
    u.strValue = "Kick";
    model.applyUpdate(u);

    ChannelProfile p;
    p.index = 1;
    p.consoleName = "Kick";
    p.role = InstrumentRole::Kick;
    p.group = "drums";
    p.fingerprint.hasSignal = true;
    channelMap.updateProfile(p);

    MeterBridge bridge(model, channelMap);

    std::vector<AudioAnalyser::MixIssue> issues;
    AudioAnalyser::MixIssue issue;
    issue.type = AudioAnalyser::MixIssue::Type::Boomy;
    issue.channel = 1;
    issue.freqHz = 350.0f;
    issue.severity = 0.7f;
    issue.description = "ch1 boomy (low-mid -6.0dB)";
    issues.push_back(issue);

    auto state = bridge.buildMixState(issues);

    EXPECT_TRUE(state.contains("issues"));
    EXPECT_EQ(state["issues"].size(), 1u);
    EXPECT_EQ(state["issues"][0]["type"], "boomy");
    EXPECT_EQ(state["issues"][0]["channel"], 1);
    EXPECT_EQ(state["issues"][0]["freq_hz"], 350);
}

TEST(MeterBridgeTest, NoIssuesOmitsArray) {
    ConsoleModel model;
    model.init(1, 0);
    DynamicChannelMap channelMap(1);

    MeterBridge bridge(model, channelMap);
    auto state = bridge.buildMixState({});

    EXPECT_FALSE(state.contains("issues"));
}

TEST(MeterBridgeTest, MultipleIssuesAllIncluded) {
    ConsoleModel model;
    model.init(2, 0);
    DynamicChannelMap channelMap(2);

    ChannelProfile p1;
    p1.index = 1; p1.consoleName = "Kick"; p1.fingerprint.hasSignal = true;
    p1.role = InstrumentRole::Kick; p1.group = "drums";
    channelMap.updateProfile(p1);

    ChannelProfile p2;
    p2.index = 2; p2.consoleName = "Bass"; p2.fingerprint.hasSignal = true;
    p2.role = InstrumentRole::BassGuitar; p2.group = "bass";
    channelMap.updateProfile(p2);

    MeterBridge bridge(model, channelMap);

    std::vector<AudioAnalyser::MixIssue> issues;
    issues.push_back({AudioAnalyser::MixIssue::Type::Clipping,
                      1, 0, 0, 0.9f, "ch1 clipping"});
    issues.push_back({AudioAnalyser::MixIssue::Type::Masking,
                      1, 2, 200, 0.6f, "ch1 & ch2 masking @200Hz"});

    auto state = bridge.buildMixState(issues);

    EXPECT_TRUE(state.contains("issues"));
    EXPECT_EQ(state["issues"].size(), 2u);
    EXPECT_EQ(state["issues"][0]["type"], "clipping");
    EXPECT_EQ(state["issues"][1]["type"], "masking");
    EXPECT_EQ(state["issues"][1]["channel2"], 2);
}
