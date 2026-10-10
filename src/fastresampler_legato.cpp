#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "fastresampler_legato.h"
#include "fastresampler_constants.h"
#include "fastresampler_helpers.h"

#define LEGATO_BAND_FRACTION    0.03f
#define LEGATO_CLASH_BANDS      1.0f
#define LEGATO_MIN_HARMONICS    3

#define LEGATO_MIN_BAND_HZ      8.0f

#define LEGATO_SMOOTH_SECONDS   0.010f
#define LEGATO_STEP_SECONDS     0.002f

#define LEGATO_HIGH_SHARE       0.5f
#define LEGATO_LOW_SHARE        0.15f
#define LEGATO_ARRIVED_FRACTION 0.90f
#define LEGATO_CONFIRM_SECONDS  0.060f
#define LEGATO_SETTLED_SECONDS  0.100f
#define LEGATO_SCORE_FLOOR      0.35f

#define LEGATO_RATE_HEADROOM    2.5f
#define LEGATO_MAX_HALVINGS     3

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

struct LegatoBin {
    bool   target;
    float  cos_w, sin_w;
    float  gain;
    float  re, im;
    float  energy;
};

static int legato_build_bins(double source_hz, double target_hz, double rate, LegatoBin* bins, int* target_harmonics) {
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
            double w = 2.0 * 3.14159265358979 * freq / rate;
            double bandwidth = freq * LEGATO_BAND_FRACTION;
            double radius = 1.0 - 3.14159265358979 * bandwidth / rate;
            if (radius < 0.0) radius = 0.0;
            if (radius > 0.9999) radius = 0.9999;
            b->cos_w = (float)(radius * cos(w));
            b->sin_w = (float)(radius * sin(w));
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
    if (!wav_reader_read_mono(wav, first_frame, frame_count, a->mono)) return false;

    double source_hz = legato_note_hz(source_note);
    double target_hz = legato_note_hz(target_note);
    double top_hz = (source_hz > target_hz ? source_hz : target_hz) * LEGATO_HARMONICS;

    const float* signal = a->mono;
    int64_t count = frame_count;
    double rate = (double)a->sample_rate;
    int64_t on = note_on;
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

    int64_t step_count = count / step;
    if (step_count < 4) return false;
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

    double late = 0.0, early = 0.0;
    for (int64_t i = written - settled_steps; i < written; ++i) late += shares[i];
    late /= (double)settled_steps;
    int64_t early_steps = on_step > 0 ? (on_step < settled_steps ? on_step : settled_steps) : 0;
    if (early_steps > 0) {
        for (int64_t i = on_step - early_steps; i < on_step; ++i) early += shares[i];
        early /= (double)early_steps;
    }
    float score = fr_clampf((float)(late - early), 0.0f, 1.0f);

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
        departure = crossing;
        while (departure > on_step && shares[departure - 1] > LEGATO_LOW_SHARE) --departure;

        double reach = (double)LEGATO_LOW_SHARE + LEGATO_ARRIVED_FRACTION * (late - (double)LEGATO_LOW_SHARE);
        for (int64_t i = crossing; i < written; ++i) {
            if ((double)shares[i] < reach) continue;
            int64_t held = 0;
            while (held < confirm_steps && i + held < written && (double)shares[i + held] >= reach) ++held;
            if (held < confirm_steps && i + held < written) continue;
            arrival = i;
            break;
        }
        if (arrival < 0) arrival = crossing;
    }

    if (departure < 0 || arrival < 0 || score < LEGATO_SCORE_FLOOR) {
        out->departure = nominal_departure;
        out->arrival = nominal_arrival;
        out->score = score;
        out->found = false;
        return true;
    }
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
