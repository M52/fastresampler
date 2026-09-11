// #pragma once (removed for unity build)
// See fastresampler_instrument.h. The FastSampler format headers and the
// nanopb runtime are included by the unity root before this file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "fastresampler_instrument.h"
#include "fastresampler_rzc.h"
#include "fastresampler_wav.h"
#include "fastresampler_loop.h"
#include "fastresampler_helpers.h"
#include "fastresampler_log.h"

// The bank is written at the depth the tab picked, 16 or 24 bit: the two the
// format carries, and the two FastSampler's own export offers. Two or three
// little-endian bytes per sample, packed, at both depths.

// A zone's release in seconds. Zones in FastSampler default to none, which
// cuts a note at the key-up; a short tail keeps a mapped instrument playable
// before the user has touched anything.
#define INSTRUMENT_ZONE_RELEASE_SECONDS 0.25f

// A release sample's attack. It begins at the note-off of a note that was
// still sounding, so its first sample is whatever level the note had got to,
// not silence. Started flat it steps from nothing to that level, which is a
// click on every key-up. This is only long enough to take the step off: a
// release is what rings out after the key, and a longer rise would eat the
// one part of it that is loud.
#define INSTRUMENT_RELEASE_ATTACK_SECONDS 0.01f

// A sustain's loop, with dynamic layers: it starts this long after the
// note-on and ends this long before the note-off, so the attack and the
// last moments of the hold stay out of it, and crosses over the loop point
// for this long, equal power, so the join does not click. Both give way on
// a hold too short for them.
//
// These are where the loop starts out, not where it ends up: once the full
// WAV is open, instrument_refine_loops() moves both points onto the
// waveform -- loop_start to where the attack has settled and the level
// matches across the wrap, loop_end to the nearest loop length that
// crossfades in phase. See fastresampler_loop.h. A zone the analysis cannot
// read, or cannot find stable phase in, keeps the timed points below.
#define INSTRUMENT_LOOP_MARGIN_SECONDS 0.25f
#define INSTRUMENT_LOOP_XFADE_SECONDS  0.2f
// FastSampler's loop crossfade shapes, as its .rzc names them: 2 is Equal Power.
#define INSTRUMENT_XFADE_EQUAL_POWER   2

// The crossfade of the dynamic layers: FastSampler's gain effect on every
// collection, driven by one controller through a curve, between this floor
// and 0 dB, with this much lag so the controller's 7-bit steps do not step
// the level. The curves go in FastSampler's first curve group, so it draws
// them over one another.
#define INSTRUMENT_LAYER_FLOOR_DB      -36.0f
#define INSTRUMENT_LAYER_LAG_MS        10.0f
#define INSTRUMENT_LAYER_CURVE_GROUP   1
#define INSTRUMENT_HALF_PI             1.5707963f

// The mapping of one zone: its span of keys and velocities, and the part of
// the full WAV it plays. A round robin run maps every segment once, whole.
// A dynamic layer run maps a sustain twice: the held note as a looped
// sustain, and what rings out past the note-off as a release sample. A
// transition is one zone, cut at the blend past where it lands.
struct InstrumentZoneMap {
    int segment;             // index into the script's segments
    int low_note, high_note, root_note;
    int low_velocity, high_velocity;
    int collection;          // 0-based; the zones of one collection are contiguous
    bool release;            // the release half: from the note-off to the end
    bool transition;         // a transition between two notes; root_note is the one it leaves
    int64_t first_frame;     // in the full WAV
    int64_t frame_count;
    uint32_t loop_start;     // a looped sustain, relative to first_frame; both 0 without a loop
    uint32_t loop_end;
    uint32_t xfade_frames;
    uint32_t departure;      // a transition: where it departs, from first_frame
    uint32_t arrival;        // a transition: where it lands, from first_frame
};

// --- The collections of a run -----------------------------------------------------
// One per round robin or dynamic layer, then one per layer's releases, then
// one per layer and legato interval. Collection c holds a contiguous run of
// zones either way, as FSICollection.first_zone_index needs.
enum InstrumentCollectionKind {
    INSTRUMENT_COL_SUSTAIN = 0,
    INSTRUMENT_COL_RELEASE,
    INSTRUMENT_COL_TRANSITION
};

#define INSTRUMENT_MAX_COLLECTIONS ((int)(sizeof(((FSIFile*)0)->collections) / sizeof(FSICollection)))

struct InstrumentCollectionPlan {
    int kind;
    int layer;       // 1-based
    int interval;    // a transition: the interval it holds
};

// The transitions are captured once per dynamic layer, and once for every other
// run: a round robin run gives all its sustain collections the same
// transitions.
static int instrument_transition_layers(const ResampleScript* script) {
    return script->dynamic_layers ? script->round_robin_count : 1;
}

// Returns how many collections the run needs, filling out with as many as it
// holds: the caller reports a run that asks for more than the format can
// carry rather than dropping the rest.
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
    // The releases of a layered run sit in a collection of their own, so the
    // sample map does not lay them under the notes they belong to, where they
    // are hard to even pick out. A legato run too: the collection names its
    // layer as the one whose releases it holds, which is how FastSampler
    // still knows to hold a release back for a note a transition has taken over --
    // the mark that says so is on the layer, not on the releases.
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

// "RR1", "dyn1", "dyn1-release", "leg+2" or "dyn1-leg-1".
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

// Which field of a segment a distinct list is taken over.
enum InstrumentField {
    INSTRUMENT_FIELD_NOTE = 0,
    INSTRUMENT_FIELD_VELOCITY,
    INSTRUMENT_FIELD_SOURCE
};

// Whether a segment belongs to the part of the run a mapping is working on.
// A layer or an interval of 0 keeps every one of them.
static bool instrument_segment_matches(const ResampleSegment* seg, int kind, int layer, int interval) {
    if (seg->kind != kind) return false;
    if (layer != 0 && seg->round_robin != layer) return false;
    if (kind == RESAMPLE_SEG_TRANSITION && interval != 0 && seg->interval != interval) return false;
    return true;
}

// Sorted distinct values of one field over the segments a filter keeps.
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

// The key range a sampled note covers. A key between two samples goes to the
// nearer one; the middle key of an even gap goes up, so it is pitched down.
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

// The sustain zones of one collection: every sampled note at every sampled
// velocity, as the held note or as what rings out past its note-off.
// Returns how many were written.
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
            if (seg < 0) continue;   // a run that was cut short
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
                // The held note, looped. The margins shrink on a short
                // hold, and the crossfade fits inside what is left.
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

// The transition zones of one collection: every note a transition of this
// interval was captured from, at every legato velocity. The key that plays
// one is the note it is left from, so that is the zone's root and what its
// span covers. The zone stops a blend past where the transition lands: what
// follows in the capture is the note landed on, which the sustain plays from
// there.
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

// The zones in file order, collection by collection. Each collection's zones
// are contiguous, as FSICollection.first_zone_index needs. A release with
// nothing past the note-off, from a run without a gap, is left out.
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

// The zone's name, which is also its bank row's name: the sample file name,
// "-release" before the extension for a release, shortened from the front
// when it would not fit the format's 63 characters.
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

// A zone as FastSampler reads it. The frames are those of the file the zone
// plays, and offset is where the zone's part of that file begins: 0 in the
// bank, where every zone is a sample of its own, and the note-off of the
// split file for a release in an .rzc. A dynamic layer run's zones ignore
// velocity for their level, as the layers carry the dynamics, and so do a
// legato run's: there the key is pressed at whatever velocity picks the
// transition, and a slur played softly to make it slow must not come out
// quiet as well. A transition always does, as its level belongs to the one
// that was captured.
//
// A release carries an attack of its own, short, because it starts at the
// note-off of a note that was still sounding and would otherwise step from
// silence to that level on every key-up.
//
// A transition carries no envelope of its own and two more frames.
// departureframe and arrivalframe are the two bounds of the transition, and
// FastSampler's legato effect shapes every edge from them: it starts the
// voice one fade-in before the departure, fading the note being left across
// that same time, and fades the voice out at the arrival while the sustain
// of the note landed on rises, so neither sustain sounds while the pitch is
// on its way between them. A zone attack or release here would double one
// of those fades with a curve that is not its complement.
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

// --- The crossfade of the layers --------------------------------------------------

// The controller the instrument crossfades its layers on: the one the curves
// tied to a layer were drawn for, else the mod wheel.
static int instrument_layer_controller(const ResampleConfig* cfg) {
    for (int i = 0; i < cfg->cc_curve_count; ++i) {
        if (cfg->cc_curves[i].layer > 0) return cfg->cc_curves[i].controller;
    }
    return MIDI_CC_MOD_WHEEL;
}

// The gain curve of one layer over the controller's range, as the driver
// reads it: 0 for the floor, 1 for 0 dB. Layer k of n is at 0 dB at k/n of
// the range, the first from the bottom up to 1/n and the last at the top.
// Between the peaks of two layers the lower fades out and the upper in by
// an equal power law, so a note keeps its level while the controller moves.
static void instrument_layer_curve(int layer, int layer_count, float* out) {
    float n = (float)layer_count;
    float lo = (float)(layer - 1) / n;
    float mid = (float)layer / n;
    float hi = (float)(layer + 1) / n;
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

// FastSampler's gain effect on one collection, its level driven by the
// controller through the curve of the collection's layer.
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
    d->target_param_idx = CFX_GAIN_LEVEL_DB;
    d->source_type = (uint32_t)FX_DRIVER_SOURCE_CC;
    d->has_cc_number = true;         d->cc_number = controller;
    d->has_lfo_rate = true;          d->lfo_rate = 1.0f;
    d->has_lfo_waveform = true;      d->lfo_waveform = 0;
    d->has_lfo_polyphonic = true;    d->lfo_polyphonic = true;
    d->has_depth = true;             d->depth = 1.0f;
    d->has_use_curve = true;         d->use_curve = true;
    d->has_expanded = true;          d->expanded = false;
    d->has_vel_source = true;        d->vel_source = 0;
    d->has_out_min = true;           d->out_min = INSTRUMENT_LAYER_FLOOR_DB;
    d->has_out_max = true;           d->out_max = 0.0f;
    d->has_range_initialized = true; d->range_initialized = true;
    d->has_curve_group = true;       d->curve_group = INSTRUMENT_LAYER_CURVE_GROUP;
    d->has_hold_rise_ms = true;      d->hold_rise_ms = CFX_HOLD_RISE_DEFAULT_MS;
    d->has_hold_fall_ms = true;      d->hold_fall_ms = CFX_HOLD_FALL_DEFAULT_MS;
    d->curve_data_count = MOD_CURVE_POINTS;
    instrument_layer_curve(layer, layer_count, d->curve_data);
}

// --- Legato --------------------------------------------------------------------

// From FastSampler's fastsampler_zonecollection.h, which cannot be included
// here: it brings in that project's platform layer, the one this project
// forked, and the two collide. Compare the pair when it changes.
#define INSTRUMENT_COLLECTION_FLAG_REFERENCED (1 << 3)

// FastSampler's legato effect on one sustain collection, with the sampler's
// own defaults for every fade and the two things this run knows better.
// The fades are clamped to the margin, which is all the room a transition
// has for them. When the run captured the transitions at more than one
// velocity, those
// layers are the library's legato speeds, and the sampler's own reading of
// the playing speed is what should pick between them; a single layer is
// played at the velocity of the note, as any zone is.
//
// The shape is equal power: the two sides of each crossfade are separate
// takes of the same note, which add as power rather than as amplitude.
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

// The intervals one link row answers for: its own, and nothing else.
//
// A row used to cover the widths either side of the one it was recorded at,
// so a leap the run never captured still played the nearest move it did.
// That is wrong, and audibly so. A move is a recording of two particular
// notes, and it is played at the note it is left from, so the note it lands
// on is fixed: answer a fourth with the move of a second and the transition
// arrives a tone above the key that was pressed, which is then heard under
// the sustain of the right note as a passing note that was never played.
// An interval with no recording of its own gets no link, so FastSampler
// resolves no move and changes note without one -- no move at all being the
// only honest answer, and the quiet one.
static void instrument_link_band(const ResampleScript* script, int index, int* low, int* high) {
    *low = *high = script->intervals[index];
}

// The sustain collection of a layer, which is the one a layer's releases
// belong to. Every layer has exactly one.
static int instrument_find_sustain(const InstrumentCollectionPlan* plan, int count, int layer) {
    for (int c = 0; c < count; ++c) {
        if (plan[c].kind == INSTRUMENT_COL_SUSTAIN && plan[c].layer == layer) return c;
    }
    return -1;
}

static int instrument_find_transition(const InstrumentCollectionPlan* plan, int count, int layer, int interval) {
    for (int c = 0; c < count; ++c) {
        if (plan[c].kind == INSTRUMENT_COL_TRANSITION && plan[c].layer == layer && plan[c].interval == interval) return c;
    }
    return -1;
}

// --- The bank ------------------------------------------------------------------

// The bytes one sample takes at a bank's depth. Anything but 16 is 24, which
// is how the format's own reader reads the field.
static int instrument_bytes_per_sample(int bits) {
    return bits == 16 ? 2 : 3;
}

// One float sample as the bank's integer at that depth. Both clamps are
// needed: the first keeps the multiply in range, the second holds the
// positive rail, which is one step below the scale.
static int32_t instrument_sample_int(float s, int bits) {
    float   scale = bits == 16 ? PCM_ENCODE_16BIT : PCM_ENCODE_24BIT;
    int32_t low   = bits == 16 ? PCM_MIN_16BIT : PCM_MIN_24BIT;
    int32_t high  = bits == 16 ? PCM_MAX_16BIT : PCM_MAX_24BIT;
    int32_t v = (int32_t)lrintf(fr_clampf(s, -1.0f, 1.0f) * scale);
    if (v > high) v = high;
    if (v < low) v = low;
    return v;
}

// Float frames to the bank's packed little-endian samples at that depth.
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

// --- WavPack ---------------------------------------------------------------------
// The streamed part of a sample is compressed the way FastSampler's own
// export compresses it: at the bank's depth, normal mode, packed in blocks
// of frames.

#define INSTRUMENT_PACK_BLOCK_FRAMES     4096
#define INSTRUMENT_PACK_FIRST_CAPACITY   65536
// WAVE channel masks: front-centre for mono, front-left and front-right for stereo.
#define INSTRUMENT_WP_CHANNEL_MASK_MONO   4
#define INSTRUMENT_WP_CHANNEL_MASK_STEREO 3

// Where the encoder puts its blocks: one growing buffer per stream.
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

// Float frames to one WavPack stream. False, with nothing kept, when the
// library refused; the log says what it said.
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

// The bank: every zone is its own stored sample. In RAM mode each sample is
// held whole in the preload block and nothing streams. Otherwise each sample
// keeps its first preload frames in the block and the rest follows, WavPack
// compressed, in a stream of its own: FastSampler's own layout for a sample
// one zone plays. Rows are written in zone order because the loader pairs
// row i with zone i.
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

    // Placeholders; the header and the rows are rewritten once the sizes are known.
    bool ok = fwrite(&header, sizeof(header), 1, f) == 1;
    ok = ok && fwrite(rows, sizeof(FSBZoneEntry), (size_t)zone_count, f) == (size_t)zone_count;

    // The preload block: the resident head of every sample, one after the other.
    //
    // A legato voice starts deep inside its sample: a transition one fade-in
    // before its departure, a sustain landed on past its attack, at the
    // legato effect's landing start. Both keep the same resident headroom
    // ahead of them that any note has at its start, or their first block
    // waits on the disk. FastSampler's own exporter applies the same rule.
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
        row->stream_first_frame = (uint64_t)preload;   // the stream, when there is one, starts past the preload
        preload_frame += (uint64_t)preload;
    }
    header.preload_size = preload_frame * (uint64_t)channels * (uint64_t)bytes_per_sample;

    // The streams: what each sample holds past its preload. The file position
    // is counted here rather than asked, so a bank past 2 GB keeps its offsets.
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
        job->progress.store(z + 1);
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

// --- The instrument ---------------------------------------------------------------

static bool instrument_pb_write_callback(pb_ostream_t* stream, const uint8_t* buf, size_t count) {
    FILE* f = (FILE*)stream->state;
    return fwrite(buf, 1, count, f) == count;
}

// magic(4) + payload size(4) + the encoded FSIFile, as fastsampler_pb.cpp writes it.
static bool instrument_write_fsi(InstrumentJob* job, const InstrumentZoneMap* map, int zone_count,
                                 const char (*names)[MAX_NAME_LENGTH],
                                 const InstrumentCollectionPlan* plan, int collection_count) {
    const ResampleScript* script = job->script;
    FSIFile* msg = (FSIFile*)calloc(1, sizeof(FSIFile));
    if (!msg) {
        fr_strcpy(job->result, sizeof(job->result), "Out of memory for the instrument file");
        return false;
    }
    snprintf(msg->instrument_name, sizeof(msg->instrument_name), "%s %s", job->cfg.instrument, job->cfg.articulation);
    fr_strcpy(msg->fsb_path, sizeof(msg->fsb_path), plat_path_basename(job->fsb_path));

    // The collections the plan asked for, each over a contiguous run of
    // zones. One left without zones is dropped, so the file's index of each
    // that stays is kept for its effects. The stable id is the plan's own
    // index, which the link rows name either end by.
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
        // A collection of transitions is only ever reached through a link, never
        // from the keyboard.
        col->has_flags = true;
        col->flags = plan[c].kind == INSTRUMENT_COL_TRANSITION ? INSTRUMENT_COLLECTION_FLAG_REFERENCED : 0;
        col->has_stable_id = true;  col->stable_id = (uint32_t)(c + 1);
        col->has_output_bus = true; col->output_bus = 0;
        // Whose releases these are: the sustain collection of the same layer,
        // which the plan lays down first, one per layer, so that layer's own
        // index is its plan index.
        col->has_release_of = true;
        col->release_of = plan[c].kind == INSTRUMENT_COL_RELEASE
                          ? (uint32_t)instrument_find_sustain(plan, collection_count, plan[c].layer) + 1
                          : 0;
    }

    msg->collection_fx_count = 0;
    if (script->dynamic_layers && script->round_robin_count > 1) {
        // Every collection, the releases and the moves too, gets the gain
        // effect with the curve of its layer, so a release and a transition
        // both come in at the level of the layer that was sounding.
        int controller = instrument_layer_controller(&job->cfg);
        for (int c = 0; c < collection_count; ++c) {
            if (fsi_index[c] < 0) continue;
            instrument_fill_layer_fx(&msg->collection_fx[msg->collection_fx_count++], fsi_index[c],
                                     plan[c].layer, script->round_robin_count, controller);
        }
    } else if (!script->dynamic_layers && sustains > 1) {
        // With more than one round robin, every sustain collection gets
        // FastSampler's round robin effect on one shared instrument counter:
        // each note advances the counter, and collection k sounds when it
        // stands at k. That is how FastSampler links round robin
        // collections; no link rows are involved. The moves are shared by
        // all of them and take no turn of their own.
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
            fx->params[CFX_RR_MODE] = 0.0f;   // sequence
        }
    }

    // The legato effect on every sustain collection, and one link row per
    // interval from it to the collection of moves that answers for that
    // interval. A layered run gives each layer its own moves; every other
    // run shares the one set.
    msg->collection_links_count = 0;
    if (script->legato) {
        // The capture's overlap is how long the note left stayed down after
        // the next went down, so a key that comes up that soon before the
        // next goes down still counts as the note a move leaves from.
        float window_ms = job->cfg.legato_overlap_seconds * 1000.0f;
        for (int c = 0; c < collection_count; ++c) {
            if (fsi_index[c] < 0 || plan[c].kind != INSTRUMENT_COL_SUSTAIN) continue;
            instrument_fill_legato_fx(&msg->collection_fx[msg->collection_fx_count++], fsi_index[c],
                                      script, window_ms);
            int layer = script->dynamic_layers ? plan[c].layer : 1;
            for (int i = 0; i < script->interval_count; ++i) {
                int target = instrument_find_transition(plan, collection_count, layer, script->intervals[i]);
                if (target < 0 || fsi_index[target] < 0) continue;
                int low, high;
                instrument_link_band(script, i, &low, &high);
                FSICollectionLink* link = &msg->collection_links[msg->collection_links_count++];
                memset(link, 0, sizeof(*link));
                link->owner_id = (uint32_t)(c + 1);
                link->target_id = (uint32_t)(target + 1);
                link->interval_low = low;
                link->interval_high = high;
                link->owner_fx_type = (uint32_t)COLLECTION_FX_LEGATO;
                // No flags: a move is played at the note it is left from,
                // which is the note its zones are mapped and rooted on.
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

// --- The readable zone collections -----------------------------------------------

// One .rzc per collection, beside the instrument: its zones, each naming
// the sample file the split wrote for it. A split file spans the whole
// segment, so a sustain's zone plays its front and a release's zone the
// part past the note-off. The zones are filled by the same function the
// .fsi uses, so the two agree by construction. FastSampler opens the paths
// as they stand, so they go in whole rather than relative to the file.
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

    // The split samples the zones name are in the folder the render wrote to.
    char sample_dir[FR_MAX_PATH];
    char dir_err[FR_STATUS_LEN];
    if (!resample_output_dir(&job->cfg, sample_dir, sizeof(sample_dir), dir_err, sizeof(dir_err))) {
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

// --- The worker ------------------------------------------------------------------

// The loop points the clock gave every sustain, moved onto the waveform.
// One analyser serves the whole run, sized for the longest held note, and
// each zone reads its own hold once. A zone the analysis declines is left
// exactly as instrument_map() left it, so the worst case here is the
// instrument this builder made before the analysis existed.
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


// The arrival the clock gave every transition, moved onto the waveform: the
// frame the move really lands on the note it goes to, which is where
// FastSampler hands over to that note's sustain. One analyser serves the
// whole run, sized for the longest capture. A zone the analysis cannot read,
// or cannot hear the two notes apart in, keeps the nominal time, so the
// worst case here is the instrument the nominal times alone would have made.
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
        // Neither bound can fall before the key that asked for the move went
        // down, and the move has to leave before it lands.
        int64_t departure = r.departure;
        if (departure < note_on) departure = note_on;
        if (departure > captured - 1) departure = captured - 1;
        int64_t arrival = r.arrival;
        if (arrival < departure) arrival = departure;
        if (arrival > captured - 1) arrival = captured - 1;
        // The sample keeps one margin either side of the move: the note
        // being left in front of the departure, the note landed on past the
        // arrival. The departure is never before the key that asked for the
        // move, which is one margin into the capture, so the cut never
        // reaches before what was analysed.
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

static void instrument_thread_main(void* user) {
    InstrumentJob* job = (InstrumentJob*)user;
    const ResampleScript* script = job->script;

    // At most two zones per segment: a sustain is a held note and a release,
    // a transition is one zone.
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
        job->progress_total.store(zone_count);
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
    // The loops and the arrivals move onto the waveform before anything is
    // written, so the bank, the .fsi and the .rzc all carry the same points.
    if (ok) instrument_refine_loops(job, &wav, map, zone_count);
    if (ok) instrument_refine_arrivals(job, &wav, map, zone_count);
    if (ok) ok = instrument_write_fsb(job, &wav, map, zone_count, rows, names);
    if (ok && !job->cancel.load()) ok = instrument_write_fsi(job, map, zone_count, names, plan, planned);
    // The .rzc names the split sample files, so a run that wrote none has
    // nothing for it to point at; the result says so rather than leaving
    // collections behind that cannot be opened.
    if (ok && !job->cancel.load() && job->cfg.export_rzc && job->cfg.split_samples) {
        ok = instrument_write_rzc(job, map, zone_count, names, plan, planned);
    }
    wav_reader_close(&wav);

    if (ok && job->cancel.load()) {
        fr_strcpy(job->result, sizeof(job->result), "Cancelled; the instrument files are incomplete.");
        job->result_is_error = true;
    } else if (ok) {
        // The bank now holds every sample the full WAV was rendered for, and
        // the reader is closed, so the file can go when the tab asked for it.
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
        if (job->rzc_written > 0) {
            snprintf(rzc_note, sizeof(rzc_note), " Plus %d .rzc file%s.",
                     job->rzc_written, job->rzc_written == 1 ? "" : "s");
        } else if (job->cfg.export_rzc) {
            fr_strcpy(rzc_note, sizeof(rzc_note), " No .rzc: they name the split samples, which this run did not write.");
        }
        // What links the collections: the counter of the round robins, or
        // the controller the layers crossfade on.
        char link[64];
        link[0] = '\0';
        if (script->dynamic_layers && script->round_robin_count > 1) {
            snprintf(link, sizeof(link), ": %d looped layers with releases, crossfaded on CC%d",
                     script->round_robin_count, instrument_layer_controller(&job->cfg));
        } else if (script->dynamic_layers) {
            fr_strcpy(link, sizeof(link), ": a looped layer with releases");
        } else if (script->round_robin_count > 1) {
            fr_strcpy(link, sizeof(link), " linked by a round robin effect");
        }
        // What the moves added, when the run captured any.
        char legato_note[96];
        legato_note[0] = '\0';
        if (script->legato) {
            snprintf(legato_note, sizeof(legato_note), " Legato over %d interval%s, the notes linked to the moves between them.",
                     script->interval_count, script->interval_count == 1 ? "" : "s");
        }
        snprintf(job->result, sizeof(job->result), "Wrote %s and %s: %d zones in %d collection%s%s, %d-bit, preload %s.%s%s%s",
                 plat_path_basename(job->fsi_path), plat_path_basename(job->fsb_path), zone_count,
                 collection_count, collection_count == 1 ? "" : "s", link,
                 job->cfg.bit_depth, preload, legato_note,
                 rzc_note, job->full_wav_removed ? " The full WAV was removed." : "");
        job->result_is_error = false;
    } else {
        job->result_is_error = true;
    }
    free(map);
    free(rows);
    free(names);
    fr_log("instrument: %s", job->result);
    job->finished.store(true);
}

bool instrument_start(InstrumentJob* job, const ResampleScript* script, const ResampleConfig* cfg,
                      const char* full_wav_path, const char* out_dir, const char* prefix) {
    if (job->running.load()) return false;
    if (!script || !script->built || script->segment_count == 0) return false;
    job->script = script;
    job->cfg = *cfg;
    fr_strcpy(job->full_wav_path, sizeof(job->full_wav_path), full_wav_path);
    fr_strcpy(job->out_dir, sizeof(job->out_dir), out_dir);
    fr_strcpy(job->prefix, sizeof(job->prefix), prefix);
    char name[RESAMPLE_FILE_NAME_LEN];
    snprintf(name, sizeof(name), "%s.fsi", prefix);
    plat_path_join(job->fsi_path, sizeof(job->fsi_path), out_dir, name);
    snprintf(name, sizeof(name), "%s.fsb", prefix);
    plat_path_join(job->fsb_path, sizeof(job->fsb_path), out_dir, name);

    job->result[0] = '\0';
    job->result_is_error = false;
    job->full_wav_removed = false;
    job->rzc_written = 0;
    job->cancel.store(false);
    job->finished.store(false);
    job->progress.store(0);
    job->progress_total.store(script->segment_count * (script->dynamic_layers ? 2 : 1));
    job->running.store(true);
    if (!plat_thread_create(&job->thread, instrument_thread_main, job)) {
        job->running.store(false);
        return false;
    }
    fr_log("instrument: building %s from %d samples%s%s", job->fsi_path, script->segment_count,
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
