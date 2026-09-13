#pragma once
// VST 2 instrument hosting, behind fastresampler_host.h: the DLL, its
// Vst2Effect, the audio master callback it talks to, the buffers it renders
// into, and its editor. The ABI is declared in fastresampler_vst2.h; no SDK
// is needed. Only instruments are hosted: a DLL that is an effect, or a
// shell that holds several plugins, is refused.
//
// Thread rules are the host's: everything runs on the UI thread except
// vst2_plugin_process() and vst2_plugin_process_events().

#include <stddef.h>
#include "fastresampler_constants.h"
#include "fastresampler_midi.h"
#include "fastresampler_state.h"
#include "fastresampler_host.h"

struct Vst2Plugin;

// Loads the DLL, opens the plugin, checks it is an instrument, and leaves it
// processing at the given rate and block. Fills in info.
Vst2Plugin* vst2_plugin_load(const char* path, double sample_rate, int block_size,
                             HostPluginInfo* info, char* err, size_t err_len);
void        vst2_plugin_unload(Vst2Plugin* p);

// Stops, reconfigures and restarts processing. Not while process can run.
bool vst2_plugin_setup_processing(Vst2Plugin* p, double sample_rate, int block_size, bool offline,
                                  HostPluginInfo* info);

// Audio thread. Drains the MIDI queue into this block, runs the plugin, and
// copies its output into outputs[0..channel_count).
void vst2_plugin_process(Vst2Plugin* p, MidiQueue* midi, float* const* outputs, int channel_count, int frames);

// The offline variant: the events for this block, each at its sample offset.
void vst2_plugin_process_events(Vst2Plugin* p, const MidiEvent* events, const int* sample_offsets,
                                int count, float* const* outputs, int channel_count, int frames);

// --- State ------------------------------------------------------------------
// See the state section of fastresampler_host.h. A plugin that keeps chunks
// writes its whole bank as one; one that does not gives its parameters, one
// float each, and the program it is on.

bool vst2_plugin_get_state(Vst2Plugin* p, StateBuffer* out);

// Puts a block back. The plugin is suspended over the call, and the setup
// that follows reads the outputs it has then: a sampler that was saved with
// sixteen of them comes back with sixteen. info is brought up to date.
bool vst2_plugin_set_state(Vst2Plugin* p, const void* data, size_t size, HostPluginInfo* info,
                           char* err, size_t err_len);

// True once for every time the plugin told the host its inputs or outputs
// changed.
bool vst2_plugin_take_io_change(Vst2Plugin* p);

// --- Editor -----------------------------------------------------------------
// The size comes from the plugin before it opens; attach opens the editor in
// the parent window and, when the plugin then reports another size, asks
// the resize callback for it.
bool vst2_plugin_editor_create(Vst2Plugin* p, int* width, int* height);
bool vst2_plugin_editor_attach(Vst2Plugin* p, void* parent_window, HostResizeFn resize_fn, void* user);
void vst2_plugin_editor_close(Vst2Plugin* p);
bool vst2_plugin_editor_is_open(const Vst2Plugin* p);
void vst2_plugin_editor_idle(Vst2Plugin* p);
void vst2_plugin_editor_size(Vst2Plugin* p, int* width, int* height);

// The scan's probe: loads the DLL, opens the plugin, reads its name and
// whether it is an instrument, and closes it again. False when the file is
// not a VST 2 plugin at all; then err says why.
bool vst2_probe(const char* path, char* name, size_t name_len, bool* is_instrument, char* err, size_t err_len);
