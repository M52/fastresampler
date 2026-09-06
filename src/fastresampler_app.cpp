// #pragma once (removed for unity build)
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "fastresampler_app.h"
#include "fastresampler_view.h"
#include "fastresampler_helpers.h"
#include "fastresampler_log.h"
#include "platform/fastresampler_platform.h"

void app_set_status(App* app, bool is_error, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(app->status, sizeof(app->status), fmt, args);
    va_end(args);
    app->status_is_error = is_error;
    fr_log("%s%s", is_error ? "error: " : "", app->status);
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
    app->plugin_active.store(false);
    app->audio_busy.store(0);

    for (int f = 0; f < HOST_FORMAT_COUNT; ++f) app->selected_plugin[f] = -1;
    app->plugin_tab_select_request = true;
    app->load_request = -1;
    app->load_request_format = HOST_FORMAT_VST3;
    app->unload_request = false;
    app->rescan_request = false;
    app->audio_apply_request = false;
    app->audio_refresh_request = false;
    app->output_dir_pick_request = false;
    app->export_dir_pick_request = false;
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
    app->resample_build_text[0] = '\0';
    app->resample_build_is_error = false;
    app->resample_result_text[0] = '\0';
    app->resample_result_is_error = false;
    app->instrument_job.running.store(false);
    app->instrument_job.cancel.store(false);
    app->instrument_job.finished.store(false);
    app->instrument_job.progress.store(0);
    app->instrument_start_request = false;
    app->resample_fsi_ready = false;
    app->resample_full_wav_removed = false;
    app->instrument_result_text[0] = '\0';
    app->instrument_result_is_error = false;
    app->project_scan_request = false;
    app->project_save_request = false;
    app->project_load_request = false;
    app->project_load_name[0] = '\0';
    app->project_result_text[0] = '\0';
    app->project_result_is_error = false;
    project_scan(&app->projects);

    app->host = host_create();
    app->plugin = nullptr;

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
    memset(app->note_held, 0, sizeof(app->note_held));
    app->keyboard_active_note = -1;
    app_set_status(app, false, "Closed %s.", name);
}

static void app_load_plugin(App* app, int format, int index) {
    if (format < 0 || format >= HOST_FORMAT_COUNT) return;
    const PluginList* list = &app->plugins[format];
    if (index < 0 || index >= list->count) return;
    const PluginEntry* entry = &list->entries[index];
    app_set_status(app, false, "Loading %s...", entry->name);

    double sample_rate = app->audio.open ? app->audio.sample_rate : FR_DEFAULT_SAMPLE_RATE;
    int block_size = app->audio.open ? app->audio.block_size : app->settings.block_size;

    char err[FR_ERROR_LEN];
    HostPlugin* p = host_plugin_load(app->host, entry->path, sample_rate, block_size, err, sizeof(err));
    if (!p) {
        app_set_status(app, true, "%s: %s", entry->name, err);
        return;
    }
    app->plugin = p;
    app_open_editor(app);
    app_resume_plugin(app);

    const HostPluginInfo* info = host_plugin_info(p);
    if (app->editor_open) {
        app_set_status(app, false, "Loaded %s by %s (%s).", info->name, info->vendor, host_format_name(info->format));
    } else {
        app_set_status(app, false, "Loaded %s by %s (%s, no editor).", info->name, info->vendor, host_format_name(info->format));
    }
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
    const char* dir = app->settings.resample.output_dir;
    uint64_t needed = resample_required_bytes(&app->resample_script);
    uint64_t free_bytes = 0;
    char needed_text[32], free_text[32];
    app_format_bytes(needed, needed_text, sizeof(needed_text));
    if (!plat_disk_free_bytes(dir, &free_bytes)) {
        snprintf(app->disk_warning, sizeof(app->disk_warning),
                 "The free space of the disk that holds %s could not be read. The render writes %s.", dir, needed_text);
        app->disk_warning_open = true;
        fr_log("disk: %s", app->disk_warning);
        return false;
    }
    if (free_bytes < needed) {
        app_format_bytes(free_bytes, free_text, sizeof(free_text));
        snprintf(app->disk_warning, sizeof(app->disk_warning),
                 "Not enough disk space for the render. The full WAV and the %d sample files take %s; the disk that holds %s has %s free.",
                 app->resample_script.segment_count, needed_text, dir, free_text);
        app->disk_warning_open = true;
        fr_log("disk: %s", app->disk_warning);
        return false;
    }
    return true;
}

void app_resample_build(App* app) {
    if (app_resample_running(app) || app_instrument_running(app)) return;
    // A new script means the files on disk no longer describe it.
    app->resample_fsi_ready = false;
    app->resample_full_wav_removed = false;
    app->instrument_result_text[0] = '\0';
    char err[FR_STATUS_LEN];
    if (!resample_build(&app->resample_script, &app->settings.resample, app_resample_rate(app), err, sizeof(err))) {
        fr_strcpy(app->resample_build_text, sizeof(app->resample_build_text), err);
        app->resample_build_is_error = true;
        return;
    }
    const ResampleScript* s = &app->resample_script;
    double seconds = (double)s->total_frames / s->sample_rate;
    char example[RESAMPLE_FILE_NAME_LEN];
    resample_segment_file_name(&app->settings.resample, &s->segments[0], example, sizeof(example));
    char curves[32] = "";
    int curve_count = app->settings.resample.cc_curve_count;
    if (curve_count > 0) snprintf(curves, sizeof(curves), ", %d CC curve%s", curve_count, curve_count == 1 ? "" : "s");
    snprintf(app->resample_build_text, sizeof(app->resample_build_text),
             "%d samples: %d notes x %d velocities x %d round robins%s, %d events, %d:%02d of audio at %.0f Hz. First file: %s",
             s->segment_count, s->note_count, s->velocity_count, s->round_robin_count, curves, s->count,
             (int)seconds / 60, (int)seconds % 60, s->sample_rate, example);
    app->resample_build_is_error = false;
    settings_save(&app->settings, app->settings_path);
    app_check_disk_space(app);
}

static void app_resample_start(App* app) {
    if (!app->plugin) {
        fr_strcpy(app->resample_result_text, sizeof(app->resample_result_text), "Load a plugin first.");
        app->resample_result_is_error = true;
        return;
    }
    if (!app->resample_script.built) {
        fr_strcpy(app->resample_result_text, sizeof(app->resample_result_text), "Build the MIDI first.");
        app->resample_result_is_error = true;
        return;
    }
    if (app->resample_script.sample_rate != app_resample_rate(app)) {
        fr_strcpy(app->resample_result_text, sizeof(app->resample_result_text),
                  "The sample rate changed since the build. Build the MIDI again.");
        app->resample_result_is_error = true;
        return;
    }
    if (!plat_dir_create(app->settings.resample.output_dir)) {
        snprintf(app->resample_result_text, sizeof(app->resample_result_text),
                 "Could not create the output folder %s", app->settings.resample.output_dir);
        app->resample_result_is_error = true;
        return;
    }
    // Checked again here: the disk may have filled up since the build.
    if (!app_check_disk_space(app)) {
        fr_strcpy(app->resample_result_text, sizeof(app->resample_result_text), "Not enough disk space; the render did not start.");
        app->resample_result_is_error = true;
        return;
    }
    // The worker owns the plugin until it is done; the device plays silence.
    // The plugin is switched to the export rate, in offline mode, for the
    // duration; app_resample_finish() puts it back on the device.
    app_pause_plugin(app);
    int block_size = app->audio.open ? app->audio.block_size : app->settings.block_size;
    double export_rate = app->resample_script.sample_rate;
    if (!host_plugin_setup_processing(app->plugin, export_rate, block_size, true)) {
        snprintf(app->resample_result_text, sizeof(app->resample_result_text),
                 "The plugin refused %.0f Hz. Pick another export rate.", export_rate);
        app->resample_result_is_error = true;
        host_plugin_setup_processing(app->plugin, app_device_sample_rate(app), block_size, false);
        app_resume_plugin(app);
        return;
    }
    if (!resample_start(&app->resample_job, app->plugin, &app->resample_script, block_size, &app->settings.resample)) {
        fr_strcpy(app->resample_result_text, sizeof(app->resample_result_text), "The render could not start.");
        app->resample_result_is_error = true;
        host_plugin_setup_processing(app->plugin, app_device_sample_rate(app), block_size, false);
        app_resume_plugin(app);
        return;
    }
    app->resample_result_text[0] = '\0';
    app->resample_result_is_error = false;
    app->resample_fsi_ready = false;
    app->resample_full_wav_removed = false;
    app->instrument_result_text[0] = '\0';
    app_set_status(app, false, "Rendering...");
}

static void app_resample_finish(App* app) {
    if (!resample_collect(&app->resample_job)) return;
    ResampleJob* job = &app->resample_job;
    fr_strcpy(app->resample_result_text, sizeof(app->resample_result_text), job->result);
    app->resample_result_is_error = job->result_is_error;
    app_set_status(app, job->result_is_error, "%s", job->result);
    app->resample_fsi_ready = !job->result_is_error && !job->cancel.load() &&
                              job->files_written == app->resample_script.segment_count;
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
        fr_strcpy(app->instrument_result_text, sizeof(app->instrument_result_text), "Render the samples first.");
        app->instrument_result_is_error = true;
        return;
    }
    char prefix[RESAMPLE_FILE_NAME_LEN];
    char err[FR_STATUS_LEN];
    if (!resample_prefix(&app->settings.resample, prefix, sizeof(prefix), err, sizeof(err))) {
        fr_strcpy(app->instrument_result_text, sizeof(app->instrument_result_text), err);
        app->instrument_result_is_error = true;
        return;
    }
    const char* export_dir = app->settings.resample.export_dir;
    if (export_dir[0] == '\0') export_dir = app->resample_job.out_dir;
    if (!plat_dir_create(export_dir)) {
        snprintf(app->instrument_result_text, sizeof(app->instrument_result_text),
                 "Could not create the export folder %s", export_dir);
        app->instrument_result_is_error = true;
        return;
    }
    // The bank is cut from the same full WAV the samples came from.
    if (!instrument_start(&app->instrument_job, &app->resample_script, &app->settings.resample,
                          app->resample_job.full_path, export_dir, prefix)) {
        fr_strcpy(app->instrument_result_text, sizeof(app->instrument_result_text), "The instrument build could not start.");
        app->instrument_result_is_error = true;
        return;
    }
    app->instrument_result_text[0] = '\0';
    app->instrument_result_is_error = false;
    app_set_status(app, false, "Building the instrument...");
}

static void app_instrument_finish(App* app) {
    if (!instrument_collect(&app->instrument_job)) return;
    fr_strcpy(app->instrument_result_text, sizeof(app->instrument_result_text), app->instrument_job.result);
    app->instrument_result_is_error = app->instrument_job.result_is_error;
    if (app->instrument_job.full_wav_removed) {
        app->resample_full_wav_removed = true;
        app->resample_fsi_ready = false;   // there is nothing left to cut a bank from
    }
    app_set_status(app, app->instrument_job.result_is_error, "%s", app->instrument_job.result);
}

// What the tab shows under the project buttons, and the status line with it.
static void app_project_result(App* app, bool is_error, const char* text) {
    fr_strcpy(app->project_result_text, sizeof(app->project_result_text), text);
    app->project_result_is_error = is_error;
    app_set_status(app, is_error, "%s", text);
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
    char text[FR_STATUS_LEN];

    if (app->project_save_request) {
        app->project_save_request = false;
        if (busy) {
            app_project_result(app, true, "Wait for the render to finish, or cancel it.");
        } else if (project_save(&app->settings.resample, app->settings.project_name, err, sizeof(err))) {
            snprintf(text, sizeof(text), "Project %s saved.", app->settings.project_name);
            app_project_result(app, false, text);
            settings_save(&app->settings, app->settings_path);
            project_scan(&app->projects);
        } else {
            app_project_result(app, true, err);
        }
    }

    if (app->project_load_request) {
        app->project_load_request = false;
        if (busy) {
            app_project_result(app, true, "Wait for the render to finish, or cancel it.");
        } else if (project_load(&app->settings.resample, app->project_load_name, err, sizeof(err))) {
            fr_strcpy(app->settings.project_name, sizeof(app->settings.project_name), app->project_load_name);
            settings_save(&app->settings, app->settings_path);
            // The script and the samples belong to the settings that made
            // them, so the tab starts again at Build MIDI.
            app->resample_script.built = false;
            app->resample_fsi_ready = false;
            app->resample_full_wav_removed = false;
            app->resample_build_text[0] = '\0';
            app->resample_result_text[0] = '\0';
            app->instrument_result_text[0] = '\0';
            snprintf(text, sizeof(text), "Project %s loaded.", app->settings.project_name);
            app_project_result(app, false, text);
        } else {
            app_project_result(app, true, err);
        }
    }
}

void app_process_requests(App* app) {
    app_process_project_requests(app);

    // A VST 2 editor repaints on this call, on the UI thread, a render in
    // progress or not: that is the plugin's own thread split.
    if (app->plugin && app->editor_open) host_plugin_editor_idle(app->plugin);

    // The folder pickers are modal and pump messages; they run here, outside
    // the ImGui frame, so a repaint meanwhile cannot re-enter the UI.
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

    // The meters: take the peaks the audio thread left, decay towards them.
    for (int c = 0; c < FR_OUTPUT_CHANNELS; ++c) {
        float peak = app->peak[c].exchange(0.0f);
        float decayed = app->meter[c] * UI_METER_DECAY;
        app->meter[c] = peak > decayed ? peak : decayed;
    }
}

void app_request_load_path(App* app, const char* path) {
    if (!path || !path[0]) return;
    if (settings_is_banned(&app->settings, path)) {
        app_set_status(app, true, "%s is banned. Unban it from the list's right-click menu.", plat_path_basename(path));
        return;
    }
    int format = host_format_of_path(path);
    PluginList* list = &app->plugins[format];
    // The list of that format comes to the front, so the row shows.
    app->settings.plugin_tab = format;
    app->plugin_tab_select_request = true;
    for (int i = 0; i < list->count; ++i) {
        if (fr_stricmp(list->entries[i].path, path) == 0) {
            app->selected_plugin[format] = i;
            app->load_request = i;
            app->load_request_format = format;
            return;
        }
    }
    if (!plat_path_exists(path)) {
        app_set_status(app, true, "Not found: %s", path);
        return;
    }
    if (list->count >= FR_MAX_PLUGINS) return;
    PluginEntry* e = &list->entries[list->count];
    fr_strcpy(e->path, FR_MAX_PATH, path);
    fr_strcpy(e->name, FR_PLUGIN_NAME_LEN, plat_path_basename(path));
    fr_strip_plugin_ext(e->name);
    app->selected_plugin[format] = list->count;
    app->load_request = list->count;
    app->load_request_format = format;
    list->count++;
}

void app_audio_render(void* user, float* const* outputs, int channel_count, int frames) {
    App* app = (App*)user;
    app->audio_busy.fetch_add(1, std::memory_order_seq_cst);
    if (app->plugin_active.load(std::memory_order_seq_cst)) {
        host_plugin_process(app->plugin, &app->midi, outputs, channel_count, frames);
    } else {
        // Nobody listens: events must not pile up for the next plugin.
        MidiEvent m;
        while (midi_queue_pop(&app->midi, &m)) {}
    }
    for (int c = 0; c < channel_count && c < FR_OUTPUT_CHANNELS; ++c) {
        float peak = 0.0f;
        for (int i = 0; i < frames; ++i) {
            float a = outputs[c][i] < 0.0f ? -outputs[c][i] : outputs[c][i];
            if (a > peak) peak = a;
        }
        if (peak > app->peak[c].load(std::memory_order_relaxed)) app->peak[c].store(peak, std::memory_order_relaxed);
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
