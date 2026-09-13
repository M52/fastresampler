#pragma once
// A resampling project: everything the Resample tab holds, in one YAML file
// under the Projects folder next to the exe, and beside it the state of the
// plugin the project was made with. Saving writes both, loading puts the
// configuration back and hands the state to the caller, and a field the file
// does not hold keeps its default. The reader is src/fastresampler_yaml.h.

#include <stddef.h>
#include "fastresampler_constants.h"
#include "fastresampler_state.h"
#include "fastresampler_resample.h"

// The projects found in the folder, by name and without the extension.
struct ProjectList {
    char names[FR_MAX_PROJECTS][FR_PROJECT_NAME_LEN];
    int  count;
};

// The plugin a project was saved with: which one was loaded, and the block
// it wrote of its own state -- its patch, its parameters and the outputs it
// was routed to. The block is the plugin's and is never read here; it goes
// in the "<name>.fstate" file beside the project's YAML, as a sampler's runs
// to megabytes and the YAML is meant to stay readable.
struct ProjectPlugin {
    bool        present;                     // a plugin was loaded at the save
    int         format;                      // HostPluginFormat: 0 VST3, 1 VSTi
    char        path[FR_MAX_PATH];
    char        name[FR_PLUGIN_NAME_LEN];    // what to say when the path is gone
    StateBuffer state;                       // empty when the plugin wrote none
};

// Starts one empty, and frees the state block a load put in it. A saved
// project is written from a filled one; the caller owns both.
void project_plugin_init(ProjectPlugin* p);
void project_plugin_release(ProjectPlugin* p);

// The Projects folder next to the exe, with a trailing separator. The folder
// is not made here; project_save() makes it.
void project_dir(char* out, size_t out_len);

// <Projects folder>\<name>.yaml, with the name cleaned for a file name.
// False when nothing is left of the name.
bool project_path(const char* name, char* out, size_t out_len);

// The name with the characters a file name cannot hold replaced by '-', and
// the outer spaces and dots removed. False when nothing is left.
bool project_clean_name(const char* name, char* out, size_t out_len);

// <Projects folder>\<name>.fstate, the plugin state beside the project.
bool project_state_path(const char* name, char* out, size_t out_len);

// The .yaml files in the Projects folder, sorted by name.
void project_scan(ProjectList* list);

// The configuration and the plugin to a file and back. False with the reason
// in err. Save writes the state file when the plugin has a state and removes
// a stale one when it has not; load leaves plugin->present false when the
// project names no plugin, and plugin->state empty when its state file is
// gone. A state that cannot be written or read is reported but does not fail
// the save or the load: the settings are still worth keeping.
bool project_save(const ResampleConfig* cfg, const ProjectPlugin* plugin, const char* name,
                  char* err, size_t err_len);
bool project_load(ResampleConfig* cfg, ProjectPlugin* plugin, const char* name,
                  char* err, size_t err_len);
