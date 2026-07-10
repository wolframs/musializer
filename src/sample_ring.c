#include "sample_ring.h"

#include <stdint.h>

bool sample_ring_init(SampleRing *ring, SampleFrame *storage, size_t capacity)
{
    if (ring == NULL || storage == NULL || capacity == 0 ||
        (capacity & (capacity - 1)) != 0 || capacity > SIZE_MAX/2) {
        return false;
    }
    ring->storage = storage;
    ring->capacity = capacity;
    atomic_init(&ring->head, 0);
    atomic_init(&ring->tail, 0);
    atomic_init(&ring->dropped, 0);
    /* Realtime safety is stronger than merely using the C atomic API. */
    if (!atomic_is_lock_free(&ring->head) || !atomic_is_lock_free(&ring->tail) ||
        !atomic_is_lock_free(&ring->dropped)) {
        ring->storage = NULL;
        ring->capacity = 0;
        return false;
    }
    return true;
}

void sample_ring_reset(SampleRing *ring)
{
    if (ring == NULL) return;
    atomic_store_explicit(&ring->head, 0, memory_order_relaxed);
    atomic_store_explicit(&ring->tail, 0, memory_order_relaxed);
    atomic_store_explicit(&ring->dropped, 0, memory_order_relaxed);
}

bool sample_ring_push(SampleRing *ring, SampleFrame frame)
{
    if (ring == NULL || ring->storage == NULL || ring->capacity == 0) return false;
    size_t head = atomic_load_explicit(&ring->head, memory_order_relaxed);
    size_t tail = atomic_load_explicit(&ring->tail, memory_order_acquire);
    if (head - tail >= ring->capacity) {
        atomic_fetch_add_explicit(&ring->dropped, 1, memory_order_relaxed);
        return false;
    }
    ring->storage[head & (ring->capacity - 1)] = frame;
    atomic_store_explicit(&ring->head, head + 1, memory_order_release);
    return true;
}

bool sample_ring_pop(SampleRing *ring, SampleFrame *frame)
{
    if (ring == NULL || frame == NULL || ring->storage == NULL || ring->capacity == 0) {
        return false;
    }
    size_t tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);
    size_t head = atomic_load_explicit(&ring->head, memory_order_acquire);
    if (tail == head) return false;
    *frame = ring->storage[tail & (ring->capacity - 1)];
    atomic_store_explicit(&ring->tail, tail + 1, memory_order_release);
    return true;
}

size_t sample_ring_push_many(SampleRing *ring, const SampleFrame *frames, size_t count)
{
    if (ring == NULL || (frames == NULL && count != 0)) return 0;
    size_t pushed = 0;
    while (pushed < count && sample_ring_push(ring, frames[pushed])) pushed += 1;
    if (pushed < count) {
        /* sample_ring_push counted the first rejection; account for the rest. */
        atomic_fetch_add_explicit(&ring->dropped, count - pushed - 1, memory_order_relaxed);
    }
    return pushed;
}

size_t sample_ring_pop_many(SampleRing *ring, SampleFrame *frames, size_t capacity)
{
    if (ring == NULL || (frames == NULL && capacity != 0)) return 0;
    size_t popped = 0;
    while (popped < capacity && sample_ring_pop(ring, &frames[popped])) popped += 1;
    return popped;
}

size_t sample_ring_count(const SampleRing *ring)
{
    if (ring == NULL) return 0;
    size_t head = atomic_load_explicit(&ring->head, memory_order_acquire);
    size_t tail = atomic_load_explicit(&ring->tail, memory_order_acquire);
    size_t count = head - tail;
    return count < ring->capacity ? count : ring->capacity;
}

size_t sample_ring_capacity(const SampleRing *ring)
{
    return ring == NULL ? 0 : ring->capacity;
}

uint64_t sample_ring_dropped(const SampleRing *ring)
{
    return ring == NULL ? 0 : (uint64_t)atomic_load_explicit(&ring->dropped, memory_order_relaxed);
}
