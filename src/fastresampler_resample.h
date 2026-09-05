#pragma once
// Resampling: play every note in a range at a set of velocities through the
// plugin and capture the result to a WAV file, faster than real time.
//
// Two steps, like the tab shows them. Build turns the configuration into a
// script: a list of MIDI events at sample positions, notes outer, velocities
// inner, each note held then left to ring out. Render runs the plugin over
// the script on a worker thread and writes what comes out.

#include <stdint.h>
#include <atomic>
#include "fastresampler_constants.h"
#include "fastresampler_midi.h"
#include "platform/fastresampler_platform.h"

#define RESAMPLE_MAX_VELOCITIES 16
#define RESAMPLE_VELOCITY_TEXT_LEN 96
#define RESAMPLE_NOTE_TEXT_LEN 8
// Every note at every velocity is an on and an off, plus the closing messages.
#define RESAMPLE_MAX_EVENTS (MIDI_NOTE_COUNT * RESAMPLE_MAX_VELOCITIES * 2 + 8)

// What the Resample tab edits. Lives in Settings so it survives a restart.
struct ResampleConfig {
    char  low_note[RESAMPLE_NOTE_TEXT_LEN];        // "C3" style, C3 = middle C
    char  high_note[RESAMPLE_NOTE_TEXT_LEN];
    bool  white_keys_only;                          // a C to C range becomes a C major scale
    char  velocities[RESAMPLE_VELOCITY_TEXT_LEN];   // "40, 60"
    float hold_seconds;                             // note on to note off
    float gap_seconds;                              // note off to the next note on
    char  output_path[FR_MAX_PATH];
};

struct ResampleEvent {
    int64_t   frame;
    MidiEvent midi;
};

struct ResampleScript {
    ResampleEvent events[RESAMPLE_MAX_EVENTS];
    int      count;
    int64_t  total_frames;
    double   sample_rate;
    int      note_count;
    int      velocity_count;
    bool     built;
};

// One render in flight. The worker thread writes progress and the result; the
// UI thread reads progress, and collects the result once finished is set.
struct ResampleJob {
    std::atomic<bool>    running;
    std::atomic<bool>    cancel;
    std::atomic<bool>    finished;
    std::atomic<int64_t> progress_frames;
    PlatThread           thread;

    struct HostPlugin*     plugin;
    const ResampleScript*  script;
    int                    block_size;
    char                   path[FR_MAX_PATH];

    char   result[FR_STATUS_LEN];
    bool   result_is_error;
    float  peak;
    double elapsed_seconds;
};

void resample_config_defaults(ResampleConfig* cfg);

// Parses the configuration and fills the script for the given sample rate.
bool resample_build(ResampleScript* script, const ResampleConfig* cfg, double sample_rate,
                    char* err, size_t err_len);

// Starts the worker. The caller has taken the plugin away from the audio
// thread first and gives it back after resample_collect().
bool resample_start(ResampleJob* job, struct HostPlugin* plugin, const ResampleScript* script,
                    int block_size, const char* path);
void resample_cancel(ResampleJob* job);

// Joins the worker once finished is set. Returns true when a result is ready.
bool resample_collect(ResampleJob* job);
