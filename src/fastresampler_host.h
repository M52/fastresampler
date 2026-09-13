#pragma once
// Plugin hosting: one plugin at a time, a VST3 module or a VST 2 instrument
// DLL. Loads the module, creates and connects the component and the
// controller, drives processing from the audio thread, and attaches the
// plugin's editor to a native window the view layer owns. The VST3 side is
// in fastresampler_host.cpp; a VST 2 plugin is carried by the same HostPlugin
// and handed to fastresampler_host_vst2.cpp.
//
// Thread rules: every function here runs on the UI thread except
// host_plugin_process() and host_plugin_process_events(), which run on the
// audio thread or the render worker. The caller keeps the two apart: it
// stops calling process before it unloads or reconfigures.

#include <stddef.h>
#include "fastresampler_constants.h"
#include "fastresampler_midi.h"
#include "fastresampler_state.h"

struct HostContext;   // the IHostApplication every plugin is given
struct HostPlugin;    // one loaded plugin

// The two plugin formats. A VST3 module is a ".vst3" bundle or file, a VST 2
// instrument a ".dll" that exports the VST 2 entry point.
enum HostPluginFormat {
    HOST_FORMAT_VST3 = 0,
    HOST_FORMAT_VST2,
    HOST_FORMAT_COUNT
};

struct HostPluginInfo {
    int  format;               // HostPluginFormat
    char name[FR_PLUGIN_NAME_LEN];
    char vendor[64];
    char version[64];
    char category[128];
    int  output_channels;      // channels on the main output bus
    // Every output channel the plugin has, its busses flattened into one
    // list: what an output bus of this program picks its stereo pair out
    // of. A VST3 with three stereo busses and a VST 2 with six output
    // channels both report six.
    int  total_output_channels;
    int  output_bus_count;     // audio output busses the plugin has; 1 for VST 2
    int  latency_frames;
    bool has_midi_mapping;     // CC and pitch bend reach the plugin as parameters
    bool has_controller;
};

// "VST3" or "VSTi", what the tabs are labelled.
const char* host_format_name(int format);

// The format a path loads as, by its extension.
int host_format_of_path(const char* path);

// Called on the UI thread when the plugin asks for a new editor size. The
// callee resizes the native parent window before it returns.
typedef void (*HostResizeFn)(void* user, int width, int height);

HostContext* host_create(void);
void         host_destroy(HostContext* ctx);

// Loads path in the format its extension says. A VST3 module gives its first
// instrument class (or its first audio class); a VST 2 DLL must be an
// instrument. Leaves the plugin active and processing at the given rate and
// block.
HostPlugin* host_plugin_load(HostContext* ctx, const char* path, double sample_rate,
                             int block_size, char* err, size_t err_len);
void        host_plugin_unload(HostPlugin* p);

// Stops, reconfigures, and restarts processing. Not while process() can run.
// Offline tells the plugin it is being rendered, not played: it may take its
// time and render at its best quality.
bool host_plugin_setup_processing(HostPlugin* p, double sample_rate, int block_size, bool offline);

// Audio thread. Drains the MIDI queue into this block's events, runs the
// plugin, and copies its output into outputs[0..channel_count). The channels
// are the plugin's output busses flattened into one list, so channel 2 and 3
// are the plugin's outputs 3/4 whether those are a second bus or two more
// channels of the first. A channel the plugin does not have is left silent,
// except that a mono plugin feeds both of the first pair.
void host_plugin_process(HostPlugin* p, MidiQueue* midi, float* const* outputs,
                         int channel_count, int frames);

// The offline variant: the events for this block, each at its sample offset
// inside the block. Same thread rules as host_plugin_process(); only one of
// the two may be running.
void host_plugin_process_events(HostPlugin* p, const MidiEvent* events, const int* sample_offsets,
                                int count, float* const* outputs, int channel_count, int frames);

const HostPluginInfo* host_plugin_info(const HostPlugin* p);

// --- State ------------------------------------------------------------------
// The plugin's own state: the patch it holds, every parameter, and the
// output routing it was set to, in the block a DAW would keep in its
// project. The container is fastresampler_state.h; what is in its parts is
// the plugin's and is never read here.
//
// Both calls run on the UI thread with the audio thread kept out, the way an
// unload is: setting a state stops the plugin, hands it the block, and
// starts it again.

// Reads the state into out, which the caller starts with state_buffer_init()
// and frees with state_buffer_release(). False when the plugin wrote nothing.
bool host_plugin_get_state(HostPlugin* p, StateBuffer* out);

// Puts a block read earlier back. It is refused when it was written for
// another format or another VST 2 plugin. A plugin may name other outputs
// once its state is back (a sampler told to send its microphones to more
// busses does): the info is up to date when this returns, and the caller
// reopens the audio device for it.
bool host_plugin_set_state(HostPlugin* p, const void* data, size_t size, char* err, size_t err_len);

// True once for every time the plugin changed its own busses or channels by
// itself, through IComponent's restart call or the VST 2 io-changed one. The
// caller reconfigures for the new layout.
bool host_plugin_take_io_change(HostPlugin* p);

// --- Editor -----------------------------------------------------------------
// Two steps so the caller can size the native parent before the view attaches:
// create reports the view's own size, attach parents it.
bool host_plugin_editor_create(HostPlugin* p, float content_scale, int* width, int* height);
bool host_plugin_editor_attach(HostPlugin* p, void* parent_window, HostResizeFn resize_fn, void* user);
void host_plugin_editor_close(HostPlugin* p);
bool host_plugin_editor_is_open(const HostPlugin* p);
void host_plugin_editor_set_scale(HostPlugin* p, float content_scale);

// Once per frame on the UI thread. A VST 2 editor repaints and reads its
// controls on this call; a VST3 editor needs nothing.
void host_plugin_editor_idle(HostPlugin* p);

// Offers the editor the area the host has for it. A resizable view takes what
// its constraints allow, a fixed one keeps its size. Reports the size to show.
void host_plugin_editor_fit(HostPlugin* p, int avail_width, int avail_height, int* width, int* height);
