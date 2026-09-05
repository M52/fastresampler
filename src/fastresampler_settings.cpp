// #pragma once (removed for unity build)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fastresampler_settings.h"
#include "fastresampler_helpers.h"
#include "platform/fastresampler_platform.h"

// Largest settings file the loader reads. 16 paths plus a few numbers fit
// many times over.
#define SETTINGS_FILE_MAX_BYTES 16384

void settings_defaults(Settings* s) {
    memset(s, 0, sizeof(*s));
    plat_default_plugin_dir(s->scan_paths[0], FR_MAX_PATH);
    s->scan_path_count = s->scan_paths[0][0] ? 1 : 0;
    s->audio_api = 0;
    s->audio_driver_name[0] = '\0';
    s->block_size = FR_DEFAULT_BLOCK_SIZE;
    s->midi_channel = 0;
    s->window_width = WINDOW_DEFAULT_WIDTH;
    s->window_height = WINDOW_DEFAULT_HEIGHT;
}

bool settings_has_scan_path(const Settings* s, const char* path) {
    for (int i = 0; i < s->scan_path_count; ++i) {
        if (fr_stricmp(s->scan_paths[i], path) == 0) return true;
    }
    return false;
}

bool settings_add_scan_path(Settings* s, const char* path) {
    if (path[0] == '\0') return false;
    if (s->scan_path_count >= FR_MAX_SCAN_PATHS) return false;
    if (settings_has_scan_path(s, path)) return false;
    fr_strcpy(s->scan_paths[s->scan_path_count], FR_MAX_PATH, path);
    s->scan_path_count++;
    return true;
}

void settings_remove_scan_path(Settings* s, int index) {
    if (index < 0 || index >= s->scan_path_count) return;
    for (int i = index; i + 1 < s->scan_path_count; ++i) {
        memcpy(s->scan_paths[i], s->scan_paths[i + 1], FR_MAX_PATH);
    }
    s->scan_path_count--;
}

// A quoted value loses its quotes; anything else is taken as written, with
// trailing whitespace removed.
static void settings_unquote(char* value) {
    size_t len = strlen(value);
    while (len > 0 && (value[len - 1] == ' ' || value[len - 1] == '\t' || value[len - 1] == '\r')) {
        value[--len] = '\0';
    }
    if (len >= 2 && value[0] == '"' && value[len - 1] == '"') {
        memmove(value, value + 1, len - 2);
        value[len - 2] = '\0';
    }
}

static bool settings_valid_block_size(int v) {
    for (int i = 0; i < FR_BLOCK_SIZE_OPTION_COUNT; ++i) {
        if (g_block_size_options[i] == v) return true;
    }
    return false;
}

bool settings_load(Settings* s, const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;

    char* text = (char*)malloc(SETTINGS_FILE_MAX_BYTES);
    if (!text) {
        fclose(f);
        return false;
    }
    size_t n = fread(text, 1, SETTINGS_FILE_MAX_BYTES - 1, f);
    fclose(f);
    text[n] = '\0';

    // The file replaces the scan path list entirely, so drop the default.
    bool saw_scan_path = false;

    char* line = text;
    while (line && *line) {
        char* next = strchr(line, '\n');
        if (next) *next++ = '\0';

        // key, then a run of whitespace, then the value to the end of the line.
        char* key = line;
        while (*key == ' ' || *key == '\t') ++key;
        if (*key == '\0' || *key == '#') {
            line = next;
            continue;
        }
        char* value = key;
        while (*value && *value != ' ' && *value != '\t') ++value;
        if (*value) *value++ = '\0';
        while (*value == ' ' || *value == '\t') ++value;
        settings_unquote(value);

        if (strcmp(key, "scan_path") == 0) {
            if (!saw_scan_path) {
                s->scan_path_count = 0;
                saw_scan_path = true;
            }
            settings_add_scan_path(s, value);
        } else if (strcmp(key, "audio_api") == 0) {
            s->audio_api = fr_clampi(atoi(value), 0, 1);
        } else if (strcmp(key, "audio_driver") == 0) {
            fr_strcpy(s->audio_driver_name, FR_DRIVER_NAME_LEN, value);
        } else if (strcmp(key, "block_size") == 0) {
            int v = atoi(value);
            if (settings_valid_block_size(v)) s->block_size = v;
        } else if (strcmp(key, "midi_channel") == 0) {
            s->midi_channel = fr_clampi(atoi(value), 0, MIDI_CHANNEL_COUNT - 1);
        } else if (strcmp(key, "window_width") == 0) {
            s->window_width = fr_clampi(atoi(value), WINDOW_MIN_WIDTH, 8192);
        } else if (strcmp(key, "window_height") == 0) {
            s->window_height = fr_clampi(atoi(value), WINDOW_MIN_HEIGHT, 8192);
        }
        line = next;
    }

    free(text);
    return true;
}

bool settings_save(const Settings* s, const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "# FastResampler settings\n");
    for (int i = 0; i < s->scan_path_count; ++i) {
        fprintf(f, "scan_path \"%s\"\n", s->scan_paths[i]);
    }
    fprintf(f, "audio_api %d\n", s->audio_api);
    fprintf(f, "audio_driver \"%s\"\n", s->audio_driver_name);
    fprintf(f, "block_size %d\n", s->block_size);
    fprintf(f, "midi_channel %d\n", s->midi_channel);
    fprintf(f, "window_width %d\n", s->window_width);
    fprintf(f, "window_height %d\n", s->window_height);
    fclose(f);
    return true;
}
