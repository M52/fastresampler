// #pragma once (removed for unity build)
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "fastresampler_app.h"
#include "fastresampler_view.h"
#include "fastresampler_helpers.h"
#include "fastresampler_log.h"
#include "platform/fastresampler_platform.h"

// One more line in the output log's ring. Full, the oldest line goes.
static void app_log_add(App* app, bool is_error, const char* text) {
    int at;
    if (app->log_count < FR_LOG_LINES) {
        at = (app->log_first + app->log_count) % FR_LOG_LINES;
        app->log_count++;
    } else {
        at = app->log_first;
        app->log_first = (app->log_first + 1) % FR_LOG_LINES;
    }
    AppLogLine* line = &app->log_lines[at];
    fr_strcpy(line->text, sizeof(line->text), text);
    line->is_error = is_error;
    time_t now = time(nullptr);
    struct tm local;
    if (fr_localtime(&now, &local)) {
        snprintf(line->time, sizeof(line->time), "%02d:%02d:%02d", local.tm_hour, local.tm_min, local.tm_sec);
    } else {
        line->time[0] = '\0';
    }
    app->log_written++;
}

const AppLogLine* app_log_line(const App* app, int index) {
    if (index < 0 || index >= app->log_count) return nullptr;
    return &app->log_lines[(app->log_first + index) % FR_LOG_LINES];
}

void app_log_clear(App* app) {
    app->log_count = 0;
    app->log_first = 0;
    app->log_written++;
}

float app_log_strip_height(const App* app, float client_height, float scale) {
    float bar = UI_LOG_HEADER_HEIGHT * scale;
    if (!app->settings.log_expanded) return bar;
    float strip = bar + (float)app->settings.log_height * scale;
    // However far the grip was dragged, the plugin area and the keyboard
    // keep room of their own.
    float most = client_height - (UI_KEYBOARD_HEIGHT + UI_MAIN_MIN_HEIGHT) * scale;
    if (strip > most) strip = most;
    if (strip < bar) strip = bar;
    return strip;
}

void app_set_status(App* app, bool is_error, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(app->status, sizeof(app->status), fmt, args);
    va_end(args);
    app->status_is_error = is_error;
    fr_log("%s%s", is_error ? "error: " : "", app->status);
    app_log_add(app, is_error, app->status);
}

// The driver the settings name, else the first one (the default WASAPI device).
static int app_find_saved_driver(const App* app) {
    for (int i = 0; i < app->drivers.count; ++i) {
        const AudioDriverInfo* d = &app->drivers.drivers[i];
        if (d->api == app->settings.audio_api && fr_stricmp(d->name, app->settings.audio_driver_name) == 0) return i;
    }
    return app->drivers.count > 0 ? 0 : -1;
}

static int app_block_size_index(int block_size) {
    for (int i = 0; i < FR_BLOCK_SIZE_OPTION_COUNT; ++i) {
        if (g_block_size_options[i] == block_size) return i;
    }
    return 3;   // 256
}

// Both lists again from the settings' folders; nothing stays selected.
static void app_scan_plugins(App* app) {
    scan_plugins(&app->plugins[HOST_FORMAT_VST3], &app->plugins[HOST_FORMAT_VST2], &app->vsti_cache, &app->settings);
    for (int f = 0; f < HOST_FORMAT_COUNT; ++f) app->selected_plugin[f] = -1;
}

void app_init(App* app) {
    char dir[FR_MAX_PATH];
    plat_module_dir(dir, sizeof(dir));
    plat_path_join(app->settings_path, sizeof(app->settings_path), dir, FR_SETTINGS_FILE);

    settings_defaults(&app->settings);
    settings_load(&app->settings, app->settings_path);

    midi_queue_init(&app->midi);
    memset(app->note_held, 0, sizeof(app->note_held));
    app->keyboard_active_note = -1;
    for (int i = 0; i < MIDI_NOTE_COUNT; ++i) app->cc_value[i] = 0;
    app->cc_value[MIDI_CC_VOLUME] = 100;
    app->cc_value[MIDI_CC_EXPRESSION] = MIDI_VALUE_MAX;
    app->cc_value[MIDI_CC_PAN] = 64;
    app->pitch_bend = MIDI_PITCH_BEND_CENTER;
    app->last_note = -1;
    app->last_velocity = 0;
    app->active_tab = APP_TAB_PLUGIN;
    app->range_valid = false;
    app->range_low = 0;
    app->range_high = 0;
    app->range_step = 1;
    for (int c = 0; c < FR_OUTPUT_CHANNELS; ++c) {
        app->peak[c].store(0.0f);
        app->meter[c] = 0.0f;
    }
    for (int b = 0; b < FR_MAX_OUTPUT_BUSSES; ++b) {
        for (int c = 0; c < FR_BUS_CHANNELS; ++c) {
            app->bus_peak[b][c].store(0.0f);
            app->bus_meter[b][c] = 0.0f;
        }
    }
    app->plugin_active.store(false);
    app->audio_busy.store(0);

    for (int f = 0; f < HOST_FORMAT_COUNT; ++f) app->selected_plugin[f] = -1;
    app->plugin_tab_select_request = true;
    app->log_tab_select_request = true;
    app->load_request = -1;
    app->load_request_format = HOST_FORMAT_VST3;
    app->unload_request = false;
    app->rescan_request = false;
    app->audio_apply_request = false;
    app->audio_refresh_request = false;
    app->root_dir_pick_request = false;
    app->output_dir_pick_request = false;
    app->export_dir_pick_request = false;
    app->log_count = 0;
    app->log_first = 0;
    app->log_written = 0;
    app->log_clear_request = false;
    app->disk_warning[0] = '\0';
    app->disk_warning_open = false;
    app->editor_open = false;
    app->editor_width = 0;
    app->editor_height = 0;
    app->new_scan_path[0] = '\0';
    app->resample_script.built = false;
    app->resample_job.running.store(false);
    app->resample_job.cancel.store(false);
    app->resample_job.finished.store(false);
    app->resample_job.progress_frames.store(0);
    app->resample_start_request = false;
    app->instrument_job.running.store(false);
    app->instrument_job.cancel.store(false);
    app->instrument_job.finished.store(false);
    app->instrument_job.progress.store(0);
    app->instrument_start_request = false;
    app->resample_fsi_ready = false;
    app->resample_full_wav_removed = false;
    app->project_scan_request = false;
    app->project_save_request = false;
    app->project_load_request = false;
    app->project_load_name[0] = '\0';
    project_scan(&app->projects);

    app->host = host_create();
    app->plugin = nullptr;
    app->plugin_path[0] = '\0';

    audio_enumerate(&app->drivers);
    app->selected_driver = app_find_saved_driver(app);
    app->pending_driver = app->selected_driver;
    app->pending_block_index = app_block_size_index(app->settings.block_size);

    char cache_path[FR_MAX_PATH];
    plat_path_join(cache_path, sizeof(cache_path), dir, FR_VSTI_CACHE_FILE);
    scan_cache_init(&app->vsti_cache, cache_path);
    app_scan_plugins(app);
    app_set_status(app, false, "%d VST3 plugins and %d VST instruments found. Double-click one to load it.",
                   app->plugins[HOST_FORMAT_VST3].count, app->plugins[HOST_FORMAT_VST2].count);
}

// Stops the audio thread from touching the plugin and waits until it is out.
static void app_pause_plugin(App* app) {
    app->plugin_active.store(false, std::memory_order_seq_cst);
    while (app->audio_busy.load(std::memory_order_seq_cst) != 0) plat_sleep_ms(1);
}

static void app_resume_plugin(App* app) {
    if (app->plugin) app->plugin_active.store(true, std::memory_order_seq_cst);
}

// IPlugFrame::resizeView lands here: the editor wants a new size.
static void app_on_editor_resize(void* user, int width, int height) {
    App* app = (App*)user;
    app->editor_width = width;
    app->editor_height = height;
    view_editor_place(app->view, width, height);
}

static void app_open_editor(App* app) {
    int w = 0, h = 0;
    if (!host_plugin_editor_create(app->plugin, view_dpi_scale(app->view), &w, &h)) return;
    if (w <= 0 || h <= 0) {
        w = 400;
        h = 300;
    }
    void* parent = view_editor_create(app->view, w, h);
    if (!parent) {
        host_plugin_editor_close(app->plugin);
        return;
    }
    if (!host_plugin_editor_attach(app->plugin, parent, app_on_editor_resize, app)) {
        view_editor_destroy(app->view);
        return;
    }
    app->editor_open = true;
    app->editor_width = w;
    app->editor_height = h;
}

static void app_unload_plugin(App* app) {
    if (!app->plugin) return;
    app_pause_plugin(app);
    host_plugin_editor_close(app->plugin);
    view_editor_destroy(app->view);
    app->editor_open = false;
    char name[FR_PLUGIN_NAME_LEN];
    fr_strcpy(name, sizeof(name), host_plugin_info(app->plugin)->name);
    host_plugin_unload(app->plugin);
    app->plugin = nullptr;
    app->plugin_path[0] = '\0';
    memset(app->note_held, 0, sizeof(app->note_held));
    app->keyboard_active_note = -1;
    app_set_status(app, false, "Closed %s.", name);
}

// Loads a plugin by path and puts its editor up. display_name is what a
// failure is reported against; the path is kept so a project saved later
// names the plugin it was made with.
static bool app_load_plugin_path(App* app, const char* path, const char* display_name) {
    app_set_status(app, false, "Loading %s...", display_name);

    double sample_rate = app->audio.open ? app->audio.sample_rate : FR_DEFAULT_SAMPLE_RATE;
    int block_size = app->audio.open ? app->audio.block_size : app->settings.block_size;

    char err[FR_ERROR_LEN];
    HostPlugin* p = host_plugin_load(app->host, path, sample_rate, block_size, err, sizeof(err));
    if (!p) {
        app_set_status(app, true, "%s: %s", display_name, err);
        return false;
    }
    app->plugin = p;
    fr_strcpy(app->plugin_path, sizeof(app->plugin_path), path);
    app_open_editor(app);
    app_resume_plugin(app);

    const HostPluginInfo* info = host_plugin_info(p);
    if (app->editor_open) {
        app_set_status(app, false, "Loaded %s by %s (%s).", info->name, info->vendor, host_format_name(info->format));
    } else {
        app_set_status(app, false, "Loaded %s by %s (%s, no editor).", info->name, info->vendor, host_format_name(info->format));
    }
    return true;
}

static void app_load_plugin(App* app, int format, int index) {
    if (format < 0 || format >= HOST_FORMAT_COUNT) return;
    const PluginList* list = &app->plugins[format];
    if (index < 0 || index >= list->count) return;
    const PluginEntry* entry = &list->entries[index];
    app_load_plugin_path(app, entry->path, entry->name);
}

// The row a path has in the list of its format, added to the end when the
// scan never found it. The list of that format comes to the front and the
// row is highlighted, so what is loaded is what is shown. -1 when the path
// is banned, gone, or the list is full.
static int app_select_plugin_path(App* app, const char* path) {
    if (!path || !path[0]) return -1;
    if (settings_is_banned(&app->settings, path)) {
        app_set_status(app, true, "%s is banned. Unban it from the list's right-click menu.",
                       plat_path_basename(path));
        return -1;
    }
    int format = host_format_of_path(path);
    PluginList* list = &app->plugins[format];
    app->settings.plugin_tab = format;
    app->plugin_tab_select_request = true;
    for (int i = 0; i < list->count; ++i) {
        if (fr_stricmp(list->entries[i].path, path) == 0) {
            app->selected_plugin[format] = i;
            return i;
        }
    }
    if (!plat_path_exists(path)) {
        app_set_status(app, true, "Not found: %s", path);
        return -1;
    }
    if (list->count >= FR_MAX_PLUGINS) return -1;
    PluginEntry* e = &list->entries[list->count];
    fr_strcpy(e->path, FR_MAX_PATH, path);
    fr_strcpy(e->name, FR_PLUGIN_NAME_LEN, plat_path_basename(path));
    fr_strip_plugin_ext(e->name);
    app->selected_plugin[format] = list->count;
    return list->count++;
}

// Closes and reopens the audio device from the pending settings, then puts
// the plugin back on the new rate and block.
static void app_apply_audio(App* app) {
    app_pause_plugin(app);
    audio_close(&app->audio);

    if (app->pending_driver >= 0 && app->pending_driver < app->drivers.count) {
        const AudioDriverInfo* d = &app->drivers.drivers[app->pending_driver];
        app->settings.audio_api = d->api;
        fr_strcpy(app->settings.audio_driver_name, FR_DRIVER_NAME_LEN, d->name);
        app->selected_driver = app->pending_driver;
    }
    app->pending_block_index = fr_clampi(app->pending_block_index, 0, FR_BLOCK_SIZE_OPTION_COUNT - 1);
    app->settings.block_size = g_block_size_options[app->pending_block_index];
    settings_save(&app->settings, app->settings_path);

    bool ok = false;
    if (app->selected_driver >= 0) {
        const AudioDriverInfo* d = &app->drivers.drivers[app->selected_driver];
        ok = audio_open(&app->audio, d, app->settings.block_size, app_audio_render, app, view_native_window(app->view));
        if (ok) {
            app_set_status(app, false, "%s: %s at %.0f Hz, %d frames.", audio_api_name(d->api), d->name,
                           app->audio.sample_rate, app->audio.block_size);
        } else {
            app_set_status(app, true, "%s: %s", d->name, app->audio.error);
        }
    } else {
        app_set_status(app, true, "No audio device available.");
    }

    if (app->plugin) {
        double sample_rate = ok ? app->audio.sample_rate : FR_DEFAULT_SAMPLE_RATE;
        int block_size = ok ? app->audio.block_size : app->settings.block_size;
        if (!host_plugin_setup_processing(app->plugin, sample_rate, block_size, false)) {
            app_set_status(app, true, "The plugin refused %.0f Hz with %d frames.", sample_rate, block_size);
        }
    }
    app_resume_plugin(app);
}

void app_start_audio(App* app) {
    app_apply_audio(app);
}

// --- Resampling -------------------------------------------------------------------

bool app_resample_running(const App* app) {
    return app->resample_job.running.load();
}

bool app_instrument_running(const App* app) {
    return app->instrument_job.running.load();
}

// The rate the plugin plays at: the device's, or the default with no device.
static double app_device_sample_rate(const App* app) {
    return app->audio.open ? app->audio.sample_rate : FR_DEFAULT_SAMPLE_RATE;
}

// The rate a render runs at: the tab's choice, or the device's.
static double app_resample_rate(const App* app) {
    int chosen = app->settings.resample.sample_rate;
    return chosen > 0 ? (double)chosen : app_device_sample_rate(app);
}

// "1.5 GB" or "320 MB".
static void app_format_bytes(uint64_t bytes, char* out, size_t out_len) {
    double mb = (double)bytes / (1024.0 * 1024.0);
    if (mb >= 1024.0) {
        snprintf(out, out_len, "%.1f GB", mb / 1024.0);
    } else {
        snprintf(out, out_len, "%.0f MB", mb);
    }
}

// Whether the disk of the output folder has room for the built script's
// files. True with no script. False raises the warning the UI shows as a
// modal, and the caller must not render.
static bool app_check_disk_space(App* app) {
    if (!app->resample_script.built) return true;
    char dir[FR_MAX_PATH];
    char dir_err[FR_STATUS_LEN];
    // The first bus's folder: every bus writes the same amount, and they are
    // all under one root unless a box sent them elsewhere.
    if (!resample_output_dir(&app->settings.resample, 0, dir, sizeof(dir), dir_err, sizeof(dir_err))) return true;
    uint64_t needed = resample_required_bytes(&app->resample_script, app->settings.resample.split_samples,
                                              app->settings.resample.bus_count);
    uint64_t free_bytes = 0;
    char needed_text[32], free_text[32];
    app_format_bytes(needed, needed_text, sizeof(needed_text));
    if (!plat_disk_free_bytes(dir, &free_bytes)) {
        snprintf(app->disk_warning, sizeof(app->disk_warning),
                 "The free space of the disk that holds %s could not be read. The render writes %s.", dir, needed_text);
        app->disk_warning_open = true;
        app_set_status(app, true, "%s", app->disk_warning);
        return false;
    }
    if (free_bytes < needed) {
        int bus_count = app->settings.resample.bus_count;
        // Every bus writes its own set, so the count is what the run makes
        // in total and the message says so.
        char over[48];
        over[0] = '\0';
        if (bus_count > 1) snprintf(over, sizeof(over), " over %d output busses", bus_count);
        app_format_bytes(free_bytes, free_text, sizeof(free_text));
        if (app->settings.resample.split_samples) {
            snprintf(app->disk_warning, sizeof(app->disk_warning),
                     "Not enough disk space for the render. The full WAV and the %d sample files%s take %s; the disk that holds %s has %s free.",
                     app->resample_script.segment_count * bus_count, over, needed_text, dir, free_text);
        } else {
            snprintf(app->disk_warning, sizeof(app->disk_warning),
                     "Not enough disk space for the render. The full WAV%s takes %s; the disk that holds %s has %s free.",
                     over, needed_text, dir, free_text);
        }
        app->disk_warning_open = true;
        app_set_status(app, true, "%s", app->disk_warning);
        return false;
    }
    return true;
}

void app_resample_build(App* app) {
    if (app_resample_running(app) || app_instrument_running(app)) return;
    // A new script means the files on disk no longer describe it.
    app->resample_fsi_ready = false;
    app->resample_full_wav_removed = false;
    char err[FR_STATUS_LEN];
    if (!resample_build(&app->resample_script, &app->settings.resample, app_resample_rate(app), err, sizeof(err))) {
        app_set_status(app, true, "%s", err);
        return;
    }
    const ResampleScript* s = &app->resample_script;
    double seconds = (double)s->total_frames / s->sample_rate;
    char example[RESAMPLE_FILE_NAME_LEN];
    resample_segment_file_name(&app->settings.resample, 0, &s->segments[0], example, sizeof(example));
    char curves[32] = "";
    int curve_count = app->settings.resample.cc_curve_count;
    if (curve_count > 0) snprintf(curves, sizeof(curves), ", %d CC curve%s", curve_count, curve_count == 1 ? "" : "s");
    char transitions[64] = "";
    if (s->legato) {
        snprintf(transitions, sizeof(transitions), " and %d transitions over %d interval%s",
                 s->transition_count, s->interval_count, s->interval_count == 1 ? "" : "s");
    }
    // The script is played once whatever the busses are, but every bus
    // writes its own copy of every sample, so the log says how many files
    // the run actually makes.
    int bus_count = app->settings.resample.bus_count;
    if (bus_count > 1) {
        fr_log("resample: %d output busses, so %d sample files in %d folders",
               bus_count, s->segment_count * bus_count, bus_count);
    }
    app_set_status(app, false,
                   "%d samples: %d notes x %d velocities x %d %s%s%s, %d events, %d:%02d of audio at %.0f Hz. First file: %s",
                   s->segment_count, s->note_count, s->velocity_count, s->round_robin_count,
                   s->dynamic_layers ? "dynamic layers" : "round robins", transitions, curves, s->count,
                   (int)seconds / 60, (int)seconds % 60, s->sample_rate, example);
    settings_save(&app->settings, app->settings_path);
    app_check_disk_space(app);
}

static void app_resample_start(App* app) {
    if (!app->plugin) {
        app_set_status(app, true, "Load a plugin first.");
        return;
    }
    if (!app->resample_script.built) {
        app_set_status(app, true, "Build the MIDI first.");
        return;
    }
    if (app->resample_script.sample_rate != app_resample_rate(app)) {
        app_set_status(app, true, "The sample rate changed since the build. Build the MIDI again.");
        return;
    }
    // A bus on a pair the plugin does not have records silence, which is
    // found out hours later. The run is stopped instead and says which bus.
    {
        const ResampleConfig* c = &app->settings.resample;
        int have = host_plugin_info(app->plugin)->total_output_channels;
        for (int b = 0; b < c->bus_count; ++b) {
            if (c->busses[b].channel + FR_BUS_CHANNELS <= have) continue;
            char pair[16];
            resample_bus_pair_label(c->busses[b].channel, pair, sizeof(pair));
            app_set_status(app, true,
                           "Output bus %d '%s' reads outputs %s, but the plugin has %d output channel%s. Move the bus or route the plugin.",
                           b + 1, c->busses[b].name, pair, have, have == 1 ? "" : "s");
            return;
        }
    }
    // One folder per bus, made before the render so the worker only writes.
    const ResampleConfig* cfg = &app->settings.resample;
    char output_dir[FR_MAX_PATH];
    char dir_err[FR_STATUS_LEN];
    for (int b = 0; b < cfg->bus_count; ++b) {
        if (!resample_output_dir(cfg, b, output_dir, sizeof(output_dir), dir_err, sizeof(dir_err))) {
            app_set_status(app, true, "%s", dir_err);
            return;
        }
        if (!plat_dir_create_all(output_dir)) {
            app_set_status(app, true, "Could not create the output folder %s", output_dir);
            return;
        }
    }
    // Checked again here: the disk may have filled up since the build.
    if (!app_check_disk_space(app)) {
        app_set_status(app, true, "Not enough disk space; the render did not start.");
        return;
    }
    // The worker owns the plugin until it is done; the device plays silence.
    // The plugin is switched to the export rate, in offline mode, for the
    // duration; app_resample_finish() puts it back on the device.
    app_pause_plugin(app);
    int block_size = app->audio.open ? app->audio.block_size : app->settings.block_size;
    double export_rate = app->resample_script.sample_rate;
    if (!host_plugin_setup_processing(app->plugin, export_rate, block_size, true)) {
        app_set_status(app, true, "The plugin refused %.0f Hz. Pick another export rate.", export_rate);
        host_plugin_setup_processing(app->plugin, app_device_sample_rate(app), block_size, false);
        app_resume_plugin(app);
        return;
    }
    if (!resample_start(&app->resample_job, app->plugin, &app->resample_script, block_size, &app->settings.resample)) {
        app_set_status(app, true, "The render could not start.");
        host_plugin_setup_processing(app->plugin, app_device_sample_rate(app), block_size, false);
        app_resume_plugin(app);
        return;
    }
    app->resample_fsi_ready = false;
    app->resample_full_wav_removed = false;
    app_set_status(app, false, "Rendering...");
}

static void app_resample_finish(App* app) {
    if (!resample_collect(&app->resample_job)) return;
    ResampleJob* job = &app->resample_job;
    app_set_status(app, job->result_is_error, "%s", job->result);
    // Every bus writes the whole set, so a complete run wrote one file per
    // segment per bus.
    int wanted = app->resample_script.segment_count * job->bus_count;
    bool samples_ok = !job->split_samples || job->files_written == wanted;
    app->resample_fsi_ready = !job->result_is_error && !job->cancel.load() && samples_ok;
    // Back to the device's rate and realtime mode.
    if (app->plugin) {
        int block_size = app->audio.open ? app->audio.block_size : app->settings.block_size;
        if (!host_plugin_setup_processing(app->plugin, app_device_sample_rate(app), block_size, false)) {
            app_set_status(app, true, "The plugin refused to return to %.0f Hz.", app_device_sample_rate(app));
        }
    }
    app_resume_plugin(app);
}

static void app_instrument_start(App* app) {
    if (!app->resample_fsi_ready || !app->resample_script.built) {
        app_set_status(app, true, "Render the samples first.");
        return;
    }
    // One instrument per output bus, each cut from that bus's own full WAV
    // and written beside it, so every microphone ends up a bank of its own.
    const ResampleConfig* cfg = &app->settings.resample;
    InstrumentTarget targets[FR_MAX_OUTPUT_BUSSES];
    char err[FR_STATUS_LEN];
    // The banks are cut from the WAVs the render wrote, so the busses must
    // still be the ones it wrote them for.
    if (cfg->bus_count != app->resample_job.bus_count) {
        app_set_status(app, true, "The output busses changed since the render. Render to WAV again.");
        return;
    }
    for (int b = 0; b < cfg->bus_count; ++b) {
        if (!resample_prefix(cfg, b, targets[b].prefix, sizeof(targets[b].prefix), err, sizeof(err))) {
            app_set_status(app, true, "%s", err);
            return;
        }
        if (strcmp(targets[b].prefix, app->resample_job.prefix[b]) != 0) {
            app_set_status(app, true, "Output bus %d was rendered as '%s'. Render to WAV again.",
                           b + 1, app->resample_job.prefix[b]);
            return;
        }
        if (!resample_export_dir(cfg, b, targets[b].out_dir, sizeof(targets[b].out_dir), err, sizeof(err))) {
            app_set_status(app, true, "%s", err);
            return;
        }
        if (!plat_dir_create_all(targets[b].out_dir)) {
            app_set_status(app, true, "Could not create the export folder %s", targets[b].out_dir);
            return;
        }
        // The bank is cut from the same full WAV the bus's samples came from.
        fr_strcpy(targets[b].full_wav_path, sizeof(targets[b].full_wav_path), app->resample_job.full_path[b]);
        fr_strcpy(targets[b].bus_name, sizeof(targets[b].bus_name), cfg->busses[b].name);
    }
    if (!instrument_start(&app->instrument_job, &app->resample_script, cfg, targets, cfg->bus_count)) {
        app_set_status(app, true, "The instrument build could not start.");
        return;
    }
    if (cfg->bus_count > 1) {
        app_set_status(app, false, "Building %d instruments...", cfg->bus_count);
    } else {
        app_set_status(app, false, "Building the instrument...");
    }
}

static void app_instrument_finish(App* app) {
    if (!instrument_collect(&app->instrument_job)) return;
    if (app->instrument_job.full_wav_removed) {
        app->resample_full_wav_removed = true;
        app->resample_fsi_ready = false;   // there is nothing left to cut a bank from
    }
    // What each bus reported, in order, before the line for the whole run:
    // the worker keeps them because it cannot write to the ledger itself.
    for (int i = 0; i < app->instrument_job.bus_result_count; ++i) {
        app_log_add(app, false, app->instrument_job.bus_result[i]);
    }
    if (app->instrument_job.result_is_error) {
        app_set_status(app, true, "%s", app->instrument_job.result);
    } else if (app->instrument_job.instruments_written > 1) {
        app_log_add(app, false, app->instrument_job.result);
        app->status_is_error = false;
        snprintf(app->status, sizeof(app->status), "Wrote %d instruments, one per output bus.",
                 app->instrument_job.instruments_written);
    } else {
        // The full detail (zone and collection counts) belongs in the output
        // log, not the side panel's one-line status strip.
        app_log_add(app, false, app->instrument_job.result);
        app->status_is_error = false;
        snprintf(app->status, sizeof(app->status), "Wrote %s and %s.",
                 plat_path_basename(app->instrument_job.fsi_path),
                 plat_path_basename(app->instrument_job.fsb_path));
    }
}

// The plugin the project is being saved with: which one is loaded, and the
// state it is in. The audio thread is kept out over the read, as it is for
// anything else that talks to the plugin off the audio callback.
static void app_read_plugin_state(App* app, ProjectPlugin* out) {
    project_plugin_init(out);
    if (!app->plugin || !app->plugin_path[0]) return;
    const HostPluginInfo* info = host_plugin_info(app->plugin);
    out->present = true;
    out->format = info->format;
    fr_strcpy(out->path, sizeof(out->path), app->plugin_path);
    fr_strcpy(out->name, sizeof(out->name), info->name);

    app_pause_plugin(app);
    bool ok = host_plugin_get_state(app->plugin, &out->state);
    app_resume_plugin(app);
    if (!ok) fr_log("app: %s wrote no state to save", info->name);
}

// Where a project's plugin is on this machine. The path it was saved with
// when that file is still there; otherwise the scanned list of its format is
// searched, by file name first and then by the name the plugin reported, so
// a project made on another machine finds the same plugin under whatever
// folder it was installed in here. out gets the path; false when nothing
// matches and the project has to load without it.
static bool app_find_project_plugin(App* app, const ProjectPlugin* plugin, char* out, size_t out_len) {
    if (plat_path_exists(plugin->path)) {
        fr_strcpy(out, out_len, plugin->path);
        return true;
    }
    int format = fr_clampi(plugin->format, 0, HOST_FORMAT_COUNT - 1);
    const PluginList* list = &app->plugins[format];
    const char* file = plat_path_basename(plugin->path);

    // The file name, which is the same wherever the plugin was installed.
    // A VST3 bundle and a bare file both end in the name the scan lists.
    for (int i = 0; i < list->count; ++i) {
        if (fr_stricmp(plat_path_basename(list->entries[i].path), file) == 0) {
            fr_strcpy(out, out_len, list->entries[i].path);
            return true;
        }
    }
    // Then what the plugin called itself. A VST3 is listed under its file
    // name and a VST 2 under the name it reported, so this catches the one
    // whose file was renamed and the one the two names differ on.
    if (plugin->name[0]) {
        for (int i = 0; i < list->count; ++i) {
            if (fr_stricmp(list->entries[i].name, plugin->name) == 0) {
                fr_strcpy(out, out_len, list->entries[i].path);
                return true;
            }
        }
    }
    return false;
}

// Puts the plugin of a loaded project back: the same one loaded again when
// another is up, then the state it was left in. A plugin this machine does
// not have at all is reported and the settings are kept.
static void app_restore_plugin(App* app, ProjectPlugin* plugin) {
    if (!plugin->present) return;
    char path[FR_MAX_PATH];
    if (!app_find_project_plugin(app, plugin, path, sizeof(path))) {
        app_set_status(app, true, "%s was not found on this computer; the settings were loaded without it.",
                       plugin->name);
        fr_log("app: the project's plugin is neither at %s nor in the %s list",
               plugin->path, host_format_name(plugin->format));
        return;
    }
    bool moved = fr_stricmp(path, plugin->path) != 0;
    if (moved) fr_log("app: the project's plugin was at %s, loading %s", plugin->path, path);

    if (fr_stricmp(app->plugin_path, path) != 0 || !app->plugin) {
        app_unload_plugin(app);
        // The list shows the plugin that is loaded, whichever tab it is on.
        app_select_plugin_path(app, path);
        if (!app_load_plugin_path(app, path, plugin->name)) return;
    }
    if (plugin->state.size == 0) {
        app_set_status(app, false, "Loaded %s; the project holds no state for it.", plugin->name);
        return;
    }

    char err[FR_ERROR_LEN];
    app_pause_plugin(app);
    bool ok = host_plugin_set_state(app->plugin, plugin->state.data, plugin->state.size, err, sizeof(err));
    app_resume_plugin(app);
    if (!ok) {
        app_set_status(app, true, "%s: %s", plugin->name, err);
        return;
    }
    // The state may have given the plugin other outputs than it had; the
    // audio device is opened for the new layout and the log says what it is.
    host_plugin_take_io_change(app->plugin);
    const HostPluginInfo* info = host_plugin_info(app->plugin);
    if (moved) {
        app_set_status(app, false, "%s is back as it was saved, from %s on this computer: %d output channels over %d busses.",
                       info->name, path, info->total_output_channels, info->output_bus_count);
    } else {
        app_set_status(app, false, "%s is back as it was saved: %d output channels over %d busses.",
                       info->name, info->total_output_channels, info->output_bus_count);
    }
}

// The plugin changed its own busses while it was running, the way a sampler
// does when its outputs are set up in its own editor. The layout is read
// again with the audio thread kept out, so the Output busses tab and a run
// both see what it offers now.
static void app_service_io_change(App* app) {
    if (!app->plugin || !host_plugin_take_io_change(app->plugin)) return;
    app_pause_plugin(app);
    double sample_rate = app->audio.open ? app->audio.sample_rate : FR_DEFAULT_SAMPLE_RATE;
    int block_size = app->audio.open ? app->audio.block_size : app->settings.block_size;
    bool ok = host_plugin_setup_processing(app->plugin, sample_rate, block_size, false);
    app_resume_plugin(app);
    const HostPluginInfo* info = host_plugin_info(app->plugin);
    if (ok) {
        app_set_status(app, false, "%s now offers %d output channels over %d busses.",
                       info->name, info->total_output_channels, info->output_bus_count);
    } else {
        app_set_status(app, true, "%s changed its outputs and refused %.0f Hz with %d frames.",
                       info->name, sample_rate, block_size);
    }
}

// The saved projects: the list, and the save and load the tab asked for.
// Neither runs while a render or an instrument build does, because both read
// the configuration a load would replace.
static void app_process_project_requests(App* app) {
    if (app->project_scan_request) {
        app->project_scan_request = false;
        project_scan(&app->projects);
    }
    if (!app->project_save_request && !app->project_load_request) return;

    bool busy = app_resample_running(app) || app_instrument_running(app);
    char err[FR_STATUS_LEN];

    if (app->project_save_request) {
        app->project_save_request = false;
        if (busy) {
            app_set_status(app, true, "Wait for the render to finish, or cancel it.");
        } else {
            ProjectPlugin plugin;
            app_read_plugin_state(app, &plugin);
            bool ok = project_save(&app->settings.resample, &plugin, app->settings.project_name, err, sizeof(err));
            if (ok) {
                if (plugin.present && plugin.state.size > 0) {
                    app_set_status(app, false, "Project %s saved, with %s as it stands.",
                                   app->settings.project_name, plugin.name);
                } else if (plugin.present) {
                    app_set_status(app, true, "Project %s saved, but %s wrote no state to keep.",
                                   app->settings.project_name, plugin.name);
                } else {
                    app_set_status(app, false, "Project %s saved, with no plugin loaded.",
                                   app->settings.project_name);
                }
                settings_save(&app->settings, app->settings_path);
                project_scan(&app->projects);
            } else {
                app_set_status(app, true, "%s", err);
            }
            project_plugin_release(&plugin);
        }
    }

    if (app->project_load_request) {
        app->project_load_request = false;
        ProjectPlugin plugin;
        project_plugin_init(&plugin);
        if (busy) {
            app_set_status(app, true, "Wait for the render to finish, or cancel it.");
        } else if (project_load(&app->settings.resample, &plugin, app->project_load_name, err, sizeof(err))) {
            fr_strcpy(app->settings.project_name, sizeof(app->settings.project_name), app->project_load_name);
            // A project made on another computer names folders on a drive
            // this one has not got; those come back to the folder next to
            // the exe before anything is written to them.
            int moved_dirs = resample_config_localize_dirs(&app->settings.resample);
            settings_save(&app->settings, app->settings_path);
            // The script and the samples belong to the settings that made
            // them, so the tab starts again at Build MIDI.
            app->resample_script.built = false;
            app->resample_fsi_ready = false;
            app->resample_full_wav_removed = false;
            if (moved_dirs == 1) {
                app_set_status(app, false, "Project %s loaded; a folder it names is not on this computer, so it now follows %s.",
                               app->settings.project_name, app->settings.resample.root_dir);
            } else if (moved_dirs > 1) {
                app_set_status(app, false, "Project %s loaded; %d folders it names are not on this computer, so they now follow %s.",
                               app->settings.project_name, moved_dirs, app->settings.resample.root_dir);
            } else {
                app_set_status(app, false, "Project %s loaded.", app->settings.project_name);
            }
            if (moved_dirs > 0) app_check_disk_space(app);
            // The plugin last, so its own message is the one left standing:
            // it takes the longest and is what the user watches for.
            app_restore_plugin(app, &plugin);
        } else {
            app_set_status(app, true, "%s", err);
        }
        project_plugin_release(&plugin);
    }
}

void app_process_requests(App* app) {
    if (app->log_clear_request) {
        app->log_clear_request = false;
        app_log_clear(app);
    }
    app_process_project_requests(app);

    // A VST 2 editor repaints on this call, on the UI thread, a render in
    // progress or not: that is the plugin's own thread split.
    if (app->plugin && app->editor_open) host_plugin_editor_idle(app->plugin);

    // The folder pickers are modal and pump messages; they run here, outside
    // the ImGui frame, so a repaint meanwhile cannot re-enter the UI.
    if (app->root_dir_pick_request) {
        app->root_dir_pick_request = false;
        char dir[FR_MAX_PATH];
        if (plat_pick_folder(view_native_window(app->view), app->settings.resample.root_dir, dir, sizeof(dir))) {
            fr_strcpy(app->settings.resample.root_dir, sizeof(app->settings.resample.root_dir), dir);
            settings_save(&app->settings, app->settings_path);
            app_set_status(app, false, "The root project folder is %s", dir);
            app_check_disk_space(app);
        }
    }
    if (app->output_dir_pick_request) {
        app->output_dir_pick_request = false;
        char dir[FR_MAX_PATH];
        if (plat_pick_folder(view_native_window(app->view), app->settings.resample.output_dir, dir, sizeof(dir))) {
            fr_strcpy(app->settings.resample.output_dir, sizeof(app->settings.resample.output_dir), dir);
            settings_save(&app->settings, app->settings_path);
            app_set_status(app, false, "Samples go to %s", dir);
            app_check_disk_space(app);
        }
    }
    if (app->export_dir_pick_request) {
        app->export_dir_pick_request = false;
        char dir[FR_MAX_PATH];
        if (plat_pick_folder(view_native_window(app->view), app->settings.resample.export_dir, dir, sizeof(dir))) {
            fr_strcpy(app->settings.resample.export_dir, sizeof(app->settings.resample.export_dir), dir);
            settings_save(&app->settings, app->settings_path);
            app_set_status(app, false, "The instrument goes to %s", dir);
        }
    }
    if (app->instrument_start_request) {
        app->instrument_start_request = false;
        if (!app_instrument_running(app) && !app_resample_running(app)) app_instrument_start(app);
    }
    if (app_instrument_running(app)) app_instrument_finish(app);

    if (app->resample_start_request) {
        app->resample_start_request = false;
        if (!app_resample_running(app) && !app_instrument_running(app)) app_resample_start(app);
    }
    if (app_resample_running(app)) {
        // The worker has the plugin: nothing below may touch it until it is done.
        app_resample_finish(app);
        if (app_resample_running(app)) {
            if (app->load_request >= 0 || app->unload_request || app->audio_apply_request) {
                app_set_status(app, true, "Wait for the render to finish, or cancel it.");
            }
            app->load_request = -1;
            app->unload_request = false;
            app->audio_apply_request = false;
            return;
        }
    }
    if (app->audio_refresh_request) {
        app->audio_refresh_request = false;
        audio_enumerate(&app->drivers);
        app->selected_driver = app_find_saved_driver(app);
        app->pending_driver = app->selected_driver;
    }
    if (app->rescan_request) {
        app->rescan_request = false;
        settings_save(&app->settings, app->settings_path);
        app_scan_plugins(app);
        app_set_status(app, false, "%d VST3 plugins and %d VST instruments found.",
                       app->plugins[HOST_FORMAT_VST3].count, app->plugins[HOST_FORMAT_VST2].count);
    }
    if (app->unload_request || app->load_request >= 0) {
        app->unload_request = false;
        app_unload_plugin(app);
    }
    if (app->load_request >= 0) {
        int index = app->load_request;
        app->load_request = -1;
        app_load_plugin(app, app->load_request_format, index);
    }
    if (app->audio_apply_request) {
        app->audio_apply_request = false;
        app_apply_audio(app);
    }
    if (app->audio.reset_requested.exchange(false)) {
        fr_log("audio: the driver asked for a reset");
        app_apply_audio(app);
    }
    // Last of the plugin work: a plugin that changed its own busses since
    // the previous frame is set up again for the layout it has now.
    app_service_io_change(app);

    // The meters: take the peaks the audio thread left, decay towards them.
    for (int c = 0; c < FR_OUTPUT_CHANNELS; ++c) {
        float peak = app->peak[c].exchange(0.0f);
        float decayed = app->meter[c] * UI_METER_DECAY;
        app->meter[c] = peak > decayed ? peak : decayed;
    }
    // One pair per output bus, the same way: the Output busses tab draws
    // them, so a note played shows which microphone the plugin routed where.
    for (int b = 0; b < FR_MAX_OUTPUT_BUSSES; ++b) {
        for (int c = 0; c < FR_BUS_CHANNELS; ++c) {
            float peak = app->bus_peak[b][c].exchange(0.0f);
            float decayed = app->bus_meter[b][c] * UI_METER_DECAY;
            app->bus_meter[b][c] = peak > decayed ? peak : decayed;
        }
    }
}

void app_request_load_path(App* app, const char* path) {
    int index = app_select_plugin_path(app, path);
    if (index < 0) return;
    app->load_request = index;
    app->load_request_format = host_format_of_path(path);
}

// The plugin's flat output channels for one block, so every bus can be
// metered while the speakers play only one of them. The audio thread is the
// only one that touches it and only one block runs at a time.
static float g_app_bus_buffers[FR_MAX_RENDER_CHANNELS][FR_MAX_BLOCK_SIZE];
static float* g_app_bus_ptrs[FR_MAX_RENDER_CHANNELS];

void app_audio_render(void* user, float* const* outputs, int channel_count, int frames) {
    App* app = (App*)user;
    app->audio_busy.fetch_add(1, std::memory_order_seq_cst);
    if (frames > FR_MAX_BLOCK_SIZE) frames = FR_MAX_BLOCK_SIZE;

    const ResampleConfig* cfg = &app->settings.resample;
    int bus_count = fr_clampi(cfg->bus_count, 1, FR_MAX_OUTPUT_BUSSES);
    int flat_count = resample_bus_channel_count(cfg);
    int monitor = fr_clampi(cfg->monitor_bus, 0, bus_count - 1);

    if (app->plugin_active.load(std::memory_order_seq_cst)) {
        // Every bus is fetched, not only the one that is heard, so the tab's
        // meters say where each microphone landed.
        for (int c = 0; c < flat_count; ++c) {
            g_app_bus_ptrs[c] = g_app_bus_buffers[c];
            memset(g_app_bus_buffers[c], 0, (size_t)frames * sizeof(float));
        }
        host_plugin_process(app->plugin, &app->midi, g_app_bus_ptrs, flat_count, frames);

        // The device is stereo, so it plays the bus picked to monitor. The
        // channel is clamped because the UI thread may be editing it while
        // this block runs.
        int first = fr_clampi(cfg->busses[monitor].channel, 0, FR_MAX_RENDER_CHANNELS - FR_BUS_CHANNELS);
        for (int c = 0; c < channel_count && c < FR_BUS_CHANNELS; ++c) {
            int flat = first + c;
            if (flat >= flat_count) continue;
            memcpy(outputs[c], g_app_bus_buffers[flat], (size_t)frames * sizeof(float));
        }
    } else {
        // Nobody listens: events must not pile up for the next plugin.
        MidiEvent m;
        while (midi_queue_pop(&app->midi, &m)) {}
        for (int c = 0; c < flat_count; ++c) memset(g_app_bus_buffers[c], 0, (size_t)frames * sizeof(float));
    }

    // The device's own meters, of what is heard, and one pair per bus.
    for (int c = 0; c < channel_count && c < FR_OUTPUT_CHANNELS; ++c) {
        float peak = 0.0f;
        for (int i = 0; i < frames; ++i) {
            float a = outputs[c][i] < 0.0f ? -outputs[c][i] : outputs[c][i];
            if (a > peak) peak = a;
        }
        if (peak > app->peak[c].load(std::memory_order_relaxed)) app->peak[c].store(peak, std::memory_order_relaxed);
    }
    for (int b = 0; b < bus_count; ++b) {
        int first = fr_clampi(cfg->busses[b].channel, 0, FR_MAX_RENDER_CHANNELS - FR_BUS_CHANNELS);
        for (int c = 0; c < FR_BUS_CHANNELS; ++c) {
            int flat = first + c;
            float peak = 0.0f;
            if (flat < flat_count) {
                for (int i = 0; i < frames; ++i) {
                    float a = g_app_bus_buffers[flat][i];
                    if (a < 0.0f) a = -a;
                    if (a > peak) peak = a;
                }
            }
            if (peak > app->bus_peak[b][c].load(std::memory_order_relaxed)) {
                app->bus_peak[b][c].store(peak, std::memory_order_relaxed);
            }
        }
    }
    app->audio_busy.fetch_sub(1, std::memory_order_seq_cst);
}

// --- MIDI from the UI -----------------------------------------------------------

void app_note_on(App* app, int note, float velocity) {
    if (note < 0 || note >= MIDI_NOTE_COUNT) return;
    int v = (int)(fr_clampf(velocity, 0.0f, 1.0f) * MIDI_VALUE_MAX + 0.5f);
    if (v < 1) v = 1;
    midi_queue_push(&app->midi, midi_make(MIDI_STATUS_NOTE_ON, app->settings.midi_channel, note, v));
    app->note_held[note] = true;
    app->last_note = note;
    app->last_velocity = v;
}

void app_note_off(App* app, int note) {
    if (note < 0 || note >= MIDI_NOTE_COUNT) return;
    midi_queue_push(&app->midi, midi_make(MIDI_STATUS_NOTE_OFF, app->settings.midi_channel, note, 0));
    app->note_held[note] = false;
}

void app_control_change(App* app, int controller, int value) {
    if (controller < 0 || controller >= MIDI_NOTE_COUNT) return;
    value = fr_clampi(value, 0, MIDI_VALUE_MAX);
    app->cc_value[controller] = value;
    midi_queue_push(&app->midi, midi_make(MIDI_STATUS_CONTROL_CHANGE, app->settings.midi_channel, controller, value));
}

void app_pitch_bend(App* app, int value) {
    value = fr_clampi(value, 0, MIDI_PITCH_BEND_MAX);
    app->pitch_bend = value;
    midi_queue_push(&app->midi, midi_make(MIDI_STATUS_PITCH_BEND, app->settings.midi_channel, value & 0x7F, (value >> 7) & 0x7F));
}

void app_all_notes_off(App* app) {
    for (int n = 0; n < MIDI_NOTE_COUNT; ++n) {
        if (app->note_held[n]) app_note_off(app, n);
    }
    app->keyboard_active_note = -1;
    // Both messages: a plugin without a CC mapping still saw the note-offs.
    midi_queue_push(&app->midi, midi_make(MIDI_STATUS_CONTROL_CHANGE, app->settings.midi_channel, MIDI_CC_ALL_NOTES_OFF, 0));
    midi_queue_push(&app->midi, midi_make(MIDI_STATUS_CONTROL_CHANGE, app->settings.midi_channel, MIDI_CC_ALL_SOUND_OFF, 0));
    if (app->cc_value[MIDI_CC_SUSTAIN] != 0) app_control_change(app, MIDI_CC_SUSTAIN, 0);
}

void app_shutdown(App* app) {
    if (app_resample_running(app)) {
        resample_cancel(&app->resample_job);
        while (!app->resample_job.finished.load()) plat_sleep_ms(1);
        resample_collect(&app->resample_job);
    }
    if (app_instrument_running(app)) {
        instrument_cancel(&app->instrument_job);
        while (!app->instrument_job.finished.load()) plat_sleep_ms(1);
        instrument_collect(&app->instrument_job);
    }
    app_pause_plugin(app);
    audio_close(&app->audio);
    if (app->plugin) {
        host_plugin_editor_close(app->plugin);
        view_editor_destroy(app->view);
        host_plugin_unload(app->plugin);
        app->plugin = nullptr;
    }
    host_destroy(app->host);
    app->host = nullptr;
    settings_save(&app->settings, app->settings_path);
}
