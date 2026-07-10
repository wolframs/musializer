#ifndef MUSIALIZER_SAMPLE_RING_H_
#define MUSIALIZER_SAMPLE_RING_H_

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct SampleFrame {
    float left;
    float right;
} SampleFrame;

/*
 * A bounded single-producer/single-consumer queue. Storage is caller-owned and
 * capacity must be a power of two (which also makes index rollover safe).
 * On overflow, new frames are dropped; queued audio is never overwritten.
 * Push/pop perform no allocation, locking, I/O, or blocking.
 */
typedef struct SampleRing {
    SampleFrame *storage;
    size_t capacity;
    _Atomic size_t head;
    _Atomic size_t tail;
    _Atomic size_t dropped;
} SampleRing;

bool sample_ring_init(SampleRing *ring, SampleFrame *storage, size_t capacity);

/* Only call reset while producer and consumer are stopped. */
void sample_ring_reset(SampleRing *ring);

bool sample_ring_push(SampleRing *ring, SampleFrame frame);
bool sample_ring_pop(SampleRing *ring, SampleFrame *frame);
size_t sample_ring_push_many(SampleRing *ring, const SampleFrame *frames, size_t count);
size_t sample_ring_pop_many(SampleRing *ring, SampleFrame *frames, size_t capacity);

size_t sample_ring_count(const SampleRing *ring);
size_t sample_ring_capacity(const SampleRing *ring);
uint64_t sample_ring_dropped(const SampleRing *ring);

#endif
