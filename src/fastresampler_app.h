#pragma once

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

struct AppLogLine {
    char text[FR_STATUS_LEN];
    char time[FR_LOG_TIME_LEN];
    bool is_error;
};

struct App {
    Settings   settings;
    char       settings_path[FR_MAX_PATH];

    PluginList plugins[HOST_FORMAT_COUNT];
    int        selected_plugin[HOST_FORMAT_COUNT];
    bool       plugin_tab_select_request;
    bool       log_tab_select_request;
    ScanCache  vsti_cache;
    ScanJob    scan;
    char       pending_load_path[FR_MAX_PATH];

    AudioDriverList drivers;
    int        selected_driver;
    AudioDevice audio;

    HostContext* host;
    HostPlugin*  plugin;
    char         plugin_path[FR_MAX_PATH];

    std::atomic<bool> plugin_active;
    std::atomic<int>  audio_busy;

    MidiQueue  midi;
    bool       note_held[MIDI_NOTE_COUNT];
    int        keyboard_active_note;
    bool       keyboard_dragging;
    int        cc_value[MIDI_NOTE_COUNT];
    int        pitch_bend;
    int        last_note;
    int        last_velocity;

    int        active_tab;
    bool       range_valid;
    int        range_low;
    int        range_high;
    int        range_step;

    std::atomic<float> peak[FR_OUTPUT_CHANNELS];
    float      meter[FR_OUTPUT_CHANNELS];

    std::atomic<float> bus_peak[FR_MAX_OUTPUT_BUSSES][FR_BUS_CHANNELS];
    float      bus_meter[FR_MAX_OUTPUT_BUSSES][FR_BUS_CHANNELS];

    char       status[FR_STATUS_LEN];
    bool       status_is_error;

    AppLogLine log_lines[FR_LOG_LINES];
    int        log_count;
    int        log_first;
    int        log_written;
    bool       log_clear_request;

    char       disk_warning[FR_STATUS_LEN];
    bool       disk_warning_open;
    bool       disclaimer_open_request;
    bool       modal_open;
    int        cc_last_controller;

    int        load_request;
    int        load_request_format;
    bool       unload_request;
    bool       rescan_request;
    bool       audio_apply_request;
    bool       audio_refresh_request;
    bool       root_dir_pick_request;
    bool       output_dir_pick_request;
    bool       export_dir_pick_request;

    int        pending_driver;
    int        pending_block_index;
    char       new_scan_path[FR_MAX_PATH];

    bool       editor_open;
    int        editor_width;
    int        editor_height;

    ResampleScript resample_script;
    ResampleJob    resample_job;
    bool           resample_start_request;

    FSBProvenanceFields render_provenance{};
    InstrumentJob  instrument_job;
    bool           instrument_start_request;
    bool           resample_fsi_ready;
    bool           resample_full_wav_removed;

    ProjectBrowser project_browser;
    bool           project_browse_request;
    char           project_browse_dir[FR_MAX_PATH];
    bool           project_save_request;
    bool           project_load_request;
    char           project_load_path[FR_MAX_PATH];

    bool       side_tab_select_request;
    bool       started;
    bool       start_request;
    bool       exit_request;

    PlatView*  view;
};

void app_resample_build(App* app);
bool app_legato_enabled(const App* app);
int  app_next_cc_controller(const App* app);
bool app_resample_running(const App* app);
bool app_instrument_running(const App* app);

void app_init(App* app);
void app_shutdown(App* app);

void app_process_requests(App* app);

void app_request_load_path(App* app, const char* path);

void app_audio_render(void* user, float* const* outputs, int channel_count, int frames);

void app_note_on(App* app, int note, float velocity);
void app_note_off(App* app, int note);
void app_control_change(App* app, int controller, int value);
void app_pitch_bend(App* app, int value);
void app_all_notes_off(App* app);

void app_set_status(App* app, bool is_error, const char* fmt, ...);
void app_disclaimer_accept(App* app, bool hide);
void app_disclaimer_reject(App* app);

const AppLogLine* app_log_line(const App* app, int index);
void app_log_clear(App* app);

float app_log_strip_height(const App* app, float client_height, float scale);
