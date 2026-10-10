#pragma once

#include <stdint.h>
#include "fastresampler_constants.h"
#include "fastresampler_settings.h"
#include "platform/fastresampler_platform.h"

#define SCAN_MAX_DEPTH 4

struct PluginEntry {
    char path[FR_MAX_PATH];
    char name[FR_PLUGIN_NAME_LEN];
};

struct PluginList {
    PluginEntry entries[FR_MAX_PLUGINS];
    int count;
};

enum ScanCacheState {
    SCAN_CACHE_NOT_INSTRUMENT = 0,
    SCAN_CACHE_INSTRUMENT     = 1,
    SCAN_CACHE_OPENING        = 2,
    SCAN_CACHE_BROKEN         = 3
};

struct ScanCacheEntry {
    char     path[FR_MAX_PATH];
    uint64_t size;
    uint64_t mtime;
    int      state;
    char     name[FR_PLUGIN_NAME_LEN];
};

struct ScanCache {
    ScanCacheEntry entries[FR_MAX_VSTI_CACHE];
    int  count;
    char path[FR_MAX_PATH];
};

enum ScanPhase {
    SCAN_PHASE_IDLE = 0,
    SCAN_PHASE_SEARCH,
    SCAN_PHASE_PROBE
};

struct ScanDir {
    PlatDirIter it;
    char        path[FR_MAX_PATH];
};

struct ScanJob {
    int     phase;
    int     folder;
    ScanDir dirs[SCAN_MAX_DEPTH + 1];
    int     depth;
    int     probe_index;
    int     probe_kept;
    int     probe_announced;
    int     probe_total;
    char    current[FR_MAX_PATH];
};

void scan_cache_init(ScanCache* cache, const char* path);

void scan_start(ScanJob* job, PluginList* vst3, PluginList* vsti);
void scan_stop(ScanJob* job);
bool scan_running(const ScanJob* job);
bool scan_step(ScanJob* job, PluginList* vst3, PluginList* vsti, ScanCache* cache, const Settings* settings,
               uint32_t budget_ms);
float scan_progress(const ScanJob* job);
