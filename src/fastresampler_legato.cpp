// #pragma once (removed for unity build)
// See fastresampler_legato.h.

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "fastresampler_legato.h"
#include "fastresampler_constants.h"
#include "fastresampler_helpers.h"

// The band of every resonator, as a fraction of its own centre frequency. A
// semitone is 5.95% of it, so half of that keeps the two notes apart at
// every harmonic at once -- the band is the same fraction everywhere, and so
// is the interval -- and +-1.5% still holds most of the +-30 cents a string
// section's vibrato swings through.
#define LEGATO_BAND_FRACTION    0.03f
// Two harmonics closer together than this many bands count as the same one
// and are dropped from both sums. A semitone apart is two bands, so only the
// intervals that really do share a partial are caught: the third harmonic of
// a note is the second of the note a fifth above it.
#define LEGATO_CLASH_BANDS      1.0f
// Harmonics left after the clashes are dropped, below which the two notes
// are too alike to tell apart -- an octave shares every one of them.
#define LEGATO_MIN_HARMONICS    3

// A band narrower than this settles too slowly to say when anything
// happened: a resonator answers in about 1/(pi * band), so 8 Hz takes 40 ms
// and 3 Hz takes 106 ms. The band is a fraction of the frequency, so the
// lowest harmonics of a low note are the slow ones, and they are left out
// rather than allowed to drag the reading late.
#define LEGATO_MIN_BAND_HZ      8.0f

// The energies are smoothed over this before the share is read, so one
// period of a low note does not modulate it.
#define LEGATO_SMOOTH_SECONDS   0.010f
// The share is read this often.
#define LEGATO_STEP_SECONDS     0.002f

// The share the note landed on has to reach for the move to be under way,
// and the share it has to still be under for the move not to have started.
#define LEGATO_HIGH_SHARE       0.5f
#define LEGATO_LOW_SHARE        0.15f
// How far up from the floor towards the share the capture settles at counts
// as landed. Half of the rise is only the crossing point of the two notes,
// which is the middle of the move and not its end; near the top the note
// being left has gone and what is sounding is the note landed on. The rest
// of the rise is the tail of the one note fading under the other, which no
// longer moves the pitch and must not hold the arrival back.
#define LEGATO_ARRIVED_FRACTION 0.90f
// How long the share has to hold above LEGATO_HIGH_SHARE before the crossing
// counts, so one vibrato peak is not read as the arrival.
#define LEGATO_CONFIRM_SECONDS  0.060f
// The span at the end of the capture the reading is checked against, and the
// rise over the lead the share has to make there to be believed.
#define LEGATO_SETTLED_SECONDS  0.100f
#define LEGATO_SCORE_FLOOR      0.35f

// The working rate keeps this much room above the highest harmonic that is
// followed, and the capture is halved towards it.
#define LEGATO_RATE_HEADROOM    2.5f
#define LEGATO_MAX_HALVINGS     3

// Frequencies the two sums run over.
#define LEGATO_MAX_BINS         (2 * LEGATO_HARMONICS)

bool legato_analyzer_init(LegatoAnalyzer* a, int64_t max_frames, int sample_rate) {
    memset(a, 0, sizeof(*a));
    if (max_frames <= 0 || sample_rate <= 0) return false;
    a->mono = (float*)malloc((size_t)max_frames * sizeof(float));
    a->work = (float*)malloc((size_t)max_frames * sizeof(float));
    if (!a->mono || !a->work) {
        legato_analyzer_free(a);
        return false;
    }
    a->capacity = max_frames;
    a->sample_rate = sample_rate;
    return true;
}

void legato_analyzer_free(LegatoAnalyzer* a) {
    free(a->mono);
    free(a->work);
    memset(a, 0, sizeof(*a));
}

// The capture as one channel, read a block at a time so only the mono copy
// is ever held whole.
static bool legato_load_mono(LegatoAnalyzer* a, WavReader* wav, int64_t first, int64_t count) {
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

// Half the rate, with a three tap filter over the pairs so what is above the
// new Nyquist does not fold back into the bands the resonators read. Returns
// the frames left.
static int64_t legato_halve(const float* src, float* dst, int64_t count) {
    int64_t out = count / 2;
    for (int64_t i = 0; i < out; ++i) {
        int64_t c = 2 * i;
        float prev = c > 0 ? src[c - 1] : src[c];
        float next = c + 1 < count ? src[c + 1] : src[c];
        dst[i] = 0.25f * prev + 0.5f * src[c] + 0.25f * next;
    }
    return out;
}

static double legato_note_hz(int note) {
    return 440.0 * pow(2.0, ((double)note - 69.0) / 12.0);
}

// One frequency the share is read at: which note it belongs to, and the
// resonator that follows it.
struct LegatoBin {
    bool   target;
    double freq;
    float  cos_w, sin_w;
    float  gain;      // what one pole adds at its own frequency, taken back out
    float  re, im;
    float  energy;
};

// The harmonics of both notes that say something. A harmonic the two notes
// share -- the fourth of one is the third of the other, a fifth apart --
// sits in both bands at once and is dropped from both sums. Returns how many
// bins were filled, and how many harmonics of the note landed on survived.
static int legato_build_bins(double source_hz, double target_hz, double rate, LegatoBin* bins, int* target_harmonics) {
    // Which harmonics say anything at all, decided for the two notes
    // together so both sums cover the same part of the spectrum: high
    // enough for a band that answers quickly, low enough for the rate.
    double lowest = source_hz < target_hz ? source_hz : target_hz;
    double highest = source_hz > target_hz ? source_hz : target_hz;
    bool source_ok[LEGATO_HARMONICS + 1];
    bool target_ok[LEGATO_HARMONICS + 1];
    for (int h = 1; h <= LEGATO_HARMONICS; ++h) {
        bool usable = lowest * h * LEGATO_BAND_FRACTION >= LEGATO_MIN_BAND_HZ &&
                      highest * h * LEGATO_RATE_HEADROOM < rate;
        source_ok[h] = usable;
        target_ok[h] = usable;
    }
    for (int hs = 1; hs <= LEGATO_HARMONICS; ++hs) {
        for (int ht = 1; ht <= LEGATO_HARMONICS; ++ht) {
            double fs = source_hz * hs;
            double ft = target_hz * ht;
            double band = LEGATO_CLASH_BANDS * LEGATO_BAND_FRACTION * (fs < ft ? fs : ft);
            if (fabs(fs - ft) < band) {
                source_ok[hs] = false;
                target_ok[ht] = false;
            }
        }
    }

    int count = 0;
    int kept_target = 0;
    for (int side = 0; side < 2; ++side) {
        bool target = side == 1;
        const bool* ok = target ? target_ok : source_ok;
        double base = target ? target_hz : source_hz;
        for (int h = 1; h <= LEGATO_HARMONICS; ++h) {
            if (!ok[h]) continue;
            double freq = base * h;
            LegatoBin* b = &bins[count++];
            memset(b, 0, sizeof(*b));
            b->target = target;
            b->freq = freq;
            double w = 2.0 * 3.14159265358979 * freq / rate;
            // A one-pole complex resonator: its band is the fraction of the
            // centre frequency above, so every harmonic separates the two
            // notes by the same margin.
            double bandwidth = freq * LEGATO_BAND_FRACTION;
            double radius = 1.0 - 3.14159265358979 * bandwidth / rate;
            if (radius < 0.0) radius = 0.0;
            if (radius > 0.9999) radius = 0.9999;
            b->cos_w = (float)(radius * cos(w));
            b->sin_w = (float)(radius * sin(w));
            // A narrow pole has a tall peak, and a constant-Q band is
            // narrowest at the fundamental. Taking the peak back out leaves
            // the partial's own size, so the sum is of the note's harmonics
            // rather than of its lowest one.
            double peak = 1.0 - radius;
            b->gain = (float)(peak * peak);
            if (target) kept_target++;
        }
    }
    *target_harmonics = kept_target;
    return count;
}

bool legato_find_bounds(LegatoAnalyzer* a, WavReader* wav, int64_t first_frame, int64_t frame_count,
                        int source_note, int target_note, int64_t note_on,
                        int64_t nominal_departure, int64_t nominal_arrival,
                        LegatoBounds* out) {
    if (!a->mono || frame_count <= 0 || frame_count > a->capacity) return false;
    if (note_on < 0 || note_on >= frame_count) return false;
    if (source_note == target_note) return false;
    if (!legato_load_mono(a, wav, first_frame, frame_count)) return false;

    double source_hz = legato_note_hz(source_note);
    double target_hz = legato_note_hz(target_note);
    double top_hz = (source_hz > target_hz ? source_hz : target_hz) * LEGATO_HARMONICS;

    // Down to a rate that still carries the highest harmonic followed: the
    // resonators cost one pass over the capture each.
    const float* signal = a->mono;
    int64_t count = frame_count;
    double rate = (double)a->sample_rate;
    int64_t on = note_on;
    // The first halving reads the mono copy and writes the work buffer; the
    // rest work in place there, which the filter's three taps allow.
    for (int i = 0; i < LEGATO_MAX_HALVINGS; ++i) {
        if (rate * 0.5 < top_hz * LEGATO_RATE_HEADROOM) break;
        count = legato_halve(signal, a->work, count);
        signal = a->work;
        rate *= 0.5;
        on /= 2;
    }
    if (count < 4 || on >= count) return false;

    LegatoBin bins[LEGATO_MAX_BINS];
    int target_harmonics = 0;
    int bin_count = legato_build_bins(source_hz, target_hz, rate, bins, &target_harmonics);
    int source_harmonics = bin_count - target_harmonics;
    if (target_harmonics < LEGATO_MIN_HARMONICS || source_harmonics < LEGATO_MIN_HARMONICS) return false;

    float smooth = (float)exp(-1.0 / (LEGATO_SMOOTH_SECONDS * rate));
    int64_t step = (int64_t)(LEGATO_STEP_SECONDS * rate + 0.5);
    if (step < 1) step = 1;
    int64_t confirm_steps = (int64_t)(LEGATO_CONFIRM_SECONDS * rate / (double)step + 0.5);
    if (confirm_steps < 1) confirm_steps = 1;

    // One pass over the capture: every resonator follows its own frequency,
    // and the share of the two sums is kept at every step.
    int64_t step_count = count / step;
    if (step_count < 4) return false;
    // The buffer the signal is not in holds the shares: both are as long as
    // the capture, and one step_count of them is needed.
    float* shares = signal == a->mono ? a->work : a->mono;

    int64_t written = 0;
    for (int64_t i = 0; i < count; ++i) {
        float x = signal[i];
        float source_energy = 0.0f, target_energy = 0.0f;
        for (int b = 0; b < bin_count; ++b) {
            LegatoBin* p = &bins[b];
            float re = p->cos_w * p->re - p->sin_w * p->im + x;
            float im = p->sin_w * p->re + p->cos_w * p->im;
            p->re = re;
            p->im = im;
            float power = (re * re + im * im) * p->gain;
            p->energy = smooth * p->energy + (1.0f - smooth) * power;
            if (p->target) target_energy += p->energy;
            else source_energy += p->energy;
        }
        if (i % step == 0 && written < step_count) {
            // The two sums hold as many harmonics as each note kept, so the
            // share is of the mean per harmonic rather than of the totals.
            float s = source_energy / (float)source_harmonics;
            float t = target_energy / (float)target_harmonics;
            float total = s + t;
            shares[written++] = total > 1e-20f ? t / total : 0.0f;
        }
    }

    int64_t on_step = on / step;
    int64_t settled_steps = (int64_t)(LEGATO_SETTLED_SECONDS * rate / (double)step + 0.5);
    if (settled_steps < 1) settled_steps = 1;
    if (settled_steps > written) settled_steps = written;

    // What the share ended at against what it started at: a move that really
    // went from one note to the other rises the whole way.
    double late = 0.0, early = 0.0;
    for (int64_t i = written - settled_steps; i < written; ++i) late += shares[i];
    late /= (double)settled_steps;
    int64_t early_steps = on_step > 0 ? (on_step < settled_steps ? on_step : settled_steps) : 0;
    if (early_steps > 0) {
        for (int64_t i = on_step - early_steps; i < on_step; ++i) early += shares[i];
        early /= (double)early_steps;
    }
    float score = fr_clampf((float)(late - early), 0.0f, 1.0f);

    // The first crossing that holds: somewhere inside the move, and the
    // anchor both bounds are found from.
    int64_t crossing = -1;
    for (int64_t i = on_step; i < written; ++i) {
        if (shares[i] < LEGATO_HIGH_SHARE) continue;
        int64_t held = 0;
        while (held < confirm_steps && i + held < written && shares[i + held] >= LEGATO_HIGH_SHARE) ++held;
        if (held < confirm_steps && i + held < written) continue;
        crossing = i;
        break;
    }

    int64_t departure = -1, arrival = -1;
    if (crossing >= 0) {
        // Back from the crossing to where the note landed on had not come up
        // at all: that is where the move leaves the note it is played from.
        departure = crossing;
        while (departure > on_step && shares[departure - 1] > LEGATO_LOW_SHARE) --departure;

        // Forward from the crossing to where the share has risen nearly all
        // the way to what it settles at, and stays there: that is where the
        // move has got to the note it goes to.
        double reach = (double)LEGATO_LOW_SHARE + LEGATO_ARRIVED_FRACTION * (late - (double)LEGATO_LOW_SHARE);
        for (int64_t i = crossing; i < written; ++i) {
            if ((double)shares[i] < reach) continue;
            int64_t held = 0;
            while (held < confirm_steps && i + held < written && (double)shares[i + held] >= reach) ++held;
            if (held < confirm_steps && i + held < written) continue;
            arrival = i;
            break;
        }
        // A capture whose share never settles still departed. Fall back to
        // the crossing so the arrival is never before it.
        if (arrival < 0) arrival = crossing;
    }

    if (departure < 0 || arrival < 0 || score < LEGATO_SCORE_FLOOR) {
        out->departure = nominal_departure;
        out->arrival = nominal_arrival;
        out->score = score;
        out->found = false;
        return true;
    }
    // Back to the rate the file is at.
    int64_t decimation = (int64_t)((double)a->sample_rate / rate + 0.5);
    int64_t dep_frame = departure * step * decimation;
    int64_t arr_frame = arrival * step * decimation;
    if (dep_frame >= frame_count) dep_frame = frame_count - 1;
    if (arr_frame >= frame_count) arr_frame = frame_count - 1;
    if (arr_frame < dep_frame) arr_frame = dep_frame;
    out->departure = dep_frame;
    out->arrival = arr_frame;
    out->score = score;
    out->found = true;
    return true;
}
