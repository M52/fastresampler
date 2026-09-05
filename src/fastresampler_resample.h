#pragma once
// Resampling: play the notes of a range at a set of velocities, a number of
// times each, through the plugin, capture the result faster than real time,
// then cut it into one WAV per sample.
//
// Build turns the configuration into a script: MIDI events at sample
// positions plus a segment table that says where each wanted sample starts
// and ends in the file. Render runs the plugin over the script on a worker
// thread into one full WAV, then copies each segment out to its own file
// named <Microphone>_<Instrument>_<Articulation>_<Note>_<Velocity>_RR<n>.wav.
//
// Pre-roll: a sampler that resets its round robin counter after silence
// plays its first variation for every note we send. To capture variation n,
// the script plays n-1 short notes of the same pitch and velocity first,
// spaced by the pre-roll gap so each rings out, then the real note. The
// segment starts at the real note, so the split drops the pre-roll.

#include <stdint.h>
#include <atomic>
#include "fastresampler_constants.h"
#include "fastresampler_midi.h"
#include "platform/fastresampler_platform.h"

#define RESAMPLE_MAX_VELOCITIES     16
#define RESAMPLE_MAX_ROUND_ROBINS   8
#define RESAMPLE_MAX_STEP           SEMITONES_PER_OCTAVE
#define RESAMPLE_MAX_SEGMENTS       4096
#define RESAMPLE_EVENTS_PER_SEGMENT (2 + 2 * (RESAMPLE_MAX_ROUND_ROBINS - 1))
#define RESAMPLE_MAX_EVENTS         (RESAMPLE_MAX_SEGMENTS * RESAMPLE_EVENTS_PER_SEGMENT + 8)
#define RESAMPLE_VELOCITY_TEXT_LEN  96
#define RESAMPLE_NOTE_TEXT_LEN      8
#define RESAMPLE_NAME_LEN           64
#define RESAMPLE_FILE_NAME_LEN      (3 * RESAMPLE_NAME_LEN + 32)

// How long a pre-roll note is held before it is released to ring out.
#define RESAMPLE_PREROLL_HOLD_SECONDS 0.2f

// What the Resample tab edits. Lives in Settings so it survives a restart.
struct ResampleConfig {
    char  instrument[RESAMPLE_NAME_LEN];
    char  articulation[RESAMPLE_NAME_LEN];
    char  microphone[RESAMPLE_NAME_LEN];

    char  low_note[RESAMPLE_NOTE_TEXT_LEN];        // "C3" style, C3 = middle C
    char  high_note[RESAMPLE_NOTE_TEXT_LEN];
    int   step;                                     // semitones between sampled notes, 1..12
    char  velocities[RESAMPLE_VELOCITY_TEXT_LEN];   // "40, 60"
    int   round_robins;                             // samples per note and velocity, 1..8
    bool  preroll;                                  // play n-1 notes before variation n
    float preroll_gap_seconds;                      // pre-roll note-on to the next note-on

    float hold_seconds;                             // note on to note off
    float gap_seconds;                              // note off to the segment's end
    char  output_dir[FR_MAX_PATH];
};

struct ResampleEvent {
    int64_t   frame;
    MidiEvent midi;
};

// One wanted sample: its span in the full file and what to call it.
struct ResampleSegment {
    int64_t start_frame;     // the real note-on
    int64_t end_frame;       // hold + gap later
    uint8_t note;
    uint8_t velocity;
    uint8_t round_robin;     // 1-based
};

struct ResampleScript {
    ResampleEvent   events[RESAMPLE_MAX_EVENTS];
    int             count;
    ResampleSegment segments[RESAMPLE_MAX_SEGMENTS];
    int             segment_count;
    int64_t         total_frames;
    double          sample_rate;
    int             note_count;
    int             velocity_count;
    int             round_robin_count;
    bool            built;
};

enum ResamplePhase {
    RESAMPLE_PHASE_RENDER = 0,
    RESAMPLE_PHASE_SPLIT  = 1
};

// One render in flight. The worker thread writes progress and the result; the
// UI thread reads progress, and collects the result once finished is set.
struct ResampleJob {
    std::atomic<bool>    running;
    std::atomic<bool>    cancel;
    std::atomic<bool>    finished;
    std::atomic<int>     phase;              // ResamplePhase
    std::atomic<int64_t> progress_frames;    // render phase
    std::atomic<int>     split_done;         // split phase, segments written
    PlatThread           thread;

    struct HostPlugin*     plugin;
    const ResampleScript*  script;
    int                    block_size;
    char                   out_dir[FR_MAX_PATH];
    char                   full_path[FR_MAX_PATH];      // the one long WAV
    char                   prefix[RESAMPLE_FILE_NAME_LEN]; // Microphone_Instrument_Articulation

    char   result[FR_STATUS_LEN];
    bool   result_is_error;
    float  peak;
    double elapsed_seconds;
    int    files_written;
};

void resample_config_defaults(ResampleConfig* cfg);

// The dropdown label for a step in semitones, "Every wholetone" for 2.
const char* resample_step_label(int step);

// Parses the configuration and fills the script for the given sample rate.
bool resample_build(ResampleScript* script, const ResampleConfig* cfg, double sample_rate,
                    char* err, size_t err_len);

// The file name a segment gets, from the naming fields of the configuration.
void resample_segment_file_name(const ResampleConfig* cfg, const ResampleSegment* seg,
                                char* out, size_t out_len);

// Starts the worker. The caller has taken the plugin away from the audio
// thread first and gives it back after resample_collect(). The output folder
// exists by then.
bool resample_start(ResampleJob* job, struct HostPlugin* plugin, const ResampleScript* script,
                    int block_size, const ResampleConfig* cfg);
void resample_cancel(ResampleJob* job);

// Joins the worker once finished is set. Returns true when a result is ready.
bool resample_collect(ResampleJob* job);
