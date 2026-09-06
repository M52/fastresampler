// #pragma once (removed for unity build)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "fastresampler_resample.h"
#include "fastresampler_host.h"
#include "fastresampler_wav.h"
#include "fastresampler_helpers.h"
#include "fastresampler_log.h"

#define RESAMPLE_DEFAULT_HOLD_SECONDS        2.0f
#define RESAMPLE_DEFAULT_GAP_SECONDS         2.0f
#define RESAMPLE_DEFAULT_PREROLL_GAP_SECONDS 1.0f
#define RESAMPLE_DEFAULT_STEP                2
// The highest rate common plugins render without complaint; 192 kHz is on the list.
#define RESAMPLE_DEFAULT_SAMPLE_RATE         96000
// Fast enough to be worth it, slow enough for a disk-streaming sampler to keep up.
#define RESAMPLE_DEFAULT_RENDER_SPEED        4
#define RESAMPLE_DEFAULT_OUTPUT_DIR_NAME     "samples"
#define RESAMPLE_FULL_FILE_SUFFIX            "_full.wav"

void resample_config_defaults(ResampleConfig* cfg) {
    memset(cfg, 0, sizeof(*cfg));
    fr_strcpy(cfg->low_note, sizeof(cfg->low_note), "C3");
    fr_strcpy(cfg->high_note, sizeof(cfg->high_note), "C4");
    cfg->step = RESAMPLE_DEFAULT_STEP;
    fr_strcpy(cfg->velocities, sizeof(cfg->velocities), "40, 60");
    cfg->round_robins = 1;
    cfg->preroll = false;
    cfg->preroll_gap_seconds = RESAMPLE_DEFAULT_PREROLL_GAP_SECONDS;
    cfg->hold_seconds = RESAMPLE_DEFAULT_HOLD_SECONDS;
    cfg->gap_seconds = RESAMPLE_DEFAULT_GAP_SECONDS;
    cfg->sample_rate = RESAMPLE_DEFAULT_SAMPLE_RATE;
    cfg->render_speed = RESAMPLE_DEFAULT_RENDER_SPEED;
    char dir[FR_MAX_PATH];
    plat_module_dir(dir, sizeof(dir));
    plat_path_join(cfg->output_dir, sizeof(cfg->output_dir), dir, RESAMPLE_DEFAULT_OUTPUT_DIR_NAME);
}

const char* resample_step_label(int step) {
    static const char* labels[RESAMPLE_MAX_STEP] = {
        "Every semitone", "Every wholetone", "Every minor third", "Every major third",
        "Every fourth", "Every tritone", "Every fifth", "Every minor sixth",
        "Every major sixth", "Every minor seventh", "Every major seventh", "Every octave"
    };
    if (step < 1 || step > RESAMPLE_MAX_STEP) return "?";
    return labels[step - 1];
}

// A naming field as it goes into a file name: the characters Windows refuses
// and the underscore, which separates the fields, become '-'; outer spaces
// go. False when nothing is left.
static bool resample_sanitize_name(char* dst, size_t dst_size, const char* src) {
    size_t o = 0;
    const char* s = src;
    while (*s == ' ' || *s == '\t') ++s;
    for (; *s && o + 1 < dst_size; ++s) {
        char c = *s;
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|' || c == '_' || c == '\t') c = '-';
        dst[o++] = c;
    }
    while (o > 0 && dst[o - 1] == ' ') --o;
    dst[o] = '\0';
    return o > 0;
}

bool resample_prefix(const ResampleConfig* cfg, char* out, size_t out_len, char* err, size_t err_len) {
    char mic[RESAMPLE_NAME_LEN], instrument[RESAMPLE_NAME_LEN], articulation[RESAMPLE_NAME_LEN];
    if (!resample_sanitize_name(instrument, sizeof(instrument), cfg->instrument)) {
        fr_strcpy(err, err_len, "Fill in the Instrument field");
        return false;
    }
    if (!resample_sanitize_name(articulation, sizeof(articulation), cfg->articulation)) {
        fr_strcpy(err, err_len, "Fill in the Articulation field");
        return false;
    }
    if (!resample_sanitize_name(mic, sizeof(mic), cfg->microphone)) {
        fr_strcpy(err, err_len, "Fill in the Microphone field");
        return false;
    }
    snprintf(out, out_len, "%s_%s_%s", mic, instrument, articulation);
    return true;
}

void resample_segment_file_name(const ResampleConfig* cfg, const ResampleSegment* seg,
                                char* out, size_t out_len) {
    char prefix[RESAMPLE_FILE_NAME_LEN];
    char err[8];
    if (!resample_prefix(cfg, prefix, sizeof(prefix), err, sizeof(err))) fr_strcpy(prefix, sizeof(prefix), "sample");
    char note[8];
    midi_note_name(seg->note, note, sizeof(note));
    snprintf(out, out_len, "%s_%s_%d_RR%d.wav", prefix, note, (int)seg->velocity, (int)seg->round_robin);
}

// "40, 60 100" to a sorted list of distinct velocities in 1..127.
static int resample_parse_velocities(const char* text, int* out, int max_count, char* err, size_t err_len) {
    int count = 0;
    const char* s = text;
    while (*s) {
        while (*s == ' ' || *s == ',' || *s == ';' || *s == '\t') ++s;
        if (!*s) break;
        if (*s < '0' || *s > '9') {
            snprintf(err, err_len, "Velocities: '%c' is not a number", *s);
            return 0;
        }
        int v = 0;
        while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
        if (v < 1 || v > MIDI_VALUE_MAX) {
            snprintf(err, err_len, "Velocities: %d is outside 1..127", v);
            return 0;
        }
        bool seen = false;
        for (int i = 0; i < count; ++i) seen = seen || out[i] == v;
        if (seen) continue;
        if (count >= max_count) {
            snprintf(err, err_len, "Velocities: at most %d", max_count);
            return 0;
        }
        out[count++] = v;
    }
    if (count == 0) {
        fr_strcpy(err, err_len, "Velocities: give at least one, like \"40, 60\"");
        return 0;
    }
    // Ascending, so the file goes soft to loud within each note.
    for (int i = 1; i < count; ++i) {
        int v = out[i];
        int j = i;
        while (j > 0 && out[j - 1] > v) {
            out[j] = out[j - 1];
            --j;
        }
        out[j] = v;
    }
    return count;
}

static void resample_add(ResampleScript* script, int64_t frame, MidiEvent midi) {
    if (script->count >= RESAMPLE_MAX_EVENTS) return;
    script->events[script->count].frame = frame;
    script->events[script->count].midi = midi;
    script->count++;
}

bool resample_build(ResampleScript* script, const ResampleConfig* cfg, double sample_rate,
                    char* err, size_t err_len) {
    err[0] = '\0';
    script->built = false;
    script->count = 0;
    script->segment_count = 0;

    char prefix[RESAMPLE_FILE_NAME_LEN];
    if (!resample_prefix(cfg, prefix, sizeof(prefix), err, err_len)) return false;

    int low = midi_note_parse(cfg->low_note);
    int high = midi_note_parse(cfg->high_note);
    if (low < 0) {
        snprintf(err, err_len, "Low note '%s' is not a note name (C3 is middle C)", cfg->low_note);
        return false;
    }
    if (high < 0) {
        snprintf(err, err_len, "High note '%s' is not a note name (C3 is middle C)", cfg->high_note);
        return false;
    }
    if (high < low) {
        int t = low;
        low = high;
        high = t;
    }
    if (cfg->step < 1 || cfg->step > RESAMPLE_MAX_STEP) {
        fr_strcpy(err, err_len, "Sample every: pick an interval");
        return false;
    }
    int velocities[RESAMPLE_MAX_VELOCITIES];
    int velocity_count = resample_parse_velocities(cfg->velocities, velocities, RESAMPLE_MAX_VELOCITIES, err, err_len);
    if (velocity_count == 0) return false;
    if (cfg->round_robins < 1 || cfg->round_robins > RESAMPLE_MAX_ROUND_ROBINS) {
        snprintf(err, err_len, "Round robins: 1 to %d", RESAMPLE_MAX_ROUND_ROBINS);
        return false;
    }
    if (cfg->preroll && cfg->preroll_gap_seconds <= RESAMPLE_PREROLL_HOLD_SECONDS) {
        snprintf(err, err_len, "Pre-roll gap must be longer than the %.1f s a pre-roll note is held",
                 RESAMPLE_PREROLL_HOLD_SECONDS);
        return false;
    }
    if (cfg->hold_seconds <= 0.0f) {
        fr_strcpy(err, err_len, "Hold must be longer than 0 seconds");
        return false;
    }
    if (cfg->gap_seconds < 0.0f) {
        fr_strcpy(err, err_len, "Gap cannot be negative");
        return false;
    }
    if (sample_rate <= 0.0) {
        fr_strcpy(err, err_len, "No sample rate: open an audio device first");
        return false;
    }
    if (cfg->output_dir[0] == '\0') {
        fr_strcpy(err, err_len, "Give an output folder");
        return false;
    }

    int note_count = (high - low) / cfg->step + 1;
    int segment_total = note_count * velocity_count * cfg->round_robins;
    if (segment_total > RESAMPLE_MAX_SEGMENTS) {
        snprintf(err, err_len, "%d samples is more than the %d one run can hold; narrow the range, velocities or round robins",
                 segment_total, RESAMPLE_MAX_SEGMENTS);
        return false;
    }

    int64_t hold_frames = (int64_t)((double)cfg->hold_seconds * sample_rate + 0.5);
    int64_t gap_frames = (int64_t)((double)cfg->gap_seconds * sample_rate + 0.5);
    int64_t preroll_hold_frames = (int64_t)((double)RESAMPLE_PREROLL_HOLD_SECONDS * sample_rate + 0.5);
    int64_t preroll_gap_frames = (int64_t)((double)cfg->preroll_gap_seconds * sample_rate + 0.5);
    int64_t t = 0;
    for (int note = low; note <= high; note += cfg->step) {
        for (int v = 0; v < velocity_count; ++v) {
            for (int rr = 1; rr <= cfg->round_robins; ++rr) {
                if (cfg->preroll) {
                    // Advance the sampler's counter to variation rr with short
                    // notes that each ring out before the next.
                    for (int i = 1; i < rr; ++i) {
                        resample_add(script, t, midi_make(MIDI_STATUS_NOTE_ON, 0, note, velocities[v]));
                        resample_add(script, t + preroll_hold_frames, midi_make(MIDI_STATUS_NOTE_OFF, 0, note, 0));
                        t += preroll_gap_frames;
                    }
                }
                ResampleSegment* seg = &script->segments[script->segment_count++];
                seg->start_frame = t;
                seg->note = (uint8_t)note;
                seg->velocity = (uint8_t)velocities[v];
                seg->round_robin = (uint8_t)rr;
                resample_add(script, t, midi_make(MIDI_STATUS_NOTE_ON, 0, note, velocities[v]));
                resample_add(script, t + hold_frames, midi_make(MIDI_STATUS_NOTE_OFF, 0, note, 0));
                t += hold_frames + gap_frames;
                seg->end_frame = t;
            }
        }
    }
    // Whatever still rings is cut at the end of the file.
    resample_add(script, t, midi_make(MIDI_STATUS_CONTROL_CHANGE, 0, MIDI_CC_ALL_NOTES_OFF, 0));
    resample_add(script, t, midi_make(MIDI_STATUS_CONTROL_CHANGE, 0, MIDI_CC_ALL_SOUND_OFF, 0));

    script->total_frames = t;
    script->sample_rate = sample_rate;
    script->note_count = note_count;
    script->velocity_count = velocity_count;
    script->round_robin_count = cfg->round_robins;
    script->built = true;
    fr_log("resample: %d notes x %d velocities x %d round robins = %d samples, %d events, %.1f s at %.0f Hz, pre-roll %s",
           note_count, velocity_count, cfg->round_robins, script->segment_count, script->count,
           (double)t / sample_rate, sample_rate, cfg->preroll ? "on" : "off");
    return true;
}

// --- The worker ------------------------------------------------------------------

// Phase one: the plugin over the whole script into the full WAV. False when
// writing failed; a cancel ends early and returns true.
static bool resample_render(ResampleJob* job, int64_t* frames_done) {
    const ResampleScript* script = job->script;
    WavWriter wav;
    if (!wav_writer_open(&wav, job->full_path, FR_OUTPUT_CHANNELS, (int)script->sample_rate)) {
        snprintf(job->result, sizeof(job->result), "Could not create %s", job->full_path);
        return false;
    }

    static float buffers[FR_OUTPUT_CHANNELS][FR_MAX_BLOCK_SIZE];
    float* ptrs[FR_OUTPUT_CHANNELS];
    for (int c = 0; c < FR_OUTPUT_CHANNELS; ++c) ptrs[c] = buffers[c];
    static MidiEvent block_events[RESAMPLE_MAX_EVENTS];
    static int block_offsets[RESAMPLE_MAX_EVENTS];

    int cursor = 0;
    int64_t pos = 0;
    float peak = 0.0f;
    bool write_ok = true;
    uint64_t pace_start_ms = plat_ms_now();
    while (pos < script->total_frames && write_ok && !job->cancel.load()) {
        // Hold the render to the chosen multiple of realtime: the wall clock
        // must have reached the audio position over the speed before the
        // next block. A sleep that overshoots is caught up by the blocks that
        // follow, so the average holds and no block ever runs early.
        if (job->speed > 0) {
            double target_ms = (double)pos / script->sample_rate / (double)job->speed * 1000.0;
            while ((double)(plat_ms_now() - pace_start_ms) < target_ms && !job->cancel.load()) plat_sleep_ms(1);
        }
        int64_t remaining = script->total_frames - pos;
        int frames = remaining < job->block_size ? (int)remaining : job->block_size;
        for (int c = 0; c < FR_OUTPUT_CHANNELS; ++c) memset(buffers[c], 0, (size_t)frames * sizeof(float));

        // The events that land in this block, at their offset inside it.
        int n = 0;
        while (cursor < script->count && script->events[cursor].frame < pos + frames) {
            block_events[n] = script->events[cursor].midi;
            block_offsets[n] = (int)(script->events[cursor].frame - pos);
            if (block_offsets[n] < 0) block_offsets[n] = 0;
            n++;
            cursor++;
        }
        host_plugin_process_events(job->plugin, block_events, block_offsets, n, ptrs, FR_OUTPUT_CHANNELS, frames);

        for (int c = 0; c < FR_OUTPUT_CHANNELS; ++c) {
            for (int i = 0; i < frames; ++i) {
                float a = fabsf(buffers[c][i]);
                if (a > peak) peak = a;
            }
        }
        write_ok = wav_writer_write(&wav, ptrs, FR_OUTPUT_CHANNELS, frames);
        pos += frames;
        job->progress_frames.store(pos);
    }
    bool closed = wav_writer_close(&wav);
    job->peak = peak;
    *frames_done = pos;
    if (!write_ok || !closed) {
        snprintf(job->result, sizeof(job->result), "Writing %s failed after %.1f s of audio",
                 job->full_path, (double)pos / script->sample_rate);
        return false;
    }
    return true;
}

// Phase two: one file per segment, cut from the full WAV.
static bool resample_split(ResampleJob* job) {
    const ResampleScript* script = job->script;
    job->phase.store(RESAMPLE_PHASE_SPLIT);
    for (int i = 0; i < script->segment_count; ++i) {
        if (job->cancel.load()) return true;
        const ResampleSegment* seg = &script->segments[i];
        char note[8];
        midi_note_name(seg->note, note, sizeof(note));
        char name[RESAMPLE_FILE_NAME_LEN];
        snprintf(name, sizeof(name), "%s_%s_%d_RR%d.wav", job->prefix, note, (int)seg->velocity, (int)seg->round_robin);
        char path[FR_MAX_PATH];
        plat_path_join(path, sizeof(path), job->out_dir, name);
        if (!wav_copy_segment(job->full_path, seg->start_frame, seg->end_frame - seg->start_frame, path)) {
            snprintf(job->result, sizeof(job->result), "Could not write %s", name);
            return false;
        }
        job->files_written++;
        job->split_done.store(job->files_written);
    }
    return true;
}

static void resample_thread_main(void* user) {
    ResampleJob* job = (ResampleJob*)user;
    const ResampleScript* script = job->script;
    uint64_t started_ms = plat_ms_now();

    int64_t frames_done = 0;
    bool ok = resample_render(job, &frames_done);
    if (ok && !job->cancel.load()) ok = resample_split(job);

    job->elapsed_seconds = (double)(plat_ms_now() - started_ms) / 1000.0;
    double audio_seconds = (double)frames_done / script->sample_rate;
    double speed = job->elapsed_seconds > 0.0 ? audio_seconds / job->elapsed_seconds : 0.0;
    float peak_db = job->peak > 0.000001f ? 20.0f * log10f(job->peak) : -120.0f;
    if (!ok) {
        job->result_is_error = true;   // result already says what failed
    } else if (job->cancel.load()) {
        snprintf(job->result, sizeof(job->result), "Cancelled after %.1f s of audio and %d sample files.",
                 audio_seconds, job->files_written);
        job->result_is_error = false;
    } else {
        snprintf(job->result, sizeof(job->result),
                 "Wrote %d samples to %s: %.1f s of audio in %.1f s (%.1fx realtime), peak %.1f dBFS.",
                 job->files_written, job->out_dir, audio_seconds, job->elapsed_seconds, speed, peak_db);
        job->result_is_error = false;
    }
    fr_log("resample: %s", job->result);
    job->finished.store(true);
}

bool resample_start(ResampleJob* job, HostPlugin* plugin, const ResampleScript* script,
                    int block_size, const ResampleConfig* cfg) {
    if (job->running.load()) return false;
    if (!plugin || !script || !script->built || block_size <= 0) return false;
    char err[FR_STATUS_LEN];
    if (!resample_prefix(cfg, job->prefix, sizeof(job->prefix), err, sizeof(err))) return false;

    job->plugin = plugin;
    job->script = script;
    job->block_size = block_size > FR_MAX_BLOCK_SIZE ? FR_MAX_BLOCK_SIZE : block_size;
    job->speed = cfg->render_speed < 0 ? 0 : cfg->render_speed;
    fr_strcpy(job->out_dir, sizeof(job->out_dir), cfg->output_dir);
    char full_name[RESAMPLE_FILE_NAME_LEN];
    snprintf(full_name, sizeof(full_name), "%s%s", job->prefix, RESAMPLE_FULL_FILE_SUFFIX);
    plat_path_join(job->full_path, sizeof(job->full_path), job->out_dir, full_name);

    job->result[0] = '\0';
    job->result_is_error = false;
    job->peak = 0.0f;
    job->elapsed_seconds = 0.0;
    job->files_written = 0;
    job->cancel.store(false);
    job->finished.store(false);
    job->phase.store(RESAMPLE_PHASE_RENDER);
    job->progress_frames.store(0);
    job->split_done.store(0);
    job->running.store(true);
    if (!plat_thread_create(&job->thread, resample_thread_main, job)) {
        job->running.store(false);
        return false;
    }
    fr_log("resample: rendering %d events, %d samples, to %s at %dx realtime (0 = unlimited)",
           script->count, script->segment_count, job->out_dir, job->speed);
    return true;
}

void resample_cancel(ResampleJob* job) {
    if (job->running.load()) job->cancel.store(true);
}

bool resample_collect(ResampleJob* job) {
    if (!job->running.load() || !job->finished.load()) return false;
    plat_thread_join(&job->thread);
    job->running.store(false);
    return true;
}
