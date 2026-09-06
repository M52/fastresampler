#pragma once
// A single-producer, single-consumer ring of MIDI events. The UI thread pushes
// (GUI keyboard, CC sliders), the audio thread pops at the start of each
// block. Capacity is a power of two so the indices wrap with a mask.

#include <stdint.h>
#include <stdio.h>
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

// --- Note names -------------------------------------------------------------
// FastSampler's convention: middle C (60) is C3, so note 0 is C-2 and
// note 127 is G8. Accidentals are '#' or 'b'.

// "C#3" or "Db-1" to a MIDI note; -1 when the text is not a note.
inline int midi_note_parse(const char* text) {
    static const int letter_pc[7] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
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

// --- Controller names -------------------------------------------------------

// The name of a controller with a standard meaning, else an empty string.
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

// "CC1 Mod wheel", or "CC3" for a controller without a name.
inline void midi_cc_label(int cc, char* buf, size_t buf_size) {
    const char* name = midi_cc_name(cc);
    if (name[0]) {
        snprintf(buf, buf_size, "CC%d %s", cc, name);
    } else {
        snprintf(buf, buf_size, "CC%d", cc);
    }
}

// A white key: no sharp in its name.
inline bool midi_note_is_white(int note) {
    int pc = note % SEMITONES_PER_OCTAVE;
    return pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 11;
}
