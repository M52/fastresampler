// #pragma once (removed for unity build)
#include <stdio.h>
#include <string.h>
#include "fastresampler_project.h"
#include "fastresampler_yaml.h"
#include "fastresampler_helpers.h"
#include "fastresampler_log.h"
#include "platform/fastresampler_platform.h"

// The version of the file format. It goes in every file so a later reader
// can tell what it has. Version 2 names the plugin the project was made
// with and the file its state is in; a version 1 file holds neither and
// loads with no plugin, the way it always did.
#define PROJECT_FILE_VERSION 2

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

// The project's own file, or the one beside it the extension names.
static bool project_path_with_ext(const char* name, const char* ext, char* out, size_t out_len) {
    char clean[FR_PROJECT_NAME_LEN];
    if (!project_clean_name(name, clean, sizeof(clean))) return false;
    char file[FR_PROJECT_NAME_LEN + 16];
    snprintf(file, sizeof(file), "%s%s", clean, ext);
    char dir[FR_MAX_PATH];
    project_dir(dir, sizeof(dir));
    plat_path_join(out, out_len, dir, file);
    return true;
}

bool project_path(const char* name, char* out, size_t out_len) {
    return project_path_with_ext(name, FR_PROJECT_EXT, out, out_len);
}

bool project_state_path(const char* name, char* out, size_t out_len) {
    return project_path_with_ext(name, FR_PROJECT_STATE_EXT, out, out_len);
}

// --- The plugin's state file ---------------------------------------------------

void project_plugin_init(ProjectPlugin* p) {
    p->present = false;
    p->format = 0;
    p->path[0] = '\0';
    p->name[0] = '\0';
    state_buffer_init(&p->state);
}

void project_plugin_release(ProjectPlugin* p) {
    state_buffer_release(&p->state);
    p->present = false;
}

// The block to <name>.fstate. The name of the file is what goes in the YAML.
static bool project_write_state(const ProjectPlugin* plugin, const char* name, char* state_file,
                                size_t state_file_len) {
    state_file[0] = '\0';
    char path[FR_MAX_PATH];
    if (!project_state_path(name, path, sizeof(path))) return false;
    // Nothing to keep: an older state of this project would be read back
    // into the wrong plugin, so it goes.
    if (!plugin->present || plugin->state.size == 0) {
        if (plat_path_exists(path)) plat_file_delete(path);
        return false;
    }
    FILE* f = fopen(path, "wb");
    if (!f) {
        fr_log("project: %s could not be written", path);
        return false;
    }
    size_t written = fwrite(plugin->state.data, 1, plugin->state.size, f);
    fclose(f);
    if (written != plugin->state.size) {
        fr_log("project: only %d of %d bytes of state reached %s",
               (int)written, (int)plugin->state.size, path);
        plat_file_delete(path);
        return false;
    }
    fr_strcpy(state_file, state_file_len, plat_path_basename(path));
    fr_log("project: wrote %d bytes of plugin state to %s", (int)plugin->state.size, path);
    return true;
}

// The block back out of <name>.fstate. A missing file is not an error: the
// project then comes back with the plugin loaded but its own settings.
static void project_read_state(ProjectPlugin* plugin, const char* name) {
    char path[FR_MAX_PATH];
    if (!project_state_path(name, path, sizeof(path))) return;
    uint64_t size = 0;
    uint64_t mtime = 0;
    if (!plat_file_stat(path, &size, &mtime)) return;
    if (size == 0 || size > FR_MAX_PLUGIN_STATE) {
        fr_log("project: %s holds %llu bytes, which is not a plugin state", path,
               (unsigned long long)size);
        return;
    }
    FILE* f = fopen(path, "rb");
    if (!f) {
        fr_log("project: %s could not be read", path);
        return;
    }
    if (!state_buffer_reserve(&plugin->state, (size_t)size)) {
        fclose(f);
        fr_log("project: no memory for the %llu bytes of state in %s", (unsigned long long)size, path);
        return;
    }
    size_t read = fread(plugin->state.data, 1, (size_t)size, f);
    fclose(f);
    plugin->state.size = read;
    if (read != (size_t)size || !state_read_header(plugin->state.data, plugin->state.size, NULL, NULL)) {
        fr_log("project: %s is not a plugin state this program wrote", path);
        plugin->state.size = 0;
        return;
    }
    fr_log("project: read %d bytes of plugin state from %s", (int)plugin->state.size, path);
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

bool project_save(const ResampleConfig* cfg, const ProjectPlugin* plugin, const char* name,
                  char* err, size_t err_len) {
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

    // The plugin's state file is written first: the YAML names it, and a
    // name in there that points at nothing would be read back as a state
    // that failed to load.
    char state_file[FR_PROJECT_NAME_LEN + 16];
    project_write_state(plugin, name, state_file, sizeof(state_file));

    fprintf(f, "# FastResampler resampling project\n");
    fprintf(f, "version: %d\n", PROJECT_FILE_VERSION);
    // The plugin the project was made with, so a load brings back the same
    // one in the same state. The state itself is in the file named here.
    if (plugin->present) {
        fprintf(f, "plugin:\n");
        fprintf(f, "  format: %d\n", plugin->format);
        fprintf(f, "  name: \"%s\"\n", plugin->name);
        fprintf(f, "  path: \"%s\"\n", plugin->path);
        fprintf(f, "  state: \"%s\"\n", state_file);
        fprintf(f, "  state_bytes: %d\n", state_file[0] ? (int)plugin->state.size : 0);
    }
    project_write_string(f, "instrument", cfg->instrument);
    project_write_string(f, "articulation", cfg->articulation);
    // The output busses, one per microphone. A file that holds none is read
    // as the one bus a fresh project has.
    fprintf(f, "monitor_bus: %d\n", cfg->monitor_bus);
    fprintf(f, "busses:\n");
    for (int i = 0; i < cfg->bus_count; ++i) {
        fprintf(f, "  - name: \"%s\"\n", cfg->busses[i].name);
        fprintf(f, "    channel: %d\n", cfg->busses[i].channel);
    }
    project_write_string(f, "low_note", cfg->low_note);
    project_write_string(f, "high_note", cfg->high_note);
    fprintf(f, "step: %d\n", cfg->step);
    project_write_string(f, "velocities", cfg->velocities);
    fprintf(f, "round_robins: %d\n", cfg->round_robins);
    fprintf(f, "dynamic_layers: %s\n", cfg->dynamic_layers ? "true" : "false");
    fprintf(f, "preroll: %s\n", cfg->preroll ? "true" : "false");
    fprintf(f, "preroll_gap_ms: %d\n", project_ms(cfg->preroll_gap_seconds));
    fprintf(f, "hold_ms: %d\n", project_ms(cfg->hold_seconds));
    fprintf(f, "gap_ms: %d\n", project_ms(cfg->gap_seconds));
    fprintf(f, "legato: %s\n", cfg->legato ? "true" : "false");
    fprintf(f, "legato_step: %d\n", cfg->legato_step);
    project_write_string(f, "legato_intervals", cfg->legato_intervals);
    project_write_string(f, "legato_velocities", cfg->legato_velocities);
    project_write_string(f, "legato_arrivals", cfg->legato_arrivals);
    fprintf(f, "legato_detect_arrival: %s\n", cfg->legato_detect_arrival ? "true" : "false");
    fprintf(f, "legato_source_hold_ms: %d\n", project_ms(cfg->legato_source_hold_seconds));
    fprintf(f, "legato_overlap_ms: %d\n", project_ms(cfg->legato_overlap_seconds));
    fprintf(f, "legato_hold_ms: %d\n", project_ms(cfg->legato_hold_seconds));
    fprintf(f, "legato_margin_ms: %d\n", project_ms(cfg->legato_margin_seconds));
    fprintf(f, "sample_rate: %d\n", cfg->sample_rate);
    fprintf(f, "render_speed: %d\n", cfg->render_speed);
    fprintf(f, "split_samples: %s\n", cfg->split_samples ? "true" : "false");
    fprintf(f, "export_rzc: %s\n", cfg->export_rzc ? "true" : "false");
    fprintf(f, "delete_full_wav: %s\n", cfg->delete_full_wav ? "true" : "false");
    project_write_string(f, "root_dir", cfg->root_dir);
    project_write_string(f, "output_dir", cfg->output_dir);
    fprintf(f, "output_dir_override: %s\n", cfg->output_dir_override ? "true" : "false");
    project_write_string(f, "export_dir", cfg->export_dir);
    fprintf(f, "export_dir_override: %s\n", cfg->export_dir_override ? "true" : "false");
    fprintf(f, "preload_frames: %d\n", cfg->preload_frames);
    fprintf(f, "bit_depth: %d\n", cfg->bit_depth);

    fprintf(f, "cc_curves:\n");
    for (int i = 0; i < cfg->cc_curve_count; ++i) {
        const ResampleCcCurve* c = &cfg->cc_curves[i];
        fprintf(f, "  - controller: %d\n", c->controller);
        fprintf(f, "    layer: %d\n", c->layer);
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
    c->layer = fr_clampi(yaml_get_int(item, "layer", 0), 0, RESAMPLE_MAX_ROUND_ROBINS);
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

bool project_load(ResampleConfig* cfg, ProjectPlugin* plugin, const char* name,
                  char* err, size_t err_len) {
    project_plugin_release(plugin);
    project_plugin_init(plugin);
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
    // The output busses. A project written before they existed names one
    // microphone instead: it becomes the first bus, so the run keeps the
    // folder and the file names it had.
    fr_strcpy(out.busses[0].name, sizeof(out.busses[0].name),
              yaml_get_string(root, "microphone", out.busses[0].name));
    YamlNode* busses = yaml_find_child(root, "busses");
    if (busses && busses->type == YAML_SEQUENCE) {
        out.bus_count = 0;
        for (YamlNode* item = busses->first_child; item; item = item->next_sibling) {
            if (item->type != YAML_MAPPING) continue;
            if (out.bus_count >= FR_MAX_OUTPUT_BUSSES) break;
            ResampleBus* bus = &out.busses[out.bus_count];
            fr_strcpy(bus->name, sizeof(bus->name), yaml_get_string(item, "name", ""));
            bus->channel = yaml_get_int(item, "channel", FR_BUS_CHANNELS * out.bus_count);
            out.bus_count++;
        }
    }
    out.monitor_bus = yaml_get_int(root, "monitor_bus", 0);
    resample_bus_validate(&out);
    fr_strcpy(out.low_note, sizeof(out.low_note), yaml_get_string(root, "low_note", out.low_note));
    fr_strcpy(out.high_note, sizeof(out.high_note), yaml_get_string(root, "high_note", out.high_note));
    out.step = fr_clampi(yaml_get_int(root, "step", out.step), 1, RESAMPLE_MAX_STEP);
    fr_strcpy(out.velocities, sizeof(out.velocities), yaml_get_string(root, "velocities", out.velocities));
    out.round_robins = fr_clampi(yaml_get_int(root, "round_robins", out.round_robins), 1, RESAMPLE_MAX_ROUND_ROBINS);
    out.dynamic_layers = yaml_get_bool(root, "dynamic_layers", out.dynamic_layers);
    out.preroll = yaml_get_bool(root, "preroll", out.preroll);
    out.preroll_gap_seconds = project_seconds(root, "preroll_gap_ms", out.preroll_gap_seconds, 1);
    out.hold_seconds = project_seconds(root, "hold_ms", out.hold_seconds, 1);
    out.gap_seconds = project_seconds(root, "gap_ms", out.gap_seconds, 0);
    out.legato = yaml_get_bool(root, "legato", out.legato);
    out.legato_step = fr_clampi(yaml_get_int(root, "legato_step", out.legato_step), 1, RESAMPLE_MAX_STEP);
    fr_strcpy(out.legato_intervals, sizeof(out.legato_intervals), yaml_get_string(root, "legato_intervals", out.legato_intervals));
    fr_strcpy(out.legato_velocities, sizeof(out.legato_velocities), yaml_get_string(root, "legato_velocities", out.legato_velocities));
    fr_strcpy(out.legato_arrivals, sizeof(out.legato_arrivals), yaml_get_string(root, "legato_arrivals", out.legato_arrivals));
    out.legato_detect_arrival = yaml_get_bool(root, "legato_detect_arrival", out.legato_detect_arrival);
    out.legato_source_hold_seconds = project_seconds(root, "legato_source_hold_ms", out.legato_source_hold_seconds, 1);
    out.legato_overlap_seconds = project_seconds(root, "legato_overlap_ms", out.legato_overlap_seconds, 0);
    out.legato_hold_seconds = project_seconds(root, "legato_hold_ms", out.legato_hold_seconds, 1);
    out.legato_margin_seconds = project_seconds(root, "legato_margin_ms", out.legato_margin_seconds, 1);
    out.sample_rate = resample_valid_sample_rate(yaml_get_int(root, "sample_rate", out.sample_rate));
    out.render_speed = resample_valid_render_speed(yaml_get_int(root, "render_speed", out.render_speed), out.render_speed);
    out.split_samples = yaml_get_bool(root, "split_samples", out.split_samples);
    out.export_rzc = yaml_get_bool(root, "export_rzc", out.export_rzc);
    out.delete_full_wav = yaml_get_bool(root, "delete_full_wav", out.delete_full_wav);
    fr_strcpy(out.root_dir, sizeof(out.root_dir), yaml_get_string(root, "root_dir", out.root_dir));
    fr_strcpy(out.output_dir, sizeof(out.output_dir), yaml_get_string(root, "output_dir", out.output_dir));
    fr_strcpy(out.export_dir, sizeof(out.export_dir), yaml_get_string(root, "export_dir", out.export_dir));
    // A project from before the root project folder existed names the two
    // folders it wrote to and no root; the root is read back off them.
    if (yaml_find_child(root, "root_dir")) {
        out.output_dir_override = yaml_get_bool(root, "output_dir_override", out.output_dir_override);
        out.export_dir_override = yaml_get_bool(root, "export_dir_override", out.export_dir_override);
    } else {
        resample_config_root_from_dirs(&out);
    }
    out.preload_frames = resample_valid_preload_frames(yaml_get_int(root, "preload_frames", out.preload_frames), out.preload_frames);
    out.bit_depth = resample_valid_bit_depth(yaml_get_int(root, "bit_depth", out.bit_depth), out.bit_depth);

    YamlNode* curves = yaml_find_child(root, "cc_curves");
    if (curves && curves->type == YAML_SEQUENCE) {
        for (YamlNode* item = curves->first_child; item; item = item->next_sibling) {
            if (item->type != YAML_MAPPING) continue;
            if (out.cc_curve_count >= RESAMPLE_MAX_CC_CURVES) break;
            project_read_curve(item, &out.cc_curves[out.cc_curve_count]);
            out.cc_curve_count++;
        }
    }

    // The plugin the project was made with. A project from before plugins
    // were saved names none, and then the one already loaded is left alone.
    YamlNode* plugin_node = yaml_find_child(root, "plugin");
    if (plugin_node && plugin_node->type == YAML_MAPPING) {
        const char* plugin_path = yaml_get_string(plugin_node, "path", "");
        if (plugin_path[0]) {
            plugin->present = true;
            plugin->format = yaml_get_int(plugin_node, "format", 0);
            fr_strcpy(plugin->path, sizeof(plugin->path), plugin_path);
            fr_strcpy(plugin->name, sizeof(plugin->name),
                      yaml_get_string(plugin_node, "name", plat_path_basename(plugin_path)));
            if (yaml_get_string(plugin_node, "state", "")[0]) project_read_state(plugin, name);
        }
    }

    yaml_file_release(&file);
    *cfg = out;
    fr_log("project: loaded %s", path);
    return true;
}
