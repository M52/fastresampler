#pragma once

#include <stdint.h>
#include <stdio.h>
#include <atomic>
#include "fastresampler_constants.h"

struct MidiEvent {
    uint8_t status;
    uint8_t channel;
    uint8_t data0;
    uint8_t data1;
};

struct MidiQueue {
    MidiEvent events[FR_MIDI_QUEUE_CAPACITY];
    std::atomic<uint32_t> head;
    std::atomic<uint32_t> tail;
};

static_assert((FR_MIDI_QUEUE_CAPACITY & (FR_MIDI_QUEUE_CAPACITY - 1)) == 0,
              "FR_MIDI_QUEUE_CAPACITY must be a power of two");

inline void midi_queue_init(MidiQueue* q) {
    q->head.store(0, std::memory_order_relaxed);
    q->tail.store(0, std::memory_order_relaxed);
}

inline bool midi_queue_push(MidiQueue* q, MidiEvent e) {
    uint32_t head = q->head.load(std::memory_order_relaxed);
    uint32_t tail = q->tail.load(std::memory_order_acquire);
    if (head - tail >= FR_MIDI_QUEUE_CAPACITY) return false;
    q->events[head & (FR_MIDI_QUEUE_CAPACITY - 1)] = e;
    q->head.store(head + 1, std::memory_order_release);
    return true;
}

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

inline int midi_note_parse(const char* text) {
    static const int letter_pc[7] = { 9, 11, 0, 2, 4, 5, 7 };
    const char* s = text;
    while (*s == ' ') ++s;
    char letter = *s;
    if (letter >= 'a' && letter <= 'g') letter = (char)(letter - 'a' + 'A');
    if (letter < 'A' || letter > 'G') return -1;
    int pc = letter_pc[letter - 'A'];
    ++s;
    if (*s == '#') { pc += 1; ++s; }
    else if (*s == 'b') { pc -= 1; ++s; }
    bool negative = false;
    if (*s == '-') { negative = true; ++s; }
    if (*s < '0' || *s > '9') return -1;
    int octave = 0;
    while (*s >= '0' && *s <= '9') octave = octave * 10 + (*s++ - '0');
    while (*s == ' ') ++s;
    if (*s != '\0') return -1;
    if (negative) octave = -octave;
    int note = (octave + 2) * SEMITONES_PER_OCTAVE + pc;
    return (note >= 0 && note < MIDI_NOTE_COUNT) ? note : -1;
}

inline const char* midi_note_name(int note, char* buf, size_t buf_size) {
    static const char* names[SEMITONES_PER_OCTAVE] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (note < 0 || note >= MIDI_NOTE_COUNT) {
        snprintf(buf, buf_size, "?");
        return buf;
    }
    snprintf(buf, buf_size, "%s%d", names[note % SEMITONES_PER_OCTAVE], note / SEMITONES_PER_OCTAVE - 2);
    return buf;
}

inline const char* midi_cc_name(int cc) {
    switch (cc) {
        case 0:   return "Bank select";
        case 1:   return "Mod wheel";
        case 2:   return "Breath";
        case 4:   return "Foot";
        case 5:   return "Portamento time";
        case 7:   return "Volume";
        case 8:   return "Balance";
        case 10:  return "Pan";
        case 11:  return "Expression";
        case 64:  return "Sustain";
        case 65:  return "Portamento";
        case 66:  return "Sostenuto";
        case 67:  return "Soft pedal";
        case 68:  return "Legato";
        case 71:  return "Resonance";
        case 72:  return "Release";
        case 73:  return "Attack";
        case 74:  return "Cutoff";
        case 91:  return "Reverb";
        case 93:  return "Chorus";
        case 120: return "All sound off";
        case 121: return "Reset controllers";
        case 123: return "All notes off";
        default:  return "";
    }
}

inline void midi_cc_label(int cc, char* buf, size_t buf_size) {
    const char* name = midi_cc_name(cc);
    if (name[0]) {
        snprintf(buf, buf_size, "CC%d %s", cc, name);
    } else {
        snprintf(buf, buf_size, "CC%d", cc);
    }
}
