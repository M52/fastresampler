#pragma once

#include <stdint.h>
#include <atomic>
#include "fastresampler_constants.h"
#include "fastresampler_midi.h"
#include "platform/fastresampler_platform.h"

#define RESAMPLE_MAX_VELOCITIES     16
#define RESAMPLE_MAX_ROUND_ROBINS   8
#define RESAMPLE_MAX_STEP           SEMITONES_PER_OCTAVE
#define RESAMPLE_MAX_SEGMENTS       4096
#define RESAMPLE_MAX_EVENTS         262144
#define RESAMPLE_VELOCITY_TEXT_LEN  96
#define RESAMPLE_NOTE_TEXT_LEN      8
#define RESAMPLE_NAME_LEN           64
#define RESAMPLE_FILE_NAME_LEN      (3 * RESAMPLE_NAME_LEN + 32)
#define RESAMPLE_LAYER_SUFFIX_LEN   8

#define RESAMPLE_PREROLL_HOLD_SECONDS 0.2f

#define RESAMPLE_MAX_LEGATO_INTERVALS  12
#define RESAMPLE_MAX_LEGATO_VELOCITIES 8
#define RESAMPLE_INTERVAL_TEXT_LEN     48

enum ResampleSegmentKind {
    RESAMPLE_SEG_SUSTAIN    = 0,
    RESAMPLE_SEG_TRANSITION = 1
};

#define RESAMPLE_MAX_CC_CURVES      64
#define RESAMPLE_CC_CURVE_POINTS    128
#define RESAMPLE_CC_MAX_NODES       16
#define RESAMPLE_CC_TICK_SECONDS    0.005f

struct ResampleCcCurve {
    int   controller;
    float values[RESAMPLE_CC_CURVE_POINTS];
    bool  is_bezier;
    int   node_count;
    float node_x[RESAMPLE_CC_MAX_NODES];
    float node_y[RESAMPLE_CC_MAX_NODES];
    float curvature[RESAMPLE_CC_MAX_NODES];
    int   constant;
    int   layer;
};

struct ResampleBus {
    char name[RESAMPLE_NAME_LEN];
    int  channel;
};

struct ResampleConfig {
    char  instrument[RESAMPLE_NAME_LEN];
    char  articulation[RESAMPLE_NAME_LEN];

    ResampleBus busses[FR_MAX_OUTPUT_BUSSES];
    int   bus_count;
    int   monitor_bus;

    char  root_dir[FR_MAX_PATH];

    char  low_note[RESAMPLE_NOTE_TEXT_LEN];
    char  high_note[RESAMPLE_NOTE_TEXT_LEN];
    int   step;
    char  velocities[RESAMPLE_VELOCITY_TEXT_LEN];
    int   round_robins;
    bool  dynamic_layers;
    bool  preroll;
    float preroll_gap_seconds;

    float hold_seconds;
    float gap_seconds;

    bool  legato;
    int   legato_step;
    char  legato_intervals[RESAMPLE_INTERVAL_TEXT_LEN];
    char  legato_velocities[RESAMPLE_VELOCITY_TEXT_LEN];
    char  legato_arrivals[RESAMPLE_VELOCITY_TEXT_LEN];
    bool  legato_detect_arrival;
    float legato_source_hold_seconds;
    float legato_overlap_seconds;
    float legato_hold_seconds;
    float legato_margin_seconds;

    int   sample_rate;
    int   render_speed;
    char  output_dir[FR_MAX_PATH];
    bool  output_dir_override;
    bool  split_samples;
    bool  export_rzc;
    bool  delete_full_wav;
    char  export_dir[FR_MAX_PATH];
    bool  export_dir_override;
    int   preload_frames;
    int   bit_depth;

    ResampleCcCurve cc_curves[RESAMPLE_MAX_CC_CURVES];
    int   cc_curve_count;
};

#define RESAMPLE_RATE_OPTION_COUNT 6
static const int g_resample_rate_options[RESAMPLE_RATE_OPTION_COUNT] = { 44100, 48000, 88200, 96000, 176400, 192000 };

#define RESAMPLE_PRELOAD_OPTION_COUNT 8
static const int g_resample_preload_options[RESAMPLE_PRELOAD_OPTION_COUNT] = { 0, 1024, 2048, 4096, 8192, 16384, 32768, 65536 };

#define RESAMPLE_BIT_DEPTH_OPTION_COUNT 2
static const int g_resample_bit_depth_options[RESAMPLE_BIT_DEPTH_OPTION_COUNT] = { 16, 24 };

#define RESAMPLE_SPEED_OPTION_COUNT 6
static const int g_resample_speed_options[RESAMPLE_SPEED_OPTION_COUNT] = { 1, 2, 4, 8, 16, 0 };

struct ResampleEvent {
    int64_t   frame;
    MidiEvent midi;
    uint32_t  order;
};

struct ResampleSegment {
    int64_t start_frame;
    int64_t off_frame;
    int64_t end_frame;
    uint8_t note;
    uint8_t velocity;
    uint8_t round_robin;
    uint8_t kind;
    uint8_t source_note;
    int8_t  interval;
    int64_t departure_frame;
    int64_t arrival_frame;
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
    bool            dynamic_layers;

    bool            legato;
    int             intervals[2 * RESAMPLE_MAX_LEGATO_INTERVALS];
    int             interval_count;
    int             legato_velocity_count;
    int             transition_count;
    float           margin_seconds;

    bool            built;
};

enum ResamplePhase {
    RESAMPLE_PHASE_RENDER = 0,
    RESAMPLE_PHASE_SPLIT  = 1
};

struct ResampleJob {
    std::atomic<bool>    running;
    std::atomic<bool>    cancel;
    std::atomic<bool>    finished;
    std::atomic<int>     phase;
    std::atomic<int64_t> progress_frames;
    std::atomic<int>     split_done;
    PlatThread           thread;

    struct HostPlugin*     plugin;
    const ResampleScript*  script;
    int                    block_size;
    int                    speed;

    int                    bus_count;
    int                    bus_channel[FR_MAX_OUTPUT_BUSSES];
    char                   out_dir[FR_MAX_OUTPUT_BUSSES][FR_MAX_PATH];
    char                   full_path[FR_MAX_OUTPUT_BUSSES][FR_MAX_PATH];
    char                   prefix[FR_MAX_OUTPUT_BUSSES][RESAMPLE_FILE_NAME_LEN];
    int                    channel_count;

    bool                   split_samples;
    bool                   dynamic_layers;

    char   result[FR_STATUS_LEN];
    bool   result_is_error;
    float  peak;
    double elapsed_seconds;
    int    files_written;
};

void resample_config_defaults(ResampleConfig* cfg);

bool resample_bus_add(ResampleConfig* cfg);

void resample_bus_remove(ResampleConfig* cfg, int index);

void resample_bus_validate(ResampleConfig* cfg);

void resample_bus_pair_label(int channel, char* out, size_t out_len);

bool resample_bus_name_clashes(const ResampleConfig* cfg, int index);
bool resample_bus_pair_clashes(const ResampleConfig* cfg, int index);

int resample_bus_channel_count(const ResampleConfig* cfg);

int resample_valid_sample_rate(int value);
int resample_valid_render_speed(int value, int fallback);
int resample_valid_preload_frames(int value, int fallback);
int resample_valid_bit_depth(int value, int fallback);

const char* resample_step_label(int step);

void resample_layer_suffix(bool dynamic_layers, int layer, char* out, size_t out_len);

void resample_interval_suffix(int interval, char* out, size_t out_len);

void resample_sample_suffix(bool dynamic_layers, const ResampleSegment* seg, bool release,
                            char* out, size_t out_len);

void resample_sample_file_name(const char* prefix, bool dynamic_layers, const ResampleSegment* seg,
                               char* out, size_t out_len);

bool resample_preroll_on(const ResampleConfig* cfg);

void resample_preload_label(int preload_frames, char* out, size_t out_len);

uint64_t resample_required_bytes(const ResampleScript* script, bool split_samples, int bus_count);

float resample_cc_span(const ResampleConfig* cfg, float* preroll, float* hold, float* gap);

bool resample_cc_add(ResampleConfig* cfg, int controller);
void resample_cc_remove(ResampleConfig* cfg, int index);

void resample_cc_set_constant(ResampleCcCurve* c, float value);

void resample_cc_from_bezier(ResampleCcCurve* c);
void resample_cc_insert_node(ResampleCcCurve* c, float x, float y);
void resample_cc_fit_bezier(ResampleCcCurve* c);
void resample_cc_smooth(ResampleCcCurve* c);

float resample_cc_value_at(const ResampleCcCurve* c, float x);

bool resample_build(ResampleScript* script, const ResampleConfig* cfg, double sample_rate,
                    char* err, size_t err_len);

void resample_segment_file_name(const ResampleConfig* cfg, int bus, const ResampleSegment* seg,
                                char* out, size_t out_len);

bool resample_prefix(const ResampleConfig* cfg, int bus, char* out, size_t out_len, char* err, size_t err_len);

bool resample_root_target_dir(const ResampleConfig* cfg, int bus, char* out, size_t out_len, char* err, size_t err_len);

void resample_config_root_from_dirs(ResampleConfig* cfg);

int resample_config_localize_dirs(ResampleConfig* cfg);

bool resample_output_dir(const ResampleConfig* cfg, int bus, char* out, size_t out_len, char* err, size_t err_len);
bool resample_export_dir(const ResampleConfig* cfg, int bus, char* out, size_t out_len, char* err, size_t err_len);

bool resample_start(ResampleJob* job, struct HostPlugin* plugin, const ResampleScript* script,
                    int block_size, const ResampleConfig* cfg);
void resample_cancel(ResampleJob* job);

bool resample_collect(ResampleJob* job);
