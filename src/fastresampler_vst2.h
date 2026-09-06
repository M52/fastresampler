#pragma once
// The VST 2.4 plugin ABI, as far as an instrument host needs it. Steinberg
// no longer ships the VST 2 SDK, so the structures and opcodes are declared
// here from the published interface, the way the ASIO driver interface is in
// src/platform/fastresampler_asio.h. The layout is the one every VST 2
// plugin was compiled against; nothing in it depends on the SDK.

#include <stdint.h>

#if defined(_WIN32) && !defined(_WIN64)
    #define VST2_CALL __cdecl
#else
    #define VST2_CALL
#endif

// 'VstP', the first field of every plugin instance.
#define VST2_EFFECT_MAGIC       0x56737450
#define VST2_VERSION            2400
#define VST2_MAX_NAME_LEN       64       // effect and product names
#define VST2_MAX_VENDOR_LEN     64
#define VST2_MAX_PROGRAM_LEN    24
#define VST2_MAX_PARAM_LEN      8

struct Vst2Effect;

typedef intptr_t (VST2_CALL *Vst2AudioMasterFn)(Vst2Effect* effect, int32_t opcode, int32_t index,
                                                 intptr_t value, void* ptr, float opt);
typedef intptr_t (VST2_CALL *Vst2DispatcherFn)(Vst2Effect* effect, int32_t opcode, int32_t index,
                                                intptr_t value, void* ptr, float opt);
typedef void     (VST2_CALL *Vst2ProcessFn)(Vst2Effect* effect, float** inputs, float** outputs, int32_t frames);
typedef void     (VST2_CALL *Vst2ProcessDoubleFn)(Vst2Effect* effect, double** inputs, double** outputs, int32_t frames);
typedef void     (VST2_CALL *Vst2SetParameterFn)(Vst2Effect* effect, int32_t index, float value);
typedef float    (VST2_CALL *Vst2GetParameterFn)(Vst2Effect* effect, int32_t index);

// The plugin's entry point, "VSTPluginMain" (or "main" in old plugins).
typedef Vst2Effect* (VST2_CALL *Vst2EntryFn)(Vst2AudioMasterFn audio_master);

// The plugin instance the entry point returns. The host reads the counts and
// flags and calls through the function pointers; resvd1 and resvd2 are the
// host's own.
struct Vst2Effect {
    int32_t             magic;
    Vst2DispatcherFn    dispatcher;
    Vst2ProcessFn       process;             // the accumulating variant, deprecated
    Vst2SetParameterFn  set_parameter;
    Vst2GetParameterFn  get_parameter;
    int32_t             num_programs;
    int32_t             num_params;
    int32_t             num_inputs;
    int32_t             num_outputs;
    int32_t             flags;
    intptr_t            resvd1;
    intptr_t            resvd2;
    int32_t             initial_delay;       // latency in frames
    int32_t             real_qualities;      // deprecated
    int32_t             off_qualities;       // deprecated
    float               io_ratio;            // deprecated
    void*               object;              // the plugin's own
    void*               user;                // the plugin's own
    int32_t             unique_id;
    int32_t             version;
    Vst2ProcessFn       process_replacing;
    Vst2ProcessDoubleFn process_double_replacing;
    char                future[56];
};

// Vst2Effect::flags
enum {
    VST2_FLAG_HAS_EDITOR          = 1 << 0,
    VST2_FLAG_CAN_REPLACING       = 1 << 4,
    VST2_FLAG_PROGRAM_CHUNKS      = 1 << 5,
    VST2_FLAG_IS_SYNTH            = 1 << 8,
    VST2_FLAG_NO_SOUND_IN_STOP    = 1 << 9,
    VST2_FLAG_CAN_DOUBLE_REPLACING = 1 << 12
};

// Opcodes the host sends to the plugin's dispatcher.
enum {
    VST2_EFF_OPEN                   = 0,
    VST2_EFF_CLOSE                  = 1,
    VST2_EFF_SET_PROGRAM            = 2,
    VST2_EFF_GET_PROGRAM            = 3,
    VST2_EFF_SET_PROGRAM_NAME       = 4,
    VST2_EFF_GET_PROGRAM_NAME       = 5,
    VST2_EFF_GET_PARAM_LABEL        = 6,
    VST2_EFF_GET_PARAM_DISPLAY      = 7,
    VST2_EFF_GET_PARAM_NAME         = 8,
    VST2_EFF_SET_SAMPLE_RATE        = 10,    // opt = rate
    VST2_EFF_SET_BLOCK_SIZE         = 11,    // value = frames
    VST2_EFF_MAINS_CHANGED          = 12,    // value = 1 on, 0 off
    VST2_EFF_EDIT_GET_RECT          = 13,    // ptr = Vst2Rect** out
    VST2_EFF_EDIT_OPEN              = 14,    // ptr = parent window
    VST2_EFF_EDIT_CLOSE             = 15,
    VST2_EFF_EDIT_IDLE              = 19,
    VST2_EFF_GET_CHUNK              = 23,
    VST2_EFF_SET_CHUNK              = 24,
    VST2_EFF_PROCESS_EVENTS         = 25,    // ptr = Vst2Events*
    VST2_EFF_CAN_BE_AUTOMATED       = 26,
    VST2_EFF_STRING_TO_PARAMETER    = 27,
    VST2_EFF_GET_PROGRAM_NAME_INDEXED = 29,
    VST2_EFF_GET_INPUT_PROPERTIES   = 33,
    VST2_EFF_GET_OUTPUT_PROPERTIES  = 34,
    VST2_EFF_GET_PLUG_CATEGORY      = 35,
    VST2_EFF_OFFLINE_NOTIFY         = 38,
    VST2_EFF_OFFLINE_PREPARE        = 39,
    VST2_EFF_OFFLINE_RUN            = 40,
    VST2_EFF_PROCESS_VAR_IO         = 41,
    VST2_EFF_SET_SPEAKER_ARRANGEMENT = 42,
    VST2_EFF_SET_BYPASS             = 44,
    VST2_EFF_GET_EFFECT_NAME        = 45,    // ptr = char[64]
    VST2_EFF_GET_VENDOR_STRING      = 47,    // ptr = char[64]
    VST2_EFF_GET_PRODUCT_STRING     = 48,    // ptr = char[64]
    VST2_EFF_GET_VENDOR_VERSION     = 49,
    VST2_EFF_VENDOR_SPECIFIC        = 50,
    VST2_EFF_CAN_DO                 = 51,    // ptr = the feature's name
    VST2_EFF_GET_TAIL_SIZE          = 52,
    VST2_EFF_GET_PARAMETER_PROPERTIES = 56,
    VST2_EFF_GET_VST_VERSION        = 58,
    VST2_EFF_EDIT_KEY_DOWN          = 59,
    VST2_EFF_EDIT_KEY_UP            = 60,
    VST2_EFF_SET_EDIT_KNOB_MODE     = 61,
    VST2_EFF_GET_MIDI_PROGRAM_NAME  = 62,
    VST2_EFF_GET_CURRENT_MIDI_PROGRAM = 63,
    VST2_EFF_GET_MIDI_PROGRAM_CATEGORY = 64,
    VST2_EFF_HAS_MIDI_PROGRAMS_CHANGED = 65,
    VST2_EFF_GET_MIDI_KEY_NAME      = 66,
    VST2_EFF_BEGIN_SET_PROGRAM      = 67,
    VST2_EFF_END_SET_PROGRAM        = 68,
    VST2_EFF_GET_SPEAKER_ARRANGEMENT = 69,
    VST2_EFF_SHELL_GET_NEXT_PLUGIN  = 70,
    VST2_EFF_START_PROCESS          = 71,
    VST2_EFF_STOP_PROCESS           = 72,
    VST2_EFF_SET_TOTAL_SAMPLES_TO_PROCESS = 73,
    VST2_EFF_SET_PAN_LAW            = 74,
    VST2_EFF_BEGIN_LOAD_BANK        = 75,
    VST2_EFF_BEGIN_LOAD_PROGRAM     = 76,
    VST2_EFF_SET_PROCESS_PRECISION  = 77,    // value = 0 float, 1 double
    VST2_EFF_GET_NUM_MIDI_INPUT_CHANNELS = 78,
    VST2_EFF_GET_NUM_MIDI_OUTPUT_CHANNELS = 79
};

// Opcodes the plugin sends to the host's audio master callback.
enum {
    VST2_AM_AUTOMATE                = 0,     // index = parameter, opt = value
    VST2_AM_VERSION                 = 1,     // returns VST2_VERSION
    VST2_AM_CURRENT_ID              = 2,     // the id of the plugin being loaded (shells)
    VST2_AM_IDLE                    = 3,
    VST2_AM_PIN_CONNECTED           = 4,     // deprecated
    VST2_AM_WANT_MIDI               = 6,     // deprecated
    VST2_AM_GET_TIME                = 7,     // returns Vst2TimeInfo*
    VST2_AM_PROCESS_EVENTS          = 8,
    VST2_AM_SET_TIME                = 9,     // deprecated
    VST2_AM_TEMPO_AT                = 10,    // deprecated
    VST2_AM_GET_NUM_AUTOMATABLE_PARAMETERS = 11,   // deprecated
    VST2_AM_GET_PARAMETER_QUANTIZATION = 12,       // deprecated
    VST2_AM_IO_CHANGED              = 13,
    VST2_AM_NEED_IDLE               = 14,    // deprecated
    VST2_AM_SIZE_WINDOW             = 15,    // index = width, value = height
    VST2_AM_GET_SAMPLE_RATE         = 16,
    VST2_AM_GET_BLOCK_SIZE          = 17,
    VST2_AM_GET_INPUT_LATENCY       = 18,
    VST2_AM_GET_OUTPUT_LATENCY      = 19,
    VST2_AM_GET_PREVIOUS_PLUG       = 20,    // deprecated
    VST2_AM_GET_NEXT_PLUG           = 21,    // deprecated
    VST2_AM_WILL_REPLACE_OR_ACCUMULATE = 22, // deprecated
    VST2_AM_GET_CURRENT_PROCESS_LEVEL = 23,
    VST2_AM_GET_AUTOMATION_STATE    = 24,
    VST2_AM_OFFLINE_START           = 25,
    VST2_AM_OFFLINE_READ            = 26,
    VST2_AM_OFFLINE_WRITE           = 27,
    VST2_AM_OFFLINE_GET_CURRENT_PASS = 28,
    VST2_AM_OFFLINE_GET_CURRENT_META_PASS = 29,
    VST2_AM_SET_OUTPUT_SAMPLE_RATE  = 30,    // deprecated
    VST2_AM_GET_OUTPUT_SPEAKER_ARRANGEMENT = 31,   // deprecated
    VST2_AM_GET_VENDOR_STRING       = 32,    // ptr = char[64]
    VST2_AM_GET_PRODUCT_STRING      = 33,    // ptr = char[64]
    VST2_AM_GET_VENDOR_VERSION      = 34,
    VST2_AM_VENDOR_SPECIFIC         = 35,
    VST2_AM_SET_ICON                = 36,    // deprecated
    VST2_AM_CAN_DO                  = 37,    // ptr = the feature's name
    VST2_AM_GET_LANGUAGE            = 38,
    VST2_AM_OPEN_WINDOW             = 39,    // deprecated
    VST2_AM_CLOSE_WINDOW            = 40,    // deprecated
    VST2_AM_GET_DIRECTORY           = 41,
    VST2_AM_UPDATE_DISPLAY          = 42,
    VST2_AM_BEGIN_EDIT              = 43,
    VST2_AM_END_EDIT                = 44,
    VST2_AM_OPEN_FILE_SELECTOR      = 45,
    VST2_AM_CLOSE_FILE_SELECTOR     = 46,
    VST2_AM_EDIT_FILE               = 47,    // deprecated
    VST2_AM_GET_CHUNK_FILE          = 48,    // deprecated
    VST2_AM_GET_INPUT_SPEAKER_ARRANGEMENT = 49     // deprecated
};

// VST2_EFF_GET_PLUG_CATEGORY
enum {
    VST2_CATEGORY_UNKNOWN           = 0,
    VST2_CATEGORY_EFFECT            = 1,
    VST2_CATEGORY_SYNTH             = 2,
    VST2_CATEGORY_ANALYSIS          = 3,
    VST2_CATEGORY_MASTERING         = 4,
    VST2_CATEGORY_SPACIALIZER       = 5,
    VST2_CATEGORY_ROOM_FX           = 6,
    VST2_CATEGORY_SURROUND_FX       = 7,
    VST2_CATEGORY_RESTORATION       = 8,
    VST2_CATEGORY_OFFLINE_PROCESS   = 9,
    VST2_CATEGORY_SHELL             = 10,
    VST2_CATEGORY_GENERATOR         = 11
};

// VST2_AM_GET_CURRENT_PROCESS_LEVEL
enum {
    VST2_PROCESS_LEVEL_UNKNOWN      = 0,
    VST2_PROCESS_LEVEL_USER         = 1,     // the UI thread
    VST2_PROCESS_LEVEL_REALTIME     = 2,
    VST2_PROCESS_LEVEL_PREFETCH     = 3,
    VST2_PROCESS_LEVEL_OFFLINE      = 4
};

// VST2_AM_GET_LANGUAGE
#define VST2_LANGUAGE_ENGLISH       1

// VST2_EFF_SET_PROCESS_PRECISION
#define VST2_PRECISION_FLOAT        0
#define VST2_PRECISION_DOUBLE       1

// The editor's rectangle, from VST2_EFF_EDIT_GET_RECT.
struct Vst2Rect {
    int16_t top;
    int16_t left;
    int16_t bottom;
    int16_t right;
};

// Events for VST2_EFF_PROCESS_EVENTS. A MIDI event is the generic event
// with its data laid out as below; both are 32 bytes.
#define VST2_EVENT_TYPE_MIDI        1
#define VST2_EVENT_TYPE_SYSEX       6
#define VST2_MIDI_EVENT_IS_REALTIME 1

struct Vst2Event {
    int32_t type;
    int32_t byte_size;
    int32_t delta_frames;
    int32_t flags;
    char    data[16];
};

struct Vst2MidiEvent {
    int32_t type;                // VST2_EVENT_TYPE_MIDI
    int32_t byte_size;           // sizeof(Vst2MidiEvent)
    int32_t delta_frames;        // offset into the block
    int32_t flags;               // VST2_MIDI_EVENT_IS_REALTIME or 0
    int32_t note_length;         // 0 when unknown
    int32_t note_offset;
    char    midi_data[4];        // status, data 1, data 2, unused
    char    detune;
    char    note_off_velocity;
    char    reserved1;
    char    reserved2;
};

// The list the plugin receives: a count, then the pointers. The two-element
// array is the declared shape; the host allocates room for more.
struct Vst2Events {
    int32_t    num_events;
    intptr_t   reserved;
    Vst2Event* events[2];
};

// What VST2_AM_GET_TIME returns. flags say which fields are valid.
struct Vst2TimeInfo {
    double  sample_pos;
    double  sample_rate;
    double  nano_seconds;
    double  ppq_pos;
    double  tempo;
    double  bar_start_pos;
    double  cycle_start_pos;
    double  cycle_end_pos;
    int32_t time_sig_numerator;
    int32_t time_sig_denominator;
    int32_t smpte_offset;
    int32_t smpte_frame_rate;
    int32_t samples_to_next_clock;
    int32_t flags;
};

enum {
    VST2_TIME_TRANSPORT_CHANGED     = 1,
    VST2_TIME_TRANSPORT_PLAYING     = 1 << 1,
    VST2_TIME_TRANSPORT_CYCLE_ACTIVE = 1 << 2,
    VST2_TIME_TRANSPORT_RECORDING   = 1 << 3,
    VST2_TIME_AUTOMATION_WRITING    = 1 << 6,
    VST2_TIME_AUTOMATION_READING    = 1 << 7,
    VST2_TIME_NANOS_VALID           = 1 << 8,
    VST2_TIME_PPQ_POS_VALID         = 1 << 9,
    VST2_TIME_TEMPO_VALID           = 1 << 10,
    VST2_TIME_BARS_VALID            = 1 << 11,
    VST2_TIME_CYCLE_POS_VALID       = 1 << 12,
    VST2_TIME_SIG_VALID             = 1 << 13,
    VST2_TIME_SMPTE_VALID           = 1 << 14,
    VST2_TIME_CLOCK_VALID           = 1 << 15
};

static_assert(sizeof(Vst2MidiEvent) == 32, "Vst2MidiEvent must be 32 bytes");
static_assert(sizeof(Vst2Event) == 32, "Vst2Event must be 32 bytes");
static_assert(sizeof(Vst2Rect) == 8, "Vst2Rect must be 8 bytes");
