// #pragma once (removed for unity build)
// See fastresampler_loop.h.

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "fastresampler_loop.h"
#include "fastresampler_constants.h"
#include "fastresampler_helpers.h"

// The RMS envelope: one value per block of this long. 10 ms is short enough
// to see an attack and long enough that the fundamental of a low note does
// not modulate the reading.
#define LOOP_ENV_BLOCK_SECONDS      0.010f
// The span the envelope's slope is measured over, to decide the attack is
// over. Shorter than this and a tremolo reads as an attack.
#define LOOP_SETTLE_WINDOW_SECONDS  0.100f
// The attack counts as over once the envelope moves slower than this, in dB
// per second: the floor, and a multiple of how fast the settled tail of the
// same note moves, whichever is looser. The second term is what lets a note
// that keeps decaying settle at all.
#define LOOP_SETTLE_FLOOR_DB_PER_S  2.0f
#define LOOP_SETTLE_TAIL_FACTOR     1.5f
// The part of the hold taken as the settled tail the reference is measured
// over: the last third.
#define LOOP_TAIL_FRACTION          3
// How near the loudest block counts as having arrived, in dB. The attack is
// looked for from the first block this close to the peak, not from the peak
// itself, which on a steady tone is an arbitrary block.
#define LOOP_PEAK_TOLERANCE_DB      1.0f

// How close the level the wrap lands on has to be to the level it left, in
// dB, before loop_start stops being moved later to match it. Moving costs
// loop length, so a difference this small is left alone.
#define LOOP_LEVEL_TOLERANCE_DB     1.0f

// The loop never starts before this, however flat the envelope reads: the
// first moments of a note are its attack whatever the RMS says.
#define LOOP_MIN_START_SECONDS      0.02f
// The loop is never shortened below this, nor below twice the crossfade --
// which is also the longest crossfade FastSampler will run over a loop.
#define LOOP_MIN_LENGTH_SECONDS     0.5f

// The decimation of the first correlation pass, as samples kept per period
// of the fundamental. Eight is plenty to find which period the best loop
// length sits in; the full-rate passes settle where inside it.
#define LOOP_COARSE_PER_PERIOD      8
// Each full-rate pass narrows the search by this much, so the cost of
// finding one frame out of a low note's period stays a few dozen windows
// rather than a few thousand.
#define LOOP_REFINE_FACTOR          4
// How many of the coarse pass's peaks are polished at full rate. A range one
// period wide holds two or three, and which of them really crossfades best
// is a question only the undecimated signal can answer.
#define LOOP_COARSE_CANDIDATES      3
// A search range no wider than this many lengths is simply walked at full
// rate. A high note's period is a few dozen frames, and steering a decimated
// search towards the right one of those costs more windows than trying them
// all -- and unlike the decimated search, walking them cannot miss.
#define LOOP_DIRECT_MAX_LENGTHS     260
// The narrowest search, in frames, whatever the period. One period is all a
// low note needs: its period is hundreds of frames, so some whole number of
// them lands within half a frame of any length. A high note's does not --
// its period is a few dozen frames and a fractional one at that, so no whole
// number of them lines up exactly and the best available alignment can be
// several periods off. Those periods are a millisecond or two between them,
// so there is nothing to lose by looking across a few.
#define LOOP_MIN_REACH_FRAMES       64
// The shortest period worth aligning, in frames. Above roughly this pitch
// the crossfade already spans hundreds of periods and the loop length is
// within a frame or two of ideal wherever it lands.
#define LOOP_MIN_PERIOD_FRAMES      4

// The correlation a loop length has to win by before loop_end is moved onto
// it. Below this the note has no stable phase to lock to -- breath noise,
// an ensemble, a struck body -- and the timed loop point with its long
// crossfade is the better answer.
#define LOOP_MIN_SCORE              0.5f

// Concert A and its MIDI note, for the period of a root note.
#define LOOP_A4_HZ                  440.0f
#define LOOP_A4_NOTE                69

// --- The signal ------------------------------------------------------------------

bool loop_analyzer_init(LoopAnalyzer* a, int64_t max_frames, int sample_rate) {
    memset(a, 0, sizeof(*a));
    if (max_frames <= 0 || sample_rate <= 0) return false;
    int64_t block_frames = (int64_t)(LOOP_ENV_BLOCK_SECONDS * (float)sample_rate + 0.5f);
    if (block_frames < 1) block_frames = 1;
    int env_capacity = (int)(max_frames / block_frames) + 1;
    a->mono = (float*)malloc((size_t)max_frames * sizeof(float));
    a->coarse = (float*)malloc((size_t)max_frames * sizeof(float));
    a->envelope = (float*)malloc((size_t)env_capacity * sizeof(float));
    if (!a->mono || !a->coarse || !a->envelope) {
        loop_analyzer_free(a);
        return false;
    }
    a->capacity = max_frames;
    a->env_capacity = env_capacity;
    a->sample_rate = sample_rate;
    return true;
}

void loop_analyzer_free(LoopAnalyzer* a) {
    free(a->mono);
    free(a->coarse);
    free(a->envelope);
    memset(a, 0, sizeof(*a));
}

// The held note as one channel, read a block at a time so only the mono copy
// is ever held whole. A stereo pair has to wrap in phase together, so one
// shared signal decides for both.
static bool loop_load_mono(LoopAnalyzer* a, WavReader* wav, int64_t first, int64_t count) {
    float buf[FR_MAX_BLOCK_SIZE * FR_OUTPUT_CHANNELS];
    float scale = 1.0f / (float)wav->channels;
    int64_t done = 0;
    while (done < count) {
        int64_t n = count - done;
        if (n > FR_MAX_BLOCK_SIZE) n = FR_MAX_BLOCK_SIZE;
        if (!wav_reader_read(wav, first + done, n, buf)) return false;
        for (int64_t i = 0; i < n; ++i) {
            float sum = 0.0f;
            for (int c = 0; c < wav->channels; ++c) sum += buf[i * wav->channels + c];
            a->mono[done + i] = sum * scale;
        }
        done += n;
    }
    return true;
}

// The RMS envelope in dB, one value per block. Returns how many blocks it
// filled.
static int loop_envelope(LoopAnalyzer* a, int64_t count, int block_frames) {
    int blocks = (int)(count / block_frames);
    if (blocks > a->env_capacity) blocks = a->env_capacity;
    for (int b = 0; b < blocks; ++b) {
        const float* p = a->mono + (int64_t)b * block_frames;
        double sum = 0.0;
        for (int i = 0; i < block_frames; ++i) sum += (double)p[i] * (double)p[i];
        double rms = sqrt(sum / (double)block_frames);
        a->envelope[b] = (float)(20.0 * log10(rms > 1e-9 ? rms : 1e-9));
    }
    return blocks;
}

// --- Where the attack is over -----------------------------------------------------

// The envelope's slope at a block, in dB per second, over the settle window.
static float loop_slope(const float* env, int block, int window_blocks) {
    return (env[block + window_blocks] - env[block]) / LOOP_SETTLE_WINDOW_SECONDS;
}

// The first block at or after the envelope's peak that is no longer moving
// fast, which is where the note has stopped attacking. -1 when the hold is
// too short to tell or never settles, and the caller keeps the timed point.
static int loop_settle_block(const float* env, int blocks, int window_blocks) {
    int last = blocks - window_blocks;
    if (last <= 1) return -1;

    // Where the note has arrived, which is not the same as its loudest
    // block: on a steady tone every block is the loudest within a hair, and
    // an argmax over that picks whichever one noise favoured -- anywhere in
    // the hold. The first block within a hair of the maximum is the one that
    // means "the attack got here", and on a flat envelope it is block zero.
    float loudest = env[0];
    for (int b = 1; b < blocks; ++b) {
        if (env[b] > loudest) loudest = env[b];
    }
    int peak = 0;
    while (peak < last && env[peak] < loudest - LOOP_PEAK_TOLERANCE_DB) peak++;

    // How fast this note moves once it is settled, measured over the last
    // third of the hold. A note that decays throughout would never settle
    // against a fixed threshold; measuring its own tail is what lets it.
    int tail_first = blocks - blocks / LOOP_TAIL_FRACTION;
    if (tail_first > last - 1) tail_first = last - 1;
    if (tail_first < 0) tail_first = 0;
    double tail_sum = 0.0;
    int tail_count = 0;
    for (int b = tail_first; b < last; ++b) {
        tail_sum += fabs((double)loop_slope(env, b, window_blocks));
        tail_count++;
    }
    float reference = tail_count > 0 ? (float)(tail_sum / (double)tail_count) : 0.0f;
    float threshold = LOOP_SETTLE_TAIL_FACTOR * reference;
    if (threshold < LOOP_SETTLE_FLOOR_DB_PER_S) threshold = LOOP_SETTLE_FLOOR_DB_PER_S;

    for (int b = peak; b < last; ++b) {
        if (fabsf(loop_slope(env, b, window_blocks)) <= threshold) return b;
    }
    return -1;
}

// --- The loop length --------------------------------------------------------------

// Normalised cross-correlation of two windows, given the first one's norm.
// Level is divided out, so this measures phase agreement alone.
static float loop_ncc(const float* x, const float* y, int64_t n, double x_norm) {
    double dot = 0.0, y_sum = 0.0;
    for (int64_t i = 0; i < n; ++i) {
        dot += (double)x[i] * (double)y[i];
        y_sum += (double)y[i] * (double)y[i];
    }
    double y_norm = sqrt(y_sum);
    if (x_norm <= 0.0 || y_norm <= 0.0) return 0.0f;
    return (float)(dot / (x_norm * y_norm));
}

static double loop_norm(const float* x, int64_t n) {
    double sum = 0.0;
    for (int64_t i = 0; i < n; ++i) sum += (double)x[i] * (double)x[i];
    return sqrt(sum);
}

// The whole objective in one place: how well a loop of this length would
// crossfade. The fade mixes frame start + length - C + i against frame
// start + i for the whole of C, so the two windows the loop joins are these.
static float loop_score_at(const float* mono, int64_t start, int64_t length, int64_t xfade, double a_norm) {
    return loop_ncc(mono + start, mono + start + length - xfade, xfade, a_norm);
}

// One candidate loop length, narrowed to the frame at full rate: each pass
// searches what the pass before it could not resolve and hands on a window
// LOOP_REFINE_FACTOR times tighter, so a low note's period costs a few dozen
// windows rather than a few thousand.
static int64_t loop_polish(const float* mono, int64_t start, int64_t candidate, int64_t xfade,
                           double a_norm, int span, int64_t low, int64_t high, float* out_score) {
    int64_t best = candidate;
    float best_score = loop_score_at(mono, start, best, xfade, a_norm);
    while (span > 1) {
        int next = span / LOOP_REFINE_FACTOR;
        if (next < 1) next = 1;
        int64_t from = best - span, to = best + span;
        if (from < low) from = low;
        if (to > high) to = high;
        for (int64_t L = from; L <= to; L += next) {
            float sc = loop_score_at(mono, start, L, xfade, a_norm);
            if (sc > best_score) {
                best_score = sc;
                best = L;
            }
        }
        span = next;
    }
    *out_score = best_score;
    return best;
}

// --- One zone ----------------------------------------------------------------------

bool loop_refine(LoopAnalyzer* a, WavReader* wav, int64_t first_frame, int64_t frame_count,
                 int root_note, const LoopBounds* nominal, LoopResult* out) {
    if (frame_count <= 0 || frame_count > a->capacity) return false;
    if (nominal->loop_end <= nominal->loop_start || nominal->xfade_frames == 0) return false;

    int rate = a->sample_rate;
    int64_t xfade = (int64_t)nominal->xfade_frames;
    int64_t min_length = (int64_t)(LOOP_MIN_LENGTH_SECONDS * (float)rate + 0.5f);
    if (min_length < 2 * xfade) min_length = 2 * xfade;
    int64_t min_start = (int64_t)(LOOP_MIN_START_SECONDS * (float)rate + 0.5f);
    int64_t end = (int64_t)nominal->loop_end;
    int64_t max_start = end - min_length;
    if (max_start < min_start) return false;   // too short a hold to move anything

    if (!loop_load_mono(a, wav, first_frame, frame_count)) return false;

    int block_frames = (int)(LOOP_ENV_BLOCK_SECONDS * (float)rate + 0.5f);
    if (block_frames < 1) block_frames = 1;
    int blocks = loop_envelope(a, frame_count, block_frames);
    int window_blocks = (int)(LOOP_SETTLE_WINDOW_SECONDS / LOOP_ENV_BLOCK_SECONDS + 0.5f);

    // 1. Where the attack has settled. Failing that, where the clock put it.
    int settle = blocks > window_blocks ? loop_settle_block(a->envelope, blocks, window_blocks) : -1;
    int64_t start = settle >= 0 ? (int64_t)settle * block_frames : (int64_t)nominal->loop_start;
    if (start < min_start) start = min_start;
    if (start > max_start) start = max_start;

    // 2. The level the wrap lands on against the level it left. After the
    //    fade the note carries on from loop_start + xfade, and what it left
    //    behind was the level at loop_end, so those are the two to match. On
    //    a note that decays through the hold, moving loop_start later brings
    //    them together; it costs loop length, so it stops as soon as the
    //    difference is inaudible and never goes below the minimum.
    if (blocks > 0) {
        int env_last = blocks - 1;
        int target_block = fr_clampi((int)((end - 1) / block_frames), 0, env_last);
        float target = a->envelope[target_block];
        int first_block = (int)(start / block_frames);
        int last_block = (int)(max_start / block_frames);
        int chosen = -1;
        int closest = first_block;
        float closest_diff = -1.0f, first_diff = -1.0f;
        for (int b = first_block; b <= last_block; ++b) {
            int at = fr_clampi((int)(((int64_t)b * block_frames + xfade) / block_frames), 0, env_last);
            float diff = fabsf(a->envelope[at] - target);
            if (b == first_block) {
                first_diff = diff;
                closest_diff = diff;
            }
            if (diff <= LOOP_LEVEL_TOLERANCE_DB) {
                chosen = b;   // the earliest that matches, so the loop stays as long as it can
                break;
            }
            if (diff < closest_diff) {
                closest_diff = diff;
                closest = b;
            }
        }
        // Nothing matched outright. A note that decays right through the
        // hold never will inside the length it can spare, and there the
        // closest on offer is still worth having -- but only when it beats
        // staying put by more than the tolerance, so a rising note is left
        // with its long loop rather than shortened for nothing.
        if (chosen < 0 && closest_diff + LOOP_LEVEL_TOLERANCE_DB < first_diff) chosen = closest;
        if (chosen > first_block) {
            int64_t matched = (int64_t)chosen * block_frames;
            start = matched > max_start ? max_start : matched;
        }
    }

    int64_t length = end - start;
    double a_norm = loop_norm(a->mono + start, xfade);
    float score = loop_score_at(a->mono, start, length, xfade, a_norm);
    bool aligned = false;

    // 3. The loop length, in whole periods of the fundamental. The root note
    //    gives the period outright, so the true optimum is within half a
    //    period of wherever the clock landed and a search one period wide
    //    around it cannot miss -- no pitch detection, and the same few dozen
    //    windows whatever the note.
    float period = root_note >= 0 && root_note < MIDI_NOTE_COUNT
                       ? (float)rate / (LOOP_A4_HZ * powf(2.0f, (float)(root_note - LOOP_A4_NOTE) / (float)SEMITONES_PER_OCTAVE))
                       : 0.0f;
    int64_t reach = (int64_t)(period + 0.5f);
    if (reach < LOOP_MIN_REACH_FRAMES) reach = LOOP_MIN_REACH_FRAMES;
    if (period >= LOOP_MIN_PERIOD_FRAMES && reach < frame_count / 4) {
        int64_t low = length - reach;
        int64_t high = length + reach;
        if (low < min_length) low = min_length;
        if (high > frame_count - start) high = frame_count - start;

        int step = (int)(period / LOOP_COARSE_PER_PERIOD);
        if (step < 1) step = 1;
        int64_t coarse_xfade = xfade / step;
        int64_t coarse_count = (frame_count - start) / step;

        // The timed length is in the running throughout, scored on the real
        // signal like every candidate, so a search can only ever hand back a
        // crossfade at least as good as the one it was given.
        int64_t best = length;
        float best_score = score;
        bool searched = false;

        if (high >= low && high - low < LOOP_DIRECT_MAX_LENGTHS) {
            for (int64_t L = low; L <= high; ++L) {
                float sc = loop_score_at(a->mono, start, L, xfade, a_norm);
                if (sc > best_score) {
                    best_score = sc;
                    best = L;
                }
            }
            searched = true;
        } else if (high >= low && coarse_xfade >= LOOP_COARSE_PER_PERIOD && coarse_count > 0) {
            // The decimated signal is built from loop_start, so both windows
            // of every candidate sit on one grid and the decimation shifts
            // neither against the other.
            for (int64_t j = 0; j < coarse_count; ++j) {
                const float* p = a->mono + start + j * step;
                float sum = 0.0f;
                for (int i = 0; i < step; ++i) sum += p[i];
                a->coarse[j] = sum / (float)step;
            }
            double coarse_norm = loop_norm(a->coarse, coarse_xfade);

            // The correlation peaks once per period, so a range one period
            // wide holds two or three of them. The coarse pass cannot be
            // trusted to rank them: it scored a signal the decimation took
            // the harmonics out of, and the harmonics are what separate one
            // peak from the next. So it keeps the best few, a half period
            // apart at least, so that they are genuinely different peaks
            // rather than three points up the same one -- and each is then
            // polished and judged on the real signal.
            int64_t peak_length[LOOP_COARSE_CANDIDATES];
            float peak_score[LOOP_COARSE_CANDIDATES];
            int peak_count = 0;
            int64_t apart = (int64_t)(period / 2.0f);
            if (apart < 1) apart = 1;
            for (int64_t L = ((low + step - 1) / step) * step; L <= high; L += step) {
                int64_t jb = (L - xfade + step / 2) / step;
                if (jb < coarse_xfade || jb + coarse_xfade > coarse_count) continue;
                float sc = loop_ncc(a->coarse, a->coarse + jb, coarse_xfade, coarse_norm);
                int hit = -1;
                for (int i = 0; i < peak_count; ++i) {
                    if (L - peak_length[i] < apart && peak_length[i] - L < apart) hit = i;
                }
                if (hit >= 0) {
                    if (sc > peak_score[hit]) {
                        peak_score[hit] = sc;
                        peak_length[hit] = L;
                    }
                } else if (peak_count < LOOP_COARSE_CANDIDATES) {
                    peak_score[peak_count] = sc;
                    peak_length[peak_count] = L;
                    peak_count++;
                } else {
                    int weakest = 0;
                    for (int i = 1; i < peak_count; ++i) {
                        if (peak_score[i] < peak_score[weakest]) weakest = i;
                    }
                    if (sc > peak_score[weakest]) {
                        peak_score[weakest] = sc;
                        peak_length[weakest] = L;
                    }
                }
            }

            for (int i = 0; i < peak_count; ++i) {
                float sc = 0.0f;
                int64_t L = loop_polish(a->mono, start, peak_length[i], xfade, a_norm, 2 * step, low, high, &sc);
                if (sc > best_score) {
                    best_score = sc;
                    best = L;
                }
            }
            searched = true;
        }

        if (searched) {
            if (best_score >= LOOP_MIN_SCORE) {
                length = best;
                aligned = true;
            }
            score = best_score;
        }
    }

    if (xfade > length / 2) xfade = length / 2;
    out->bounds.loop_start = (uint32_t)start;
    out->bounds.loop_end = (uint32_t)(start + length);
    out->bounds.xfade_frames = (uint32_t)(xfade > 0 ? xfade : 0);
    out->score = score;
    out->aligned = aligned;
    out->start_shift = (int)(start - (int64_t)nominal->loop_start);
    out->end_shift = (int)((start + length) - (int64_t)nominal->loop_end);
    return true;
}
