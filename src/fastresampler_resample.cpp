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

#define RESAMPLE_DEFAULT_HOLD_SECONDS 2.0f
#define RESAMPLE_DEFAULT_GAP_SECONDS  2.0f
#define RESAMPLE_DEFAULT_OUTPUT_NAME  "resample.wav"

void resample_config_defaults(ResampleConfig* cfg) {
    memset(cfg, 0, sizeof(*cfg));
    fr_strcpy(cfg->low_note, sizeof(cfg->low_note), "C3");
    fr_strcpy(cfg->high_note, sizeof(cfg->high_note), "C4");
    cfg->white_keys_only = true;
    fr_strcpy(cfg->velocities, sizeof(cfg->velocities), "40, 60");
    cfg->hold_seconds = RESAMPLE_DEFAULT_HOLD_SECONDS;
    cfg->gap_seconds = RESAMPLE_DEFAULT_GAP_SECONDS;
    char dir[FR_MAX_PATH];
    plat_module_dir(dir, sizeof(dir));
    plat_path_join(cfg->output_path, sizeof(cfg->output_path), dir, RESAMPLE_DEFAULT_OUTPUT_NAME);
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
    int velocities[RESAMPLE_MAX_VELOCITIES];
    int velocity_count = resample_parse_velocities(cfg->velocities, velocities, RESAMPLE_MAX_VELOCITIES, err, err_len);
    if (velocity_count == 0) return false;
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

    int64_t hold_frames = (int64_t)((double)cfg->hold_seconds * sample_rate + 0.5);
    int64_t gap_frames = (int64_t)((double)cfg->gap_seconds * sample_rate + 0.5);
    int64_t t = 0;
    int note_count = 0;
    for (int note = low; note <= high; ++note) {
        if (cfg->white_keys_only && !midi_note_is_white(note)) continue;
        note_count++;
        for (int v = 0; v < velocity_count; ++v) {
            resample_add(script, t, midi_make(MIDI_STATUS_NOTE_ON, 0, note, velocities[v]));
            resample_add(script, t + hold_frames, midi_make(MIDI_STATUS_NOTE_OFF, 0, note, 0));
            t += hold_frames + gap_frames;
        }
    }
    if (note_count == 0) {
        fr_strcpy(err, err_len, "The range holds no notes");
        return false;
    }
    // Whatever still rings is cut at the end of the file.
    resample_add(script, t, midi_make(MIDI_STATUS_CONTROL_CHANGE, 0, MIDI_CC_ALL_NOTES_OFF, 0));
    resample_add(script, t, midi_make(MIDI_STATUS_CONTROL_CHANGE, 0, MIDI_CC_ALL_SOUND_OFF, 0));

    script->total_frames = t;
    script->sample_rate = sample_rate;
    script->note_count = note_count;
    script->velocity_count = velocity_count;
    script->built = true;
    fr_log("resample: %d notes x %d velocities, %d events, %.1f s at %.0f Hz",
           note_count, velocity_count, script->count, (double)t / sample_rate, sample_rate);
    return true;
}

// --- The worker ------------------------------------------------------------------

static void resample_thread_main(void* user) {
    ResampleJob* job = (ResampleJob*)user;
    const ResampleScript* script = job->script;

    WavWriter wav;
    if (!wav_writer_open(&wav, job->path, FR_OUTPUT_CHANNELS, (int)script->sample_rate)) {
        snprintf(job->result, sizeof(job->result), "Could not create %s", job->path);
        job->result_is_error = true;
        job->finished.store(true);
        return;
    }

    static float buffers[FR_OUTPUT_CHANNELS][FR_MAX_BLOCK_SIZE];
    float* ptrs[FR_OUTPUT_CHANNELS];
    for (int c = 0; c < FR_OUTPUT_CHANNELS; ++c) ptrs[c] = buffers[c];
    MidiEvent block_events[RESAMPLE_MAX_EVENTS];
    int block_offsets[RESAMPLE_MAX_EVENTS];

    uint64_t started_ms = plat_ms_now();
    int cursor = 0;
    int64_t pos = 0;
    float peak = 0.0f;
    bool write_ok = true;
    while (pos < script->total_frames && write_ok && !job->cancel.load()) {
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

    job->elapsed_seconds = (double)(plat_ms_now() - started_ms) / 1000.0;
    job->peak = peak;
    double audio_seconds = (double)pos / script->sample_rate;
    double speed = job->elapsed_seconds > 0.0 ? audio_seconds / job->elapsed_seconds : 0.0;
    float peak_db = peak > 0.000001f ? 20.0f * log10f(peak) : -120.0f;
    if (!write_ok || !closed) {
        snprintf(job->result, sizeof(job->result), "Writing %s failed after %.1f s of audio", job->path, audio_seconds);
        job->result_is_error = true;
    } else if (job->cancel.load()) {
        snprintf(job->result, sizeof(job->result), "Cancelled after %.1f s of audio; %s holds what was rendered.",
                 audio_seconds, plat_path_basename(job->path));
        job->result_is_error = false;
    } else {
        snprintf(job->result, sizeof(job->result), "Wrote %s: %.1f s of audio in %.1f s (%.1fx realtime), peak %.1f dBFS.",
                 plat_path_basename(job->path), audio_seconds, job->elapsed_seconds, speed, peak_db);
        job->result_is_error = false;
    }
    fr_log("resample: %s", job->result);
    job->finished.store(true);
}

bool resample_start(ResampleJob* job, HostPlugin* plugin, const ResampleScript* script,
                    int block_size, const char* path) {
    if (job->running.load()) return false;
    if (!plugin || !script || !script->built || block_size <= 0) return false;
    job->plugin = plugin;
    job->script = script;
    job->block_size = block_size > FR_MAX_BLOCK_SIZE ? FR_MAX_BLOCK_SIZE : block_size;
    fr_strcpy(job->path, sizeof(job->path), path);
    job->result[0] = '\0';
    job->result_is_error = false;
    job->peak = 0.0f;
    job->elapsed_seconds = 0.0;
    job->cancel.store(false);
    job->finished.store(false);
    job->progress_frames.store(0);
    job->running.store(true);
    if (!plat_thread_create(&job->thread, resample_thread_main, job)) {
        job->running.store(false);
        return false;
    }
    fr_log("resample: rendering %d events to %s", script->count, path);
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
