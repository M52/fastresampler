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
        e->size = 0;
        e->mtime = 0;
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

static void scan_clean_text(char* text) {
    for (char* s = text; *s; ++s) {
        if (*s == '\t' || *s == '\r' || *s == '\n') *s = ' ';
    }
}

int scan_probe_child(const char* dll_path, const char* result_path) {
    plat_child_process_setup();
    plat_system_init();
    char name[FR_PLUGIN_NAME_LEN];
    char err[FR_ERROR_LEN];
    bool is_instrument = false;
    bool ok = vst2_probe(dll_path, name, sizeof(name), &is_instrument, err, sizeof(err));
    char* text = ok ? name : err;
    scan_clean_text(text);
    char line[FR_PLUGIN_NAME_LEN + FR_ERROR_LEN + 8];
    int n = snprintf(line, sizeof(line), "%d\t%s\n", ok ? (is_instrument ? 1 : 0) : -1, text);
    bool written = n > 0 && plat_file_write(result_path, line, (size_t)n);
    plat_exit_now(written ? 0 : 2);
    return written ? 0 : 2;
}

static void scan_cache_record(ScanCache* cache, const char* path, uint64_t size, uint64_t mtime, int state,
                              const char* name) {
    ScanCacheEntry* ce = scan_cache_find(cache, path);
    if (!ce) {
        if (cache->count >= FR_MAX_VSTI_CACHE) return;
        ce = &cache->entries[cache->count++];
        fr_strcpy(ce->path, sizeof(ce->path), path);
    }
    ce->size = size;
    ce->mtime = mtime;
    ce->state = state;
    fr_strcpy(ce->name, sizeof(ce->name), name);
    scan_cache_save(cache);
}

static void scan_probe_release(ScanProbe* probe) {
    plat_process_close(&probe->process);
    plat_file_delete(probe->result_path);
    probe->busy = false;
}

static void scan_probe_launch(ScanJob* job, int slot, PluginList* vsti, int entry) {
    ScanProbe* probe = &job->probes[slot];
    const PluginEntry* e = &vsti->entries[entry];
    if (!plat_file_stat(e->path, &probe->size, &probe->mtime)) {
        job->probe_done++;
        return;
    }

    char exe[FR_MAX_PATH];
    char temp[FR_MAX_PATH];
    char file[64];
    plat_module_path(exe, sizeof(exe));
    plat_temp_dir(temp, sizeof(temp));
    snprintf(file, sizeof(file), "fastresampler-probe-%u-%d.txt", plat_process_id(), slot);
    plat_path_join(probe->result_path, sizeof(probe->result_path), temp, file);
    plat_file_delete(probe->result_path);

    const char* args[4] = { exe, FR_SCAN_PROBE_ARG, e->path, probe->result_path };
    if (!exe[0] || !temp[0] || !plat_process_start(&probe->process, args, 4)) {
        fr_log("scan: %s: the probe process could not be started", e->path);
        job->probe_done++;
        return;
    }
    probe->busy = true;
    probe->entry = entry;
    probe->started_ms = plat_ms_now();
}

static void scan_probe_collect(ScanJob* job, ScanProbe* probe, PluginList* vsti, ScanCache* cache, bool timed_out,
                               int exit_code) {
    PluginEntry* e = &vsti->entries[probe->entry];
    char result[FR_PLUGIN_NAME_LEN + FR_ERROR_LEN + 8];
    bool have_result = !timed_out && exit_code == 0 && plat_file_read_text(probe->result_path, result, sizeof(result));
    if (!have_result) {
        if (timed_out) {
            fr_log("scan: %s did not answer within %d s and is skipped until it changes", e->path,
                   FR_SCAN_PROBE_TIMEOUT_MS / 1000);
        } else {
            fr_log("scan: %s crashed while it was opened (exit code 0x%08X) and is skipped until it changes", e->path,
                   (unsigned)exit_code);
        }
        scan_cache_record(cache, e->path, probe->size, probe->mtime, SCAN_CACHE_BROKEN, "");
    } else {
        char* text = strchr(result, '\t');
        text = text ? text + 1 : result + strlen(result);
        size_t len = strlen(text);
        while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r')) text[--len] = '\0';
        int state = atoi(result);
        if (state == 1) {
            fr_log("scan: %s: %s", e->path, text);
            scan_cache_record(cache, e->path, probe->size, probe->mtime, SCAN_CACHE_INSTRUMENT, text);
            if (text[0]) fr_strcpy(e->name, sizeof(e->name), text);
            job->keep[probe->entry] = true;
        } else if (state == 0) {
            fr_log("scan: %s: %s (not an instrument)", e->path, text);
            scan_cache_record(cache, e->path, probe->size, probe->mtime, SCAN_CACHE_NOT_INSTRUMENT, text);
        } else {
            fr_log("scan: %s: %s", e->path, text);
            scan_cache_record(cache, e->path, probe->size, probe->mtime, SCAN_CACHE_NOT_INSTRUMENT, "");
        }
    }
    scan_probe_release(probe);
    job->probe_done++;
}

static void scan_probe_begin(ScanJob* job, PluginList* vsti, ScanCache* cache) {
    job->queue_count = 0;
    job->queue_next = 0;
    job->probe_done = 0;
    for (int i = 0; i < vsti->count; ++i) {
        PluginEntry* e = &vsti->entries[i];
        job->keep[i] = false;
        uint64_t size = 0, mtime = 0;
        if (!plat_file_stat(e->path, &size, &mtime)) continue;
        const ScanCacheEntry* ce = scan_cache_find(cache, e->path);
        if (ce && ce->size == size && ce->mtime == mtime) {
            job->keep[i] = ce->state == SCAN_CACHE_INSTRUMENT;
            if (job->keep[i] && ce->name[0]) fr_strcpy(e->name, sizeof(e->name), ce->name);
            continue;
        }
        job->queue[job->queue_count++] = i;
    }
    if (job->queue_count > 0) fr_log("scan: %d new or changed VST 2 DLLs to open", job->queue_count);
}

static bool scan_probe_step(ScanJob* job, PluginList* vsti, ScanCache* cache) {
    bool busy = false;
    for (int s = 0; s < FR_SCAN_PARALLEL_PROBES; ++s) {
        ScanProbe* probe = &job->probes[s];
        if (probe->busy) {
            int exit_code = 0;
            if (plat_process_finished(&probe->process, &exit_code)) {
                scan_probe_collect(job, probe, vsti, cache, false, exit_code);
            } else if (plat_ms_now() - probe->started_ms >= FR_SCAN_PROBE_TIMEOUT_MS) {
                plat_process_kill(&probe->process);
                scan_probe_collect(job, probe, vsti, cache, true, 0);
            }
        }
        while (!probe->busy && job->queue_next < job->queue_count) {
            scan_probe_launch(job, s, vsti, job->queue[job->queue_next++]);
        }
        busy = busy || probe->busy;
    }
    if (busy || job->queue_next < job->queue_count) return false;

    int kept = 0;
    for (int i = 0; i < vsti->count; ++i) {
        if (!job->keep[i]) continue;
        if (kept != i) vsti->entries[kept] = vsti->entries[i];
        kept++;
    }
    vsti->count = kept;
    return true;
}

void scan_stop(ScanJob* job) {
    while (job->depth > 0) scan_pop_dir(job);
    for (int s = 0; s < FR_SCAN_PARALLEL_PROBES; ++s) {
        ScanProbe* probe = &job->probes[s];
        if (!probe->busy) continue;
        plat_process_kill(&probe->process);
        scan_probe_release(probe);
    }
    job->phase = SCAN_PHASE_IDLE;
}

void scan_start(ScanJob* job, PluginList* vst3, PluginList* vsti) {
    scan_stop(job);
    job->phase = SCAN_PHASE_SEARCH;
    job->folder = 0;
    job->queue_count = 0;
    job->queue_next = 0;
    job->probe_done = 0;
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
    while (job->phase == SCAN_PHASE_SEARCH) {
        if (plat_ms_now() - start >= budget_ms) return false;
        if (scan_search_step(job, vst3, vsti, settings)) {
            job->phase = SCAN_PHASE_PROBE;
            job->current[0] = '\0';
            scan_probe_begin(job, vsti, cache);
        }
    }
    if (job->phase != SCAN_PHASE_PROBE || !scan_probe_step(job, vsti, cache)) return false;

    qsort(vst3->entries, (size_t)vst3->count, sizeof(PluginEntry), scan_compare);
    qsort(vsti->entries, (size_t)vsti->count, sizeof(PluginEntry), scan_compare);
    fr_log("scan: %d VST3 plugins and %d VST 2 instruments in %d folders, %d banned, %d DLLs opened",
           vst3->count, vsti->count, settings->scan_path_count, settings->banned_count, job->queue_count);
    job->phase = SCAN_PHASE_IDLE;
    return true;
}

float scan_progress(const ScanJob* job) {
    if (job->phase != SCAN_PHASE_PROBE || job->queue_count <= 0) return -1.0f;
    return (float)job->probe_done / (float)job->queue_count;
}
