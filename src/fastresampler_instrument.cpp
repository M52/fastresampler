#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "fastresampler_instrument.h"
#include "fastresampler_rzc.h"
#include "fastresampler_wav.h"
#include "fastresampler_loop.h"
#include "fastresampler_legato.h"
#include "fastresampler_fingerprint.h"
#include "fastresampler_helpers.h"
#include "fastresampler_log.h"

#define INSTRUMENT_ZONE_RELEASE_SECONDS   0.25f
#define INSTRUMENT_RELEASE_ATTACK_SECONDS 0.01f

#define INSTRUMENT_LOOP_MARGIN_SECONDS 0.25f
#define INSTRUMENT_LOOP_XFADE_SECONDS  0.2f
#define INSTRUMENT_XFADE_EQUAL_POWER   2

#define INSTRUMENT_LAYER_FLOOR_DB      -50.0f
#define INSTRUMENT_LAYER_LAG_MS        10.0f
#define INSTRUMENT_LAYER_CURVE_GROUP   1
#define INSTRUMENT_HALF_PI             1.5707963f

#define INSTRUMENT_MORPH_SMOOTH_MS     30.0f
#define INSTRUMENT_MORPH_MAX_BOOST_DB  12.0f

struct InstrumentZoneMap {
    int segment;
    int low_note, high_note, root_note;
    int low_velocity, high_velocity;
    int collection;
    bool release;
    bool transition;
    int64_t first_frame;
    int64_t frame_count;
    uint32_t loop_start;
    uint32_t loop_end;
    uint32_t xfade_frames;
    uint32_t departure;
    uint32_t arrival;
};

enum InstrumentCollectionKind {
    INSTRUMENT_COL_SUSTAIN = 0,
    INSTRUMENT_COL_RELEASE,
    INSTRUMENT_COL_TRANSITION
};

#define INSTRUMENT_MAX_COLLECTIONS ((int)(sizeof(((FSIFile*)0)->collections) / sizeof(FSICollection)))

struct InstrumentCollectionPlan {
    int kind;
    int layer;
    int interval;
};

static int instrument_transition_layers(const ResampleScript* script) {
    return script->dynamic_layers ? script->round_robin_count : 1;
}

static int instrument_plan(const ResampleScript* script, InstrumentCollectionPlan* out, int max_out) {
    int count = 0;
    for (int k = 1; k <= script->round_robin_count; ++k) {
        if (count < max_out) {
            out[count].kind = INSTRUMENT_COL_SUSTAIN;
            out[count].layer = k;
            out[count].interval = 0;
        }
        count++;
    }
    if (script->dynamic_layers) {
        for (int k = 1; k <= script->round_robin_count; ++k) {
            if (count < max_out) {
                out[count].kind = INSTRUMENT_COL_RELEASE;
                out[count].layer = k;
                out[count].interval = 0;
            }
            count++;
        }
    }
    if (script->legato) {
        int layers = instrument_transition_layers(script);
        for (int k = 1; k <= layers; ++k) {
            for (int i = 0; i < script->interval_count; ++i) {
                if (count < max_out) {
                    out[count].kind = INSTRUMENT_COL_TRANSITION;
                    out[count].layer = k;
                    out[count].interval = script->intervals[i];
                }
                count++;
            }
        }
    }
    return count;
}

static void instrument_collection_name(const ResampleScript* script, const InstrumentCollectionPlan* plan,
                                       char* out, size_t out_len) {
    char layer[RESAMPLE_LAYER_SUFFIX_LEN];
    resample_layer_suffix(script->dynamic_layers, plan->layer, layer, sizeof(layer));
    if (plan->kind == INSTRUMENT_COL_TRANSITION) {
        char interval[8];
        resample_interval_suffix(plan->interval, interval, sizeof(interval));
        if (script->dynamic_layers) snprintf(out, out_len, "%s-leg%s", layer, interval);
        else                        snprintf(out, out_len, "leg%s", interval);
        return;
    }
    snprintf(out, out_len, "%s%s", layer, plan->kind == INSTRUMENT_COL_RELEASE ? "-release" : "");
}

static int instrument_find_collection(const InstrumentCollectionPlan* plan, int count, int kind, int layer, int interval) {
    for (int c = 0; c < count; ++c) {
        if (plan[c].kind != kind || plan[c].layer != layer) continue;
        if (kind == INSTRUMENT_COL_TRANSITION && plan[c].interval != interval) continue;
        return c;
    }
    return -1;
}

enum InstrumentField {
    INSTRUMENT_FIELD_NOTE = 0,
    INSTRUMENT_FIELD_VELOCITY,
    INSTRUMENT_FIELD_SOURCE
};

static bool instrument_segment_matches(const ResampleSegment* seg, int kind, int layer, int interval) {
    if (seg->kind != kind) return false;
    if (layer != 0 && seg->round_robin != layer) return false;
    if (kind == RESAMPLE_SEG_TRANSITION && interval != 0 && seg->interval != interval) return false;
    return true;
}

static int instrument_distinct(const ResampleScript* script, int kind, int layer, int interval,
                               int field, int* out, int max_out) {
    int count = 0;
    for (int i = 0; i < script->segment_count; ++i) {
        const ResampleSegment* seg = &script->segments[i];
        if (!instrument_segment_matches(seg, kind, layer, interval)) continue;
        int v = field == INSTRUMENT_FIELD_VELOCITY ? seg->velocity
              : field == INSTRUMENT_FIELD_SOURCE   ? seg->source_note
                                                   : seg->note;
        bool seen = false;
        for (int j = 0; j < count; ++j) seen = seen || out[j] == v;
        if (seen) continue;
        if (count >= max_out) break;
        int k = count++;
        while (k > 0 && out[k - 1] > v) {
            out[k] = out[k - 1];
            --k;
        }
        out[k] = v;
    }
    return count;
}

static void instrument_note_span(const int* notes, int count, int index, int* low, int* high) {
    if (index == 0) {
        *low = notes[0];
    } else {
        int gap = notes[index] - notes[index - 1];
        *low = notes[index - 1] + (gap + 1) / 2;
    }
    if (index == count - 1) {
        *high = notes[count - 1];
    } else {
        int gap = notes[index + 1] - notes[index];
        *high = notes[index] + (gap + 1) / 2 - 1;
    }
}

static void instrument_velocity_span(const int* velocities, int count, int index, int* low, int* high) {
    *low = index == 0 ? 0 : velocities[index - 1] + 1;
    *high = index == count - 1 ? MIDI_VALUE_MAX : velocities[index];
}

static int instrument_map_sustains(const ResampleScript* script, InstrumentZoneMap* out, int collection,
                                   int layer, bool release) {
    int notes[MIDI_NOTE_COUNT];
    int velocities[RESAMPLE_MAX_VELOCITIES];
    int note_count = instrument_distinct(script, RESAMPLE_SEG_SUSTAIN, 0, 0, INSTRUMENT_FIELD_NOTE,
                                         notes, MIDI_NOTE_COUNT);
    int velocity_count = instrument_distinct(script, RESAMPLE_SEG_SUSTAIN, 0, 0, INSTRUMENT_FIELD_VELOCITY,
                                             velocities, RESAMPLE_MAX_VELOCITIES);
    int64_t margin = (int64_t)(INSTRUMENT_LOOP_MARGIN_SECONDS * script->sample_rate + 0.5);
    int64_t xfade = (int64_t)(INSTRUMENT_LOOP_XFADE_SECONDS * script->sample_rate + 0.5);

    int count = 0;
    for (int n = 0; n < note_count; ++n) {
        for (int v = 0; v < velocity_count; ++v) {
            int seg = -1;
            for (int i = 0; i < script->segment_count; ++i) {
                const ResampleSegment* s = &script->segments[i];
                if (s->kind != RESAMPLE_SEG_SUSTAIN) continue;
                if (s->note == notes[n] && s->velocity == velocities[v] && s->round_robin == layer) {
                    seg = i;
                    break;
                }
            }
            if (seg < 0) continue;
            const ResampleSegment* s = &script->segments[seg];
            InstrumentZoneMap* z = &out[count];
            memset(z, 0, sizeof(*z));
            z->segment = seg;
            z->root_note = notes[n];
            instrument_note_span(notes, note_count, n, &z->low_note, &z->high_note);
            instrument_velocity_span(velocities, velocity_count, v, &z->low_velocity, &z->high_velocity);
            z->collection = collection;
            z->release = release;
            if (release) {
                z->first_frame = s->off_frame;
                z->frame_count = s->end_frame - s->off_frame;
            } else if (script->dynamic_layers) {
                z->first_frame = s->start_frame;
                z->frame_count = s->off_frame - s->start_frame;
                int64_t m = margin < z->frame_count / 4 ? margin : z->frame_count / 4;
                int64_t room = (z->frame_count - 2 * m) / 2;
                int64_t x = xfade < room ? xfade : room;
                z->loop_start = (uint32_t)m;
                z->loop_end = (uint32_t)(z->frame_count - m);
                z->xfade_frames = (uint32_t)(x > 0 ? x : 0);
            } else {
                z->first_frame = s->start_frame;
                z->frame_count = s->end_frame - s->start_frame;
            }
            if (z->frame_count <= 0) continue;
            count++;
        }
    }
    return count;
}

static int instrument_map_transitions(const ResampleScript* script, InstrumentZoneMap* out, int collection,
                                      int layer, int interval) {
    int sources[MIDI_NOTE_COUNT];
    int velocities[RESAMPLE_MAX_LEGATO_VELOCITIES];
    int source_count = instrument_distinct(script, RESAMPLE_SEG_TRANSITION, layer, interval,
                                           INSTRUMENT_FIELD_SOURCE, sources, MIDI_NOTE_COUNT);
    int velocity_count = instrument_distinct(script, RESAMPLE_SEG_TRANSITION, layer, interval,
                                             INSTRUMENT_FIELD_VELOCITY, velocities, RESAMPLE_MAX_LEGATO_VELOCITIES);
    int64_t margin = (int64_t)((double)script->margin_seconds * script->sample_rate + 0.5);

    int count = 0;
    for (int n = 0; n < source_count; ++n) {
        for (int v = 0; v < velocity_count; ++v) {
            int seg = -1;
            for (int i = 0; i < script->segment_count; ++i) {
                const ResampleSegment* s = &script->segments[i];
                if (!instrument_segment_matches(s, RESAMPLE_SEG_TRANSITION, layer, interval)) continue;
                if (s->source_note == sources[n] && s->velocity == velocities[v]) {
                    seg = i;
                    break;
                }
            }
            if (seg < 0) continue;
            const ResampleSegment* s = &script->segments[seg];
            InstrumentZoneMap* z = &out[count];
            memset(z, 0, sizeof(*z));
            z->segment = seg;
            z->root_note = sources[n];
            instrument_note_span(sources, source_count, n, &z->low_note, &z->high_note);
            instrument_velocity_span(velocities, velocity_count, v, &z->low_velocity, &z->high_velocity);
            z->collection = collection;
            z->transition = true;
            z->first_frame = s->start_frame;
            z->departure = (uint32_t)s->departure_frame;
            z->arrival = (uint32_t)s->arrival_frame;
            int64_t captured = s->end_frame - s->start_frame;
            int64_t wanted = s->arrival_frame + margin;
            z->frame_count = wanted < captured ? wanted : captured;
            if (z->frame_count <= 0) continue;
            count++;
        }
    }
    return count;
}

static int instrument_map(const ResampleScript* script, InstrumentZoneMap* out,
                          const InstrumentCollectionPlan* plan, int collection_count) {
    int zone_count = 0;
    for (int c = 0; c < collection_count; ++c) {
        const InstrumentCollectionPlan* p = &plan[c];
        if (p->kind == INSTRUMENT_COL_TRANSITION) {
            zone_count += instrument_map_transitions(script, out + zone_count, c, p->layer, p->interval);
            continue;
        }
        zone_count += instrument_map_sustains(script, out + zone_count, c, p->layer,
                                              p->kind == INSTRUMENT_COL_RELEASE);
    }
    return zone_count;
}

static void instrument_zone_name(const char* prefix, const ResampleScript* script, const InstrumentZoneMap* map,
                                 char* out, size_t out_len) {
    const ResampleSegment* seg = &script->segments[map->segment];
    char suffix[48];
    resample_sample_suffix(script->dynamic_layers, seg, map->release, suffix, sizeof(suffix));
    size_t room = MAX_NAME_LENGTH - 1 - strlen(suffix);
    char short_prefix[MAX_NAME_LENGTH];
    size_t n = strlen(prefix);
    if (n > room) n = room;
    memcpy(short_prefix, prefix, n);
    short_prefix[n] = '\0';
    snprintf(out, out_len, "%s%s", short_prefix, suffix);
}

static void instrument_fill_zone(FSIZone* dst, const char* name, const InstrumentZoneMap* map, uint32_t offset,
                                 bool ignore_velocity) {
    uint32_t frames = (uint32_t)map->frame_count;
    bool loop = map->loop_end > map->loop_start;
    if (map->transition) ignore_velocity = true;
    memset(dst, 0, sizeof(*dst));
    fr_strcpy(dst->name, sizeof(dst->name), name);
    dst->attack = map->release ? INSTRUMENT_RELEASE_ATTACK_SECONDS : 0.0f;
    dst->decay = 0.0f;
    dst->sustain = 1.0f;
    dst->release = map->transition ? 0.0f : INSTRUMENT_ZONE_RELEASE_SECONDS;
    dst->startframe = offset;
    dst->endframe = offset + frames;
    dst->loopstart = offset + (loop ? map->loop_start : 0);
    dst->loopend = offset + (loop ? map->loop_end : frames);
    dst->xfadeframes = loop ? map->xfade_frames : 0;
    dst->xfadetype = loop ? INSTRUMENT_XFADE_EQUAL_POWER : 0;
    dst->volume = 1.0f;
    dst->pitch = 0.0f;
    dst->has_loop_enabled = true;       dst->loop_enabled = loop;
    dst->has_oneshot = true;            dst->oneshot = false;
    dst->has_is_release_sample = true;  dst->is_release_sample = map->release;
    dst->has_ignore_vel_mapping = true; dst->ignore_vel_mapping = ignore_velocity;
    dst->has_low_note = true;           dst->low_note = (uint32_t)map->low_note;
    dst->has_high_note = true;          dst->high_note = (uint32_t)map->high_note;
    dst->has_root_note = true;          dst->root_note = (uint32_t)map->root_note;
    dst->has_low_velocity = true;       dst->low_velocity = (uint32_t)map->low_velocity;
    dst->has_high_velocity = true;      dst->high_velocity = (uint32_t)map->high_velocity;
    dst->has_pitchshift_type = true;    dst->pitchshift_type = 0;
    dst->has_flip_lr = true;            dst->flip_lr = false;
    dst->has_zoom_start = true;         dst->zoom_start = offset;
    dst->has_zoom_end = true;           dst->zoom_end = offset + frames;
    dst->has_zoom_v = true;             dst->zoom_v = 1.0f;
    dst->has_inframe = true;            dst->inframe = offset;
    dst->has_outframe = true;           dst->outframe = offset + frames;
    dst->has_arrivalframe = true;       dst->arrivalframe = map->transition ? offset + map->arrival : 0;
    dst->has_departureframe = true;     dst->departureframe = map->transition ? offset + map->departure : 0;
}

static int instrument_layer_controller(const ResampleConfig* cfg) {
    for (int i = 0; i < cfg->cc_curve_count; ++i) {
        if (cfg->cc_curves[i].layer > 0) return cfg->cc_curves[i].controller;
    }
    return MIDI_CC_MOD_WHEEL;
}

static float instrument_layer_peak(int layer, int layer_count) {
    return (float)layer / (float)layer_count;
}

static void instrument_layer_curve(int layer, int layer_count, float* out) {
    float n = (float)layer_count;
    float lo = instrument_layer_peak(layer - 1, layer_count);
    float mid = instrument_layer_peak(layer, layer_count);
    float hi = instrument_layer_peak(layer + 1, layer_count);
    for (int p = 0; p < MOD_CURVE_POINTS; ++p) {
        float x = (float)p / (float)(MOD_CURVE_POINTS - 1);
        float amp;
        if (layer_count == 1) {
            amp = 1.0f;
        } else if (x < lo) {
            amp = 0.0f;
        } else if (x < mid) {
            amp = layer == 1 ? 1.0f : sinf((x - lo) * n * INSTRUMENT_HALF_PI);
        } else if (x <= hi) {
            amp = layer == layer_count ? 1.0f : cosf((x - mid) * n * INSTRUMENT_HALF_PI);
        } else {
            amp = 0.0f;
        }
        amp = fr_clampf(amp, 0.0f, 1.0f);
        float db = amp > 0.0f ? 20.0f * log10f(amp) : INSTRUMENT_LAYER_FLOOR_DB;
        out[p] = fr_clampf((db - INSTRUMENT_LAYER_FLOOR_DB) / -INSTRUMENT_LAYER_FLOOR_DB, 0.0f, 1.0f);
    }
}

static void instrument_fill_cc_driver(FSICollectionFXDriver* d, int target_param, int controller,
                                      float out_min, float out_max) {
    memset(d, 0, sizeof(*d));
    d->target_param_idx = (uint32_t)target_param;
    d->source_type = (uint32_t)FX_DRIVER_SOURCE_CC;
    d->has_cc_number = true;         d->cc_number = controller;
    d->has_lfo_rate = true;          d->lfo_rate = 1.0f;
    d->has_lfo_waveform = true;      d->lfo_waveform = 0;
    d->has_lfo_polyphonic = true;    d->lfo_polyphonic = true;
    d->has_depth = true;             d->depth = 1.0f;
    d->has_use_curve = true;         d->use_curve = false;
    d->has_expanded = true;          d->expanded = false;
    d->has_vel_source = true;        d->vel_source = 0;
    d->has_out_min = true;           d->out_min = out_min;
    d->has_out_max = true;           d->out_max = out_max;
    d->has_range_initialized = true; d->range_initialized = true;
    d->has_curve_group = true;       d->curve_group = 0;
    d->has_hold_rise_ms = true;      d->hold_rise_ms = CFX_HOLD_RISE_DEFAULT_MS;
    d->has_hold_fall_ms = true;      d->hold_fall_ms = CFX_HOLD_FALL_DEFAULT_MS;
}

static void instrument_fill_layer_fx(FSICollectionFX* fx, int collection_index, int layer, int layer_count, int controller) {
    static_assert(sizeof(FSICollectionFXDriver::curve_data) / sizeof(float) >= MOD_CURVE_POINTS,
                  "the driver's curve must hold FastSampler's curve points");
    memset(fx, 0, sizeof(*fx));
    fx->type = (uint32_t)COLLECTION_FX_GAIN;
    fx->collection_index = (uint32_t)collection_index;
    fx->params_count = CFX_GAIN_LAG_MS + 1;
    fx->params[CFX_STRENGTH] = 1.0f;
    fx->params[CFX_GAIN_LEVEL_DB] = 0.0f;
    fx->params[CFX_GAIN_LAG_MS] = INSTRUMENT_LAYER_LAG_MS;
    fx->drivers_count = 1;
    FSICollectionFXDriver* d = &fx->drivers[0];
    instrument_fill_cc_driver(d, CFX_GAIN_LEVEL_DB, controller, INSTRUMENT_LAYER_FLOOR_DB, 0.0f);
    d->use_curve = true;
    d->curve_group = INSTRUMENT_LAYER_CURVE_GROUP;
    d->curve_data_count = MOD_CURVE_POINTS;
    instrument_layer_curve(layer, layer_count, d->curve_data);
}

static void instrument_fill_morph_fx(FSICollectionFX* fx, int collection_index, int layer, int layer_count, int controller) {
    memset(fx, 0, sizeof(*fx));
    fx->type = (uint32_t)COLLECTION_FX_SPECTRAL_MORPH;
    fx->collection_index = (uint32_t)collection_index;
    fx->params_count = CFX_SMF_MAX_BOOST + 1;
    fx->params[CFX_STRENGTH] = 1.0f;
    fx->params[CFX_SMF_MORPH] = 0.0f;
    fx->params[CFX_SMF_SMOOTH_MS] = INSTRUMENT_MORPH_SMOOTH_MS;
    fx->params[CFX_SMF_SELF_POS] = instrument_layer_peak(layer, layer_count);
    fx->params[CFX_SMF_MAX_BOOST] = INSTRUMENT_MORPH_MAX_BOOST_DB;
    fx->drivers_count = 1;
    instrument_fill_cc_driver(&fx->drivers[0], CFX_SMF_MORPH, controller, 0.0f, 1.0f);
}

static bool instrument_fingerprint_path(const InstrumentJob* job, const InstrumentCollectionPlan* plan,
                                        char* out, size_t out_len) {
    char collection_name[32];
    instrument_collection_name(job->script, plan, collection_name, sizeof(collection_name));
    char name[RESAMPLE_FILE_NAME_LEN];
    snprintf(name, sizeof(name), "%s_%s.smfp", job->prefix, collection_name);
    char path[FR_MAX_PATH];
    plat_path_join(path, sizeof(path), job->out_dir, name);
    return strlen(path) < sizeof(path) - 1 && plat_path_absolute(path, out, out_len);
}

static bool instrument_fill_morph_targets(FSISMFFingerprints* fp, const InstrumentJob* job,
                                          const InstrumentCollectionPlan* plan, int collection_count,
                                          int c, int file_index, const bool* fingerprinted) {
    const pb_size_t capacity = (pb_size_t)(sizeof(fp->target_paths) / sizeof(fp->target_paths[0]));
    int layers = job->script->round_robin_count;
    memset(fp, 0, sizeof(*fp));
    fp->collection_index = (uint32_t)file_index;
    for (int k = 1; k <= layers && fp->target_paths_count < capacity; ++k) {
        if (k == plan[c].layer) continue;
        int target = instrument_find_collection(plan, collection_count, plan[c].kind, k, plan[c].interval);
        if (target < 0 || !fingerprinted[target]) continue;
        if (!instrument_fingerprint_path(job, &plan[target], fp->target_paths[fp->target_paths_count],
                                         sizeof(fp->target_paths[0]))) continue;
        fp->target_positions[fp->target_paths_count] = instrument_layer_peak(k, layers);
        fp->target_paths_count++;
    }
    fp->target_positions_count = fp->target_paths_count;
    return fp->target_paths_count > 0;
}

static void instrument_fill_smart_release_fx(FSICollectionFX* fx, int collection_index) {
    memset(fx, 0, sizeof(*fx));
    fx->type = (uint32_t)COLLECTION_FX_SMART_RELEASE;
    fx->collection_index = (uint32_t)collection_index;
    fx->params_count = CFX_SMART_RELEASE_PARAM_COUNT;
    fx->params[CFX_STRENGTH] = 1.0f;
    fx->params[CFX_SREL_LEVEL_ON] = 1.0f;
    fx->params[CFX_SREL_LEVEL_DB] = SREL_MAX_MATCH_DB;
    fx->params[CFX_SREL_LEVEL_AMOUNT] = 100.0f;
    fx->params[CFX_SREL_LEVEL_OFFSET] = 0.0f;
    fx->params[CFX_SREL_ATTACK_ON] = 1.0f;
    fx->params[CFX_SREL_ATTACK_PCT] = 100.0f;
    fx->params[CFX_SREL_ATTACK_BEND] = 0.0f;
}

static void instrument_fill_legato_fx(FSICollectionFX* fx, int collection_index, const ResampleScript* script,
                                      float window_ms) {
    memset(fx, 0, sizeof(*fx));
    fx->type = (uint32_t)COLLECTION_FX_LEGATO;
    fx->collection_index = (uint32_t)collection_index;
    fx->params_count = CFX_LEGATO_PARAM_COUNT;
    float margin_ms = script->margin_seconds * 1000.0f;
    float in_slow = LEGATO_DEFAULT_FADE_IN_SLOW_MS < margin_ms ? LEGATO_DEFAULT_FADE_IN_SLOW_MS : margin_ms;
    float in_fast = LEGATO_DEFAULT_FADE_IN_FAST_MS < margin_ms ? LEGATO_DEFAULT_FADE_IN_FAST_MS : margin_ms;
    float out = LEGATO_DEFAULT_FADE_OUT_MS < margin_ms ? LEGATO_DEFAULT_FADE_OUT_MS : margin_ms;
    fx->params[CFX_STRENGTH] = 1.0f;
    fx->params[CFX_LEGATO_FADE_IN_SLOW] = in_slow;
    fx->params[CFX_LEGATO_FADE_IN_FAST] = in_fast;
    fx->params[CFX_LEGATO_FADE_OUT] = out;
    fx->params[CFX_LEGATO_SLOW_AT_MS] = LEGATO_DEFAULT_SLOW_AT_MS;
    fx->params[CFX_LEGATO_FAST_AT_MS] = LEGATO_DEFAULT_FAST_AT_MS;
    fx->params[CFX_LEGATO_FASTEST_MS] = 0.0f;
    fx->params[CFX_LEGATO_LAND_OFFSET] = 0.0f;
    fx->params[CFX_LEGATO_LAND_START] = LEGATO_DEFAULT_START_MS;
    fx->params[CFX_LEGATO_SHAPE] = (float)LEGATO_SHAPE_EQUAL_POWER;
    fx->params[CFX_LEGATO_WINDOW_MS] = window_ms;
    fx->params[CFX_LEGATO_RETURN] = 1.0f;
    fx->params[CFX_LEGATO_VEL_SOURCE] = (float)(script->legato_velocity_count > 1 ? LEGATO_VEL_FROM_SPEED
                                                                                   : LEGATO_VEL_FROM_NOTE);
    fx->params[CFX_LEGATO_VEL_FIXED] = 1.0f;
    fx->params[CFX_LEGATO_MATCH_DB] = LEGATO_DEFAULT_MATCH_DB;
    fx->params[CFX_LEGATO_MATCH_AMOUNT] = 100.0f;
    fx->params[CFX_LEGATO_LENGTH_SLOW] = 100.0f;
    fx->params[CFX_LEGATO_LENGTH_FAST] = 100.0f;
    fx->params[CFX_LEGATO_RETURN_LENGTH_SLOW] = 100.0f;
    fx->params[CFX_LEGATO_RETURN_LENGTH_FAST] = 100.0f;
}

static int instrument_bytes_per_sample(int bits) {
    return bits == 16 ? 2 : 3;
}

static int32_t instrument_sample_int(float s, int bits) {
    float   scale = bits == 16 ? PCM_ENCODE_16BIT : PCM_ENCODE_24BIT;
    int32_t low   = bits == 16 ? PCM_MIN_16BIT : PCM_MIN_24BIT;
    int32_t high  = bits == 16 ? PCM_MAX_16BIT : PCM_MAX_24BIT;
    int32_t v = (int32_t)lrintf(fr_clampf(s, -1.0f, 1.0f) * scale);
    if (v > high) v = high;
    if (v < low) v = low;
    return v;
}

static void instrument_pcm_pack(const float* src, uint8_t* dst, size_t sample_count, int bits) {
    size_t bytes = (size_t)instrument_bytes_per_sample(bits);
    for (size_t i = 0; i < sample_count; ++i) {
        int32_t v = instrument_sample_int(src[i], bits);
        uint8_t* out = dst + i * bytes;
        out[0] = (uint8_t)(v & 0xFF);
        out[1] = (uint8_t)((v >> 8) & 0xFF);
        if (bytes == 3) out[2] = (uint8_t)((v >> 16) & 0xFF);
    }
}

#define INSTRUMENT_PACK_BLOCK_FRAMES     4096
#define INSTRUMENT_PACK_FIRST_CAPACITY   65536
#define INSTRUMENT_WP_CHANNEL_MASK_MONO   4
#define INSTRUMENT_WP_CHANNEL_MASK_STEREO 3

struct InstrumentPackBuffer {
    uint8_t* data;
    size_t   size;
    size_t   capacity;
    bool     failed;
};

static int instrument_pack_output(void* id, void* data, int32_t count) {
    InstrumentPackBuffer* buf = (InstrumentPackBuffer*)id;
    if (count <= 0) return 1;
    if (buf->size + (size_t)count > buf->capacity) {
        size_t capacity = buf->capacity ? buf->capacity * 2 : INSTRUMENT_PACK_FIRST_CAPACITY;
        while (capacity < buf->size + (size_t)count) capacity *= 2;
        uint8_t* grown = (uint8_t*)realloc(buf->data, capacity);
        if (!grown) {
            buf->failed = true;
            return 0;
        }
        buf->data = grown;
        buf->capacity = capacity;
    }
    memcpy(buf->data + buf->size, data, (size_t)count);
    buf->size += (size_t)count;
    return 1;
}

static bool instrument_compress(const float* pcm, int frames, int channels, int sample_rate, int bits,
                                InstrumentPackBuffer* out) {
    memset(out, 0, sizeof(*out));
    if (frames <= 0) return false;
    WavpackContext* wpc = WavpackOpenFileOutput(instrument_pack_output, out, NULL);
    if (!wpc) {
        fr_log("instrument: WavPack could not open an output stream");
        return false;
    }
    WavpackConfig config = {};
    config.bits_per_sample = bits;
    config.bytes_per_sample = instrument_bytes_per_sample(bits);
    config.num_channels = channels;
    config.sample_rate = sample_rate;
    config.channel_mask = channels == 1 ? INSTRUMENT_WP_CHANNEL_MASK_MONO : INSTRUMENT_WP_CHANNEL_MASK_STEREO;
    config.block_samples = INSTRUMENT_PACK_BLOCK_FRAMES;

    size_t samples = (size_t)frames * (size_t)channels;
    int32_t* ints = (int32_t*)malloc(samples * sizeof(int32_t));
    bool ok = ints != nullptr && WavpackSetConfiguration(wpc, &config, (uint32_t)frames) && WavpackPackInit(wpc);
    if (ok) {
        for (size_t i = 0; i < samples; ++i) ints[i] = instrument_sample_int(pcm[i], bits);
        for (int done = 0; done < frames && ok; done += INSTRUMENT_PACK_BLOCK_FRAMES) {
            int block = frames - done < INSTRUMENT_PACK_BLOCK_FRAMES ? frames - done : INSTRUMENT_PACK_BLOCK_FRAMES;
            ok = WavpackPackSamples(wpc, ints + (size_t)done * (size_t)channels, (uint32_t)block) != 0;
        }
        ok = ok && WavpackFlushSamples(wpc) != 0;
    }
    if (!ok) fr_log("instrument: WavPack: %s", WavpackGetErrorMessage(wpc));
    WavpackCloseFile(wpc);
    free(ints);
    ok = ok && !out->failed && out->size > 0;
    if (!ok) {
        free(out->data);
        memset(out, 0, sizeof(*out));
    }
    return ok;
}

static bool instrument_write_fsb(InstrumentJob* job, WavReader* wav, const InstrumentZoneMap* map, int zone_count,
                                 FSBZoneEntry* rows, const char (*names)[MAX_NAME_LENGTH]) {
    FILE* f = fopen(job->fsb_path, "wb");
    if (!f) {
        snprintf(job->result, sizeof(job->result), "Could not create %s", job->fsb_path);
        return false;
    }
    bool ram_only = job->cfg.preload_frames <= 0;
    int channels = wav->channels;
    int bits = job->cfg.bit_depth;
    int bytes_per_sample = instrument_bytes_per_sample(bits);
    FSBHeader header = {};
    header.magic = FSB_MAGIC;
    header.version = FSB_VERSION;
    header.sample_rate = (uint32_t)wav->sample_rate;
    header.channels = (uint32_t)channels;
    header.preload_frames = ram_only ? FSB_PRELOAD_ALL_FRAMES : (uint32_t)job->cfg.preload_frames;
    header.zone_count = (uint32_t)zone_count;
    header.bps = (uint32_t)bits;
    header.preload_offset = sizeof(FSBHeader) + sizeof(FSBZoneEntry) * (uint64_t)zone_count;

    bool ok = fwrite(&header, sizeof(header), 1, f) == 1;
    ok = ok && fwrite(rows, sizeof(FSBZoneEntry), (size_t)zone_count, f) == (size_t)zone_count;
    if (!ok) snprintf(job->result, sizeof(job->result), "Could not write %s", job->fsb_path);

    const ResampleScript* script = job->script;
    int64_t land_start = (int64_t)((double)LEGATO_DEFAULT_START_MS * wav->sample_rate / 1000.0 + 0.5);
    uint64_t preload_frame = 0;
    for (int z = 0; z < zone_count && ok; ++z) {
        if (job->cancel.load()) break;
        int64_t frames = map[z].frame_count;
        int64_t preload = ram_only || frames < (int64_t)job->cfg.preload_frames ? frames : (int64_t)job->cfg.preload_frames;
        if (!ram_only && script->legato) {
            int64_t reach = 0;
            if (map[z].transition) reach = (int64_t)map[z].departure + (int64_t)job->cfg.preload_frames;
            else if (!map[z].release) reach = land_start + (int64_t)job->cfg.preload_frames;
            if (reach > frames) reach = frames;
            if (reach > preload) preload = reach;
        }
        size_t samples = (size_t)preload * (size_t)channels;
        float* pcm = (float*)malloc(samples * sizeof(float));
        uint8_t* packed = (uint8_t*)malloc(samples * (size_t)bytes_per_sample);
        ok = pcm && packed && wav_reader_read(wav, map[z].first_frame, preload, pcm);
        if (ok) {
            instrument_pcm_pack(pcm, packed, samples, bits);
            ok = fwrite(packed, (size_t)bytes_per_sample, samples, f) == samples;
        }
        free(pcm);
        free(packed);
        if (!ok) {
            snprintf(job->result, sizeof(job->result), "Could not write sample %d of %d to the bank", z + 1, zone_count);
            break;
        }
        FSBZoneEntry* row = &rows[z];
        memset(row, 0, sizeof(*row));
        fr_strcpy(row->name, sizeof(row->name), names[z]);
        row->preload_frame_offset = preload_frame;
        row->preload_frame_count = (uint64_t)preload;
        row->total_frames = (uint64_t)frames;
        row->sample_frame_offset = 0;
        row->sample_index = (uint32_t)z;
        row->sample_frames = (uint32_t)frames;
        row->stream_offset = 0;
        row->stream_size = 0;
        row->stream_first_frame = (uint64_t)preload;
        preload_frame += (uint64_t)preload;
    }
    header.preload_size = preload_frame * (uint64_t)channels * (uint64_t)bytes_per_sample;

    uint64_t write_pos = header.preload_offset + header.preload_size;
    for (int z = 0; z < zone_count && ok; ++z) {
        if (job->cancel.load()) break;
        FSBZoneEntry* row = &rows[z];
        int64_t stream_frames = (int64_t)(row->total_frames - row->preload_frame_count);
        if (stream_frames > 0) {
            size_t samples = (size_t)stream_frames * (size_t)channels;
            float* pcm = (float*)malloc(samples * sizeof(float));
            InstrumentPackBuffer stream = {};
            ok = pcm && wav_reader_read(wav, map[z].first_frame + (int64_t)row->preload_frame_count, stream_frames, pcm) &&
                 instrument_compress(pcm, (int)stream_frames, channels, wav->sample_rate, bits, &stream);
            free(pcm);
            if (ok) {
                ok = fwrite(stream.data, 1, stream.size, f) == stream.size;
                row->stream_offset = write_pos;
                row->stream_size = (uint64_t)stream.size;
                write_pos += (uint64_t)stream.size;
            }
            free(stream.data);
            if (!ok) {
                snprintf(job->result, sizeof(job->result), "Could not compress sample %d of %d for the bank", z + 1, zone_count);
                break;
            }
        }
        job->progress.store(job->progress_done + z + 1);
    }

    if (ok && !job->cancel.load()) {
        ok = fseek(f, 0, SEEK_SET) == 0 &&
             fwrite(&header, sizeof(header), 1, f) == 1 &&
             fwrite(rows, sizeof(FSBZoneEntry), (size_t)zone_count, f) == (size_t)zone_count;
        if (!ok) snprintf(job->result, sizeof(job->result), "Could not finish %s", job->fsb_path);
    }
    fclose(f);
    return ok;
}

static bool instrument_morphs(const ResampleScript* script) {
    return script->dynamic_layers && script->round_robin_count > 1;
}

static int instrument_progress_units(const ResampleScript* script, int zone_count) {
    return instrument_morphs(script) ? zone_count * 2 : zone_count;
}

static int instrument_write_fingerprints(InstrumentJob* job, WavReader* wav, const InstrumentZoneMap* map, int zone_count,
                                         const char (*names)[MAX_NAME_LENGTH],
                                         const InstrumentCollectionPlan* plan, int collection_count,
                                         bool* fingerprinted) {
    const ResampleScript* script = job->script;
    for (int c = 0; c < collection_count; ++c) fingerprinted[c] = false;
    if (!instrument_morphs(script)) return 0;

    int64_t longest = 0;
    for (int z = 0; z < zone_count; ++z) {
        if (map[z].frame_count > longest) longest = map[z].frame_count;
    }
    FingerprintAnalyzer analyzer;
    SMFPZoneEntry* entries = (SMFPZoneEntry*)calloc((size_t)zone_count, sizeof(SMFPZoneEntry));
    if (!entries || !fingerprint_analyzer_init(&analyzer, longest, wav->channels)) {
        free(entries);
        fr_log("instrument: no room to measure the layers; they crossfade on gain alone");
        return 0;
    }

    int written = 0, collections = 0, flat = 0;
    for (int c = 0; c < collection_count && !job->cancel.load(); ++c) {
        int count = 0;
        bool readable = true;
        for (int z = 0; z < zone_count && readable; ++z) {
            if (map[z].collection != c) continue;
            SMFPZoneEntry* e = &entries[count++];
            memset(e, 0, sizeof(*e));
            fr_strcpy(e->file_path, sizeof(e->file_path), names[z]);
            e->total_frames = (uint32_t)map[z].frame_count;
            e->sample_rate = (uint32_t)wav->sample_rate;
            e->root_note = map[z].root_note;
            e->low_velocity = map[z].low_velocity;
            e->high_velocity = map[z].high_velocity;
            readable = fingerprint_zone(&analyzer, wav, map[z].first_frame, e->total_frames,
                                        map[z].loop_end > map[z].loop_start, map[z].loop_start, map[z].loop_end,
                                        map[z].root_note, e->band_db, &e->analysis_frames);
            if (readable && e->analysis_frames == 0) flat++;
            job->progress.store(job->progress_done + zone_count + z + 1);
        }
        if (count == 0) continue;
        collections++;
        SMFPHeader header = {};
        header.magic = SMFP_MAGIC_V4;
        header.version = SMFP_VERSION;
        header.zone_count = (uint32_t)count;
        header.band_count = SMFP_BAND_COUNT;
        instrument_collection_name(script, &plan[c], header.collection_name, sizeof(header.collection_name));
        char path[FR_MAX_PATH];
        if (!readable) {
            fr_log("instrument: could not read %s to measure it; the other layers do not morph toward it",
                   header.collection_name);
        } else if (!instrument_fingerprint_path(job, &plan[c], path, sizeof(path))) {
            fr_log("instrument: the fingerprint path of %s is too long; the other layers do not morph toward it",
                   header.collection_name);
        } else if (!smfp_save_file(path, &header, entries, (uint32_t)count)) {
            fr_log("instrument: could not write %s", path);
        } else {
            fingerprinted[c] = true;
            written++;
        }
    }
    fingerprint_analyzer_free(&analyzer);
    free(entries);
    fr_log("instrument: wrote %d of %d fingerprints for the morph between the layers; %d zones too short to measure are flat",
           written, collections, flat);
    return written;
}

static bool instrument_pb_write_callback(pb_ostream_t* stream, const uint8_t* buf, size_t count) {
    FILE* f = (FILE*)stream->state;
    return fwrite(buf, 1, count, f) == count;
}

static bool instrument_write_fsi(InstrumentJob* job, const InstrumentZoneMap* map, int zone_count,
                                 const char (*names)[MAX_NAME_LENGTH],
                                 const InstrumentCollectionPlan* plan, int collection_count,
                                 const bool* fingerprinted) {
    static_assert(sizeof(((FSIFile*)0)->smf_fingerprints) / sizeof(FSISMFFingerprints) >= INSTRUMENT_MAX_COLLECTIONS,
                  "every collection must be able to name its morph targets");
    const ResampleScript* script = job->script;
    FSIFile* msg = (FSIFile*)calloc(1, sizeof(FSIFile));
    if (!msg) {
        fr_strcpy(job->result, sizeof(job->result), "Out of memory for the instrument file");
        return false;
    }
    snprintf(msg->instrument_name, sizeof(msg->instrument_name), "%s %s", job->cfg.instrument, job->cfg.articulation);
    fr_strcpy(msg->fsb_path, sizeof(msg->fsb_path), plat_path_basename(job->fsb_path));

    int fsi_index[INSTRUMENT_MAX_COLLECTIONS];
    int sustain_step[INSTRUMENT_MAX_COLLECTIONS];
    int sustains = 0;
    msg->collections_count = 0;
    for (int c = 0; c < collection_count; ++c) {
        fsi_index[c] = -1;
        sustain_step[c] = 0;
        int first = -1, count = 0;
        for (int z = 0; z < zone_count; ++z) {
            if (map[z].collection != c) continue;
            if (first < 0) first = z;
            count++;
        }
        if (count == 0) continue;
        fsi_index[c] = (int)msg->collections_count;
        if (plan[c].kind == INSTRUMENT_COL_SUSTAIN) sustain_step[c] = ++sustains;
        FSICollection* col = &msg->collections[msg->collections_count++];
        instrument_collection_name(script, &plan[c], col->name, sizeof(col->name));
        col->zone_count = (uint32_t)count;
        col->modulator_count = 0;
        col->first_zone_index = (uint32_t)first;
        col->has_flags = true;
        col->flags = plan[c].kind == INSTRUMENT_COL_TRANSITION ? COLLECTION_FLAG_REFERENCED : 0;
        col->has_stable_id = true;  col->stable_id = (uint32_t)(c + 1);
        col->has_output_bus = true; col->output_bus = 0;
        col->has_release_of = true;
        col->release_of = plan[c].kind == INSTRUMENT_COL_RELEASE
                          ? (uint32_t)instrument_find_collection(plan, collection_count, INSTRUMENT_COL_SUSTAIN, plan[c].layer, 0) + 1
                          : 0;
    }

    msg->collection_fx_count = 0;
    msg->smf_fingerprints_count = 0;
    if (script->dynamic_layers) {
        bool layered = script->round_robin_count > 1;
        int controller = instrument_layer_controller(&job->cfg);
        for (int c = 0; c < collection_count; ++c) {
            if (fsi_index[c] < 0) continue;
            if (plan[c].kind == INSTRUMENT_COL_RELEASE) {
                instrument_fill_smart_release_fx(&msg->collection_fx[msg->collection_fx_count++], fsi_index[c]);
            } else if (layered) {
                instrument_fill_layer_fx(&msg->collection_fx[msg->collection_fx_count++], fsi_index[c],
                                         plan[c].layer, script->round_robin_count, controller);
            }
            if (layered && instrument_fill_morph_targets(&msg->smf_fingerprints[msg->smf_fingerprints_count], job,
                                                         plan, collection_count, c, fsi_index[c], fingerprinted)) {
                msg->smf_fingerprints_count++;
                instrument_fill_morph_fx(&msg->collection_fx[msg->collection_fx_count++], fsi_index[c],
                                         plan[c].layer, script->round_robin_count, controller);
            }
        }
    } else if (sustains > 1) {
        for (int c = 0; c < collection_count; ++c) {
            if (fsi_index[c] < 0 || sustain_step[c] == 0) continue;
            FSICollectionFX* fx = &msg->collection_fx[msg->collection_fx_count++];
            memset(fx, 0, sizeof(*fx));
            fx->type = (uint32_t)COLLECTION_FX_ROUND_ROBIN;
            fx->collection_index = (uint32_t)fsi_index[c];
            fx->params_count = CFX_RR_MODE + 1;
            fx->params[CFX_STRENGTH] = 1.0f;
            fx->params[CFX_RR_START] = 1.0f;
            fx->params[CFX_RR_END] = (float)sustains;
            fx->params[CFX_RR_ACTIVATE_STEP] = (float)sustain_step[c];
            fx->params[CFX_RR_GLOBAL_COUNTER] = 1.0f;
            fx->params[CFX_RR_MODE] = 0.0f;
        }
    }

    msg->collection_links_count = 0;
    if (script->legato) {
        float window_ms = job->cfg.legato_overlap_seconds * 1000.0f;
        for (int c = 0; c < collection_count; ++c) {
            if (fsi_index[c] < 0 || plan[c].kind != INSTRUMENT_COL_SUSTAIN) continue;
            instrument_fill_legato_fx(&msg->collection_fx[msg->collection_fx_count++], fsi_index[c],
                                      script, window_ms);
            int layer = script->dynamic_layers ? plan[c].layer : 1;
            for (int i = 0; i < script->interval_count; ++i) {
                int target = instrument_find_collection(plan, collection_count, INSTRUMENT_COL_TRANSITION, layer, script->intervals[i]);
                if (target < 0 || fsi_index[target] < 0) continue;
                FSICollectionLink* link = &msg->collection_links[msg->collection_links_count++];
                memset(link, 0, sizeof(*link));
                link->owner_id = (uint32_t)(c + 1);
                link->target_id = (uint32_t)(target + 1);
                link->interval_low = script->intervals[i];
                link->interval_high = script->intervals[i];
                link->owner_fx_type = (uint32_t)COLLECTION_FX_LEGATO;
                link->has_flags = true;
                link->flags = 0;
            }
        }
    }

    msg->zones_count = (pb_size_t)zone_count;
    bool ignore_velocity = script->dynamic_layers || script->legato;
    for (int z = 0; z < zone_count; ++z) {
        instrument_fill_zone(&msg->zones[z], names[z], &map[z], 0, ignore_velocity);
    }

    FILE* f = fopen(job->fsi_path, "wb");
    if (!f) {
        snprintf(job->result, sizeof(job->result), "Could not create %s", job->fsi_path);
        free(msg);
        return false;
    }
    uint32_t magic = FSI_MAGIC_PB;
    uint32_t size_placeholder = 0;
    bool ok = fwrite(&magic, sizeof(magic), 1, f) == 1 && fwrite(&size_placeholder, sizeof(size_placeholder), 1, f) == 1;

    pb_ostream_t stream = {};
    stream.callback = instrument_pb_write_callback;
    stream.state = f;
    stream.max_size = SIZE_MAX;
    ok = ok && pb_encode(&stream, FSIFile_fields, msg);
    if (!ok) {
        snprintf(job->result, sizeof(job->result), "Encoding the instrument failed: %s", PB_GET_ERROR(&stream));
    } else {
        uint32_t payload_size = (uint32_t)stream.bytes_written;
        ok = fseek(f, sizeof(uint32_t), SEEK_SET) == 0 && fwrite(&payload_size, sizeof(payload_size), 1, f) == 1;
        if (!ok) snprintf(job->result, sizeof(job->result), "Could not finish %s", job->fsi_path);
    }
    fclose(f);
    free(msg);
    return ok;
}

static bool instrument_write_rzc(InstrumentJob* job, const InstrumentZoneMap* map, int zone_count,
                                 const char (*names)[MAX_NAME_LENGTH],
                                 const InstrumentCollectionPlan* plan, int collection_count) {
    const ResampleScript* script = job->script;
    FSIZone* zones = (FSIZone*)calloc((size_t)zone_count, sizeof(FSIZone));
    char (*paths)[FR_MAX_PATH] = (char (*)[FR_MAX_PATH])calloc((size_t)zone_count, FR_MAX_PATH);
    if (!zones || !paths) {
        free(zones);
        free(paths);
        fr_strcpy(job->result, sizeof(job->result), "Out of memory for the zone collections");
        return false;
    }

    char sample_dir[FR_MAX_PATH];
    char dir_err[FR_STATUS_LEN];
    if (!resample_output_dir(&job->cfg, job->target, sample_dir, sizeof(sample_dir), dir_err, sizeof(dir_err))) {
        sample_dir[0] = '\0';
    }

    bool ok = true;
    bool ignore_velocity = script->dynamic_layers || script->legato;
    for (int c = 0; c < collection_count && ok; ++c) {
        int count = 0;
        for (int z = 0; z < zone_count; ++z) {
            if (map[z].collection != c) continue;
            const ResampleSegment* seg = &script->segments[map[z].segment];
            uint32_t offset = (uint32_t)(map[z].first_frame - seg->start_frame);
            instrument_fill_zone(&zones[count], names[z], &map[z], offset, ignore_velocity);
            char file_name[RESAMPLE_FILE_NAME_LEN];
            resample_sample_file_name(job->prefix, script->dynamic_layers, seg, file_name, sizeof(file_name));
            plat_path_join(paths[count], FR_MAX_PATH, sample_dir, file_name);
            count++;
        }
        if (count == 0) continue;
        char collection_name[32];
        instrument_collection_name(script, &plan[c], collection_name, sizeof(collection_name));
        char name[RESAMPLE_FILE_NAME_LEN];
        snprintf(name, sizeof(name), "%s_%s.rzc", job->prefix, collection_name);
        char path[FR_MAX_PATH];
        plat_path_join(path, sizeof(path), job->out_dir, name);
        ok = rzc_write(path, zones, paths, count);
        if (ok) job->rzc_written++;
        else snprintf(job->result, sizeof(job->result), "Could not write %s", name);
    }
    free(zones);
    free(paths);
    return ok;
}

static void instrument_refine_loops(InstrumentJob* job, WavReader* wav, InstrumentZoneMap* map, int zone_count) {
    int64_t longest = 0;
    int looped = 0;
    for (int z = 0; z < zone_count; ++z) {
        if (map[z].loop_end <= map[z].loop_start) continue;
        looped++;
        if (map[z].frame_count > longest) longest = map[z].frame_count;
    }
    if (looped == 0) return;

    LoopAnalyzer analyzer;
    if (!loop_analyzer_init(&analyzer, longest, wav->sample_rate)) {
        fr_log("instrument: no room to analyse the loops; keeping the timed points");
        return;
    }

    int analysed = 0, aligned = 0, start_moved = 0;
    double score_sum = 0.0;
    int64_t start_shift_sum = 0, end_shift_sum = 0;
    for (int z = 0; z < zone_count && !job->cancel.load(); ++z) {
        InstrumentZoneMap* m = &map[z];
        if (m->loop_end <= m->loop_start) continue;
        LoopBounds nominal = { m->loop_start, m->loop_end, m->xfade_frames };
        LoopResult r;
        if (!loop_refine(&analyzer, wav, m->first_frame, m->frame_count, m->root_note, &nominal, &r)) continue;
        m->loop_start = r.bounds.loop_start;
        m->loop_end = r.bounds.loop_end;
        m->xfade_frames = r.bounds.xfade_frames;
        analysed++;
        score_sum += r.score;
        if (r.aligned) aligned++;
        if (r.start_shift != 0) start_moved++;
        start_shift_sum += r.start_shift;
        end_shift_sum += r.end_shift;
    }
    loop_analyzer_free(&analyzer);

    if (analysed == 0) {
        fr_log("instrument: none of the %d looped zones could be analysed; keeping the timed points", looped);
        return;
    }
    fr_log("instrument: analysed %d of %d loops, mean correlation %.2f; %d phase-aligned (mean %+d frames), "
           "%d starts moved (mean %+d frames), %d kept their timed loop length",
           analysed, looped, score_sum / (double)analysed,
           aligned, (int)(end_shift_sum / analysed),
           start_moved, (int)(start_shift_sum / analysed),
           analysed - aligned);
}

static void instrument_refine_arrivals(InstrumentJob* job, WavReader* wav, InstrumentZoneMap* map, int zone_count) {
    const ResampleScript* script = job->script;
    if (!job->cfg.legato_detect_arrival || !script->legato) return;

    int64_t longest = 0;
    int transitions = 0;
    for (int z = 0; z < zone_count; ++z) {
        if (!map[z].transition) continue;
        const ResampleSegment* seg = &script->segments[map[z].segment];
        int64_t captured = seg->end_frame - seg->start_frame;
        if (captured > longest) longest = captured;
        transitions++;
    }
    if (transitions == 0) return;

    LegatoAnalyzer analyzer;
    if (!legato_analyzer_init(&analyzer, longest, wav->sample_rate)) {
        fr_log("instrument: no room to read the arrivals; keeping the times the tab gave them");
        return;
    }
    int64_t margin = (int64_t)((double)script->margin_seconds * script->sample_rate + 0.5);

    int read = 0, moved = 0;
    double score_sum = 0.0;
    int64_t shift_sum = 0;
    for (int z = 0; z < zone_count && !job->cancel.load(); ++z) {
        if (!map[z].transition) continue;
        const ResampleSegment* seg = &script->segments[map[z].segment];
        int64_t captured = seg->end_frame - seg->start_frame;
        int64_t note_on = seg->off_frame - seg->start_frame;
        LegatoBounds r;
        if (!legato_find_bounds(&analyzer, wav, seg->start_frame, captured, seg->source_note, seg->note,
                                note_on, (int64_t)map[z].departure, (int64_t)map[z].arrival, &r)) continue;
        read++;
        score_sum += r.score;
        if (!r.found) continue;
        int64_t departure = r.departure;
        if (departure < note_on) departure = note_on;
        if (departure > captured - 1) departure = captured - 1;
        int64_t arrival = r.arrival;
        if (arrival < departure) arrival = departure;
        if (arrival > captured - 1) arrival = captured - 1;
        int64_t first = departure - margin;
        if (first < 0) first = 0;
        int64_t wanted = arrival + margin;
        if (wanted > captured) wanted = captured;
        shift_sum += arrival - (int64_t)map[z].arrival;
        moved++;
        map[z].first_frame = seg->start_frame + first;
        map[z].departure = (uint32_t)(departure - first);
        map[z].arrival = (uint32_t)(arrival - first);
        map[z].frame_count = wanted - first;
    }
    legato_analyzer_free(&analyzer);

    if (read == 0) {
        fr_log("instrument: none of the %d transitions could be read; keeping the times the tab gave them", transitions);
        return;
    }
    fr_log("instrument: read %d of %d transitions, mean separation %.2f; %d arrivals moved (mean %+d ms), %d kept their nominal time",
           read, transitions, score_sum / (double)read, moved,
           moved > 0 ? (int)((double)shift_sum / (double)moved / script->sample_rate * 1000.0) : 0,
           read - moved);
}

static bool instrument_build_one(InstrumentJob* job) {
    const ResampleScript* script = job->script;
    int rzc_before = job->rzc_written;

    size_t capacity = (size_t)script->segment_count * 2;
    InstrumentZoneMap* map = (InstrumentZoneMap*)calloc(capacity, sizeof(InstrumentZoneMap));
    FSBZoneEntry* rows = (FSBZoneEntry*)calloc(capacity, sizeof(FSBZoneEntry));
    char (*names)[MAX_NAME_LENGTH] = (char (*)[MAX_NAME_LENGTH])calloc(capacity, MAX_NAME_LENGTH);
    WavReader wav = {};
    bool ok = map && rows && names;
    if (!ok) fr_strcpy(job->result, sizeof(job->result), "Out of memory for the mapping");

    const int fsi_zone_capacity = (int)(sizeof(FSIFile::zones) / sizeof(FSIZone));
    InstrumentCollectionPlan plan[INSTRUMENT_MAX_COLLECTIONS];
    int planned = instrument_plan(script, plan, INSTRUMENT_MAX_COLLECTIONS);
    int zone_count = 0;
    int collection_count = 0;
    if (ok && planned > INSTRUMENT_MAX_COLLECTIONS) {
        snprintf(job->result, sizeof(job->result),
                 "%d collections is more than the %d an instrument can hold; use fewer layers or legato intervals",
                 planned, INSTRUMENT_MAX_COLLECTIONS);
        ok = false;
    }
    if (ok) {
        zone_count = instrument_map(script, map, plan, planned);
        for (int z = 0; z < zone_count; ++z) {
            instrument_zone_name(job->prefix, script, &map[z], names[z], MAX_NAME_LENGTH);
            if (z == 0 || map[z].collection != map[z - 1].collection) collection_count++;
        }
        job->progress_total.store(instrument_progress_units(script, zone_count) * job->target_count);
        ok = zone_count > 0;
        if (!ok) fr_strcpy(job->result, sizeof(job->result), "The run holds no samples to map");
        if (ok && zone_count > fsi_zone_capacity) {
            snprintf(job->result, sizeof(job->result), "%d zones is more than the %d an instrument can hold", zone_count, fsi_zone_capacity);
            ok = false;
        }
    }
    if (ok) {
        ok = wav_reader_open(&wav, job->full_wav_path);
        if (!ok) snprintf(job->result, sizeof(job->result), "Could not read %s", plat_path_basename(job->full_wav_path));
    }
    if (ok) instrument_refine_loops(job, &wav, map, zone_count);
    if (ok) instrument_refine_arrivals(job, &wav, map, zone_count);
    if (ok) ok = instrument_write_fsb(job, &wav, map, zone_count, rows, names);
    bool fingerprinted[INSTRUMENT_MAX_COLLECTIONS] = {};
    int fingerprints = 0;
    if (ok && !job->cancel.load()) {
        fingerprints = instrument_write_fingerprints(job, &wav, map, zone_count, names, plan, planned, fingerprinted);
    }
    if (ok && !job->cancel.load()) ok = instrument_write_fsi(job, map, zone_count, names, plan, planned, fingerprinted);
    if (ok && !job->cancel.load() && job->cfg.export_rzc && job->cfg.split_samples) {
        ok = instrument_write_rzc(job, map, zone_count, names, plan, planned);
    }
    wav_reader_close(&wav);

    if (ok && job->cancel.load()) {
        fr_strcpy(job->result, sizeof(job->result), "Cancelled; the instrument files are incomplete.");
        job->result_is_error = true;
    } else if (ok) {
        if (job->cfg.delete_full_wav) {
            if (plat_file_delete(job->full_wav_path)) {
                job->full_wav_removed = true;
            } else {
                fr_log("instrument: could not remove %s", job->full_wav_path);
            }
        }
        char preload[32];
        resample_preload_label(job->cfg.preload_frames, preload, sizeof(preload));
        char rzc_note[96];
        rzc_note[0] = '\0';
        int rzc_here = job->rzc_written - rzc_before;
        if (rzc_here > 0) {
            snprintf(rzc_note, sizeof(rzc_note), " Plus %d .rzc file%s.", rzc_here, rzc_here == 1 ? "" : "s");
        } else if (job->cfg.export_rzc) {
            fr_strcpy(rzc_note, sizeof(rzc_note), " No .rzc: they name the split samples, which this run did not write.");
        }
        char link[64];
        link[0] = '\0';
        if (instrument_morphs(script)) {
            snprintf(link, sizeof(link), ": %d looped layers with releases, crossfaded on CC%d",
                     script->round_robin_count, instrument_layer_controller(&job->cfg));
        } else if (script->dynamic_layers) {
            fr_strcpy(link, sizeof(link), ": a looped layer with releases");
        } else if (script->round_robin_count > 1) {
            fr_strcpy(link, sizeof(link), " linked by a round robin effect");
        }
        char legato_note[96];
        legato_note[0] = '\0';
        if (script->legato) {
            snprintf(legato_note, sizeof(legato_note), " Legato over %d interval%s, the notes linked to the moves between them.",
                     script->interval_count, script->interval_count == 1 ? "" : "s");
        }
        char morph_note[80];
        morph_note[0] = '\0';
        if (instrument_morphs(script) && fingerprints > 0) {
            snprintf(morph_note, sizeof(morph_note), " The layers morph through %d fingerprints.", fingerprints);
        } else if (instrument_morphs(script)) {
            fr_strcpy(morph_note, sizeof(morph_note), " No fingerprints, so the layers crossfade on gain alone.");
        }
        snprintf(job->result, sizeof(job->result), "Wrote %s and %s: %d zones in %d collection%s%s, %d-bit, preload %s.%s%s%s%s",
                 plat_path_basename(job->fsi_path), plat_path_basename(job->fsb_path), zone_count,
                 collection_count, collection_count == 1 ? "" : "s", link,
                 job->cfg.bit_depth, preload, morph_note, legato_note,
                 rzc_note, job->full_wav_removed ? " The full WAV was removed." : "");
        job->result_is_error = false;
        job->zones_written += zone_count;
        job->progress_done += instrument_progress_units(script, zone_count);
        job->instruments_written++;
    } else {
        job->result_is_error = true;
    }
    free(map);
    free(rows);
    free(names);
    fr_log("instrument: %s", job->result);
    return ok && !job->cancel.load();
}

static void instrument_thread_main(void* user) {
    InstrumentJob* job = (InstrumentJob*)user;
    bool ok = true;
    for (int t = 0; t < job->target_count && ok; ++t) {
        const InstrumentTarget* target = &job->targets[t];
        job->target = t;
        fr_strcpy(job->full_wav_path, sizeof(job->full_wav_path), target->full_wav_path);
        fr_strcpy(job->out_dir, sizeof(job->out_dir), target->out_dir);
        fr_strcpy(job->prefix, sizeof(job->prefix), target->prefix);
        char name[RESAMPLE_FILE_NAME_LEN];
        snprintf(name, sizeof(name), "%s.fsi", target->prefix);
        plat_path_join(job->fsi_path, sizeof(job->fsi_path), target->out_dir, name);
        snprintf(name, sizeof(name), "%s.fsb", target->prefix);
        plat_path_join(job->fsb_path, sizeof(job->fsb_path), target->out_dir, name);

        if (job->target_count > 1) {
            fr_log("instrument: bus %d of %d, '%s'", t + 1, job->target_count, target->bus_name);
        }
        ok = instrument_build_one(job);
        if (job->target_count > 1 && job->bus_result_count < FR_MAX_OUTPUT_BUSSES) {
            snprintf(job->bus_result[job->bus_result_count], FR_STATUS_LEN, "%s: %s", target->bus_name, job->result);
            job->bus_result_count++;
        }
        job->bus_done.store(job->instruments_written);
        if (!ok && job->target_count > 1) {
            char why[FR_STATUS_LEN];
            fr_strcpy(why, sizeof(why), job->result);
            snprintf(job->result, sizeof(job->result), "Output bus '%s': %s", target->bus_name, why);
        }
    }

    if (ok && job->target_count > 1) {
        snprintf(job->result, sizeof(job->result),
                 "Wrote %d instruments, one per output bus: %d zones over %d folders, %d-bit.%s",
                 job->instruments_written, job->zones_written, job->instruments_written,
                 job->cfg.bit_depth, job->full_wav_removed ? " The full WAVs were removed." : "");
        job->result_is_error = false;
    }
    job->finished.store(true);
}

bool instrument_start(InstrumentJob* job, const ResampleScript* script, const ResampleConfig* cfg,
                      const InstrumentTarget* targets, int target_count) {
    if (job->running.load()) return false;
    if (!script || !script->built || script->segment_count == 0) return false;
    if (!targets || target_count < 1 || target_count > FR_MAX_OUTPUT_BUSSES) return false;
    job->script = script;
    job->cfg = *cfg;
    job->target_count = target_count;
    for (int t = 0; t < target_count; ++t) job->targets[t] = targets[t];
    job->target = 0;
    fr_strcpy(job->full_wav_path, sizeof(job->full_wav_path), targets[0].full_wav_path);
    fr_strcpy(job->out_dir, sizeof(job->out_dir), targets[0].out_dir);
    fr_strcpy(job->prefix, sizeof(job->prefix), targets[0].prefix);
    char name[RESAMPLE_FILE_NAME_LEN];
    snprintf(name, sizeof(name), "%s.fsi", targets[0].prefix);
    plat_path_join(job->fsi_path, sizeof(job->fsi_path), targets[0].out_dir, name);
    snprintf(name, sizeof(name), "%s.fsb", targets[0].prefix);
    plat_path_join(job->fsb_path, sizeof(job->fsb_path), targets[0].out_dir, name);

    job->result[0] = '\0';
    job->result_is_error = false;
    job->full_wav_removed = false;
    job->rzc_written = 0;
    job->zones_written = 0;
    job->instruments_written = 0;
    job->bus_result_count = 0;
    job->cancel.store(false);
    job->finished.store(false);
    job->progress.store(0);
    job->progress_done = 0;
    job->bus_done.store(0);
    job->progress_total.store(instrument_progress_units(script, script->segment_count * (script->dynamic_layers ? 2 : 1)) * target_count);
    job->running.store(true);
    if (!plat_thread_create(&job->thread, instrument_thread_main, job)) {
        job->running.store(false);
        return false;
    }
    fr_log("instrument: building %d instrument%s from %d samples%s%s", target_count, target_count == 1 ? "" : "s",
           script->segment_count,
           script->dynamic_layers ? ", as looped dynamic layers with releases" : "",
           script->legato ? ", with the moves between the notes" : "");
    return true;
}

void instrument_cancel(InstrumentJob* job) {
    if (job->running.load()) job->cancel.store(true);
}

bool instrument_collect(InstrumentJob* job) {
    if (!job->running.load() || !job->finished.load()) return false;
    plat_thread_join(&job->thread);
    job->running.store(false);
    return true;
}
