#pragma once
// Global constants for FastResampler. Nothing in here depends on a platform.

#define FR_APP_NAME             "FastResampler"
#define FR_SETTINGS_FILE        "fastresampler_settings.txt"
#define FR_LOG_FILE             "fastresampler.log"

// --- Sizes of the fixed arrays ----------------------------------------------
#define FR_MAX_PATH             260
#define FR_MAX_SCAN_PATHS       16
#define FR_MAX_PLUGINS          512
#define FR_PLUGIN_NAME_LEN      128
#define FR_MAX_AUDIO_DRIVERS    64
#define FR_DRIVER_NAME_LEN      128
#define FR_DRIVER_ID_LEN        256
#define FR_STATUS_LEN           256
#define FR_ERROR_LEN            512

// --- Audio ------------------------------------------------------------------
// The device is driven as stereo. A plugin with more channels on its main
// output is folded down to the first two, a mono plugin is copied to both.
#define FR_OUTPUT_CHANNELS      2
#define FR_MAX_BLOCK_SIZE       4096
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
#define WINDOW_MIN_WIDTH        900
#define WINDOW_MIN_HEIGHT       600

#define UI_SIDEPANEL_WIDTH      320.0f
#define UI_KEYBOARD_HEIGHT      130.0f
#define UI_KEYBOARD_RAIL_HEIGHT 18.0f    // the readout strip above the keys
#define UI_LABEL_COLUMN_WIDTH   100.0f   // labels sit left of their fields
#define UI_MAIN_HEADER_HEIGHT   34.0f
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
