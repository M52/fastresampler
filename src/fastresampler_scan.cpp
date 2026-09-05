// #pragma once (removed for unity build)
#include <stdlib.h>
#include <string.h>
#include "fastresampler_scan.h"
#include "fastresampler_helpers.h"
#include "fastresampler_log.h"
#include "platform/fastresampler_platform.h"

// How deep below a configured folder the scan looks. Vendors nest one or two
// folders deep; a bundle's own Contents folder is never entered.
#define SCAN_MAX_DEPTH 4

static void scan_add(PluginList* out, const char* path, const char* file_name) {
    if (out->count >= FR_MAX_PLUGINS) return;
    for (int i = 0; i < out->count; ++i) {
        if (fr_stricmp(out->entries[i].path, path) == 0) return;
    }
    PluginEntry* e = &out->entries[out->count++];
    fr_strcpy(e->path, FR_MAX_PATH, path);
    fr_strcpy(e->name, FR_PLUGIN_NAME_LEN, file_name);
    fr_strip_vst3_ext(e->name);
}

static void scan_dir(PluginList* out, const char* dir, int depth) {
    PlatDirIter it;
    if (!plat_dir_open(&it, dir)) return;
    while (plat_dir_next(&it)) {
        char full[FR_MAX_PATH];
        plat_path_join(full, sizeof(full), dir, it.name);
        if (plat_path_has_ext(it.name, ".vst3")) {
            scan_add(out, full, it.name);
            continue;
        }
        if (it.is_dir && depth < SCAN_MAX_DEPTH) scan_dir(out, full, depth + 1);
    }
    plat_dir_close(&it);
}

static int scan_compare(const void* a, const void* b) {
    return fr_stricmp(((const PluginEntry*)a)->name, ((const PluginEntry*)b)->name);
}

void scan_plugins(PluginList* out, const Settings* settings) {
    out->count = 0;
    for (int i = 0; i < settings->scan_path_count; ++i) {
        if (!plat_path_is_dir(settings->scan_paths[i])) {
            fr_log("scan: folder not found: %s", settings->scan_paths[i]);
            continue;
        }
        scan_dir(out, settings->scan_paths[i], 0);
    }
    qsort(out->entries, (size_t)out->count, sizeof(PluginEntry), scan_compare);
    fr_log("scan: %d plugins in %d folders", out->count, settings->scan_path_count);
}
