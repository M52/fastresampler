// #pragma once (removed for unity build)
#include <stdio.h>
#include <string.h>
#include "fastresampler_project.h"
#include "fastresampler_yaml.h"
#include "fastresampler_helpers.h"
#include "fastresampler_log.h"
#include "platform/fastresampler_platform.h"

// The version of the file format. It goes in every file so a later reader
// can tell what it has.
#define PROJECT_FILE_VERSION 1

// Times are written in milliseconds, the unit the sliders read in.
#define PROJECT_MAX_TIME_MS 600000

void project_dir(char* out, size_t out_len) {
    char module_dir[FR_MAX_PATH];
    plat_module_dir(module_dir, sizeof(module_dir));
    plat_path_join(out, out_len, module_dir, FR_PROJECTS_DIR);
}

bool project_clean_name(const char* name, char* out, size_t out_len) {
    size_t o = 0;
    const char* s = name;
    while (*s == ' ' || *s == '\t' || *s == '.') ++s;
    for (; *s && o + 1 < out_len; ++s) {
        char c = *s;
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|' || c == '\t') c = '-';
        out[o++] = c;
    }
    while (o > 0 && (out[o - 1] == ' ' || out[o - 1] == '.')) --o;
    out[o] = '\0';
    return o > 0;
}

bool project_path(const char* name, char* out, size_t out_len) {
    char clean[FR_PROJECT_NAME_LEN];
    if (!project_clean_name(name, clean, sizeof(clean))) return false;
    char file[FR_PROJECT_NAME_LEN + 8];
    snprintf(file, sizeof(file), "%s%s", clean, FR_PROJECT_EXT);
    char dir[FR_MAX_PATH];
    project_dir(dir, sizeof(dir));
    plat_path_join(out, out_len, dir, file);
    return true;
}

void project_scan(ProjectList* list) {
    list->count = 0;
    char dir[FR_MAX_PATH];
    project_dir(dir, sizeof(dir));

    PlatDirIter it;
    if (!plat_dir_open(&it, dir)) return;
    while (plat_dir_next(&it)) {
        if (it.is_dir) continue;
        if (!plat_path_has_ext(it.name, FR_PROJECT_EXT)) continue;
        if (list->count >= FR_MAX_PROJECTS) break;

        char name[FR_PROJECT_NAME_LEN];
        fr_strcpy(name, sizeof(name), it.name);
        size_t len = strlen(name);
        size_t ext_len = strlen(FR_PROJECT_EXT);
        if (len > ext_len) name[len - ext_len] = '\0';
        if (!name[0]) continue;

        // In place, by name: the list is short and the combo shows it sorted.
        int i = list->count;
        while (i > 0 && fr_stricmp(list->names[i - 1], name) > 0) {
            memcpy(list->names[i], list->names[i - 1], sizeof(list->names[0]));
            --i;
        }
        fr_strcpy(list->names[i], sizeof(list->names[0]), name);
        list->count++;
    }
    plat_dir_close(&it);
}

// --- Writing -------------------------------------------------------------------

// A quoted value. The reader takes the outer quotes off and everything
// between them as written, so a path keeps its separators.
static void project_write_string(FILE* f, const char* key, const char* value) {
    fprintf(f, "%s: \"%s\"\n", key, value);
}

static int project_ms(float seconds) {
    return (int)(seconds * 1000.0f + 0.5f);
}

bool project_save(const ResampleConfig* cfg, const char* name, char* err, size_t err_len) {
    char path[FR_MAX_PATH];
    if (!project_path(name, path, sizeof(path))) {
        fr_strcpy(err, err_len, "Give the project a name.");
        return false;
    }
    char dir[FR_MAX_PATH];
    project_dir(dir, sizeof(dir));
    if (!plat_dir_create(dir)) {
        snprintf(err, err_len, "The folder %s could not be made.", dir);
        return false;
    }

    FILE* f = fopen(path, "wb");
    if (!f) {
        snprintf(err, err_len, "%s could not be written.", path);
        return false;
    }

    fprintf(f, "# FastResampler resampling project\n");
    fprintf(f, "version: %d\n", PROJECT_FILE_VERSION);
    project_write_string(f, "instrument", cfg->instrument);
    project_write_string(f, "articulation", cfg->articulation);
    project_write_string(f, "microphone", cfg->microphone);
    project_write_string(f, "low_note", cfg->low_note);
    project_write_string(f, "high_note", cfg->high_note);
    fprintf(f, "step: %d\n", cfg->step);
    project_write_string(f, "velocities", cfg->velocities);
    fprintf(f, "round_robins: %d\n", cfg->round_robins);
    fprintf(f, "preroll: %s\n", cfg->preroll ? "true" : "false");
    fprintf(f, "preroll_gap_ms: %d\n", project_ms(cfg->preroll_gap_seconds));
    fprintf(f, "hold_ms: %d\n", project_ms(cfg->hold_seconds));
    fprintf(f, "gap_ms: %d\n", project_ms(cfg->gap_seconds));
    fprintf(f, "sample_rate: %d\n", cfg->sample_rate);
    fprintf(f, "render_speed: %d\n", cfg->render_speed);
    fprintf(f, "delete_full_wav: %s\n", cfg->delete_full_wav ? "true" : "false");
    project_write_string(f, "output_dir", cfg->output_dir);
    project_write_string(f, "export_dir", cfg->export_dir);
    fprintf(f, "preload_frames: %d\n", cfg->preload_frames);

    fprintf(f, "cc_curves:\n");
    for (int i = 0; i < cfg->cc_curve_count; ++i) {
        const ResampleCcCurve* c = &cfg->cc_curves[i];
        fprintf(f, "  - controller: %d\n", c->controller);
        fprintf(f, "    bezier: %s\n", c->is_bezier ? "true" : "false");
        fprintf(f, "    constant: %d\n", c->constant);
        // The nodes are x, y and the curvature of the segment after them;
        // the values are what plays. Both are one line of numbers.
        fprintf(f, "    node_count: %d\n", c->node_count);
        fprintf(f, "    nodes: \"");
        for (int k = 0; k < c->node_count; ++k) {
            fprintf(f, "%s%.4f %.4f %.4f", k > 0 ? " " : "", c->node_x[k], c->node_y[k], c->curvature[k]);
        }
        fprintf(f, "\"\n");
        fprintf(f, "    values: \"");
        for (int k = 0; k < RESAMPLE_CC_CURVE_POINTS; ++k) {
            fprintf(f, "%s%.4f", k > 0 ? " " : "", c->values[k]);
        }
        fprintf(f, "\"\n");
    }

    fclose(f);
    fr_log("project: saved %s", path);
    return true;
}

// --- Reading -------------------------------------------------------------------

// A time in milliseconds from the file, in seconds and inside its range.
static float project_seconds(YamlNode* root, const char* key, float fallback, int min_ms) {
    YamlNode* child = yaml_find_child(root, key);
    if (!child || child->type != YAML_SCALAR) return fallback;
    return (float)fr_clampi(yaml_get_int(root, key, 0), min_ms, PROJECT_MAX_TIME_MS) / 1000.0f;
}

// One curve of the cc_curves sequence. It starts out flat at 127, so a file
// that holds only a controller still gives a curve that plays.
static void project_read_curve(YamlNode* item, ResampleCcCurve* c) {
    resample_cc_set_constant(c, 1.0f);
    c->controller = fr_clampi(yaml_get_int(item, "controller", 1), 0, MIDI_VALUE_MAX);
    c->is_bezier = yaml_get_bool(item, "bezier", true);
    c->constant = fr_clampi(yaml_get_int(item, "constant", MIDI_VALUE_MAX), 0, MIDI_VALUE_MAX);

    float nums[3 * RESAMPLE_CC_MAX_NODES];
    int count = fr_clampi(yaml_get_int(item, "node_count", 0), 0, RESAMPLE_CC_MAX_NODES);
    int read = fr_parse_floats(yaml_get_string(item, "nodes", ""), nums, 3 * RESAMPLE_CC_MAX_NODES);
    if (count >= 2 && read >= 3 * count) {
        c->node_count = count;
        for (int k = 0; k < count; ++k) {
            c->node_x[k] = fr_clampf(nums[3 * k], 0.0f, 1.0f);
            c->node_y[k] = fr_clampf(nums[3 * k + 1], 0.0f, 1.0f);
            c->curvature[k] = fr_clampf(nums[3 * k + 2], 0.0f, 1.0f);
        }
    }

    float values[RESAMPLE_CC_CURVE_POINTS];
    if (fr_parse_floats(yaml_get_string(item, "values", ""), values, RESAMPLE_CC_CURVE_POINTS) == RESAMPLE_CC_CURVE_POINTS) {
        for (int k = 0; k < RESAMPLE_CC_CURVE_POINTS; ++k) c->values[k] = fr_clampf(values[k], 0.0f, 1.0f);
    }
}

bool project_load(ResampleConfig* cfg, const char* name, char* err, size_t err_len) {
    char path[FR_MAX_PATH];
    if (!project_path(name, path, sizeof(path))) {
        fr_strcpy(err, err_len, "Choose a project.");
        return false;
    }

    YamlFile file;
    if (!yaml_load_file(path, &file)) {
        snprintf(err, err_len, "%s could not be read.", path);
        return false;
    }
    YamlNode* root = file.root;

    // The file replaces the whole configuration: what it does not hold gets
    // the default, not what was on the tab before.
    ResampleConfig out;
    resample_config_defaults(&out);

    fr_strcpy(out.instrument, sizeof(out.instrument), yaml_get_string(root, "instrument", out.instrument));
    fr_strcpy(out.articulation, sizeof(out.articulation), yaml_get_string(root, "articulation", out.articulation));
    fr_strcpy(out.microphone, sizeof(out.microphone), yaml_get_string(root, "microphone", out.microphone));
    fr_strcpy(out.low_note, sizeof(out.low_note), yaml_get_string(root, "low_note", out.low_note));
    fr_strcpy(out.high_note, sizeof(out.high_note), yaml_get_string(root, "high_note", out.high_note));
    out.step = fr_clampi(yaml_get_int(root, "step", out.step), 1, RESAMPLE_MAX_STEP);
    fr_strcpy(out.velocities, sizeof(out.velocities), yaml_get_string(root, "velocities", out.velocities));
    out.round_robins = fr_clampi(yaml_get_int(root, "round_robins", out.round_robins), 1, RESAMPLE_MAX_ROUND_ROBINS);
    out.preroll = yaml_get_bool(root, "preroll", out.preroll);
    out.preroll_gap_seconds = project_seconds(root, "preroll_gap_ms", out.preroll_gap_seconds, 1);
    out.hold_seconds = project_seconds(root, "hold_ms", out.hold_seconds, 1);
    out.gap_seconds = project_seconds(root, "gap_ms", out.gap_seconds, 0);
    out.sample_rate = resample_valid_sample_rate(yaml_get_int(root, "sample_rate", out.sample_rate));
    out.render_speed = resample_valid_render_speed(yaml_get_int(root, "render_speed", out.render_speed), out.render_speed);
    out.delete_full_wav = yaml_get_bool(root, "delete_full_wav", out.delete_full_wav);
    fr_strcpy(out.output_dir, sizeof(out.output_dir), yaml_get_string(root, "output_dir", out.output_dir));
    fr_strcpy(out.export_dir, sizeof(out.export_dir), yaml_get_string(root, "export_dir", out.export_dir));
    out.preload_frames = resample_valid_preload_frames(yaml_get_int(root, "preload_frames", out.preload_frames), out.preload_frames);

    YamlNode* curves = yaml_find_child(root, "cc_curves");
    if (curves && curves->type == YAML_SEQUENCE) {
        for (YamlNode* item = curves->first_child; item; item = item->next_sibling) {
            if (item->type != YAML_MAPPING) continue;
            if (out.cc_curve_count >= RESAMPLE_MAX_CC_CURVES) break;
            project_read_curve(item, &out.cc_curves[out.cc_curve_count]);
            out.cc_curve_count++;
        }
    }

    yaml_file_release(&file);
    *cfg = out;
    fr_log("project: loaded %s", path);
    return true;
}
