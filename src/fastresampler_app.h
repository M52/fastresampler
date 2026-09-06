#pragma once
// The application state: settings, the plugin list, the audio device, the
// loaded plugin, and what the UI asked for this frame. One instance, owned by
// the platform entry point.

#include <atomic>
#include "fastresampler_constants.h"
#include "fastresampler_settings.h"
#include "fastresampler_scan.h"
#include "fastresampler_audio.h"
#include "fastresampler_host.h"
#include "fastresampler_midi.h"
#include "fastresampler_resample.h"
#include "fastresampler_instrument.h"

struct PlatView;

enum AppTab {
    APP_TAB_PLUGIN = 0,
    APP_TAB_MIDI,
    APP_TAB_RESAMPLE,
    APP_TAB_SETTINGS
};

struct App {
    Settings   settings;
    char       settings_path[FR_MAX_PATH];

    PluginList plugins;
    int        selected_plugin;        // row highlighted in the list, -1 none

    AudioDriverList drivers;
    int        selected_driver;        // index into drivers, -1 none
    AudioDevice audio;

    HostContext* host;
    HostPlugin*  plugin;               // the one loaded plugin, or null

    // Handshake with the audio thread. The audio callback raises audio_busy
    // around its work and only touches the plugin while plugin_active is set;
    // the UI thread clears plugin_active and waits for audio_busy to drop
    // before it unloads or reconfigures.
    std::atomic<bool> plugin_active;
    std::atomic<int>  audio_busy;

    MidiQueue  midi;
    bool       note_held[MIDI_NOTE_COUNT];   // UI-side mirror of what was sent
    int        keyboard_active_note;         // the key under a pressed mouse, -1 none
    bool       keyboard_dragging;            // the current mouse press began on a key
    int        cc_value[MIDI_NOTE_COUNT];    // last value sent per controller
    int        pitch_bend;                   // 0..16383, centre 8192
    int        last_note;                    // the keyboard's readout, -1 before any
    int        last_velocity;

    // What the side panel shows, and the note range the Resample tab holds,
    // parsed each frame so the keyboard can mark the notes it will sample.
    int        active_tab;                   // AppTab
    bool       range_valid;
    int        range_low;
    int        range_high;
    int        range_step;

    std::atomic<float> peak[FR_OUTPUT_CHANNELS];   // written by the audio thread
    float      meter[FR_OUTPUT_CHANNELS];          // decayed copy the UI draws

    char       status[FR_STATUS_LEN];
    bool       status_is_error;

    // The disk space warning: raised when the output folder's disk cannot
    // hold the run, shown by the UI as a modal until dismissed.
    char       disk_warning[FR_STATUS_LEN];
    bool       disk_warning_open;

    // Requests raised by the UI during a frame, served after it.
    int        load_request;           // plugin list index, -1 none
    bool       unload_request;
    bool       rescan_request;
    bool       audio_apply_request;
    bool       audio_refresh_request;
    bool       output_dir_pick_request; // the folder picker for the samples
    bool       export_dir_pick_request; // the folder picker for the instrument

    // Settings tab scratch: what the combos show before "Apply".
    int        pending_driver;
    int        pending_block_index;
    char       new_scan_path[FR_MAX_PATH];

    bool       editor_open;
    int        editor_width;
    int        editor_height;

    // Resampling: the script the tab built, the render in flight, and what
    // the tab shows about the last build and the last render.
    ResampleScript resample_script;
    ResampleJob    resample_job;
    bool           resample_start_request;
    char           resample_build_text[FR_STATUS_LEN];
    bool           resample_build_is_error;
    char           resample_result_text[FR_STATUS_LEN];
    bool           resample_result_is_error;

    // The instrument build: allowed once a render finished with every sample
    // file written, and until the script is built or rendered again.
    InstrumentJob  instrument_job;
    bool           instrument_start_request;
    bool           resample_fsi_ready;
    char           instrument_result_text[FR_STATUS_LEN];
    bool           instrument_result_is_error;

    PlatView*  view;
};

// The Resample tab's buttons. Build parses the tab's settings into the
// script; the render and instrument requests are served by
// app_process_requests().
void app_resample_build(App* app);
bool app_resample_running(const App* app);
bool app_instrument_running(const App* app);

void app_init(App* app);
void app_shutdown(App* app);

// After the window exists: open the audio device from the settings.
void app_start_audio(App* app);

// UI thread, once per frame, outside the ImGui frame.
void app_process_requests(App* app);

// Asks for a plugin by path (the command line). A path not in the scanned
// list is added to it.
void app_request_load_path(App* app, const char* path);

// The audio device's render callback.
void app_audio_render(void* user, float* const* outputs, int channel_count, int frames);

// MIDI from the UI. Channel comes from the settings.
void app_note_on(App* app, int note, float velocity);
void app_note_off(App* app, int note);
void app_control_change(App* app, int controller, int value);
void app_pitch_bend(App* app, int value);
void app_all_notes_off(App* app);

void app_set_status(App* app, bool is_error, const char* fmt, ...);
