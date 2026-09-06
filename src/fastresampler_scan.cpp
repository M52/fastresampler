// #pragma once (removed for unity build)
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "fastresampler_scan.h"
#include "fastresampler_helpers.h"
#include "fastresampler_log.h"
#include "fastresampler_host_vst2.h"
#include "platform/fastresampler_platform.h"

// How deep below a configured folder the scan looks. Vendors nest one or two
// folders deep; a bundle's own Contents folder is never entered.
#define SCAN_MAX_DEPTH 4

// The entry point a VST 2 DLL exports: the current name, and the old one.
static const char* const g_vst2_entry_names[2] = { "VSTPluginMain", "main" };

static void scan_add(PluginList* out, const char* path, const char* file_name) {
    if (out->count >= FR_MAX_PLUGINS) return;
    for (int i = 0; i < out->count; ++i) {
        if (fr_stricmp(out->entries[i].path, path) == 0) return;
    }
    PluginEntry* e = &out->entries[out->count++];
    fr_strcpy(e->path, FR_MAX_PATH, path);
    fr_strcpy(e->name, FR_PLUGIN_NAME_LEN, file_name);
    fr_strip_plugin_ext(e->name);
}

// The VST3 modules go straight to their list. A DLL goes to the VST 2 list
// when its export table names the entry point, which is read without running
// it; the probe below then sorts the instruments from the rest.
static void scan_dir(PluginList* vst3, PluginList* vsti, const char* dir, int depth) {
    PlatDirIter it;
    if (!plat_dir_open(&it, dir)) return;
    while (plat_dir_next(&it)) {
        char full[FR_MAX_PATH];
        plat_path_join(full, sizeof(full), dir, it.name);
        if (plat_path_has_ext(it.name, ".vst3")) {
            scan_add(vst3, full, it.name);
            continue;
        }
        if (!it.is_dir && plat_path_has_ext(it.name, ".dll")) {
            if (plat_library_exports(full, g_vst2_entry_names, 2)) scan_add(vsti, full, it.name);
            continue;
        }
        if (it.is_dir && depth < SCAN_MAX_DEPTH) scan_dir(vst3, vsti, full, depth + 1);
    }
    plat_dir_close(&it);
}

static int scan_compare(const void* a, const void* b) {
    return fr_stricmp(((const PluginEntry*)a)->name, ((const PluginEntry*)b)->name);
}

// --- The VST 2 cache ---------------------------------------------------------------
// One line per DLL, tab separated: state, size, write time, path, name.

static ScanCacheEntry* scan_cache_find(ScanCache* cache, const char* path) {
    for (int i = 0; i < cache->count; ++i) {
        if (fr_stricmp(cache->entries[i].path, path) == 0) return &cache->entries[i];
    }
    return nullptr;
}

static void scan_cache_save(const ScanCache* cache) {
    if (!cache->path[0]) return;
    FILE* f = fopen(cache->path, "wb");
    if (!f) {
        fr_log("scan: could not write %s", cache->path);
        return;
    }
    fprintf(f, "# FastResampler VST 2 scan cache: state, size, write time, path, name\n");
    for (int i = 0; i < cache->count; ++i) {
        const ScanCacheEntry* e = &cache->entries[i];
        fprintf(f, "%d\t%llu\t%llu\t%s\t%s\n", e->state, (unsigned long long)e->size, (unsigned long long)e->mtime, e->path, e->name);
    }
    fclose(f);
}

static void scan_cache_load(ScanCache* cache) {
    cache->count = 0;
    FILE* f = fopen(cache->path, "rb");
    if (!f) return;
    char line[2 * FR_MAX_PATH];
    while (fgets(line, sizeof(line), f) && cache->count < FR_MAX_VSTI_CACHE) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';

        // The four tabs split the line; the name is whatever follows the last.
        char* fields[5];
        int count = 0;
        char* s = line;
        fields[count++] = s;
        while (count < 5 && (s = strchr(s, '\t')) != nullptr) {
            *s++ = '\0';
            fields[count++] = s;
        }
        if (count < 4) continue;
        ScanCacheEntry* e = &cache->entries[cache->count];
        e->state = fr_clampi(atoi(fields[0]), SCAN_CACHE_NOT_INSTRUMENT, SCAN_CACHE_BROKEN);
        e->size = strtoull(fields[1], nullptr, 10);
        e->mtime = strtoull(fields[2], nullptr, 10);
        fr_strcpy(e->path, sizeof(e->path), fields[3]);
        fr_strcpy(e->name, sizeof(e->name), count >= 5 ? fields[4] : "");
        if (e->path[0]) cache->count++;
    }
    fclose(f);
}

void scan_cache_init(ScanCache* cache, const char* path) {
    fr_strcpy(cache->path, sizeof(cache->path), path);
    scan_cache_load(cache);
    bool changed = false;
    for (int i = 0; i < cache->count; ++i) {
        ScanCacheEntry* e = &cache->entries[i];
        if (e->state != SCAN_CACHE_OPENING) continue;
        // The last scan opened this DLL and did not come back from it.
        fr_log("scan: %s took the last scan down and is skipped", e->path);
        e->state = SCAN_CACHE_BROKEN;
        changed = true;
    }
    if (changed) scan_cache_save(cache);
    fr_log("scan: %d DLLs in the cache", cache->count);
}

// Opens every DLL the cache does not know at this size and write time, and
// keeps the ones that are instruments, named as they name themselves.
static void scan_probe_vsti(PluginList* vsti, ScanCache* cache) {
    int kept = 0;
    for (int i = 0; i < vsti->count; ++i) {
        PluginEntry* e = &vsti->entries[i];
        uint64_t size = 0, mtime = 0;
        if (!plat_file_stat(e->path, &size, &mtime)) continue;

        ScanCacheEntry* ce = scan_cache_find(cache, e->path);
        bool fresh = ce && ce->size == size && ce->mtime == mtime;
        ScanCacheEntry scratch;
        if (!fresh) {
            if (!ce) {
                // A full cache still probes, it just cannot remember.
                ce = cache->count < FR_MAX_VSTI_CACHE ? &cache->entries[cache->count++] : &scratch;
                fr_strcpy(ce->path, sizeof(ce->path), e->path);
            }
            ce->size = size;
            ce->mtime = mtime;
            ce->state = SCAN_CACHE_OPENING;
            ce->name[0] = '\0';
            // On disk before the DLL runs, so a crash leaves its mark.
            scan_cache_save(cache);

            char name[FR_PLUGIN_NAME_LEN];
            char err[FR_ERROR_LEN];
            bool is_instrument = false;
            if (vst2_probe(e->path, name, sizeof(name), &is_instrument, err, sizeof(err))) {
                ce->state = is_instrument ? SCAN_CACHE_INSTRUMENT : SCAN_CACHE_NOT_INSTRUMENT;
                fr_strcpy(ce->name, sizeof(ce->name), name);
                fr_log("scan: %s: %s%s", e->path, name, is_instrument ? "" : " (not an instrument)");
            } else {
                ce->state = SCAN_CACHE_NOT_INSTRUMENT;
                fr_log("scan: %s: %s", e->path, err);
            }
            scan_cache_save(cache);
        }
        if (ce->state != SCAN_CACHE_INSTRUMENT) continue;
        if (ce->name[0]) fr_strcpy(e->name, sizeof(e->name), ce->name);
        if (kept != i) vsti->entries[kept] = *e;
        kept++;
    }
    vsti->count = kept;
}

void scan_plugins(PluginList* vst3, PluginList* vsti, ScanCache* cache, const Settings* settings) {
    vst3->count = 0;
    vsti->count = 0;
    for (int i = 0; i < settings->scan_path_count; ++i) {
        if (!plat_path_is_dir(settings->scan_paths[i])) {
            fr_log("scan: folder not found: %s", settings->scan_paths[i]);
            continue;
        }
        scan_dir(vst3, vsti, settings->scan_paths[i], 0);
    }
    int candidates = vsti->count;
    scan_probe_vsti(vsti, cache);
    qsort(vst3->entries, (size_t)vst3->count, sizeof(PluginEntry), scan_compare);
    qsort(vsti->entries, (size_t)vsti->count, sizeof(PluginEntry), scan_compare);
    fr_log("scan: %d VST3 plugins and %d VST 2 instruments (of %d DLLs) in %d folders",
           vst3->count, vsti->count, candidates, settings->scan_path_count);
}
