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
// The note events of a full run always fit; the controller curves add
// events on top and the build fails, with a message, when they overflow.
#define RESAMPLE_MAX_EVENTS         262144
#define RESAMPLE_VELOCITY_TEXT_LEN  96
#define RESAMPLE_NOTE_TEXT_LEN      8
#define RESAMPLE_NAME_LEN           64
#define RESAMPLE_FILE_NAME_LEN      (3 * RESAMPLE_NAME_LEN + 32)

// How long a pre-roll note is held before it is released to ring out.
#define RESAMPLE_PREROLL_HOLD_SECONDS 0.2f

// --- Controller curves ----------------------------------------------------------
// A controller drawn over the time of one sample: the pre-roll notes, the
// held note, and the gap it rings out in. The values cover that whole span,
// 0 to 1 for controller value 0 to 127, and stretch with the timing. The
// bezier nodes are the editor's; the values are what plays. While the script
// plays, each curve is read every tick and a controller event goes out when
// its 7-bit value differs from the last one sent.
#define RESAMPLE_MAX_CC_CURVES      8
#define RESAMPLE_CC_CURVE_POINTS    128
#define RESAMPLE_CC_MAX_NODES       16
#define RESAMPLE_CC_TICK_SECONDS    0.005f

struct ResampleCcCurve {
    int   controller;                            // 0..127
    float values[RESAMPLE_CC_CURVE_POINTS];
    bool  is_bezier;
    int   node_count;
    float node_x[RESAMPLE_CC_MAX_NODES];
    float node_y[RESAMPLE_CC_MAX_NODES];
    float curvature[RESAMPLE_CC_MAX_NODES];      // the control point of the segment after node i
    int   constant;                              // the Constant button's slider, 0..127
};

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
    int   sample_rate;                              // the render's rate; 0 = the audio device's
    int   render_speed;                             // realtime multiple the render is held to; 0 = unlimited
    char  output_dir[FR_MAX_PATH];                  // the full WAV and the sample files
    bool  delete_full_wav;                          // remove the full WAV once the bank is built from it
    char  export_dir[FR_MAX_PATH];                  // the .fsi and .fsb
    int   preload_frames;                           // frames of each sample the bank keeps in RAM; 0 = whole samples

    ResampleCcCurve cc_curves[RESAMPLE_MAX_CC_CURVES];
    int   cc_curve_count;
};

// The export rates the tab offers, besides the device's own.
#define RESAMPLE_RATE_OPTION_COUNT 6
static const int g_resample_rate_options[RESAMPLE_RATE_OPTION_COUNT] = { 44100, 48000, 88200, 96000, 176400, 192000 };

// The preload sizes the tab offers for the bank, in frames of each sample;
// 0 keeps every sample whole in RAM, the rest stream from disk past that.
#define RESAMPLE_PRELOAD_OPTION_COUNT 8
static const int g_resample_preload_options[RESAMPLE_PRELOAD_OPTION_COUNT] = { 0, 1024, 2048, 4096, 8192, 16384, 32768, 65536 };

// The bytes of one WAV header the writer makes.
#define RESAMPLE_WAV_HEADER_BYTES 44

// The render speeds the tab offers. A sampler that streams from disk needs
// wall time to read its sample tails: rendered too fast, it delivers only
// what it had preloaded and the samples come out cut short. 0 is unlimited.
#define RESAMPLE_SPEED_OPTION_COUNT 6
static const int g_resample_speed_options[RESAMPLE_SPEED_OPTION_COUNT] = { 1, 2, 4, 8, 16, 0 };

struct ResampleEvent {
    int64_t   frame;
    MidiEvent midi;
    uint32_t  order;     // the position it was added at; keeps the sort stable
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
    int                    speed;                   // realtime multiple, 0 = unlimited
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

// A value read from a file against the list the tab offers. The rate falls
// back to 0, the device's own; the other two to what the caller gives, which
// is the default that was already there.
int resample_valid_sample_rate(int value);
int resample_valid_render_speed(int value, int fallback);
int resample_valid_preload_frames(int value, int fallback);

// The dropdown label for a step in semitones, "Every wholetone" for 2.
const char* resample_step_label(int step);

// The dropdown label for a preload size: "RAM" for 0, else "8192 frames".
void resample_preload_label(int preload_frames, char* out, size_t out_len);

// Bytes a render writes to the output folder: the full WAV and one file per
// sample, all 32 bit float with a header each.
uint64_t resample_required_bytes(const ResampleScript* script);

// --- Controller curves ---------------------------------------------------------

// The time one curve spans for the configuration, in seconds: the longest
// pre-roll (that of the last round robin, 0 without pre-roll), the hold and
// the gap. Each part comes back on its own when a pointer is given.
float resample_cc_span(const ResampleConfig* cfg, float* preroll, float* hold, float* gap);

// A new curve at the end of the list: the lowest controller not in use yet,
// flat at 127. False when the list is full.
bool resample_cc_add(ResampleConfig* cfg);
void resample_cc_remove(ResampleConfig* cfg, int index);

// A flat curve at value 0..1: the values and the two bezier nodes.
void resample_cc_set_constant(ResampleCcCurve* c, float value);

// The values from the bezier nodes; the nodes fitted to the values.
void resample_cc_from_bezier(ResampleCcCurve* c);
void resample_cc_fit_bezier(ResampleCcCurve* c);
void resample_cc_smooth(ResampleCcCurve* c);

// The value at a position 0..1 along the span, linear between the points.
float resample_cc_value_at(const ResampleCcCurve* c, float x);

// Parses the configuration and fills the script for the given sample rate.
bool resample_build(ResampleScript* script, const ResampleConfig* cfg, double sample_rate,
                    char* err, size_t err_len);

// The file name a segment gets, from the naming fields of the configuration.
void resample_segment_file_name(const ResampleConfig* cfg, const ResampleSegment* seg,
                                char* out, size_t out_len);

// "Microphone_Instrument_Articulation" from the naming fields, cleaned for a
// file name. False, with the reason in err, when a field is empty.
bool resample_prefix(const ResampleConfig* cfg, char* out, size_t out_len, char* err, size_t err_len);

// Starts the worker. The caller has taken the plugin away from the audio
// thread first and gives it back after resample_collect(). The output folder
// exists by then.
bool resample_start(ResampleJob* job, struct HostPlugin* plugin, const ResampleScript* script,
                    int block_size, const ResampleConfig* cfg);
void resample_cancel(ResampleJob* job);

// Joins the worker once finished is set. Returns true when a result is ready.
bool resample_collect(ResampleJob* job);
