#pragma once
// Resampling: play the notes of a range at a set of velocities, a number of
// times each, through the plugin, capture the result faster than real time,
// then cut it into one WAV per sample.
//
// Build turns the configuration into a script: MIDI events at sample
// positions plus a segment table that says where each wanted sample starts
// and ends in the file. Render runs the plugin over the script on a worker
// thread into one full WAV per output bus, then copies each segment out to
// its own file named
// <Bus>_<Instrument>_<Articulation>_<Note>_<Velocity>_RR<n>.wav.
//
// Output busses: a run captures every microphone of a multi-miced library in
// one pass. Each bus is one stereo pair of the plugin's outputs and is named
// after the microphone it carries, so the plugin's own routing -- close to
// outputs 1/2, tree to 3/4 -- decides what lands where. One pass of the
// script fills every bus at once; each bus then gets a folder of its own,
// named after it, holding its full WAV, its samples and its instrument.
//
// Pre-roll: a sampler that resets its round robin counter after silence
// plays its first variation for every note we send. To capture variation n,
// the script plays n-1 short notes of the same pitch and velocity first,
// spaced by the pre-roll gap so each rings out, then the real note. The
// segment starts at the real note, so the split drops the pre-roll.
//
// Legato: with "Capture legato transitions" on, the run plays the moves
// between the notes as well as the notes. One capture holds the note it
// leaves, presses the note it lands on over it, and lets go of the first;
// whatever the plugin does with that -- a slur, a portamento, a re-bow -- is
// in the sample. The sample keeps a little of the first note before that
// second note-on and a little of the second past the point the move lands,
// and those two are what the instrument crossfades on either side. See the
// Legato part of ResampleConfig and fastresampler_legato.h.
//
// Dynamic layers: with "RR are dynamic layers" on, the round robin count is
// the number of dynamic layers instead. Each note is still played that many
// times, but a controller curve can be tied to one layer, so the three
// passes of a note can hold CC1 at three levels, say, and capture three
// dynamics of a sustain patch. The files end in _dyn<n>.wav, pre-roll is
// off (there is no counter to advance), and the instrument built from the
// run crossfades the layers on that controller instead of taking turns.

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
// "RR8" or "dyn8": the part of a file name that says which pass a sample is.
#define RESAMPLE_LAYER_SUFFIX_LEN   8

// How long a pre-roll note is held before it is released to ring out.
#define RESAMPLE_PREROLL_HOLD_SECONDS 0.2f

// --- Legato ---------------------------------------------------------------------
// A legato run captures the moves between notes as well as the notes. One
// capture plays the note it leaves, then overlaps the note it lands on, and
// the sample keeps a little of the first note before the second note-on and
// a little of the second past the point the move arrives at it.
//
// The intervals are magnitudes: "1, 2" records a move up and a move down at
// each of them. The source notes have a step of their own, so wholetone
// sustains can carry semitone transitions.
//
// Every interval a player might slur has to be among them, because an
// interval the run never captured gets no move at all: a move of the wrong
// width lands on the wrong note, which is worse than none. Twelve covers
// every slur inside an octave, and a leap wider than that is rarely played
// legato. Capturing intervals costs far less than it looks, because the
// source notes need no step of their own to match: a move is rooted on the
// note it is left from, so a source note between two captured ones plays the
// nearer one transposed, and transposing a move keeps its width and so still
// lands where it should. Spend the run on intervals, not on source notes.
#define RESAMPLE_MAX_LEGATO_INTERVALS  12
#define RESAMPLE_MAX_LEGATO_VELOCITIES 8
#define RESAMPLE_INTERVAL_TEXT_LEN     48

// What one segment holds.
enum ResampleSegmentKind {
    RESAMPLE_SEG_SUSTAIN    = 0,
    RESAMPLE_SEG_TRANSITION = 1
};

// --- Controller curves ----------------------------------------------------------
// A controller drawn over the time of one sample: the pre-roll notes, the
// held note, and the gap it rings out in. The values cover that whole span,
// 0 to 1 for controller value 0 to 127, and stretch with the timing -- over
// a legato capture, which is longer than one note, they stretch over the
// whole of it, from the first note-on to the end of the gap. The
// bezier nodes are the editor's; the values are what plays. While the script
// plays, each curve is read every tick and a controller event goes out when
// its 7-bit value differs from the last one sent.
#define RESAMPLE_MAX_CC_CURVES      64
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
    int   layer;                                 // with dynamic layers: the one layer it plays for; 0 = every layer
};

// --- Output busses --------------------------------------------------------------
// One stereo pair of the plugin's outputs, named after the microphone the
// plugin routes into it. The channel is the first of the pair, counted over
// the plugin's output busses flattened into one list: 0 for outputs 1/2, 2
// for 3/4, and so on. The name is the folder the bus writes to and the first
// part of every file name it makes, so it takes the place the Microphone
// field used to hold.
struct ResampleBus {
    char name[RESAMPLE_NAME_LEN];
    int  channel;                  // first plugin output channel of the pair
};

// What the Resample tab edits. Lives in Settings so it survives a restart.
struct ResampleConfig {
    char  instrument[RESAMPLE_NAME_LEN];
    char  articulation[RESAMPLE_NAME_LEN];

    // The output busses, one per microphone, edited in the Output busses tab.
    // There is always at least one. monitor_bus is the one the speakers play
    // while the plugin is live, as the device itself is stereo.
    ResampleBus busses[FR_MAX_OUTPUT_BUSSES];
    int   bus_count;
    int   monitor_bus;

    // The root of the whole set of projects. Under it, a run writes to
    // <root>\<Instrument>\<Articulation>\<Bus>, so the naming fields and the
    // bus put every take in a folder of its own. The samples and the
    // instrument both go there unless the box of their section overrides it,
    // and a bus is the last folder either way.
    char  root_dir[FR_MAX_PATH];

    char  low_note[RESAMPLE_NOTE_TEXT_LEN];        // "C3" style, C3 = middle C
    char  high_note[RESAMPLE_NOTE_TEXT_LEN];
    int   step;                                     // semitones between sampled notes, 1..12
    char  velocities[RESAMPLE_VELOCITY_TEXT_LEN];   // "40, 60"
    int   round_robins;                             // samples per note and velocity, 1..8
    bool  dynamic_layers;                           // the round robins are dynamic layers: see the top of the file
    bool  preroll;                                  // give every capture a run-up of n-1 gaps; not with dynamic layers
    float preroll_gap_seconds;                      // one gap of that run-up, and a pre-roll note-on to the next

    float hold_seconds;                             // note on to note off
    float gap_seconds;                              // note off to the segment's end

    // Legato: the moves between the notes, captured after the sustains.
    bool  legato;                                   // capture them at all
    int   legato_step;                              // semitones between sampled source notes, 1..12
    char  legato_intervals[RESAMPLE_INTERVAL_TEXT_LEN];  // "1, 2": magnitudes, each recorded up and down
    char  legato_velocities[RESAMPLE_VELOCITY_TEXT_LEN]; // the note landed on, which many libraries read as the speed
    char  legato_arrivals[RESAMPLE_VELOCITY_TEXT_LEN];   // ms from that note-on to where the move lands, one per velocity
    bool  legato_detect_arrival;                    // read the landing out of the audio instead
    float legato_source_hold_seconds;               // source note-on to target note-on
    float legato_overlap_seconds;                   // target note-on to source note-off
    float legato_hold_seconds;                      // target note-on to target note-off
    float legato_margin_seconds;                    // sustain kept either side of the move: before its departure, past its arrival

    int   sample_rate;                              // the render's rate; 0 = the audio device's
    int   render_speed;                             // realtime multiple the render is held to; 0 = unlimited
    char  output_dir[FR_MAX_PATH];                  // the full WAV and the sample files, when the box below overrides the root
    bool  output_dir_override;                      // write them there rather than under the root
    bool  split_samples;                            // cut the full WAV into one file per sample
    bool  export_rzc;                               // one FastSampler .rzc per round robin, beside the bank
    bool  delete_full_wav;                          // remove the full WAV once the bank is built from it
    char  export_dir[FR_MAX_PATH];                  // the .fsi and .fsb, when the box below overrides the root
    bool  export_dir_override;                      // write them there rather than under the root
    int   preload_frames;                           // frames of each sample the bank keeps in RAM; 0 = whole samples
    int   bit_depth;                                // bits the bank stores every sample at, 16 or 24

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

// The bit depths the bank can be written at. The format takes these two and
// no others, and FastSampler's own export offers the same pair.
#define RESAMPLE_BIT_DEPTH_OPTION_COUNT 2
static const int g_resample_bit_depth_options[RESAMPLE_BIT_DEPTH_OPTION_COUNT] = { 16, 24 };

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
//
// A transition reads the same fields differently: note is the note landed
// on, start_frame is one margin before its note-on and off_frame that
// note-on itself, and end_frame is where it is released. departure_frame and
// arrival_frame, from start_frame, are where the move leaves the first note
// and lands on the second -- the nominal ones until the instrument build
// reads them out of the audio.
struct ResampleSegment {
    int64_t start_frame;     // the real note-on
    int64_t off_frame;       // the note-off, hold later: where a release sample starts
    int64_t end_frame;       // hold + gap later
    uint8_t note;
    uint8_t velocity;
    uint8_t round_robin;     // 1-based; the dynamic layer when the run has those
    uint8_t kind;            // ResampleSegmentKind
    uint8_t source_note;     // a transition: the note it is left; a sustain: note
    int8_t  interval;        // a transition: note - source_note
    // The two bounds of the move, from start_frame. Nominal until the
    // instrument build reads them out of the audio.
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
    bool            dynamic_layers;   // the round robins are dynamic layers; the files say dyn<n>

    // The legato part of the run, empty when it has none. The intervals are
    // signed and sorted, a down one before the up one of the same width, and
    // the widest of each sign takes every interval past it when the
    // instrument links its collections.
    bool            legato;
    int             intervals[2 * RESAMPLE_MAX_LEGATO_INTERVALS];
    int             interval_count;
    int             legato_velocity_count;
    int             transition_count;   // segments of RESAMPLE_SEG_TRANSITION
    float           margin_seconds;     // what the transition samples keep either side of the move

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

    // One set per output bus: where it writes, the long WAV it fills, and
    // the first part of every file name it makes. The render fills them all
    // in one pass over the script.
    int                    bus_count;
    int                    bus_channel[FR_MAX_OUTPUT_BUSSES];   // first plugin channel of the pair
    char                   out_dir[FR_MAX_OUTPUT_BUSSES][FR_MAX_PATH];
    char                   full_path[FR_MAX_OUTPUT_BUSSES][FR_MAX_PATH];
    char                   prefix[FR_MAX_OUTPUT_BUSSES][RESAMPLE_FILE_NAME_LEN]; // Bus_Instrument_Articulation
    // The flat plugin channels the render asks for: one past the highest any
    // bus reads, so a bus on outputs 5/6 is fetched without the pairs under
    // it being written anywhere.
    int                    channel_count;

    bool                   split_samples;
    bool                   dynamic_layers;          // the script's: names the files dyn<n> instead of RR<n>

    char   result[FR_STATUS_LEN];
    bool   result_is_error;
    float  peak;                                    // the loudest of every bus
    double elapsed_seconds;
    int    files_written;                           // over every bus
};

void resample_config_defaults(ResampleConfig* cfg);

// --- Output busses --------------------------------------------------------------

// A bus at the end of the list, named "Mic <n>" and on the first stereo pair
// no other bus reads. False when the list is full.
bool resample_bus_add(ResampleConfig* cfg);

// Takes one out. The last bus cannot go: a run needs somewhere to write.
void resample_bus_remove(ResampleConfig* cfg, int index);

// At least one bus, every channel a pair the plugin could carry, and the
// monitor inside the list. Run over a configuration read from a file.
void resample_bus_validate(ResampleConfig* cfg);

// "1/2" for channel 0: the pair a bus reads, as the plugin's own routing
// names it.
void resample_bus_pair_label(int channel, char* out, size_t out_len);

// Whether two busses read the same pair, or a bus has no usable name. The
// tab marks them and a build refuses them, as either one would have two
// busses write the same files.
bool resample_bus_name_clashes(const ResampleConfig* cfg, int index);
bool resample_bus_pair_clashes(const ResampleConfig* cfg, int index);

// The flat plugin channels a run reads: one past the highest any bus needs.
int resample_bus_channel_count(const ResampleConfig* cfg);

// A value read from a file against the list the tab offers. The rate falls
// back to 0, the device's own; the other two to what the caller gives, which
// is the default that was already there.
int resample_valid_sample_rate(int value);
int resample_valid_render_speed(int value, int fallback);
int resample_valid_preload_frames(int value, int fallback);
int resample_valid_bit_depth(int value, int fallback);

// The dropdown label for a step in semitones, "Every wholetone" for 2.
const char* resample_step_label(int step);

// "RR1", or "dyn1" when the round robins are dynamic layers: the part of a
// file name, a zone name and a collection name that says which pass it is.
void resample_layer_suffix(bool dynamic_layers, int layer, char* out, size_t out_len);

// "+2" or "-1": an interval as a file name and a collection name carry it.
void resample_interval_suffix(int interval, char* out, size_t out_len);

// Everything after the prefix of a sample's name: "_C3_60_RR1.wav" for a
// sustain, "_C3-D3_60_leg.wav" for the move between two notes, and
// "-release" before the extension for the half of a sustain past its
// note-off, which is a zone rather than a file of its own.
void resample_sample_suffix(bool dynamic_layers, const ResampleSegment* seg, bool release,
                            char* out, size_t out_len);

// The file one segment is split out to, the prefix and that suffix. The .rzc
// export and the zone names use the same pair, so all of them agree.
void resample_sample_file_name(const char* prefix, bool dynamic_layers, const ResampleSegment* seg,
                               char* out, size_t out_len);


// Whether the run gives its captures a pre-roll run-up: only when asked, and
// only for round robins. Dynamic layers have no counter to carry.
bool resample_preroll_on(const ResampleConfig* cfg);

// The dropdown label for a preload size: "RAM" for 0, else "8192 frames".
void resample_preload_label(int preload_frames, char* out, size_t out_len);

// Bytes a render writes: the full WAV and, when the split runs, one file per
// sample, all 32 bit float with a header each, for every one of the busses.
uint64_t resample_required_bytes(const ResampleScript* script, bool split_samples, int bus_count);

// --- Controller curves ---------------------------------------------------------

// The time one curve spans for the configuration, in seconds: the pre-roll
// run-up every capture is given (n-1 gaps, 0 without pre-roll), the hold and
// the gap. Each part comes back on its own when a pointer is given.
float resample_cc_span(const ResampleConfig* cfg, float* preroll, float* hold, float* gap);

// A new curve at the end of the list, flat at 127: the lowest controller not
// in use yet, or, with dynamic layers, the last curve's controller for the
// layer after its own, so three CC1 curves are three clicks. False when the
// list is full.
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

// The file name a segment gets on one bus, from the naming fields.
void resample_segment_file_name(const ResampleConfig* cfg, int bus, const ResampleSegment* seg,
                                char* out, size_t out_len);

// "Close_Instrument_Articulation" for one bus, cleaned for a file name.
// False, with the reason in err, when a field or the bus name is empty.
bool resample_prefix(const ResampleConfig* cfg, int bus, char* out, size_t out_len, char* err, size_t err_len);

// <root>\<Instrument>\<Articulation>\<Bus>: the folder one bus writes to when
// it follows the root project folder. False, with the reason in err, when
// the root or one of the names is empty.
bool resample_root_target_dir(const ResampleConfig* cfg, int bus, char* out, size_t out_len, char* err, size_t err_len);

// A configuration read from a file written before the root project folder
// existed: it names the two folders it wrote to and no root. When the
// samples already went to <root>\<Instrument>\<Articulation>\<Bus>, that
// root is taken; otherwise the boxes that override the root go on. A run
// keeps writing exactly where it did either way.
void resample_config_root_from_dirs(ResampleConfig* cfg);

// Puts the folders of a project made on another computer back on this one.
// A folder is kept when anything above it exists, as the rest of the chain
// is made when a run writes; one whose whole tree is missing -- another
// computer's drive letter -- is replaced by the folder beside the exe, and
// an override that pointed there is dropped so the folder follows the root
// again. Reports how many were replaced, 0 when the project is at home.
int resample_config_localize_dirs(ResampleConfig* cfg);

// Where one bus puts its samples and where it puts its instrument: the
// folder above, or, when that section's box overrides the root, the picked
// folder with the bus's own name under it. A bus is the last folder either
// way, so two microphones never write over one another.
bool resample_output_dir(const ResampleConfig* cfg, int bus, char* out, size_t out_len, char* err, size_t err_len);
bool resample_export_dir(const ResampleConfig* cfg, int bus, char* out, size_t out_len, char* err, size_t err_len);

// Starts the worker. The caller has taken the plugin away from the audio
// thread first and gives it back after resample_collect(). The output folder
// exists by then.
bool resample_start(ResampleJob* job, struct HostPlugin* plugin, const ResampleScript* script,
                    int block_size, const ResampleConfig* cfg);
void resample_cancel(ResampleJob* job);

// Joins the worker once finished is set. Returns true when a result is ready.
bool resample_collect(ResampleJob* job);
