#pragma once
// Loop points taken from the waveform instead of off the clock.
//
// The instrument builder places a sustain's loop by time alone: a fixed
// margin after the note-on, a fixed margin before the note-off, and a long
// equal-power crossfade over the join (see fastresampler_instrument.cpp).
// That never clicks, but it is deaf to what the note is doing, and it leaves
// two artefacts behind.
//
// The first is phase. FastSampler's crossfade is not a splice: over the last
// xfadeframes before loop_end it mixes the material there against the same
// length of material from loop_start onwards, then carries on from
// loop_start + xfadeframes (fastsampler_voice.cpp, the xfade segment). So
// frame loop_end - C + i is heard on top of frame loop_start + i for the
// whole fade. On a harmonic note whose loop length is not a whole number of
// periods those two copies fight, and the fade comes out hollow and phasey --
// comb filtering that lasts its full length and returns on every wrap. A
// zero crossing at the join would do nothing about it; only the loop length
// matters, and only in whole periods of the fundamental.
//
// The second is level. A note that decays through the hold is louder at
// loop_start than at loop_end, so every wrap steps the level back up.
//
// This file fixes both, from the mono sum of the held note:
//
//   1. An RMS envelope says where the attack has settled, and loop_start
//      goes there rather than at a guessed margin -- later on a slow pad,
//      earlier on a fast one. It then moves on, as far as a minimum loop
//      length allows, until the level the wrap lands on matches the level it
//      left, which is what takes the step out.
//
//   2. loop_end moves within one period of where it was, to whichever loop
//      length correlates best over the whole crossfade. The root note gives
//      the period outright, so the search is one period wide and needs no
//      pitch detection. It runs decimated first, then at full rate around
//      the winner.
//
//   3. The correlation it won by is the confidence. Below a floor the note
//      has no phase to lock to -- breath, ensemble detune, percussion -- and
//      loop_end stays where the clock put it, leaving the long crossfade to
//      average the two ends as it does today. That is what keeps the
//      analysis from ever being worse than no analysis.
//
// One shared mono signal decides for both channels, so a stereo pair always
// lands in phase together.

#include <stdint.h>
#include "fastresampler_wav.h"

// A sustain's loop, in frames from the start of the held note.
struct LoopBounds {
    uint32_t loop_start;
    uint32_t loop_end;
    uint32_t xfade_frames;
};

// What the analysis made of one zone.
struct LoopResult {
    LoopBounds bounds;
    float      score;        // the correlation the chosen loop length won, 0..1
    bool       aligned;      // the score cleared the floor, so loop_end moved
    int        start_shift;  // frames loop_start moved, signed
    int        end_shift;    // frames loop_end moved, signed
};

// The buffers the analysis works in, sized once for the longest zone and
// reused for the rest: a run maps hundreds of them.
struct LoopAnalyzer {
    float*  mono;       // the held note, one channel
    float*  coarse;     // the same, decimated for the first pass
    float*  envelope;   // RMS per block, in dB
    int64_t capacity;   // frames the two signal buffers hold
    int     env_capacity;
    int     sample_rate;
};

// False when the buffers could not be had; the caller then keeps the timed
// loop points, which is a working instrument either way.
bool loop_analyzer_init(LoopAnalyzer* a, int64_t max_frames, int sample_rate);
void loop_analyzer_free(LoopAnalyzer* a);

// Moves one zone's loop onto the waveform. first_frame and frame_count are
// the held note's span in the full WAV, root_note the note it was played at,
// and nominal the loop the clock gave it. False when the audio could not be
// read or the zone is too short to analyse, and out is left untouched.
bool loop_refine(LoopAnalyzer* a, WavReader* wav, int64_t first_frame, int64_t frame_count,
                 int root_note, const LoopBounds* nominal, LoopResult* out);
