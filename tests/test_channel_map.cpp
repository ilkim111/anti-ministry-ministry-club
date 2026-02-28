#include <gtest/gtest.h>
#include "discovery/DynamicChannelMap.hpp"

TEST(DynamicChannelMapTest, InitializesWithCorrectCount) {
    DynamicChannelMap map(32);
    EXPECT_EQ(map.count(), 32);
}

TEST(DynamicChannelMapTest, GetProfileReturnsCorrectIndex) {
    DynamicChannelMap map(32);
    auto p = map.getProfile(1);
    EXPECT_EQ(p.index, 1);

    p = map.getProfile(32);
    EXPECT_EQ(p.index, 32);
}

TEST(DynamicChannelMapTest, UpdateAndRetrieveProfile) {
    DynamicChannelMap map(32);

    ChannelProfile profile;
    profile.index = 5;
    profile.consoleName = "Kick";
    profile.role = InstrumentRole::Kick;
    profile.group = "drums";
    profile.confidence = DiscoveryConfidence::High;

    map.updateProfile(profile);

    auto retrieved = map.getProfile(5);
    EXPECT_EQ(retrieved.consoleName, "Kick");
    EXPECT_EQ(retrieved.role, InstrumentRole::Kick);
    EXPECT_EQ(retrieved.group, "drums");
    EXPECT_EQ(retrieved.confidence, DiscoveryConfidence::High);
}

TEST(DynamicChannelMapTest, QueryByRole) {
    DynamicChannelMap map(4);

    ChannelProfile p1; p1.index = 1; p1.role = InstrumentRole::Kick;
    ChannelProfile p2; p2.index = 2; p2.role = InstrumentRole::Snare;
    ChannelProfile p3; p3.index = 3; p3.role = InstrumentRole::Kick;
    ChannelProfile p4; p4.index = 4; p4.role = InstrumentRole::LeadVocal;

    map.updateProfile(p1);
    map.updateProfile(p2);
    map.updateProfile(p3);
    map.updateProfile(p4);

    auto kicks = map.byRole(InstrumentRole::Kick);
    EXPECT_EQ(kicks.size(), 2);
    EXPECT_EQ(kicks[0].index, 1);
    EXPECT_EQ(kicks[1].index, 3);
}

TEST(DynamicChannelMapTest, QueryByGroup) {
    DynamicChannelMap map(3);

    ChannelProfile p1; p1.index = 1; p1.group = "drums";
    ChannelProfile p2; p2.index = 2; p2.group = "vocals";
    ChannelProfile p3; p3.index = 3; p3.group = "drums";

    map.updateProfile(p1);
    map.updateProfile(p2);
    map.updateProfile(p3);

    auto drums = map.byGroup("drums");
    EXPECT_EQ(drums.size(), 2);
}

TEST(DynamicChannelMapTest, ActiveOnlyReturnsSignalChannels) {
    DynamicChannelMap map(3);

    ChannelProfile p1;
    p1.index = 1;
    p1.fingerprint.hasSignal = true;
    p1.muted = false;

    ChannelProfile p2;
    p2.index = 2;
    p2.fingerprint.hasSignal = false;  // no signal
    p2.muted = false;

    ChannelProfile p3;
    p3.index = 3;
    p3.fingerprint.hasSignal = true;
    p3.muted = true;  // muted

    map.updateProfile(p1);
    map.updateProfile(p2);
    map.updateProfile(p3);

    auto active = map.active();
    EXPECT_EQ(active.size(), 1);
    EXPECT_EQ(active[0].index, 1);
}

TEST(DynamicChannelMapTest, ResizeWorks) {
    DynamicChannelMap map(8);
    EXPECT_EQ(map.count(), 8);

    map.resize(32);
    EXPECT_EQ(map.count(), 32);
    EXPECT_EQ(map.getProfile(32).index, 32);
}

TEST(DynamicChannelMapTest, InvalidIndexIgnored) {
    DynamicChannelMap map(4);

    ChannelProfile p;
    p.index = 10;  // out of range
    map.updateProfile(p);  // should not crash

    EXPECT_EQ(map.count(), 4);
}
