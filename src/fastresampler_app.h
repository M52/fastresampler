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

struct PlatView;

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

    std::atomic<float> peak[FR_OUTPUT_CHANNELS];   // written by the audio thread
    float      meter[FR_OUTPUT_CHANNELS];          // decayed copy the UI draws

    char       status[FR_STATUS_LEN];
    bool       status_is_error;

    // Requests raised by the UI during a frame, served after it.
    int        load_request;           // plugin list index, -1 none
    bool       unload_request;
    bool       rescan_request;
    bool       audio_apply_request;
    bool       audio_refresh_request;

    // Settings tab scratch: what the combos show before "Apply".
    int        pending_driver;
    int        pending_block_index;
    char       new_scan_path[FR_MAX_PATH];

    bool       editor_open;
    int        editor_width;
    int        editor_height;

    PlatView*  view;
};

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
