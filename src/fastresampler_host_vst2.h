#pragma once

#include <stddef.h>
#include "fastresampler_constants.h"
#include "fastresampler_midi.h"
#include "fastresampler_state.h"
#include "fastresampler_host.h"

struct Vst2Plugin;

Vst2Plugin* vst2_plugin_load(const char* path, double sample_rate, int block_size,
                             HostPluginInfo* info, char* err, size_t err_len);
void        vst2_plugin_unload(Vst2Plugin* p);

bool vst2_plugin_setup_processing(Vst2Plugin* p, double sample_rate, int block_size, bool offline,
                                  HostPluginInfo* info);

void vst2_plugin_process(Vst2Plugin* p, MidiQueue* midi, float* const* outputs, int channel_count, int frames);

void vst2_plugin_process_events(Vst2Plugin* p, const MidiEvent* events, const int* sample_offsets,
                                int count, float* const* outputs, int channel_count, int frames);

bool vst2_plugin_get_state(Vst2Plugin* p, StateBuffer* out);

bool vst2_plugin_set_state(Vst2Plugin* p, const void* data, size_t size, HostPluginInfo* info,
                           char* err, size_t err_len);

bool vst2_plugin_take_io_change(Vst2Plugin* p);

bool vst2_plugin_editor_create(Vst2Plugin* p, int* width, int* height);
bool vst2_plugin_editor_attach(Vst2Plugin* p, void* parent_window, HostResizeFn resize_fn, void* user);
void vst2_plugin_editor_close(Vst2Plugin* p);
void vst2_plugin_editor_idle(Vst2Plugin* p);
void vst2_plugin_editor_size(Vst2Plugin* p, int* width, int* height);

bool vst2_probe(const char* path, char* name, size_t name_len, bool* is_instrument, char* err, size_t err_len);
