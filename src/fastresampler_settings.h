#pragma once

#include "fastresampler_constants.h"
#include "fastresampler_resample.h"

struct Settings {
    ResampleConfig resample;
    char project_name[FR_PROJECT_NAME_LEN];

    char scan_paths[FR_MAX_SCAN_PATHS][FR_MAX_PATH];
    int  scan_path_count;
    int  plugin_tab;

    char banned_paths[FR_MAX_BANNED_PLUGINS][FR_MAX_PATH];
    int  banned_count;

    int  audio_api;
    char audio_driver_name[FR_DRIVER_NAME_LEN];
    int  block_size;

    int  midi_channel;

    bool show_tips;
    bool show_experimental;
    bool disclaimer_accepted;
    bool disclaimer_hidden;
    bool log_expanded;
    int  log_height;
    int  log_tab;
    int  side_tab;

    int  window_width;
    int  window_height;
};

void settings_defaults(Settings* s);
bool settings_load(Settings* s, const char* path);
bool settings_save(const Settings* s, const char* path);

bool settings_has_scan_path(const Settings* s, const char* path);
bool settings_add_scan_path(Settings* s, const char* path);
void settings_remove_scan_path(Settings* s, int index);

bool settings_is_banned(const Settings* s, const char* path);
bool settings_ban(Settings* s, const char* path);
bool settings_unban(Settings* s, const char* path);
