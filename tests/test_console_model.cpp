#include <gtest/gtest.h>
#include "console/ConsoleModel.hpp"

class ConsoleModelTest : public ::testing::Test {
protected:
    ConsoleModel model;

    void SetUp() override {
        model.init(32, 16);
    }
};

TEST_F(ConsoleModelTest, InitSetsCorrectCounts) {
    EXPECT_EQ(model.channelCount(), 32);
    EXPECT_EQ(model.busCount(), 16);
}

TEST_F(ConsoleModelTest, ChannelDefaultValues) {
    auto ch = model.channel(1);
    EXPECT_EQ(ch.index, 1);
    EXPECT_FLOAT_EQ(ch.fader, 0.75f);
    EXPECT_FALSE(ch.muted);
    EXPECT_FLOAT_EQ(ch.rmsDB, -96.0f);
    EXPECT_FLOAT_EQ(ch.peakDB, -96.0f);
}

TEST_F(ConsoleModelTest, ApplyFaderUpdate) {
    ParameterUpdate u;
    u.target = ParameterUpdate::Target::Channel;
    u.index  = 5;
    u.param  = ChannelParam::Fader;
    u.value  = 0.6f;

    model.applyUpdate(u);

    auto ch = model.channel(5);
    EXPECT_FLOAT_EQ(ch.fader, 0.6f);
}

TEST_F(ConsoleModelTest, ApplyMuteUpdate) {
    ParameterUpdate u;
    u.target = ParameterUpdate::Target::Channel;
    u.index  = 3;
    u.param  = ChannelParam::Mute;
    u.value  = true;

    model.applyUpdate(u);

    auto ch = model.channel(3);
    EXPECT_TRUE(ch.muted);
}

TEST_F(ConsoleModelTest, ApplyNameUpdate) {
    ParameterUpdate u;
    u.target   = ParameterUpdate::Target::Channel;
    u.index    = 1;
    u.param    = ChannelParam::Name;
    u.strValue = "Kick";
    u.value    = std::string("Kick");

    model.applyUpdate(u);

    auto ch = model.channel(1);
    EXPECT_EQ(ch.name, "Kick");
}

TEST_F(ConsoleModelTest, ApplyEqUpdate) {
    ParameterUpdate u;
    u.target = ParameterUpdate::Target::Channel;
    u.index  = 2;
    u.param  = ChannelParam::EqBand1Freq;
    u.value  = 250.0f;

    model.applyUpdate(u);

    auto ch = model.channel(2);
    EXPECT_FLOAT_EQ(ch.eq[0].freq, 250.0f);
}

TEST_F(ConsoleModelTest, ApplyCompressorUpdate) {
    ParameterUpdate u;
    u.target = ParameterUpdate::Target::Channel;
    u.index  = 4;
    u.param  = ChannelParam::CompThreshold;
    u.value  = -18.0f;

    model.applyUpdate(u);

    auto ch = model.channel(4);
    EXPECT_FLOAT_EQ(ch.comp.threshold, -18.0f);
}

TEST_F(ConsoleModelTest, ApplyGateUpdate) {
    ParameterUpdate u;
    u.target = ParameterUpdate::Target::Channel;
    u.index  = 2;
    u.param  = ChannelParam::GateThreshold;
    u.value  = -40.0f;

    model.applyUpdate(u);

    auto ch = model.channel(2);
    EXPECT_FLOAT_EQ(ch.gate.threshold, -40.0f);
}

TEST_F(ConsoleModelTest, ApplySendLevelUpdate) {
    ParameterUpdate u;
    u.target   = ParameterUpdate::Target::Channel;
    u.index    = 1;
    u.param    = ChannelParam::SendLevel;
    u.auxIndex = 3;
    u.value    = 0.5f;

    model.applyUpdate(u);

    auto ch = model.channel(1);
    EXPECT_FLOAT_EQ(ch.sends[2], 0.5f);  // auxIndex 3 -> sends[2]
}

TEST_F(ConsoleModelTest, ApplyBusUpdate) {
    ParameterUpdate u;
    u.target = ParameterUpdate::Target::Bus;
    u.index  = 2;
    u.param  = ChannelParam::Fader;
    u.value  = 0.9f;

    model.applyUpdate(u);

    auto bus = model.bus(2);
    EXPECT_FLOAT_EQ(bus.fader, 0.9f);
}

TEST_F(ConsoleModelTest, OutOfBoundsChannelUpdateIgnored) {
    ParameterUpdate u;
    u.target = ParameterUpdate::Target::Channel;
    u.index  = 999;  // out of bounds
    u.param  = ChannelParam::Fader;
    u.value  = 0.5f;

    // Should not crash
    model.applyUpdate(u);
    EXPECT_EQ(model.channelCount(), 32);
}

TEST_F(ConsoleModelTest, ZeroIndexUpdateIgnored) {
    ParameterUpdate u;
    u.target = ParameterUpdate::Target::Channel;
    u.index  = 0;  // invalid — 1-based
    u.param  = ChannelParam::Fader;
    u.value  = 0.5f;

    model.applyUpdate(u);
    // Should not crash, channel 1 should be unchanged
    auto ch = model.channel(1);
    EXPECT_FLOAT_EQ(ch.fader, 0.75f);
}

TEST_F(ConsoleModelTest, UpdateMeter) {
    model.updateMeter(1, -12.0f, -6.0f);

    auto ch = model.channel(1);
    EXPECT_FLOAT_EQ(ch.rmsDB, -12.0f);
    EXPECT_FLOAT_EQ(ch.peakDB, -6.0f);
}

TEST_F(ConsoleModelTest, UpdateMeterOutOfBoundsIgnored) {
    // Should not crash
    model.updateMeter(0, -12.0f, -6.0f);
    model.updateMeter(999, -12.0f, -6.0f);
}

TEST_F(ConsoleModelTest, UpdateSpectral) {
    ChannelSnapshot::SpectralData data;
    data.bass = -20.0f;
    data.mid  = -15.0f;
    data.presence = -10.0f;
    data.crestFactor = 8.0f;
    data.spectralCentroid = 3000.0f;

    model.updateSpectral(1, data);

    auto ch = model.channel(1);
    EXPECT_FLOAT_EQ(ch.spectral.bass, -20.0f);
    EXPECT_FLOAT_EQ(ch.spectral.crestFactor, 8.0f);
    EXPECT_FLOAT_EQ(ch.spectral.spectralCentroid, 3000.0f);
}

TEST_F(ConsoleModelTest, AllChannelsReturnsFullSnapshot) {
    auto all = model.allChannels();
    EXPECT_EQ(all.size(), 32);

    for (int i = 0; i < 32; i++) {
        EXPECT_EQ(all[i].index, i + 1);
    }
}

TEST_F(ConsoleModelTest, MultipleUpdatesAccumulate) {
    ParameterUpdate u1;
    u1.target = ParameterUpdate::Target::Channel;
    u1.index  = 1;
    u1.param  = ChannelParam::Fader;
    u1.value  = 0.5f;
    model.applyUpdate(u1);

    ParameterUpdate u2;
    u2.target   = ParameterUpdate::Target::Channel;
    u2.index    = 1;
    u2.param    = ChannelParam::Name;
    u2.strValue = "Kick";
    u2.value    = std::string("Kick");
    model.applyUpdate(u2);

    ParameterUpdate u3;
    u3.target = ParameterUpdate::Target::Channel;
    u3.index  = 1;
    u3.param  = ChannelParam::Mute;
    u3.value  = true;
    model.applyUpdate(u3);

    auto ch = model.channel(1);
    EXPECT_FLOAT_EQ(ch.fader, 0.5f);
    EXPECT_EQ(ch.name, "Kick");
    EXPECT_TRUE(ch.muted);
}
