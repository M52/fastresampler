#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "fastresampler_scan.h"
#include "fastresampler_helpers.h"
#include "fastresampler_log.h"
#include "fastresampler_host_vst2.h"
#include "platform/fastresampler_platform.h"

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

static int scan_compare(const void* a, const void* b) {
    return fr_stricmp(((const PluginEntry*)a)->name, ((const PluginEntry*)b)->name);
}

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
        fr_log("scan: %s took the last scan down and is skipped", e->path);
        e->state = SCAN_CACHE_BROKEN;
        changed = true;
    }
    if (changed) scan_cache_save(cache);
    fr_log("scan: %d DLLs in the cache", cache->count);
}

static bool scan_push_dir(ScanJob* job, const char* path) {
    if (job->depth > SCAN_MAX_DEPTH) return false;
    ScanDir* d = &job->dirs[job->depth];
    if (!plat_dir_open(&d->it, path)) return false;
    fr_strcpy(d->path, sizeof(d->path), path);
    fr_strcpy(job->current, sizeof(job->current), path);
    job->depth++;
    return true;
}

static void scan_pop_dir(ScanJob* job) {
    if (job->depth <= 0) return;
    job->depth--;
    plat_dir_close(&job->dirs[job->depth].it);
}

static bool scan_search_step(ScanJob* job, PluginList* vst3, PluginList* vsti, const Settings* settings) {
    if (job->depth == 0) {
        while (job->folder < settings->scan_path_count) {
            const char* root = settings->scan_paths[job->folder++];
            if (!plat_path_is_dir(root)) {
                fr_log("scan: folder not found: %s", root);
                continue;
            }
            if (scan_push_dir(job, root)) return false;
        }
        return true;
    }

    ScanDir* top = &job->dirs[job->depth - 1];
    if (!plat_dir_next(&top->it)) {
        scan_pop_dir(job);
        return false;
    }
    char full[FR_MAX_PATH];
    plat_path_join(full, sizeof(full), top->path, top->it.name);
    if (settings_is_banned(settings, full)) return false;
    if (plat_path_has_ext(top->it.name, ".vst3")) {
        scan_add(vst3, full, top->it.name);
    } else if (!top->it.is_dir && plat_path_has_ext(top->it.name, ".dll")) {
        if (plat_library_exports(full, g_vst2_entry_names, 2)) scan_add(vsti, full, top->it.name);
    } else if (top->it.is_dir && job->depth <= SCAN_MAX_DEPTH) {
        scan_push_dir(job, full);
    }
    return false;
}

enum ScanProbeResult {
    SCAN_PROBE_NEXT,
    SCAN_PROBE_YIELD,
    SCAN_PROBE_DONE
};

static int scan_probe_step(ScanJob* job, PluginList* vsti, ScanCache* cache, bool out_of_time) {
    if (job->probe_index >= vsti->count) {
        vsti->count = job->probe_kept;
        return SCAN_PROBE_DONE;
    }
    int index = job->probe_index;
    PluginEntry* e = &vsti->entries[index];
    uint64_t size = 0, mtime = 0;
    if (!plat_file_stat(e->path, &size, &mtime)) {
        job->probe_index++;
        return SCAN_PROBE_NEXT;
    }

    ScanCacheEntry* ce = scan_cache_find(cache, e->path);
    bool fresh = ce && ce->size == size && ce->mtime == mtime;
    if (!fresh) {
        bool announced = job->probe_announced == index;
        job->probe_announced = index;
        fr_strcpy(job->current, sizeof(job->current), e->path);
        if (out_of_time && !announced) return SCAN_PROBE_YIELD;

        ScanCacheEntry scratch;
        if (!ce) {
            ce = cache->count < FR_MAX_VSTI_CACHE ? &cache->entries[cache->count++] : &scratch;
            fr_strcpy(ce->path, sizeof(ce->path), e->path);
        }
        ce->size = size;
        ce->mtime = mtime;
        ce->state = SCAN_CACHE_OPENING;
        ce->name[0] = '\0';
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
    if (ce->state == SCAN_CACHE_INSTRUMENT) {
        if (ce->name[0]) fr_strcpy(e->name, sizeof(e->name), ce->name);
        if (job->probe_kept != index) vsti->entries[job->probe_kept] = *e;
        job->probe_kept++;
    }
    job->probe_index++;
    return SCAN_PROBE_NEXT;
}

void scan_stop(ScanJob* job) {
    while (job->depth > 0) scan_pop_dir(job);
    job->phase = SCAN_PHASE_IDLE;
}

void scan_start(ScanJob* job, PluginList* vst3, PluginList* vsti) {
    scan_stop(job);
    job->phase = SCAN_PHASE_SEARCH;
    job->folder = 0;
    job->probe_index = 0;
    job->probe_kept = 0;
    job->probe_announced = -1;
    job->probe_total = 0;
    job->current[0] = '\0';
    vst3->count = 0;
    vsti->count = 0;
}

bool scan_running(const ScanJob* job) {
    return job->phase != SCAN_PHASE_IDLE;
}

bool scan_step(ScanJob* job, PluginList* vst3, PluginList* vsti, ScanCache* cache, const Settings* settings,
               uint32_t budget_ms) {
    uint64_t start = plat_ms_now();
    while (job->phase != SCAN_PHASE_IDLE) {
        bool out_of_time = plat_ms_now() - start >= budget_ms;
        if (job->phase == SCAN_PHASE_SEARCH) {
            if (out_of_time) return false;
            if (scan_search_step(job, vst3, vsti, settings)) {
                job->phase = SCAN_PHASE_PROBE;
                job->probe_total = vsti->count;
            }
        } else {
            int result = scan_probe_step(job, vsti, cache, out_of_time);
            if (result == SCAN_PROBE_YIELD) return false;
            if (result == SCAN_PROBE_DONE) {
                qsort(vst3->entries, (size_t)vst3->count, sizeof(PluginEntry), scan_compare);
                qsort(vsti->entries, (size_t)vsti->count, sizeof(PluginEntry), scan_compare);
                fr_log("scan: %d VST3 plugins and %d VST 2 instruments (of %d DLLs) in %d folders, %d banned",
                       vst3->count, vsti->count, job->probe_total, settings->scan_path_count, settings->banned_count);
                job->phase = SCAN_PHASE_IDLE;
                job->current[0] = '\0';
                return true;
            }
        }
    }
    return false;
}

float scan_progress(const ScanJob* job) {
    if (job->phase != SCAN_PHASE_PROBE || job->probe_total <= 0) return -1.0f;
    return (float)job->probe_index / (float)job->probe_total;
}
