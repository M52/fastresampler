#pragma once
// Where a legato move lands on the note it goes to, read out of the audio
// instead of off the clock.
//
// A transition sample holds three things in a row: the note it leaves, the
// move, and the note it lands on. The instrument has to know the two frames
// that divide them. FastSampler calls them the departure and the arrival,
// keeps them on the zone as departureframe and arrivalframe, and uses each
// to place one half of the handover: the note being left finishes fading at
// the departure, and the note landed on starts its sustain at the arrival
// (fastsampler_cfx_legato_dsp.cpp, legato_source_delay_samples and
// legato_target_delay_samples). Between the two nothing but the move itself
// sounds, which is what keeps a slur off the notes either side of it.
//
// The clock cannot say where it is. Many libraries hold the move back by an
// amount they read off the velocity of the note pressed -- Cinematic Studio
// Strings takes 333, 250 or 100 ms over three velocity zones -- and the
// figure is rarely published. The Resample tab therefore takes a nominal
// time per velocity, and this file finds the real one.
//
// How: both notes are known, so both fundamentals are. A bank of one-pole
// complex resonators, one per harmonic of each note, gives the energy at
// each of the two pitches over time. Their share
//
//     r(t) = E_target / (E_source + E_target)
//
// starts near 0, where only the note being left sounds, and ends near 1. The
// arrival is where it leaves the floor on its way up.
//
// The two bounds are two heights of the same curve. The departure is where
// r leaves its floor, the first sign that the note being left is giving way;
// the arrival is where r has reached the level it settles at, so the note
// landed on is all that is left. Reading both from one curve costs nothing
// over reading either.
//
// The resonators are constant-Q, so the band is always the same fraction of
// the centre frequency: wide enough for the vibrato of a string section to
// stay inside it, narrow enough to keep a semitone apart at every harmonic.
// A harmonic the two notes share -- the fourth harmonic of the note left is
// the third of the note landed on, a fifth up -- carries no information and
// is dropped from both sums.
//
// The share r has to end high for the reading to mean anything. Below a
// floor the note has no clear pitch to follow -- percussion, breath, a
// portamento that never settles -- and the caller keeps the nominal time,
// which is what makes the analysis never worse than none. Two notes that
// share too many partials to be told apart -- an octave shares all of them,
// a fifth most -- are refused for the same reason.
//
// A filter answers a little after the thing it hears, so the frame this
// finds is a few tens of milliseconds past the real one. That is the safe
// side: the sustain of the note landed on then starts inside material that
// is already that note, rather than before it has got there.

#include <stdint.h>
#include "fastresampler_wav.h"

// Harmonics of each note the sums run over.
#define LEGATO_HARMONICS 6

// What the analysis made of one transition. Both frames are from the start
// of the sample, and departure is never past arrival.
struct LegatoBounds {
    int64_t departure;  // where the move leaves the note it is played from
    int64_t arrival;    // where it has reached the note it goes to
    float   score;      // how far the share rose over the capture, 0..1
    bool    found;      // the score cleared the floor, so both are the audio's
};

// The buffers the analysis works in, sized once for the longest transition
// and reused for the rest: a run captures hundreds of them.
struct LegatoAnalyzer {
    float*  mono;       // the capture, one channel
    float*  work;       // the same, decimated for the resonators
    int64_t capacity;   // frames the two buffers hold
    int     sample_rate;
};

// False when the buffers could not be had; the caller then keeps the nominal
// arrival, which is a working instrument either way.
bool legato_analyzer_init(LegatoAnalyzer* a, int64_t max_frames, int sample_rate);
void legato_analyzer_free(LegatoAnalyzer* a);

// Reads one transition out of the full WAV. first_frame and frame_count are
// its span there, note_on the frame the note landed on was pressed at (from
// first_frame), and the two nominals the bounds the clock gave it, which are
// what comes back when the audio cannot be believed. False when the audio
// could not be read or the two notes share too much to tell apart, and out
// is left untouched.
bool legato_find_bounds(LegatoAnalyzer* a, WavReader* wav, int64_t first_frame, int64_t frame_count,
                        int source_note, int target_note, int64_t note_on,
                        int64_t nominal_departure, int64_t nominal_arrival,
                        LegatoBounds* out);
