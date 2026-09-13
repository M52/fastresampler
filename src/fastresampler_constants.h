#pragma once
// Global constants for FastResampler. Nothing in here depends on a platform.

#define FR_APP_NAME             "FastResampler"
#define FR_SETTINGS_FILE        "fastresampler_settings.txt"
#define FR_LOG_FILE             "fastresampler.log"

// What the scan learnt about every VST 2 DLL it opened: whether it is an
// instrument and its name, so a DLL is opened once, not on every start.
#define FR_VSTI_CACHE_FILE      "fastresampler_vsti_cache.txt"
#define FR_MAX_VSTI_CACHE       1024

// The saved resampling projects: one YAML file each, in this folder next to
// the exe.
#define FR_PROJECTS_DIR         "Projects"
#define FR_PROJECT_EXT          ".yaml"
#define FR_PROJECT_NAME_LEN     64
#define FR_MAX_PROJECTS         256

// The plugin's own state belongs to the project but not in its YAML: it is
// the plugin's binary block and a sampler's runs to megabytes. It goes in a
// file of the same name beside it, and the YAML names that file.
#define FR_PROJECT_STATE_EXT    ".fstate"
// The largest state a project takes back. A sampler with a big multi loaded
// writes a few megabytes; this is the guard against a file that is wrong.
#define FR_MAX_PLUGIN_STATE     (256u * 1024u * 1024u)

// --- Sizes of the fixed arrays ----------------------------------------------
#define FR_MAX_PATH             260
#define FR_MAX_SCAN_PATHS       16
#define FR_MAX_BANNED_PLUGINS   256
#define FR_MAX_PLUGINS          512
#define FR_PLUGIN_NAME_LEN      128
#define FR_MAX_AUDIO_DRIVERS    64
#define FR_DRIVER_NAME_LEN      128
#define FR_DRIVER_ID_LEN        256
#define FR_STATUS_LEN           256
#define FR_ERROR_LEN            512

// The output log over the keyboard: the lines it keeps, and the "14:03:22"
// each of them is stamped with.
#define FR_LOG_LINES            512
#define FR_LOG_TIME_LEN         12

// --- Audio ------------------------------------------------------------------
// The device is driven as stereo. A plugin with more channels on its main
// output is folded down to the first two, a mono plugin is copied to both.
#define FR_OUTPUT_CHANNELS      2
#define FR_MAX_BLOCK_SIZE       4096

// --- Output busses ----------------------------------------------------------
// A resampling run captures more than one microphone at a time. Every output
// bus is one stereo pair of the plugin's own outputs, so a library that sends
// its close mic to outputs 1/2 and its tree to 3/4 is captured in one pass.
// The plugin's output busses are flattened into one list of channels first,
// which makes a VST3 with three stereo busses and a VST 2 with six output
// channels look the same from here.
#define FR_MAX_OUTPUT_BUSSES    16
#define FR_BUS_CHANNELS         2
#define FR_MAX_RENDER_CHANNELS  (FR_MAX_OUTPUT_BUSSES * FR_BUS_CHANNELS)
#define FR_MIN_BLOCK_SIZE       32
#define FR_DEFAULT_BLOCK_SIZE   256
#define FR_DEFAULT_SAMPLE_RATE  48000.0
#define FR_DEFAULT_TEMPO        120.0

// The block sizes the settings tab offers.
#define FR_BLOCK_SIZE_OPTION_COUNT 7
static const int g_block_size_options[FR_BLOCK_SIZE_OPTION_COUNT] = { 32, 64, 128, 256, 512, 1024, 2048 };

// --- MIDI -------------------------------------------------------------------
#define MIDI_NOTE_COUNT         128
#define MIDI_CHANNEL_COUNT      16
#define MIDI_VALUE_MAX          127
#define MIDI_PITCH_BEND_CENTER  8192
#define MIDI_PITCH_BEND_MAX     16383
#define SEMITONES_PER_OCTAVE    12

#define MIDI_STATUS_NOTE_OFF        0x80
#define MIDI_STATUS_NOTE_ON         0x90
#define MIDI_STATUS_POLY_PRESSURE   0xA0
#define MIDI_STATUS_CONTROL_CHANGE  0xB0
#define MIDI_STATUS_CHANNEL_PRESSURE 0xD0
#define MIDI_STATUS_PITCH_BEND      0xE0

#define MIDI_CC_MOD_WHEEL       1
#define MIDI_CC_BREATH          2
#define MIDI_CC_VOLUME          7
#define MIDI_CC_PAN             10
#define MIDI_CC_EXPRESSION      11
#define MIDI_CC_SUSTAIN         64
#define MIDI_CC_ALL_SOUND_OFF   120
#define MIDI_CC_ALL_NOTES_OFF   123

// Events the UI thread can queue up between two audio callbacks.
#define FR_MIDI_QUEUE_CAPACITY  1024

// The GUI keyboard: 88 keys, A0 (21) to C8 (108).
#define MIDI_NOTE_A0            21
#define MIDI_NOTE_C8            108
#define PIANO_LOW_NOTE          MIDI_NOTE_A0
#define PIANO_HIGH_NOTE         MIDI_NOTE_C8
#define PIANO_KEY_COUNT         88
#define PIANO_WHITE_KEY_COUNT   52

// The lowest velocity a click at the very top of a key sends.
#define KEYBOARD_MIN_VELOCITY   0.15f

// --- Window and layout (logical pixels at 100% scale) ----------------------
#define WINDOW_DEFAULT_WIDTH    1400
#define WINDOW_DEFAULT_HEIGHT   900
#define WINDOW_MIN_WIDTH        1000
#define WINDOW_MIN_HEIGHT       600

// The side panel is one column down the whole left edge; the plugin editor
// and, under it, the keyboard share the rest.
#define UI_SIDEPANEL_WIDTH      420.0f
#define UI_KEYBOARD_HEIGHT      130.0f
#define UI_KEYBOARD_RAIL_HEIGHT 18.0f    // the readout strip above the keys
#define UI_LABEL_COLUMN_WIDTH   100.0f   // labels sit left of their fields
#define UI_MAIN_HEADER_HEIGHT   34.0f

// The output log, a strip over the keyboard: the bar that is always there
// and holds the grip and the fold arrow, how far the ledger opens over it,
// and how far a drag on the grip may take that. The plugin area keeps its
// own least height whatever the drag asks for.
#define UI_LOG_HEADER_HEIGHT    40.0f
#define UI_LOG_BODY_HEIGHT      160.0f
#define UI_LOG_MIN_BODY_HEIGHT  40.0f
#define UI_LOG_MAX_BODY_HEIGHT  2000.0f
#define UI_LOG_GRIP_HEIGHT      6.0f

// The strip holds two tabs: the ledger of what every step reported, and the
// output busses a run captures at once. The settings keep the one shown.
enum AppLogTab {
    APP_LOG_TAB_LOG = 0,
    APP_LOG_TAB_BUSSES
};

#define UI_MAIN_MIN_HEIGHT      120.0f
#define UI_PANEL_PADDING        8.0f
#define UI_ITEM_SPACING         4.0f
#define UI_BASE_FONT_SIZE       13.0f

// The repaint timer: one id, fired at roughly the display rate.
#define UI_TIMER_ID             1
#define UI_FRAME_INTERVAL_MS    16

// Windows' baseline DPI; the scale factor is the window's DPI over this.
#define WIN_BASE_DPI            96

// How the output meter falls back after a peak, per frame.
#define UI_METER_DECAY          0.92f

// Standard signed PCM conversion limits used by the bank writer.
#define PCM_ENCODE_16BIT 32768.0f
#define PCM_ENCODE_24BIT 8388608.0f
#define PCM_MAX_16BIT 32767
#define PCM_MAX_24BIT 8388607
#define PCM_MIN_16BIT (-32768)
#define PCM_MIN_24BIT (-8388608)
