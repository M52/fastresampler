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
#include "fastresampler_project.h"

struct PlatView;

enum AppTab {
    APP_TAB_PLUGIN = 0,
    APP_TAB_MIDI,
    APP_TAB_RESAMPLE,
    APP_TAB_SETTINGS
};

// One line of the output log: what a status message said, and the time of
// day it was written at.
struct AppLogLine {
    char text[FR_STATUS_LEN];
    char time[FR_LOG_TIME_LEN];
    bool is_error;
};

struct App {
    Settings   settings;
    char       settings_path[FR_MAX_PATH];

    // One list per format, indexed by HostPluginFormat: the VST3 modules
    // and the VST 2 instruments. The main area shows one at a time, under
    // the VST3 and VSTi tabs; settings.plugin_tab says which.
    PluginList plugins[HOST_FORMAT_COUNT];
    int        selected_plugin[HOST_FORMAT_COUNT];   // row highlighted in each list, -1 none
    bool       plugin_tab_select_request;            // the UI moves to settings.plugin_tab
    bool       log_tab_select_request;               // the UI moves to settings.log_tab
    ScanCache  vsti_cache;

    AudioDriverList drivers;
    int        selected_driver;        // index into drivers, -1 none
    AudioDevice audio;

    HostContext* host;
    HostPlugin*  plugin;               // the one loaded plugin, or null
    char         plugin_path[FR_MAX_PATH];   // where it was loaded from, for a project

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

    // One meter per output bus, so the Output busses tab shows what the
    // plugin's own routing actually sends where: play a note and the bus
    // holding that microphone moves. Written by the audio thread over every
    // bus, whichever one the speakers are on.
    std::atomic<float> bus_peak[FR_MAX_OUTPUT_BUSSES][FR_BUS_CHANNELS];
    float      bus_meter[FR_MAX_OUTPUT_BUSSES][FR_BUS_CHANNELS];

    char       status[FR_STATUS_LEN];
    bool       status_is_error;

    // The output log: every status line kept, so what a run reported stays
    // readable after the next line replaces it. A ring, oldest dropped
    // first, drawn over the keyboard as a ledger that folds away.
    AppLogLine log_lines[FR_LOG_LINES];
    int        log_count;                // lines held, up to FR_LOG_LINES
    int        log_first;                // where the oldest one sits in the ring
    int        log_written;              // lines ever added; the UI scrolls on a change
    bool       log_clear_request;

    // The disk space warning: raised when the output folder's disk cannot
    // hold the run, shown by the UI as a modal until dismissed.
    char       disk_warning[FR_STATUS_LEN];
    bool       disk_warning_open;

    // Requests raised by the UI during a frame, served after it.
    int        load_request;           // plugin list index, -1 none
    int        load_request_format;    // HostPluginFormat: the list the index is in
    bool       unload_request;
    bool       rescan_request;
    bool       audio_apply_request;
    bool       audio_refresh_request;
    bool       root_dir_pick_request;   // the folder picker for the root project folder
    bool       output_dir_pick_request; // the folder picker for the samples
    bool       export_dir_pick_request; // the folder picker for the instrument

    // Settings tab scratch: what the combos show before "Apply".
    int        pending_driver;
    int        pending_block_index;
    char       new_scan_path[FR_MAX_PATH];

    bool       editor_open;
    int        editor_width;
    int        editor_height;

    // Resampling: the script the tab built and the render in flight. What
    // each step reported goes to the status line and the output log; the
    // tab itself says nothing about it.
    ResampleScript resample_script;
    ResampleJob    resample_job;
    bool           resample_start_request;

    // The instrument build: allowed once a render finished with every sample
    // file written, and until the script is built or rendered again.
    InstrumentJob  instrument_job;
    bool           instrument_start_request;
    bool           resample_fsi_ready;
    bool           resample_full_wav_removed;   // the bank build deleted it; why fsi_ready went false

    // The saved resampling projects: the names in the Projects folder next
    // to the exe, and what the tab asked for this frame.
    ProjectList projects;
    bool        project_scan_request;
    bool        project_save_request;
    bool        project_load_request;
    char        project_load_name[FR_PROJECT_NAME_LEN];

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

// Asks for a plugin by path (the command line), a VST3 module or a VST 2
// DLL by its extension. A path not in the scanned list is added to it.
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

// The output log, oldest line first: index 0 to app->log_count - 1.
const AppLogLine* app_log_line(const App* app, int index);
void app_log_clear(App* app);

// How tall the output log's strip over the keyboard is: the bar alone when
// it is folded, the bar and the ledger when it is open. The UI lays the
// strip out and the plugin editor's child window is kept clear of it, so
// both ask here. client_height is the window's, in the same pixels the
// answer comes back in; the plugin area and the keyboard keep room of their
// own in it whatever the grip was dragged to.
float app_log_strip_height(const App* app, float client_height, float scale);
