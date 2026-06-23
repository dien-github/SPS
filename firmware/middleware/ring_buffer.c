#include "ring_buffer.h"

void ring_buffer_init(ring_buffer_t *rb, uint8_t *storage, size_t capacity)
{
    if (rb == 0)
    {
        return;
    }

    rb->buffer = storage;
    rb->capacity = capacity;
    rb->head = 0u;
    rb->tail = 0u;
    rb->overflow_count = 0u;
}

bool ring_buffer_push(ring_buffer_t *rb, uint8_t byte)
{
    return ring_buffer_push_isr(rb, byte);
}

bool ring_buffer_push_isr(ring_buffer_t *rb, uint8_t byte)
{
    size_t next;

    if ((rb == 0) || (rb->buffer == 0) || (rb->capacity < 2u))
    {
        return false;
    }

    next = (rb->head + 1u) % rb->capacity;
    if (next == rb->tail)
    {
        rb->overflow_count++;
        return false;
    }

    rb->buffer[rb->head] = byte;
    rb->head = next;
    return true;
}

bool ring_buffer_pop(ring_buffer_t *rb, uint8_t *byte)
{
    if ((rb == 0) || (byte == 0) || (rb->buffer == 0) || (rb->tail == rb->head))
    {
        return false;
    }

    *byte = rb->buffer[rb->tail];
    rb->tail = (rb->tail + 1u) % rb->capacity;
    return true;
}

size_t ring_buffer_count(const ring_buffer_t *rb)
{
    size_t head;
    size_t tail;

    if ((rb == 0) || (rb->buffer == 0) || (rb->capacity == 0u))
    {
        return 0u;
    }

    head = rb->head;
    tail = rb->tail;
    if (head >= tail)
    {
        return head - tail;
    }

    return rb->capacity - tail + head;
}

void ring_buffer_clear(ring_buffer_t *rb)
{
    if (rb != 0)
    {
        rb->head = 0u;
        rb->tail = 0u;
        rb->overflow_count = 0u;
    }
}
