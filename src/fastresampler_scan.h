#pragma once
// Finds plugins under the configured folders. A VST3 module is a bundle
// directory named "*.vst3" or a bare "*.vst3" file, and is never loaded here,
// so a broken one cannot take the scan down. A VST 2 instrument is a "*.dll"
// that exports the VST 2 entry point; whether such a DLL is an instrument or
// an effect only shows once it runs, so each is opened once and what it said
// is kept in a cache file next to the exe. A DLL is marked in that file
// before it opens: one that took the scan down is skipped from then on.

#include <stdint.h>
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

// What the cache knows about one VST 2 DLL.
enum ScanCacheState {
    SCAN_CACHE_NOT_INSTRUMENT = 0,   // an effect, a shell, or not a plugin after all
    SCAN_CACHE_INSTRUMENT     = 1,
    SCAN_CACHE_OPENING        = 2,   // written before the DLL opens; still there means it never came back
    SCAN_CACHE_BROKEN         = 3    // found as opening at startup: skipped until the file changes
};

struct ScanCacheEntry {
    char     path[FR_MAX_PATH];
    uint64_t size;
    uint64_t mtime;
    int      state;                  // ScanCacheState
    char     name[FR_PLUGIN_NAME_LEN];
};

struct ScanCache {
    ScanCacheEntry entries[FR_MAX_VSTI_CACHE];
    int  count;
    char path[FR_MAX_PATH];          // the file it is kept in
};

// Reads the cache file, and marks the DLL a previous scan did not survive.
void scan_cache_init(ScanCache* cache, const char* path);

// Fills vst3 with the VST3 modules and vsti with the VST 2 instruments under
// the settings' folders, both sorted by name.
void scan_plugins(PluginList* vst3, PluginList* vsti, ScanCache* cache, const Settings* settings);
