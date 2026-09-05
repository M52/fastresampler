// #pragma once (removed for unity build)
#include <stdio.h>
#include <math.h>
#include "imgui.h"
#include "ui/fastresampler_ui.h"
#include "ui/fastresampler_palette.h"
#include "fastresampler_app.h"
#include "fastresampler_helpers.h"
#include "platform/fastresampler_platform.h"

// Height of the status strip at the bottom of the side panel.
#define UI_STATUS_HEIGHT 44.0f

// The meter's floor and the width of its bars.
#define UI_METER_MIN_DB   -60.0f
#define UI_METER_HEIGHT   10.0f

// --- Small pieces --------------------------------------------------------------

static float ui_db(float amplitude) {
    if (amplitude <= 0.000001f) return UI_METER_MIN_DB;
    float db = 20.0f * log10f(amplitude);
    return db < UI_METER_MIN_DB ? UI_METER_MIN_DB : db;
}

static ImU32 ui_meter_color(float db) {
    if (db < METER_AMBER_DB) return METER_COLOR_GREEN_U32;
    if (db < 0.0f) return METER_COLOR_AMBER_U32;
    return METER_COLOR_RED_U32;
}

// Two horizontal bars, left and right, in dB from the meter floor.
static void ui_draw_meters(App* app, float scale) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float width = ImGui::GetContentRegionAvail().x;
    float bar_h = UI_METER_HEIGHT * scale;
    for (int c = 0; c < FR_OUTPUT_CHANNELS; ++c) {
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImVec2 end = ImVec2(pos.x + width, pos.y + bar_h);
        dl->AddRectFilled(pos, end, CODE_COLOR_PANEL_BG_U32, 2.0f);
        float db = ui_db(app->meter[c]);
        float fill = (db - UI_METER_MIN_DB) / (0.0f - UI_METER_MIN_DB);
        if (fill > 1.0f) fill = 1.0f;
        if (fill > 0.0f) {
            dl->AddRectFilled(pos, ImVec2(pos.x + width * fill, end.y), ui_meter_color(db), 2.0f);
        }
        dl->AddRect(pos, end, CODE_COLOR_BORDER_U32, 2.0f);
        ImGui::Dummy(ImVec2(width, bar_h + UI_ITEM_SPACING * scale));
    }
}

static void ui_push_button_colors(ImVec4 normal, ImVec4 hovered, ImVec4 active) {
    ImGui::PushStyleColor(ImGuiCol_Button, normal);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
}

static bool ui_red_button(const char* label, ImVec2 size = ImVec2(0, 0)) {
    ui_push_button_colors(CODE_COLOR_RED_BUTTON, CODE_COLOR_RED_BUTTON_HOVERED, CODE_COLOR_RED_BUTTON_ACTIVE);
    bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return pressed;
}

static bool ui_green_button(const char* label, ImVec2 size = ImVec2(0, 0)) {
    ui_push_button_colors(CODE_COLOR_GREEN_BUTTON, CODE_COLOR_GREEN_BUTTON_HOVERED, CODE_COLOR_GREEN_BUTTON_ACTIVE);
    bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return pressed;
}

// A hint in the dimmed text colour that wraps to the panel width.
static void ui_hint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

// A CC slider: shows the last value sent, sends on change.
static void ui_cc_slider(App* app, const char* label, int controller) {
    int value = app->cc_value[controller];
    char id[64];
    snprintf(id, sizeof(id), "%s (CC%d)", label, controller);
    if (ImGui::SliderInt(id, &value, 0, MIDI_VALUE_MAX)) app_control_change(app, controller, value);
}

// --- Side panel tabs -----------------------------------------------------------

static void ui_tab_plugin(App* app, float scale) {
    if (!app->plugin) {
        ImGui::TextWrapped("No plugin loaded.");
        ImGui::TextDisabled("Double-click a plugin in the list to load it.");
        return;
    }
    const HostPluginInfo* info = host_plugin_info(app->plugin);
    ImGui::TextWrapped("%s", info->name);
    ImGui::TextDisabled("by %s", info->vendor);
    if (info->version[0]) ImGui::TextDisabled("Version %s", info->version);
    if (info->category[0]) ImGui::TextDisabled("%s", info->category);
    ImGui::Separator();

    ImGui::Text("Output channels: %d", info->output_channels);
    ImGui::Text("Latency: %d frames", info->latency_frames);
    ImGui::Text("MIDI CC mapping: %s", info->has_midi_mapping ? "yes" : "no");
    if (app->editor_open) {
        ImGui::Text("Editor: %d x %d", app->editor_width, app->editor_height);
    } else {
        ImGui::Text("Editor: none");
    }
    ImGui::Separator();

    ImGui::Text("Output");
    ui_draw_meters(app, scale);
    ImGui::Separator();

    if (ui_red_button("Close plugin", ImVec2(-1.0f, 0.0f))) app->unload_request = true;
}

static void ui_tab_midi(App* app, float scale) {
    int channel = app->settings.midi_channel;
    char channel_label[16];
    snprintf(channel_label, sizeof(channel_label), "%d", channel + 1);
    ImGui::SetNextItemWidth(80.0f * scale);
    if (ImGui::BeginCombo("MIDI channel", channel_label)) {
        for (int i = 0; i < MIDI_CHANNEL_COUNT; ++i) {
            char item[16];
            snprintf(item, sizeof(item), "%d", i + 1);
            if (ImGui::Selectable(item, i == channel)) app->settings.midi_channel = i;
        }
        ImGui::EndCombo();
    }
    ui_hint("Click a key to play it. Lower on the key is louder.");
    ImGui::Separator();

    ImGui::Text("Controllers");
    ImGui::PushItemWidth(-90.0f * scale);
    ui_cc_slider(app, "Mod wheel", MIDI_CC_MOD_WHEEL);
    ui_cc_slider(app, "Breath", MIDI_CC_BREATH);
    ui_cc_slider(app, "Volume", MIDI_CC_VOLUME);
    ui_cc_slider(app, "Pan", MIDI_CC_PAN);
    ui_cc_slider(app, "Expression", MIDI_CC_EXPRESSION);

    bool sustain = app->cc_value[MIDI_CC_SUSTAIN] >= 64;
    if (ImGui::Checkbox("Sustain pedal (CC64)", &sustain)) app_control_change(app, MIDI_CC_SUSTAIN, sustain ? MIDI_VALUE_MAX : 0);

    int bend = app->pitch_bend - MIDI_PITCH_BEND_CENTER;
    if (ImGui::SliderInt("Pitch bend", &bend, -MIDI_PITCH_BEND_CENTER, MIDI_PITCH_BEND_CENTER - 1)) {
        app_pitch_bend(app, bend + MIDI_PITCH_BEND_CENTER);
    }
    ImGui::PopItemWidth();
    if (ImGui::Button("Centre bend")) app_pitch_bend(app, MIDI_PITCH_BEND_CENTER);
    ImGui::Separator();

    if (app->plugin && !host_plugin_info(app->plugin)->has_midi_mapping) {
        ImGui::PushStyleColor(ImGuiCol_Text, CODE_COLOR_WARNING_VEC4);
        ImGui::TextWrapped("This plugin publishes no MIDI controller mapping, so CC and pitch bend do not reach it.");
        ImGui::PopStyleColor();
        ImGui::Separator();
    }

    if (ui_red_button("All notes off", ImVec2(-1.0f, 0.0f))) app_all_notes_off(app);
}

static void ui_tab_resample(App* app, float scale) {
    ResampleConfig* cfg = &app->settings.resample;
    bool running = app_resample_running(app);
    float label_w = 90.0f * scale;

    ImGui::Text("Naming");
    ImGui::PushItemWidth(-label_w);
    ImGui::InputText("Instrument", cfg->instrument, sizeof(cfg->instrument));
    ImGui::InputText("Articulation", cfg->articulation, sizeof(cfg->articulation));
    ImGui::InputText("Microphone", cfg->microphone, sizeof(cfg->microphone));
    ImGui::PopItemWidth();
    ui_hint("Files are named Microphone_Instrument_Articulation_Note_Velocity_RRn.wav.");
    ImGui::Separator();

    ImGui::Text("Notes");
    ImGui::SetNextItemWidth(60.0f * scale);
    ImGui::InputText("Low", cfg->low_note, sizeof(cfg->low_note));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60.0f * scale);
    ImGui::InputText("High", cfg->high_note, sizeof(cfg->high_note));
    ui_hint("Note names: C3 is middle C, C-2 is note 0.");
    ImGui::SetNextItemWidth(-label_w);
    if (ImGui::BeginCombo("Sample every", resample_step_label(cfg->step))) {
        for (int step = 1; step <= RESAMPLE_MAX_STEP; ++step) {
            if (ImGui::Selectable(resample_step_label(step), step == cfg->step)) cfg->step = step;
        }
        ImGui::EndCombo();
    }
    ImGui::SetNextItemWidth(-label_w);
    ImGui::InputText("Velocities", cfg->velocities, sizeof(cfg->velocities));
    ImGui::SetNextItemWidth(70.0f * scale);
    if (ImGui::InputInt("Round robins", &cfg->round_robins)) {
        cfg->round_robins = fr_clampi(cfg->round_robins, 1, RESAMPLE_MAX_ROUND_ROBINS);
    }
    ImGui::Checkbox("Pre-roll", &cfg->preroll);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60.0f * scale);
    ImGui::BeginDisabled(!cfg->preroll);
    ImGui::InputFloat("Pre-roll gap (s)", &cfg->preroll_gap_seconds, 0.0f, 0.0f, "%.2f");
    ImGui::EndDisabled();
    ui_hint("Pre-roll plays n-1 short notes before round robin n, one per gap, so a sampler that resets its counter after silence lands on variation n. The split discards them.");
    ImGui::Separator();

    ImGui::Text("Timing");
    ImGui::SetNextItemWidth(70.0f * scale);
    ImGui::InputFloat("Hold (s)", &cfg->hold_seconds, 0.0f, 0.0f, "%.2f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f * scale);
    ImGui::InputFloat("Gap (s)", &cfg->gap_seconds, 0.0f, 0.0f, "%.2f");
    ui_hint("Each note is held, then left to ring out for the gap; the sample file spans both.");

    ImGui::Text("Output folder");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##output", cfg->output_dir, sizeof(cfg->output_dir));
    ImGui::Separator();

    ImGui::BeginDisabled(running);
    if (ImGui::Button("Build MIDI", ImVec2(-1.0f, 0.0f))) app_resample_build(app);
    ImGui::EndDisabled();
    if (app->resample_build_text[0]) {
        ImGui::PushStyleColor(ImGuiCol_Text, app->resample_build_is_error ? CODE_COLOR_RED_VEC4 : CODE_COLOR_SLOT_LABEL_TEXT);
        ImGui::TextWrapped("%s", app->resample_build_text);
        ImGui::PopStyleColor();
    }
    ImGui::Separator();

    if (running) {
        const ResampleScript* s = &app->resample_script;
        char overlay[64];
        float fraction;
        if (app->resample_job.phase.load() == RESAMPLE_PHASE_SPLIT) {
            int done = app->resample_job.split_done.load();
            fraction = s->segment_count > 0 ? (float)done / (float)s->segment_count : 0.0f;
            snprintf(overlay, sizeof(overlay), "Splitting %d / %d", done, s->segment_count);
        } else {
            fraction = s->total_frames > 0 ? (float)app->resample_job.progress_frames.load() / (float)s->total_frames : 0.0f;
            snprintf(overlay, sizeof(overlay), "Rendering %.0f%%", fraction * 100.0f);
        }
        ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), overlay);
        if (ui_red_button("Cancel", ImVec2(-1.0f, 0.0f))) resample_cancel(&app->resample_job);
    } else {
        bool can_render = app->plugin != nullptr && app->resample_script.built;
        ImGui::BeginDisabled(!can_render);
        if (ui_green_button("Render to WAV", ImVec2(-1.0f, 0.0f))) app->resample_start_request = true;
        ImGui::EndDisabled();
        if (!app->plugin) {
            ui_hint("Load a plugin to render.");
        } else if (!app->resample_script.built) {
            ui_hint("Build the MIDI to render.");
        } else {
            ui_hint("Renders faster than realtime with the speakers silent, then splits the file into one WAV per sample.");
        }
    }
    if (app->resample_result_text[0]) {
        ImGui::PushStyleColor(ImGuiCol_Text, app->resample_result_is_error ? CODE_COLOR_RED_VEC4 : CODE_COLOR_GREEN_VEC4);
        ImGui::TextWrapped("%s", app->resample_result_text);
        ImGui::PopStyleColor();
    }
}

static void ui_tab_settings(App* app, float scale) {
    ImGui::Text("Plugin folders");
    int remove_index = -1;
    for (int i = 0; i < app->settings.scan_path_count; ++i) {
        char id[32];
        snprintf(id, sizeof(id), "x##path%d", i);
        if (ImGui::SmallButton(id)) remove_index = i;
        ImGui::SameLine();
        ImGui::TextWrapped("%s", app->settings.scan_paths[i]);
    }
    if (remove_index >= 0) {
        settings_remove_scan_path(&app->settings, remove_index);
        app->rescan_request = true;
    }
    ImGui::SetNextItemWidth(-70.0f * scale);
    bool submitted = ImGui::InputTextWithHint("##newpath", "Folder to scan", app->new_scan_path,
                                              sizeof(app->new_scan_path), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Add", ImVec2(-1.0f, 0.0f)) || submitted) {
        if (!plat_path_is_dir(app->new_scan_path)) {
            app_set_status(app, true, "Not a folder: %s", app->new_scan_path);
        } else if (settings_add_scan_path(&app->settings, app->new_scan_path)) {
            app->new_scan_path[0] = '\0';
            app->rescan_request = true;
        } else {
            app_set_status(app, true, "That folder is already in the list.");
        }
    }
    if (ImGui::Button("Rescan plugins", ImVec2(-1.0f, 0.0f))) app->rescan_request = true;
    ImGui::Separator();

    ImGui::Text("Audio output");
    const char* driver_preview = "No devices";
    char preview_buf[FR_DRIVER_NAME_LEN + 16];
    if (app->pending_driver >= 0 && app->pending_driver < app->drivers.count) {
        const AudioDriverInfo* d = &app->drivers.drivers[app->pending_driver];
        snprintf(preview_buf, sizeof(preview_buf), "%s: %s", audio_api_name(d->api), d->name);
        driver_preview = preview_buf;
    }
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##driver", driver_preview)) {
        for (int i = 0; i < app->drivers.count; ++i) {
            const AudioDriverInfo* d = &app->drivers.drivers[i];
            char item[FR_DRIVER_NAME_LEN + 32];
            snprintf(item, sizeof(item), "%s: %s##drv%d", audio_api_name(d->api), d->name, i);
            if (ImGui::Selectable(item, i == app->pending_driver)) app->pending_driver = i;
        }
        ImGui::EndCombo();
    }

    char block_label[32];
    snprintf(block_label, sizeof(block_label), "%d frames", g_block_size_options[app->pending_block_index]);
    ImGui::SetNextItemWidth(-90.0f * scale);
    if (ImGui::BeginCombo("Block size", block_label)) {
        for (int i = 0; i < FR_BLOCK_SIZE_OPTION_COUNT; ++i) {
            char item[32];
            snprintf(item, sizeof(item), "%d frames", g_block_size_options[i]);
            if (ImGui::Selectable(item, i == app->pending_block_index)) app->pending_block_index = i;
        }
        ImGui::EndCombo();
    }
    ui_hint("ASIO drivers may round the block to what they support.");

    if (ui_green_button("Apply", ImVec2(-1.0f, 0.0f))) app->audio_apply_request = true;
    if (ImGui::Button("Refresh devices", ImVec2(-1.0f, 0.0f))) app->audio_refresh_request = true;
    if (audio_has_control_panel(&app->audio)) {
        if (ImGui::Button("ASIO control panel", ImVec2(-1.0f, 0.0f))) audio_control_panel(&app->audio);
    }
    ImGui::Separator();

    if (app->audio.open) {
        ImGui::Text("Running: %s", app->audio.name);
        ImGui::Text("%.0f Hz, %d frames per block", app->audio.sample_rate, app->audio.block_size);
        ImGui::Text("Output latency: %d frames (%.1f ms)", app->audio.latency_frames,
                    1000.0 * (double)app->audio.latency_frames / app->audio.sample_rate);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, CODE_COLOR_WARNING_VEC4);
        ImGui::TextWrapped("No audio device is open.");
        ImGui::PopStyleColor();
    }
}

static void ui_draw_side_panel(App* app, ImVec2 size, float scale) {
    ImGui::BeginChild("##sidepanel", size, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);

    float status_h = UI_STATUS_HEIGHT * scale;
    ImVec2 tabs_size = ImVec2(0.0f, ImGui::GetContentRegionAvail().y - status_h);
    ImGui::BeginChild("##tabs", tabs_size, ImGuiChildFlags_None, ImGuiWindowFlags_None);
    if (ImGui::BeginTabBar("##sidetabs")) {
        if (ImGui::BeginTabItem("Plugin")) {
            ui_tab_plugin(app, scale);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("MIDI")) {
            ui_tab_midi(app, scale);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Resample")) {
            ui_tab_resample(app, scale);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Settings")) {
            ui_tab_settings(app, scale);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, app->status_is_error ? CODE_COLOR_RED_VEC4 : CODE_COLOR_SLOT_LABEL_TEXT);
    ImGui::TextWrapped("%s", app->status);
    ImGui::PopStyleColor();

    ImGui::EndChild();
}

// --- Main area -----------------------------------------------------------------

static void ui_draw_plugin_list(App* app, float scale) {
    ImGui::Text("Available VST3 plugins (%d)", app->plugins.count);
    ImGui::SameLine();
    if (ImGui::SmallButton("Rescan")) app->rescan_request = true;
    if (app->selected_plugin >= 0 && app->selected_plugin < app->plugins.count) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Load selected")) app->load_request = app->selected_plugin;
    }
    ImGui::Separator();

    if (app->plugins.count == 0) {
        ImGui::TextDisabled("No plugins found. Add a folder in the Settings tab.");
        return;
    }

    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("##plugins", 2, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Plugin", ImGuiTableColumnFlags_WidthFixed, 260.0f * scale);
        ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (int i = 0; i < app->plugins.count; ++i) {
            const PluginEntry* e = &app->plugins.entries[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            char id[FR_PLUGIN_NAME_LEN + 16];
            snprintf(id, sizeof(id), "%s##plug%d", e->name, i);
            ImGuiSelectableFlags sel_flags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick;
            if (ImGui::Selectable(id, app->selected_plugin == i, sel_flags)) {
                app->selected_plugin = i;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) app->load_request = i;
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("%s", e->path);
        }
        ImGui::EndTable();
    }
}

// The strip above the plugin editor: its name and a way out.
static void ui_draw_plugin_header(App* app, float scale) {
    const HostPluginInfo* info = host_plugin_info(app->plugin);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", info->name);
    ImGui::SameLine();
    ImGui::TextDisabled("by %s", info->vendor);
    float close_w = 110.0f * scale;
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - close_w);
    if (ui_red_button("Close plugin", ImVec2(close_w, 0.0f))) app->unload_request = true;
    if (!app->editor_open) {
        ImGui::TextDisabled("This plugin has no editor. Play it from the keyboard below.");
    }
}

static void ui_draw_main_area(App* app, ImVec2 size, float scale) {
    ImGui::BeginChild("##main", size, ImGuiChildFlags_Borders, ImGuiWindowFlags_None);
    if (app->plugin) {
        ui_draw_plugin_header(app, scale);
    } else {
        ui_draw_plugin_list(app, scale);
    }
    ImGui::EndChild();
}

// --- Frame ---------------------------------------------------------------------

void ui_draw_frame(App* app, float width, float height, float scale) {
    float side_w = UI_SIDEPANEL_WIDTH * scale;
    float kb_h = UI_KEYBOARD_HEIGHT * scale;
    float panel_h = height - kb_h;
    if (panel_h < 1.0f) panel_h = 1.0f;

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGuiWindowFlags root_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                  ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    ImGui::Begin("##root", nullptr, root_flags);
    ImGui::PopStyleVar(3);

    ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
    ui_draw_side_panel(app, ImVec2(side_w, panel_h), scale);

    ImGui::SetCursorPos(ImVec2(side_w, 0.0f));
    ui_draw_main_area(app, ImVec2(width - side_w, panel_h), scale);

    ImGui::SetCursorPos(ImVec2(0.0f, panel_h));
    ImGui::BeginChild("##keyboard", ImVec2(width, kb_h), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    float pad = UI_PANEL_PADDING * scale;
    ImVec2 kb_pos = ImGui::GetWindowPos();
    ImVec2 kb_size = ImGui::GetWindowSize();
    ui_draw_keyboard(app, ImVec2(kb_pos.x + pad, kb_pos.y + pad), ImVec2(kb_size.x - 2.0f * pad, kb_size.y - 2.0f * pad), scale);
    ImGui::EndChild();

    ImGui::End();
}
