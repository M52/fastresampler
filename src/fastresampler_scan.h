#pragma once
// Finds VST3 plugins under the configured folders. A plugin is either a bundle
// directory named "*.vst3" or a bare "*.vst3" DLL. Nothing is loaded here; the
// list carries paths and display names only, so a broken plugin cannot take
// the scan down.

#include "fastresampler_constants.h"
#include "fastresampler_settings.h"

struct PluginEntry {
    char path[FR_MAX_PATH];
    char name[FR_PLUGIN_NAME_LEN];
};

struct PluginList {
    PluginEntry entries[FR_MAX_PLUGINS];
    int count;
};

void scan_plugins(PluginList* out, const Settings* settings);
