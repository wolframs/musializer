#include "sample_ring.h"
#include "test_support.h"

TEST(sample_ring_validates_caller_owned_storage)
{
    SampleRing ring = {0};
    SampleFrame storage[4];
    EXPECT_FALSE(sample_ring_init(NULL, storage, 4));
    EXPECT_FALSE(sample_ring_init(&ring, NULL, 4));
    EXPECT_FALSE(sample_ring_init(&ring, storage, 0));
    EXPECT_FALSE(sample_ring_init(&ring, storage, 3));
    EXPECT_TRUE(sample_ring_init(&ring, storage, 4));
    EXPECT_EQ_SIZE(sample_ring_capacity(&ring), 4);
    EXPECT_EQ_SIZE(sample_ring_count(&ring), 0);
}
TEST(sample_ring_wraparound_preserves_fifo_order)
{
    SampleRing ring;
    SampleFrame storage[4];
    REQUIRE_TRUE(sample_ring_init(&ring, storage, 4));
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(sample_ring_push(&ring, (SampleFrame){(float)i, (float)(i + 10)}));
    }
    EXPECT_EQ_SIZE(sample_ring_count(&ring), 4);
    EXPECT_FALSE(sample_ring_push(&ring, (SampleFrame){99, 99}));
    EXPECT_EQ_U64(sample_ring_dropped(&ring), 1);

    SampleFrame frame;
    EXPECT_TRUE(sample_ring_pop(&ring, &frame));
    EXPECT_NEAR(frame.left, 0.0f, 0.0f);
    EXPECT_TRUE(sample_ring_pop(&ring, &frame));
    EXPECT_NEAR(frame.left, 1.0f, 0.0f);
    EXPECT_TRUE(sample_ring_push(&ring, (SampleFrame){4, 14}));
    EXPECT_TRUE(sample_ring_push(&ring, (SampleFrame){5, 15}));

    for (size_t expected = 2; expected < 6; ++expected) {
        EXPECT_TRUE(sample_ring_pop(&ring, &frame));
        EXPECT_NEAR(frame.left, (float)expected, 0.0f);
        EXPECT_NEAR(frame.right, (float)(expected + 10), 0.0f);
    }
    EXPECT_FALSE(sample_ring_pop(&ring, &frame));
    EXPECT_EQ_SIZE(sample_ring_count(&ring), 0);
}

TEST(sample_ring_bulk_operations_report_every_dropped_frame)
{
    SampleRing ring;
    SampleFrame storage[4];
    SampleFrame input[6];
    SampleFrame output[6] = {0};
    for (size_t i = 0; i < 6; ++i) input[i] = (SampleFrame){(float)i, (float)-i};
    REQUIRE_TRUE(sample_ring_init(&ring, storage, 4));

    EXPECT_EQ_SIZE(sample_ring_push_many(&ring, input, 6), 4);
    EXPECT_EQ_U64(sample_ring_dropped(&ring), 2);
    EXPECT_EQ_SIZE(sample_ring_pop_many(&ring, output, 6), 4);
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_NEAR(output[i].left, input[i].left, 0.0f);
        EXPECT_NEAR(output[i].right, input[i].right, 0.0f);
    }
    EXPECT_EQ_SIZE(sample_ring_push_many(&ring, NULL, 1), 0);
    EXPECT_EQ_SIZE(sample_ring_pop_many(&ring, NULL, 1), 0);
    EXPECT_EQ_U64(sample_ring_dropped(&ring), 2);
}

TEST(sample_ring_reset_clears_indices_and_diagnostics)
{
    SampleRing ring;
    SampleFrame storage[2];
    SampleFrame frame;
    REQUIRE_TRUE(sample_ring_init(&ring, storage, 2));
    EXPECT_TRUE(sample_ring_push(&ring, (SampleFrame){1, 2}));
    EXPECT_TRUE(sample_ring_push(&ring, (SampleFrame){3, 4}));
    EXPECT_FALSE(sample_ring_push(&ring, (SampleFrame){5, 6}));
    sample_ring_reset(&ring);
    EXPECT_EQ_SIZE(sample_ring_count(&ring), 0);
    EXPECT_EQ_U64(sample_ring_dropped(&ring), 0);
    EXPECT_FALSE(sample_ring_pop(&ring, &frame));
}
