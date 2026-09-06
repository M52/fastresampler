#pragma once
// User settings, kept in a plain "key value" text file next to the exe.

#include "fastresampler_constants.h"
#include "fastresampler_resample.h"

struct Settings {
    ResampleConfig resample;

    char scan_paths[FR_MAX_SCAN_PATHS][FR_MAX_PATH];
    int  scan_path_count;

    int  audio_api;                              // AudioApi: 0 WASAPI, 1 ASIO
    char audio_driver_name[FR_DRIVER_NAME_LEN];  // matched by name at startup
    int  block_size;

    int  midi_channel;                           // 0..15

    bool show_tips;                              // the dim lines that explain a control

    int  window_width;
    int  window_height;
};

void settings_defaults(Settings* s);
bool settings_load(Settings* s, const char* path);
bool settings_save(const Settings* s, const char* path);

// True when the path is already in the list (case-insensitive).
bool settings_has_scan_path(const Settings* s, const char* path);
bool settings_add_scan_path(Settings* s, const char* path);
void settings_remove_scan_path(Settings* s, int index);
