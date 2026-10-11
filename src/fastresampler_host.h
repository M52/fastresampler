#pragma once

#include <stddef.h>
#include "fastresampler_constants.h"
#include "fastresampler_midi.h"
#include "fastresampler_state.h"

struct HostContext;
struct HostPlugin;

enum HostPluginFormat {
    HOST_FORMAT_VST3 = 0,
    HOST_FORMAT_VST2,
    HOST_FORMAT_COUNT
};

struct HostPluginInfo {
    int  format;
    char name[FR_PLUGIN_NAME_LEN];
    char vendor[64];
    char version[64];
    char category[128];
    char id[128];
    int  output_channels;
    int  total_output_channels;
    int  output_bus_count;
    int  latency_frames;
    bool has_midi_mapping;
    bool has_controller;
};

const char* host_format_name(int format);

int host_format_of_path(const char* path);

typedef void (*HostResizeFn)(void* user, int width, int height);

HostContext* host_create(void);
void         host_destroy(HostContext* ctx);

HostPlugin* host_plugin_load(HostContext* ctx, const char* path, double sample_rate,
                             int block_size, char* err, size_t err_len);
void        host_plugin_unload(HostPlugin* p);

bool host_plugin_setup_processing(HostPlugin* p, double sample_rate, int block_size, bool offline);

void host_plugin_process(HostPlugin* p, MidiQueue* midi, float* const* outputs,
                         int channel_count, int frames);

void host_plugin_process_events(HostPlugin* p, const MidiEvent* events, const int* sample_offsets,
                                int count, float* const* outputs, int channel_count, int frames);

const HostPluginInfo* host_plugin_info(const HostPlugin* p);

bool host_plugin_get_state(HostPlugin* p, StateBuffer* out);

bool host_plugin_set_state(HostPlugin* p, const void* data, size_t size, char* err, size_t err_len);

bool host_plugin_take_io_change(HostPlugin* p);

bool host_plugin_editor_create(HostPlugin* p, float content_scale, int* width, int* height);
bool host_plugin_editor_attach(HostPlugin* p, void* parent_window, HostResizeFn resize_fn, void* user);
void host_plugin_editor_close(HostPlugin* p);
void host_plugin_editor_set_scale(HostPlugin* p, float content_scale);
void host_plugin_editor_idle(HostPlugin* p);
void host_plugin_editor_fit(HostPlugin* p, int avail_width, int avail_height, int* width, int* height);
