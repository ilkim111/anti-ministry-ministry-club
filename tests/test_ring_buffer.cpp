#include <gtest/gtest.h>
#include "audio/RingBuffer.hpp"
#include <vector>

TEST(RingBufferTest, WriteAndRead) {
    RingBuffer buf(1024);
    float data[] = {1.0f, 2.0f, 3.0f};
    EXPECT_EQ(buf.write(data, 3), 3u);
    EXPECT_EQ(buf.available(), 3u);

    float out[3];
    EXPECT_EQ(buf.read(out, 3), 3u);
    EXPECT_FLOAT_EQ(out[0], 1.0f);
    EXPECT_FLOAT_EQ(out[1], 2.0f);
    EXPECT_FLOAT_EQ(out[2], 3.0f);
    EXPECT_EQ(buf.available(), 0u);
}

TEST(RingBufferTest, WrapAround) {
    RingBuffer buf(4);  // tiny buffer

    float data1[] = {1.0f, 2.0f, 3.0f};
    buf.write(data1, 3);

    float out[2];
    buf.read(out, 2);  // read 2, leaving 1
    EXPECT_FLOAT_EQ(out[0], 1.0f);
    EXPECT_FLOAT_EQ(out[1], 2.0f);

    // Now write 2 more — should wrap around
    float data2[] = {4.0f, 5.0f};
    EXPECT_EQ(buf.write(data2, 2), 2u);
    EXPECT_EQ(buf.available(), 3u);

    float out2[3];
    buf.read(out2, 3);
    EXPECT_FLOAT_EQ(out2[0], 3.0f);
    EXPECT_FLOAT_EQ(out2[1], 4.0f);
    EXPECT_FLOAT_EQ(out2[2], 5.0f);
}

TEST(RingBufferTest, ReadMoreThanAvailable) {
    RingBuffer buf(1024);
    float data[] = {1.0f};
    buf.write(data, 1);

    float out[10];
    EXPECT_EQ(buf.read(out, 10), 1u);
    EXPECT_FLOAT_EQ(out[0], 1.0f);
}

TEST(RingBufferTest, WriteMoreThanCapacity) {
    RingBuffer buf(4);

    float data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    // Can only write 4 (capacity)
    size_t written = buf.write(data, 8);
    EXPECT_EQ(written, 4u);
    EXPECT_EQ(buf.available(), 4u);
}

TEST(RingBufferTest, EmptyBufferReadReturnsZero) {
    RingBuffer buf(1024);
    float out[10];
    EXPECT_EQ(buf.read(out, 10), 0u);
    EXPECT_EQ(buf.available(), 0u);
}

TEST(RingBufferTest, ResetClearsBuffer) {
    RingBuffer buf(1024);
    float data[] = {1.0f, 2.0f};
    buf.write(data, 2);
    EXPECT_EQ(buf.available(), 2u);

    buf.reset();
    EXPECT_EQ(buf.available(), 0u);
}

TEST(RingBufferTest, ManyWriteReadCycles) {
    RingBuffer buf(64);

    for (int cycle = 0; cycle < 100; cycle++) {
        float data[8];
        for (int i = 0; i < 8; i++) data[i] = (float)(cycle * 8 + i);
        EXPECT_EQ(buf.write(data, 8), 8u);

        float out[8];
        EXPECT_EQ(buf.read(out, 8), 8u);
        for (int i = 0; i < 8; i++) {
            EXPECT_FLOAT_EQ(out[i], (float)(cycle * 8 + i));
        }
    }
}
