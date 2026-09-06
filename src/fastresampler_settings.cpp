// #pragma once (removed for unity build)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fastresampler_settings.h"
#include "fastresampler_helpers.h"
#include "platform/fastresampler_platform.h"

// Largest settings file the loader reads. 16 paths, a few numbers and the
// controller curves (about a kilobyte each) fit many times over.
#define SETTINGS_FILE_MAX_BYTES 65536

void settings_defaults(Settings* s) {
    memset(s, 0, sizeof(*s));
    plat_default_plugin_dir(s->scan_paths[0], FR_MAX_PATH);
    s->scan_path_count = s->scan_paths[0][0] ? 1 : 0;
    s->audio_api = 0;
    s->audio_driver_name[0] = '\0';
    s->block_size = FR_DEFAULT_BLOCK_SIZE;
    s->midi_channel = 0;
    s->show_tips = true;
    s->window_width = WINDOW_DEFAULT_WIDTH;
    s->window_height = WINDOW_DEFAULT_HEIGHT;
    resample_config_defaults(&s->resample);
}

// Whitespace-separated numbers from text into out, at most max_count of
// them. Returns how many were read.
static int settings_parse_floats(const char* text, float* out, int max_count) {
    int count = 0;
    const char* s = text;
    while (count < max_count) {
        char* end = nullptr;
        double v = strtod(s, &end);
        if (end == s) break;
        out[count++] = (float)v;
        s = end;
    }
    return count;
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
        } else if (strcmp(key, "resample_low") == 0) {
            fr_strcpy(s->resample.low_note, sizeof(s->resample.low_note), value);
        } else if (strcmp(key, "resample_high") == 0) {
            fr_strcpy(s->resample.high_note, sizeof(s->resample.high_note), value);
        } else if (strcmp(key, "resample_step") == 0) {
            s->resample.step = fr_clampi(atoi(value), 1, RESAMPLE_MAX_STEP);
        } else if (strcmp(key, "resample_velocities") == 0) {
            fr_strcpy(s->resample.velocities, sizeof(s->resample.velocities), value);
        } else if (strcmp(key, "resample_round_robins") == 0) {
            s->resample.round_robins = fr_clampi(atoi(value), 1, RESAMPLE_MAX_ROUND_ROBINS);
        } else if (strcmp(key, "resample_preroll") == 0) {
            s->resample.preroll = atoi(value) != 0;
        } else if (strcmp(key, "resample_preroll_gap_ms") == 0) {
            s->resample.preroll_gap_seconds = (float)fr_clampi(atoi(value), 1, 600000) / 1000.0f;
        } else if (strcmp(key, "resample_hold_ms") == 0) {
            s->resample.hold_seconds = (float)fr_clampi(atoi(value), 1, 600000) / 1000.0f;
        } else if (strcmp(key, "resample_gap_ms") == 0) {
            s->resample.gap_seconds = (float)fr_clampi(atoi(value), 0, 600000) / 1000.0f;
        } else if (strcmp(key, "resample_instrument") == 0) {
            fr_strcpy(s->resample.instrument, sizeof(s->resample.instrument), value);
        } else if (strcmp(key, "resample_articulation") == 0) {
            fr_strcpy(s->resample.articulation, sizeof(s->resample.articulation), value);
        } else if (strcmp(key, "resample_microphone") == 0) {
            fr_strcpy(s->resample.microphone, sizeof(s->resample.microphone), value);
        } else if (strcmp(key, "resample_sample_rate") == 0) {
            int v = atoi(value);
            s->resample.sample_rate = 0;
            for (int i = 0; i < RESAMPLE_RATE_OPTION_COUNT; ++i) {
                if (g_resample_rate_options[i] == v) s->resample.sample_rate = v;
            }
        } else if (strcmp(key, "resample_render_speed") == 0) {
            // A value off the list leaves the default in place.
            int v = atoi(value);
            for (int i = 0; i < RESAMPLE_SPEED_OPTION_COUNT; ++i) {
                if (g_resample_speed_options[i] == v) s->resample.render_speed = v;
            }
        } else if (strcmp(key, "resample_output_dir") == 0) {
            if (value[0]) fr_strcpy(s->resample.output_dir, sizeof(s->resample.output_dir), value);
        } else if (strcmp(key, "resample_preload_frames") == 0) {
            // A value off the list leaves the default in place.
            int v = atoi(value);
            for (int i = 0; i < RESAMPLE_PRELOAD_OPTION_COUNT; ++i) {
                if (g_resample_preload_options[i] == v) s->resample.preload_frames = v;
            }
        } else if (strcmp(key, "show_tips") == 0) {
            s->show_tips = atoi(value) != 0;
        } else if (strcmp(key, "resample_cc") == 0) {
            // "index controller bezier constant"; the nodes and values follow
            // on their own lines, so the curve starts out flat at 127.
            float nums[4];
            if (settings_parse_floats(value, nums, 4) == 4) {
                int i = (int)nums[0];
                if (i >= 0 && i < RESAMPLE_MAX_CC_CURVES) {
                    ResampleCcCurve* c = &s->resample.cc_curves[i];
                    resample_cc_set_constant(c, 1.0f);
                    c->controller = fr_clampi((int)nums[1], 0, MIDI_VALUE_MAX);
                    c->is_bezier = nums[2] != 0.0f;
                    c->constant = fr_clampi((int)nums[3], 0, MIDI_VALUE_MAX);
                    if (s->resample.cc_curve_count < i + 1) s->resample.cc_curve_count = i + 1;
                }
            }
        } else if (strcmp(key, "resample_cc_nodes") == 0) {
            // "index count", then x y curvature per node.
            float nums[2 + 3 * RESAMPLE_CC_MAX_NODES];
            int read = settings_parse_floats(value, nums, 2 + 3 * RESAMPLE_CC_MAX_NODES);
            int i = read >= 2 ? (int)nums[0] : -1;
            int count = read >= 2 ? (int)nums[1] : 0;
            if (i >= 0 && i < RESAMPLE_MAX_CC_CURVES && count >= 2 && count <= RESAMPLE_CC_MAX_NODES && read >= 2 + 3 * count) {
                ResampleCcCurve* c = &s->resample.cc_curves[i];
                c->node_count = count;
                for (int k = 0; k < count; ++k) {
                    c->node_x[k] = fr_clampf(nums[2 + 3 * k], 0.0f, 1.0f);
                    c->node_y[k] = fr_clampf(nums[3 + 3 * k], 0.0f, 1.0f);
                    c->curvature[k] = fr_clampf(nums[4 + 3 * k], 0.0f, 1.0f);
                }
            }
        } else if (strcmp(key, "resample_cc_values") == 0) {
            // "index", then every point of the curve.
            float nums[1 + RESAMPLE_CC_CURVE_POINTS];
            int read = settings_parse_floats(value, nums, 1 + RESAMPLE_CC_CURVE_POINTS);
            int i = read >= 1 ? (int)nums[0] : -1;
            if (i >= 0 && i < RESAMPLE_MAX_CC_CURVES && read == 1 + RESAMPLE_CC_CURVE_POINTS) {
                ResampleCcCurve* c = &s->resample.cc_curves[i];
                for (int k = 0; k < RESAMPLE_CC_CURVE_POINTS; ++k) c->values[k] = fr_clampf(nums[1 + k], 0.0f, 1.0f);
            }
        }
        line = next;
    }
    // A curve the file skipped over still has to be a curve.
    for (int i = 0; i < s->resample.cc_curve_count; ++i) {
        ResampleCcCurve* c = &s->resample.cc_curves[i];
        if (c->node_count < 2) resample_cc_set_constant(c, 1.0f);
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
    fprintf(f, "resample_low \"%s\"\n", s->resample.low_note);
    fprintf(f, "resample_high \"%s\"\n", s->resample.high_note);
    fprintf(f, "resample_step %d\n", s->resample.step);
    fprintf(f, "resample_velocities \"%s\"\n", s->resample.velocities);
    fprintf(f, "resample_round_robins %d\n", s->resample.round_robins);
    fprintf(f, "resample_preroll %d\n", s->resample.preroll ? 1 : 0);
    fprintf(f, "resample_preroll_gap_ms %d\n", (int)(s->resample.preroll_gap_seconds * 1000.0f + 0.5f));
    fprintf(f, "resample_hold_ms %d\n", (int)(s->resample.hold_seconds * 1000.0f + 0.5f));
    fprintf(f, "resample_gap_ms %d\n", (int)(s->resample.gap_seconds * 1000.0f + 0.5f));
    fprintf(f, "resample_instrument \"%s\"\n", s->resample.instrument);
    fprintf(f, "resample_articulation \"%s\"\n", s->resample.articulation);
    fprintf(f, "resample_microphone \"%s\"\n", s->resample.microphone);
    fprintf(f, "resample_sample_rate %d\n", s->resample.sample_rate);
    fprintf(f, "resample_render_speed %d\n", s->resample.render_speed);
    fprintf(f, "resample_output_dir \"%s\"\n", s->resample.output_dir);
    fprintf(f, "resample_preload_frames %d\n", s->resample.preload_frames);
    fprintf(f, "show_tips %d\n", s->show_tips ? 1 : 0);
    for (int i = 0; i < s->resample.cc_curve_count; ++i) {
        const ResampleCcCurve* c = &s->resample.cc_curves[i];
        fprintf(f, "resample_cc %d %d %d %d\n", i, c->controller, c->is_bezier ? 1 : 0, c->constant);
        fprintf(f, "resample_cc_nodes %d %d", i, c->node_count);
        for (int k = 0; k < c->node_count; ++k) {
            fprintf(f, " %.4f %.4f %.4f", c->node_x[k], c->node_y[k], c->curvature[k]);
        }
        fprintf(f, "\n");
        fprintf(f, "resample_cc_values %d", i);
        for (int k = 0; k < RESAMPLE_CC_CURVE_POINTS; ++k) fprintf(f, " %.4f", c->values[k]);
        fprintf(f, "\n");
    }
    fclose(f);
    return true;
}
