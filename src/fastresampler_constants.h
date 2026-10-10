#pragma once

#define FR_APP_NAME             "FastResampler"
#define FR_SETTINGS_FILE        "fastresampler_settings.txt"
#define FR_LOG_FILE             "fastresampler.log"
#define FR_ICON_FONT_FILE       "lucide.ttf"

#define FR_VSTI_CACHE_FILE      "fastresampler_vsti_cache.txt"
#define FR_MAX_VSTI_CACHE       1024
#define FR_SCAN_FRAME_BUDGET_MS 30
#define FR_SCAN_PARALLEL_PROBES 4
#define FR_SCAN_PROBE_TIMEOUT_MS 120000
#define FR_SCAN_PROBE_ARG       "--probe-vst2"
#define FR_DISCLAIMER_SECONDS   5

#define FR_PROJECTS_DIR         "Projects"
#define FR_PROJECT_EXT          ".yaml"
#define FR_PROJECT_NAME_LEN     64
#define FR_MAX_PROJECTS         256

#define FR_PROJECT_STATE_EXT    ".fstate"
#define FR_MAX_PLUGIN_STATE     (256u * 1024u * 1024u)

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

#define FR_LOG_LINES            512
#define FR_LOG_TIME_LEN         12

#define FR_OUTPUT_CHANNELS      2
#define FR_MAX_BLOCK_SIZE       4096

#define FR_MAX_OUTPUT_BUSSES    16
#define FR_BUS_CHANNELS         2
#define FR_MAX_RENDER_CHANNELS  (FR_MAX_OUTPUT_BUSSES * FR_BUS_CHANNELS)
#define FR_MIN_BLOCK_SIZE       32
#define FR_DEFAULT_BLOCK_SIZE   256
#define FR_DEFAULT_SAMPLE_RATE  48000.0
#define FR_DEFAULT_TEMPO        120.0

#define FR_BLOCK_SIZE_OPTION_COUNT 7
static const int g_block_size_options[FR_BLOCK_SIZE_OPTION_COUNT] = { 32, 64, 128, 256, 512, 1024, 2048 };

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

#define FR_MIDI_QUEUE_CAPACITY  1024

#define MIDI_NOTE_A0            21
#define MIDI_NOTE_C8            108
#define PIANO_HIGH_NOTE         MIDI_NOTE_C8
#define PIANO_WHITE_KEY_COUNT   52

#define KEYBOARD_MIN_VELOCITY   0.15f

#define WINDOW_DEFAULT_WIDTH    1400
#define WINDOW_DEFAULT_HEIGHT   900
#define WINDOW_MIN_WIDTH        1000
#define WINDOW_MIN_HEIGHT       600

#define UI_SIDEPANEL_WIDTH      420.0f
#define UI_KEYBOARD_HEIGHT      130.0f
#define UI_KEYBOARD_RAIL_HEIGHT 18.0f
#define UI_LABEL_COLUMN_WIDTH   100.0f
#define UI_MAIN_HEADER_HEIGHT   34.0f

#define UI_LOG_HEADER_HEIGHT    40.0f
#define UI_LOG_BODY_HEIGHT      160.0f
#define UI_LOG_MIN_BODY_HEIGHT  40.0f
#define UI_LOG_MAX_BODY_HEIGHT  2000.0f
#define UI_LOG_GRIP_HEIGHT      6.0f

enum AppLogTab {
    APP_LOG_TAB_LOG = 0,
    APP_LOG_TAB_BUSSES
};

enum AppTab {
    APP_TAB_PLUGIN = 0,
    APP_TAB_MIDI,
    APP_TAB_RESAMPLE,
    APP_TAB_SETTINGS,
    APP_TAB_LICENSES
};

#define UI_MAIN_MIN_HEIGHT      120.0f
#define UI_PANEL_PADDING        8.0f
#define UI_ITEM_SPACING         4.0f
#define UI_BASE_FONT_SIZE       13.0f
#define UI_ICON_GLYPH_OFFSET_Y  2.0f

#define UI_TIMER_ID             1
#define UI_FRAME_INTERVAL_MS    16

#define WIN_BASE_DPI            96

#define UI_METER_DECAY          0.92f

#define PCM_ENCODE_16BIT 32768.0f
#define PCM_ENCODE_24BIT 8388608.0f
#define PCM_MAX_16BIT 32767
#define PCM_MAX_24BIT 8388607
#define PCM_MIN_16BIT (-32768)
#define PCM_MIN_24BIT (-8388608)
