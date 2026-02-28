#include <gtest/gtest.h>
#include "discovery/NameClassifier.hpp"

class NameClassifierTest : public ::testing::Test {
protected:
    NameClassifier classifier;
};

// ── Drums ────────────────────────────────────────────────────────────────

TEST_F(NameClassifierTest, ClassifiesKick) {
    auto r = classifier.classify("Kick");
    EXPECT_EQ(r.role, InstrumentRole::Kick);
    EXPECT_EQ(r.group, "drums");
    EXPECT_EQ(r.confidence, DiscoveryConfidence::High);
}

TEST_F(NameClassifierTest, ClassifiesKickVariants) {
    EXPECT_EQ(classifier.classify("KK").role, InstrumentRole::Kick);
    EXPECT_EQ(classifier.classify("Kk").role, InstrumentRole::Kick);
    EXPECT_EQ(classifier.classify("BD").role, InstrumentRole::Kick);
    EXPECT_EQ(classifier.classify("Bass Drum").role, InstrumentRole::Kick);
}

TEST_F(NameClassifierTest, ClassifiesSnare) {
    EXPECT_EQ(classifier.classify("Snare").role, InstrumentRole::Snare);
    EXPECT_EQ(classifier.classify("SN").role, InstrumentRole::Snare);
    EXPECT_EQ(classifier.classify("Snr").role, InstrumentRole::Snare);
}

TEST_F(NameClassifierTest, ClassifiesHiHat) {
    EXPECT_EQ(classifier.classify("HH").role, InstrumentRole::HiHat);
    EXPECT_EQ(classifier.classify("Hi-Hat").role, InstrumentRole::HiHat);
    EXPECT_EQ(classifier.classify("HiHat").role, InstrumentRole::HiHat);
}

TEST_F(NameClassifierTest, ClassifiesTom) {
    EXPECT_EQ(classifier.classify("Tom").role, InstrumentRole::Tom);
    EXPECT_EQ(classifier.classify("Tom 1").role, InstrumentRole::Tom);
    EXPECT_EQ(classifier.classify("T1").role, InstrumentRole::Tom);
}

TEST_F(NameClassifierTest, ClassifiesOverhead) {
    EXPECT_EQ(classifier.classify("OH").role, InstrumentRole::Overhead);
    EXPECT_EQ(classifier.classify("Overhead").role, InstrumentRole::Overhead);
    EXPECT_EQ(classifier.classify("Cymbal").role, InstrumentRole::Overhead);
}

// ── Bass ─────────────────────────────────────────────────────────────────

TEST_F(NameClassifierTest, ClassifiesBassGuitar) {
    EXPECT_EQ(classifier.classify("Bass").role, InstrumentRole::BassGuitar);
    EXPECT_EQ(classifier.classify("Bass DI").role, InstrumentRole::BassGuitar);
    EXPECT_EQ(classifier.classify("B.D.I.").role, InstrumentRole::BassGuitar);
}

TEST_F(NameClassifierTest, ClassifiesBassAmp) {
    EXPECT_EQ(classifier.classify("Bass Amp").role, InstrumentRole::BassAmp);
    EXPECT_EQ(classifier.classify("B.Amp").role, InstrumentRole::BassAmp);
}

// ── Guitars ──────────────────────────────────────────────────────────────

TEST_F(NameClassifierTest, ClassifiesElectricGuitar) {
    EXPECT_EQ(classifier.classify("E.Gtr").role, InstrumentRole::ElectricGuitar);
    EXPECT_EQ(classifier.classify("Gtr L").role, InstrumentRole::ElectricGuitar);
    EXPECT_EQ(classifier.classify("Gtr").role, InstrumentRole::ElectricGuitar);
}

TEST_F(NameClassifierTest, ClassifiesAcousticGuitar) {
    EXPECT_EQ(classifier.classify("Acoustic").role, InstrumentRole::AcousticGuitar);
    EXPECT_EQ(classifier.classify("A.Gtr").role, InstrumentRole::AcousticGuitar);
}

// ── Vocals ───────────────────────────────────────────────────────────────

TEST_F(NameClassifierTest, ClassifiesLeadVocal) {
    EXPECT_EQ(classifier.classify("Lead Vox").role, InstrumentRole::LeadVocal);
    EXPECT_EQ(classifier.classify("Vox").role, InstrumentRole::LeadVocal);
    EXPECT_EQ(classifier.classify("LV").role, InstrumentRole::LeadVocal);
    EXPECT_EQ(classifier.classify("Vocal").role, InstrumentRole::LeadVocal);
}

TEST_F(NameClassifierTest, ClassifiesBackingVocal) {
    EXPECT_EQ(classifier.classify("BV 1").role, InstrumentRole::BackingVocal);
    EXPECT_EQ(classifier.classify("Back Voc").role, InstrumentRole::BackingVocal);
    EXPECT_EQ(classifier.classify("Backing").role, InstrumentRole::BackingVocal);
}

// ── Keys ─────────────────────────────────────────────────────────────────

TEST_F(NameClassifierTest, ClassifiesKeys) {
    EXPECT_EQ(classifier.classify("Piano").role, InstrumentRole::Piano);
    EXPECT_EQ(classifier.classify("Keys").role, InstrumentRole::Keys);
    EXPECT_EQ(classifier.classify("Organ").role, InstrumentRole::Organ);
    EXPECT_EQ(classifier.classify("Synth").role, InstrumentRole::Synth);
}

// ── Edge cases ───────────────────────────────────────────────────────────

TEST_F(NameClassifierTest, EmptyNameReturnsUnknown) {
    auto r = classifier.classify("");
    EXPECT_EQ(r.role, InstrumentRole::Unknown);
    EXPECT_EQ(r.confidence, DiscoveryConfidence::Unknown);
}

TEST_F(NameClassifierTest, GenericChannelNameLowConfidence) {
    auto r = classifier.classify("CH 01");
    EXPECT_EQ(r.role, InstrumentRole::Unknown);
    EXPECT_EQ(r.confidence, DiscoveryConfidence::Low);
}

TEST_F(NameClassifierTest, UnknownNameLowConfidence) {
    auto r = classifier.classify("Something Random");
    EXPECT_EQ(r.role, InstrumentRole::Unknown);
    EXPECT_EQ(r.confidence, DiscoveryConfidence::Low);
}

TEST_F(NameClassifierTest, HandlesWhitespace) {
    EXPECT_EQ(classifier.classify("  Kick  ").role, InstrumentRole::Kick);
    EXPECT_EQ(classifier.classify("\tSnare\t").role, InstrumentRole::Snare);
}

TEST_F(NameClassifierTest, CaseInsensitive) {
    EXPECT_EQ(classifier.classify("KICK").role, InstrumentRole::Kick);
    EXPECT_EQ(classifier.classify("kick").role, InstrumentRole::Kick);
    EXPECT_EQ(classifier.classify("SNARE").role, InstrumentRole::Snare);
    EXPECT_EQ(classifier.classify("VOX").role, InstrumentRole::LeadVocal);
}
