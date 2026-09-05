#pragma once
// A single-producer, single-consumer ring of MIDI events. The UI thread pushes
// (GUI keyboard, CC sliders), the audio thread pops at the start of each
// block. Capacity is a power of two so the indices wrap with a mask.

#include <stdint.h>
#include <atomic>
#include "fastresampler_constants.h"

struct MidiEvent {
    uint8_t status;    // MIDI_STATUS_* without the channel nibble
    uint8_t channel;   // 0..15
    uint8_t data0;     // note / controller / bend LSB
    uint8_t data1;     // velocity / value / bend MSB
};

struct MidiQueue {
    MidiEvent events[FR_MIDI_QUEUE_CAPACITY];
    std::atomic<uint32_t> head;   // next slot the producer writes
    std::atomic<uint32_t> tail;   // next slot the consumer reads
};

static_assert((FR_MIDI_QUEUE_CAPACITY & (FR_MIDI_QUEUE_CAPACITY - 1)) == 0,
              "FR_MIDI_QUEUE_CAPACITY must be a power of two");

inline void midi_queue_init(MidiQueue* q) {
    q->head.store(0, std::memory_order_relaxed);
    q->tail.store(0, std::memory_order_relaxed);
}

// Producer side. Returns false when the ring is full; the event is dropped.
inline bool midi_queue_push(MidiQueue* q, MidiEvent e) {
    uint32_t head = q->head.load(std::memory_order_relaxed);
    uint32_t tail = q->tail.load(std::memory_order_acquire);
    if (head - tail >= FR_MIDI_QUEUE_CAPACITY) return false;
    q->events[head & (FR_MIDI_QUEUE_CAPACITY - 1)] = e;
    q->head.store(head + 1, std::memory_order_release);
    return true;
}

// Consumer side. Returns false when the ring is empty.
inline bool midi_queue_pop(MidiQueue* q, MidiEvent* out) {
    uint32_t tail = q->tail.load(std::memory_order_relaxed);
    uint32_t head = q->head.load(std::memory_order_acquire);
    if (tail == head) return false;
    *out = q->events[tail & (FR_MIDI_QUEUE_CAPACITY - 1)];
    q->tail.store(tail + 1, std::memory_order_release);
    return true;
}

inline MidiEvent midi_make(uint8_t status, int channel, int data0, int data1) {
    MidiEvent e;
    e.status = status;
    e.channel = (uint8_t)(channel & 0x0F);
    e.data0 = (uint8_t)(data0 & 0x7F);
    e.data1 = (uint8_t)(data1 & 0x7F);
    return e;
}
