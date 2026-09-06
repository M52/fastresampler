// #pragma once (removed for unity build)
// See fastresampler_host_vst2.h.

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "fastresampler_host_vst2.h"
#include "fastresampler_vst2.h"
#include "fastresampler_helpers.h"
#include "fastresampler_log.h"
#include "platform/fastresampler_platform.h"

// The most MIDI events one block carries: what the queue can hold.
#define VST2_MAX_EVENTS FR_MIDI_QUEUE_CAPACITY

// The fewest channel buffers handed to the plugin. A plugin with no input
// may still read inputs[0], so a silent pair is always there.
#define VST2_MIN_CHANNELS 2

// Room for the strings a plugin writes: the ABI says 64 bytes, some write more.
#define VST2_STRING_LEN 256

// The event list as the plugin reads it: the header of Vst2Events, then
// room for every pointer instead of the two the ABI declares.
struct Vst2EventList {
    int32_t    num_events;
    intptr_t   reserved;
    Vst2Event* events[VST2_MAX_EVENTS];
};

struct Vst2Plugin {
    void*        library;
    Vst2Effect*  effect;
    char         path[FR_MAX_PATH];
    char         dir[FR_MAX_PATH];         // what the plugin gets as its directory

    double       sample_rate;
    int          block_size;
    bool         offline;
    bool         open;                     // VST2_EFF_OPEN done
    bool         mains_on;                 // VST2_EFF_MAINS_CHANGED 1 sent
    bool         processing;               // VST2_EFF_START_PROCESS sent
    bool         in_process;               // inside process_replacing

    // The plugin's buffers, block_size frames per channel, made on setup.
    // The inputs stay silent.
    float**      inputs;
    float**      outputs;
    int          input_count;              // buffers, at least VST2_MIN_CHANNELS
    int          output_count;

    Vst2EventList events;
    Vst2MidiEvent midi_events[VST2_MAX_EVENTS];
    Vst2TimeInfo  time;

    bool         editor_open;
    HostResizeFn resize_fn;
    void*        resize_user;
    int          editor_width;
    int          editor_height;
};

// The plugin being created: its entry point calls the host back before
// there is an effect to hang the instance on. Loads run on the UI thread
// only, so one slot is enough.
static Vst2Plugin* g_vst2_loading = nullptr;

static Vst2Plugin* vst2_from_effect(Vst2Effect* effect) {
    if (effect && effect->resvd2) return (Vst2Plugin*)effect->resvd2;
    return g_vst2_loading;
}

static intptr_t vst2_dispatch(Vst2Plugin* p, int32_t opcode, int32_t index = 0, intptr_t value = 0,
                              void* ptr = nullptr, float opt = 0.0f) {
    return p->effect->dispatcher(p->effect, opcode, index, value, ptr, opt);
}

// --- The host, as the plugin sees it -------------------------------------------

static intptr_t vst2_can_do(const char* feature) {
    static const char* const yes[] = {
        "sendVstEvents", "sendVstMidiEvent", "sendVstTimeInfo", "sizeWindow", "supplyIdle"
    };
    if (!feature) return 0;
    for (size_t i = 0; i < sizeof(yes) / sizeof(yes[0]); ++i) {
        if (strcmp(feature, yes[i]) == 0) return 1;
    }
    return 0;
}

static intptr_t VST2_CALL vst2_audio_master(Vst2Effect* effect, int32_t opcode, int32_t index,
                                            intptr_t value, void* ptr, float opt) {
    Vst2Plugin* p = vst2_from_effect(effect);
    switch (opcode) {
        case VST2_AM_VERSION:
            return VST2_VERSION;
        case VST2_AM_CURRENT_ID:
            return 0;
        case VST2_AM_GET_TIME:
            return p ? (intptr_t)&p->time : 0;
        case VST2_AM_GET_SAMPLE_RATE:
            return p && p->sample_rate > 0.0 ? (intptr_t)(p->sample_rate + 0.5) : (intptr_t)FR_DEFAULT_SAMPLE_RATE;
        case VST2_AM_GET_BLOCK_SIZE:
            return p && p->block_size > 0 ? p->block_size : FR_DEFAULT_BLOCK_SIZE;
        case VST2_AM_GET_CURRENT_PROCESS_LEVEL:
            if (!p || !p->in_process) return VST2_PROCESS_LEVEL_USER;
            return p->offline ? VST2_PROCESS_LEVEL_OFFLINE : VST2_PROCESS_LEVEL_REALTIME;
        case VST2_AM_GET_AUTOMATION_STATE:
            return 1;   // off
        case VST2_AM_SIZE_WINDOW:
            // The editor wants another size: the view resizes its parent.
            if (!p || !p->editor_open || index <= 0 || value <= 0) return 0;
            p->editor_width = index;
            p->editor_height = (int)value;
            if (p->resize_fn) p->resize_fn(p->resize_user, index, (int)value);
            return 1;
        case VST2_AM_GET_VENDOR_STRING:
        case VST2_AM_GET_PRODUCT_STRING:
            if (ptr) fr_strcpy((char*)ptr, VST2_MAX_VENDOR_LEN, FR_APP_NAME);
            return 1;
        case VST2_AM_GET_VENDOR_VERSION:
            return 1;
        case VST2_AM_CAN_DO:
            return vst2_can_do((const char*)ptr);
        case VST2_AM_GET_LANGUAGE:
            return VST2_LANGUAGE_ENGLISH;
        case VST2_AM_GET_DIRECTORY:
            return p ? (intptr_t)p->dir : 0;
        case VST2_AM_UPDATE_DISPLAY:
            return 1;
        default:
            // Automation, edits, idle requests and the deprecated calls:
            // nothing to do for a host without automation.
            return 0;
    }
}

// --- Loading -------------------------------------------------------------------

static void vst2_fail(char* err, size_t err_len, const char* msg) {
    fr_strcpy(err, err_len, msg);
    fr_log("vst2: %s", msg);
}

// A string the plugin writes for an opcode; empty when it writes none.
static void vst2_read_string(Vst2Plugin* p, int32_t opcode, char* out, size_t out_len) {
    char buf[VST2_STRING_LEN];
    memset(buf, 0, sizeof(buf));
    vst2_dispatch(p, opcode, 0, 0, buf, 0.0f);
    buf[VST2_STRING_LEN - 1] = '\0';
    fr_strcpy(out, out_len, buf);
}

static bool vst2_is_instrument(Vst2Plugin* p, intptr_t category) {
    if (category == VST2_CATEGORY_SHELL) return false;
    return (p->effect->flags & VST2_FLAG_IS_SYNTH) != 0 || category == VST2_CATEGORY_SYNTH;
}

// The name the list and the header show: the effect's own, else the
// product's, else the file's.
static void vst2_read_name(Vst2Plugin* p, char* out, size_t out_len) {
    vst2_read_string(p, VST2_EFF_GET_EFFECT_NAME, out, out_len);
    if (out[0]) return;
    vst2_read_string(p, VST2_EFF_GET_PRODUCT_STRING, out, out_len);
    if (out[0]) return;
    fr_strcpy(out, out_len, plat_path_basename(p->path));
    fr_strip_plugin_ext(out);
}

// Loads the DLL, creates the plugin and opens it. What load and probe share.
static bool vst2_open(Vst2Plugin* p, const char* path, char* err, size_t err_len) {
    fr_strcpy(p->path, sizeof(p->path), path);
    fr_strcpy(p->dir, sizeof(p->dir), path);
    size_t cut = 0;
    for (size_t i = 0; p->dir[i]; ++i) {
        if (p->dir[i] == '\\' || p->dir[i] == '/') cut = i + 1;
    }
    p->dir[cut] = '\0';

    p->library = plat_library_load(path, err, err_len);
    if (!p->library) return false;
    Vst2EntryFn entry = (Vst2EntryFn)plat_library_symbol(p->library, "VSTPluginMain");
    if (!entry) entry = (Vst2EntryFn)plat_library_symbol(p->library, "main");
    if (!entry) {
        vst2_fail(err, err_len, "The DLL has no VST 2 entry point");
        return false;
    }
    g_vst2_loading = p;
    Vst2Effect* effect = entry(vst2_audio_master);
    g_vst2_loading = nullptr;
    if (!effect) {
        vst2_fail(err, err_len, "The plugin did not create itself");
        return false;
    }
    if (effect->magic != VST2_EFFECT_MAGIC) {
        vst2_fail(err, err_len, "The DLL is not a VST 2 plugin");
        return false;
    }
    p->effect = effect;
    effect->resvd2 = (intptr_t)p;
    vst2_dispatch(p, VST2_EFF_OPEN);
    p->open = true;
    return true;
}

// Undoes vst2_open: stops, closes and frees the DLL.
static void vst2_close(Vst2Plugin* p) {
    if (p->effect && p->open) {
        if (p->processing) vst2_dispatch(p, VST2_EFF_STOP_PROCESS);
        if (p->mains_on) vst2_dispatch(p, VST2_EFF_MAINS_CHANGED, 0, 0);
        vst2_dispatch(p, VST2_EFF_CLOSE);
    }
    p->processing = false;
    p->mains_on = false;
    p->open = false;
    p->effect = nullptr;
    if (p->library) plat_library_unload(p->library);
    p->library = nullptr;
}

static void vst2_free_buffers(Vst2Plugin* p) {
    if (p->inputs) {
        for (int c = 0; c < p->input_count; ++c) free(p->inputs[c]);
        free(p->inputs);
    }
    if (p->outputs) {
        for (int c = 0; c < p->output_count; ++c) free(p->outputs[c]);
        free(p->outputs);
    }
    p->inputs = nullptr;
    p->outputs = nullptr;
    p->input_count = 0;
    p->output_count = 0;
}

static float** vst2_alloc_channels(int count, int frames) {
    float** channels = (float**)calloc((size_t)count, sizeof(float*));
    if (!channels) return nullptr;
    for (int c = 0; c < count; ++c) {
        channels[c] = (float*)calloc((size_t)frames, sizeof(float));
        if (!channels[c]) {
            for (int k = 0; k < c; ++k) free(channels[k]);
            free(channels);
            return nullptr;
        }
    }
    return channels;
}

static bool vst2_alloc_buffers(Vst2Plugin* p, int frames) {
    vst2_free_buffers(p);
    int in = p->effect->num_inputs > VST2_MIN_CHANNELS ? p->effect->num_inputs : VST2_MIN_CHANNELS;
    int out = p->effect->num_outputs > VST2_MIN_CHANNELS ? p->effect->num_outputs : VST2_MIN_CHANNELS;
    p->inputs = vst2_alloc_channels(in, frames);
    p->outputs = vst2_alloc_channels(out, frames);
    p->input_count = p->inputs ? in : 0;
    p->output_count = p->outputs ? out : 0;
    if (!p->inputs || !p->outputs) {
        vst2_free_buffers(p);
        return false;
    }
    return true;
}

Vst2Plugin* vst2_plugin_load(const char* path, double sample_rate, int block_size,
                             HostPluginInfo* info, char* err, size_t err_len) {
    err[0] = '\0';
    Vst2Plugin* p = (Vst2Plugin*)calloc(1, sizeof(Vst2Plugin));
    if (!p) {
        vst2_fail(err, err_len, "Out of memory");
        return nullptr;
    }
    if (!vst2_open(p, path, err, err_len)) {
        vst2_plugin_unload(p);
        return nullptr;
    }
    Vst2Effect* e = p->effect;
    intptr_t category = vst2_dispatch(p, VST2_EFF_GET_PLUG_CATEGORY);
    if (category == VST2_CATEGORY_SHELL) {
        vst2_fail(err, err_len, "A shell plugin holds several plugins and is not supported");
        vst2_plugin_unload(p);
        return nullptr;
    }
    if (!vst2_is_instrument(p, category)) {
        vst2_fail(err, err_len, "The plugin is an effect, not an instrument");
        vst2_plugin_unload(p);
        return nullptr;
    }
    if (!(e->flags & VST2_FLAG_CAN_REPLACING) || !e->process_replacing) {
        vst2_fail(err, err_len, "The plugin does not process 32 bit float");
        vst2_plugin_unload(p);
        return nullptr;
    }
    if (e->num_outputs <= 0) {
        vst2_fail(err, err_len, "The plugin has no audio output");
        vst2_plugin_unload(p);
        return nullptr;
    }

    memset(info, 0, sizeof(*info));
    info->format = HOST_FORMAT_VST2;
    vst2_read_name(p, info->name, sizeof(info->name));
    vst2_read_string(p, VST2_EFF_GET_VENDOR_STRING, info->vendor, sizeof(info->vendor));
    intptr_t vendor_version = vst2_dispatch(p, VST2_EFF_GET_VENDOR_VERSION);
    if (vendor_version == 0) vendor_version = e->version;
    if (vendor_version != 0) snprintf(info->version, sizeof(info->version), "%d", (int)vendor_version);
    fr_strcpy(info->category, sizeof(info->category), "Instrument (VST 2)");
    info->output_channels = e->num_outputs;
    info->has_midi_mapping = true;   // every MIDI message reaches a VST 2 plugin as it is
    info->has_controller = (e->flags & VST2_FLAG_HAS_EDITOR) != 0;

    vst2_dispatch(p, VST2_EFF_SET_PROCESS_PRECISION, 0, VST2_PRECISION_FLOAT);
    if (!vst2_plugin_setup_processing(p, sample_rate, block_size, false, info)) {
        vst2_fail(err, err_len, "The plugin refused the processing setup");
        vst2_plugin_unload(p);
        return nullptr;
    }
    fr_log("vst2: loaded '%s' by %s, %d inputs, %d outputs, latency %d, editor %s",
           info->name, info->vendor, e->num_inputs, e->num_outputs, info->latency_frames,
           info->has_controller ? "yes" : "no");
    return p;
}

void vst2_plugin_unload(Vst2Plugin* p) {
    if (!p) return;
    vst2_plugin_editor_close(p);
    vst2_close(p);
    vst2_free_buffers(p);
    free(p);
}

bool vst2_plugin_setup_processing(Vst2Plugin* p, double sample_rate, int block_size, bool offline,
                                  HostPluginInfo* info) {
    if (!p->effect || block_size <= 0) return false;
    if (p->processing) {
        vst2_dispatch(p, VST2_EFF_STOP_PROCESS);
        p->processing = false;
    }
    if (p->mains_on) {
        vst2_dispatch(p, VST2_EFF_MAINS_CHANGED, 0, 0);
        p->mains_on = false;
    }
    p->sample_rate = sample_rate;
    p->block_size = block_size;
    p->offline = offline;
    if (!vst2_alloc_buffers(p, block_size)) {
        fr_log("vst2: no memory for %d frames of buffers", block_size);
        return false;
    }
    memset(&p->time, 0, sizeof(p->time));
    p->time.sample_rate = sample_rate;
    p->time.tempo = FR_DEFAULT_TEMPO;
    p->time.time_sig_numerator = 4;
    p->time.time_sig_denominator = 4;
    p->time.flags = VST2_TIME_TRANSPORT_PLAYING | VST2_TIME_TEMPO_VALID | VST2_TIME_SIG_VALID | VST2_TIME_PPQ_POS_VALID;
    p->events.num_events = 0;

    vst2_dispatch(p, VST2_EFF_SET_SAMPLE_RATE, 0, 0, nullptr, (float)sample_rate);
    vst2_dispatch(p, VST2_EFF_SET_BLOCK_SIZE, 0, block_size);
    vst2_dispatch(p, VST2_EFF_MAINS_CHANGED, 0, 1);
    p->mains_on = true;
    vst2_dispatch(p, VST2_EFF_START_PROCESS);
    p->processing = true;
    info->latency_frames = p->effect->initial_delay;
    return true;
}

// --- Processing (audio thread) -------------------------------------------------

static void vst2_add_midi(Vst2Plugin* p, const MidiEvent& m, int sample_offset) {
    if (p->events.num_events >= VST2_MAX_EVENTS) return;
    Vst2MidiEvent* e = &p->midi_events[p->events.num_events];
    memset(e, 0, sizeof(*e));
    e->type = VST2_EVENT_TYPE_MIDI;
    e->byte_size = (int32_t)sizeof(Vst2MidiEvent);
    e->delta_frames = sample_offset;
    e->flags = VST2_MIDI_EVENT_IS_REALTIME;
    e->midi_data[0] = (char)(m.status | m.channel);
    e->midi_data[1] = (char)m.data0;
    e->midi_data[2] = (char)m.data1;
    p->events.events[p->events.num_events++] = (Vst2Event*)e;
}

// Runs one block once this block's events are queued.
static void vst2_run_block(Vst2Plugin* p, float* const* outputs, int channel_count, int frames) {
    if (p->events.num_events > 0) vst2_dispatch(p, VST2_EFF_PROCESS_EVENTS, 0, 0, &p->events);

    // Silent inputs and clean outputs every block: a plugin may write either.
    for (int c = 0; c < p->input_count; ++c) memset(p->inputs[c], 0, (size_t)frames * sizeof(float));
    for (int c = 0; c < p->output_count; ++c) memset(p->outputs[c], 0, (size_t)frames * sizeof(float));

    p->in_process = true;
    p->effect->process_replacing(p->effect, p->inputs, p->outputs, frames);
    p->in_process = false;

    int plugin_channels = p->effect->num_outputs;
    for (int c = 0; c < channel_count; ++c) {
        // A mono plugin feeds both device channels.
        int src = c < plugin_channels ? c : plugin_channels - 1;
        if (src < 0 || src >= p->output_count) continue;
        memcpy(outputs[c], p->outputs[src], (size_t)frames * sizeof(float));
    }

    p->time.sample_pos += (double)frames;
    p->time.ppq_pos = p->time.sample_pos / p->sample_rate * (p->time.tempo / 60.0);
    p->events.num_events = 0;
}

void vst2_plugin_process(Vst2Plugin* p, MidiQueue* midi, float* const* outputs, int channel_count, int frames) {
    if (frames > p->block_size) frames = p->block_size;
    MidiEvent m;
    while (midi_queue_pop(midi, &m)) vst2_add_midi(p, m, 0);
    vst2_run_block(p, outputs, channel_count, frames);
}

void vst2_plugin_process_events(Vst2Plugin* p, const MidiEvent* events, const int* sample_offsets,
                                int count, float* const* outputs, int channel_count, int frames) {
    if (frames > p->block_size) frames = p->block_size;
    for (int i = 0; i < count; ++i) {
        int offset = sample_offsets[i];
        if (offset < 0) offset = 0;
        if (offset >= frames) offset = frames - 1;
        vst2_add_midi(p, events[i], offset);
    }
    vst2_run_block(p, outputs, channel_count, frames);
}

// --- Editor --------------------------------------------------------------------

// The editor's size as the plugin reports it now; false when it reports none.
static bool vst2_editor_rect(Vst2Plugin* p, int* width, int* height) {
    Vst2Rect* rect = nullptr;
    vst2_dispatch(p, VST2_EFF_EDIT_GET_RECT, 0, 0, &rect);
    if (!rect) return false;
    int w = rect->right - rect->left;
    int h = rect->bottom - rect->top;
    if (w <= 0 || h <= 0) return false;
    *width = w;
    *height = h;
    return true;
}

bool vst2_plugin_editor_create(Vst2Plugin* p, int* width, int* height) {
    *width = 0;
    *height = 0;
    if (!p->effect || !(p->effect->flags & VST2_FLAG_HAS_EDITOR)) return false;
    vst2_plugin_editor_close(p);
    vst2_editor_rect(p, width, height);
    p->editor_width = *width;
    p->editor_height = *height;
    return true;
}

bool vst2_plugin_editor_attach(Vst2Plugin* p, void* parent_window, HostResizeFn resize_fn, void* user) {
    if (!p->effect || !parent_window) return false;
    p->resize_fn = resize_fn;
    p->resize_user = user;
    // The return value is not trusted: plugins answer 0 for an editor that
    // did open. The rectangle after the open says what happened.
    vst2_dispatch(p, VST2_EFF_EDIT_OPEN, 0, 0, parent_window);
    p->editor_open = true;

    // Some plugins only know their size once the editor is open.
    int w = 0, h = 0;
    if (vst2_editor_rect(p, &w, &h) && (w != p->editor_width || h != p->editor_height)) {
        p->editor_width = w;
        p->editor_height = h;
        if (resize_fn) resize_fn(user, w, h);
    }
    return true;
}

void vst2_plugin_editor_close(Vst2Plugin* p) {
    if (!p->editor_open) return;
    if (p->effect) vst2_dispatch(p, VST2_EFF_EDIT_CLOSE);
    p->editor_open = false;
    p->resize_fn = nullptr;
    p->resize_user = nullptr;
}

bool vst2_plugin_editor_is_open(const Vst2Plugin* p) {
    return p->editor_open;
}

void vst2_plugin_editor_idle(Vst2Plugin* p) {
    if (p->editor_open && p->effect) vst2_dispatch(p, VST2_EFF_EDIT_IDLE);
}

void vst2_plugin_editor_size(Vst2Plugin* p, int* width, int* height) {
    int w = 0, h = 0;
    if (p->editor_open && vst2_editor_rect(p, &w, &h)) {
        p->editor_width = w;
        p->editor_height = h;
    }
    *width = p->editor_width;
    *height = p->editor_height;
}

// --- The scan's probe ------------------------------------------------------------

bool vst2_probe(const char* path, char* name, size_t name_len, bool* is_instrument, char* err, size_t err_len) {
    err[0] = '\0';
    name[0] = '\0';
    *is_instrument = false;
    Vst2Plugin* p = (Vst2Plugin*)calloc(1, sizeof(Vst2Plugin));
    if (!p) return false;
    bool ok = vst2_open(p, path, err, err_len);
    if (ok) {
        intptr_t category = vst2_dispatch(p, VST2_EFF_GET_PLUG_CATEGORY);
        *is_instrument = vst2_is_instrument(p, category);
        vst2_read_name(p, name, name_len);
    }
    vst2_close(p);
    free(p);
    return ok;
}
