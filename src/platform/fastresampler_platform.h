#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "fastresampler_constants.h"

#if defined(_WIN32)
    #define PLAT_PATH_SEP      '\\'
    #define PLAT_PATH_SEP_STR  "\\"
#else
    #define PLAT_PATH_SEP      '/'
    #define PLAT_PATH_SEP_STR  "/"
#endif

bool plat_system_init(void);
void plat_system_shutdown(void);

void plat_debug_output(const char* text);

uint64_t plat_ms_now(void);
void     plat_sleep_ms(uint32_t ms);

struct PlatThread {
    void* handle;
};
typedef void (*PlatThreadFn)(void* user);
bool plat_thread_create(PlatThread* t, PlatThreadFn fn, void* user);
void plat_thread_join(PlatThread* t);

struct PlatProcess {
    void* handle;
};
bool plat_process_start(PlatProcess* p, const char* const* args, int arg_count);
bool plat_process_finished(PlatProcess* p, int* exit_code);
void plat_process_kill(PlatProcess* p);
void plat_process_close(PlatProcess* p);
uint32_t plat_process_id(void);
void plat_child_process_setup(void);
void plat_exit_now(int code);

bool plat_path_exists(const char* path);
bool plat_path_is_dir(const char* path);
bool plat_dir_create(const char* path);

bool plat_path_absolute(const char* path, char* out, size_t out_len);

bool plat_disk_free_bytes(const char* path, uint64_t* out_free);

void plat_module_dir(char* out_dir, size_t max_len);
void plat_module_path(char* out, size_t max_len);
void plat_temp_dir(char* out, size_t max_len);

void plat_default_plugin_dir(char* out_dir, size_t max_len);

void plat_default_vst2_dir(char* out_dir, size_t max_len);

bool plat_file_stat(const char* path, uint64_t* out_size, uint64_t* out_mtime);

bool plat_file_delete(const char* path);
bool plat_file_write(const char* path, const void* data, size_t size);
bool plat_file_read_text(const char* path, char* out, size_t out_len);

void* plat_library_load(const char* path, char* err, size_t err_len);
void* plat_library_symbol(void* library, const char* name);
void  plat_library_unload(void* library);

bool plat_library_exports(const char* path, const char* const* names, int name_count);

bool plat_pick_folder(void* parent_window, const char* start_dir, char* out, size_t out_len);

struct PlatDirIter {
    void* handle;
    char  name[FR_MAX_PATH];
    bool  is_dir;
    bool  has_pending;
};
bool plat_dir_open(PlatDirIter* it, const char* dir);
bool plat_dir_next(PlatDirIter* it);
void plat_dir_close(PlatDirIter* it);

inline const char* plat_path_basename(const char* path) {
    const char* base = path;
    for (const char* p = path; *p; ++p) {
        if (*p == '\\' || *p == '/') base = p + 1;
    }
    return base;
}

inline bool plat_path_has_ext(const char* path, const char* ext) {
    size_t path_len = strlen(path);
    size_t ext_len = strlen(ext);
    if (ext_len > path_len) return false;
    const char* tail = path + path_len - ext_len;
    for (size_t i = 0; i < ext_len; ++i) {
        char a = tail[i], b = ext[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

inline bool plat_dir_create_all(const char* path) {
    if (plat_path_is_dir(path)) return true;
    char work[FR_MAX_PATH];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(work)) return false;
    memcpy(work, path, len + 1);
    bool unc = (work[0] == '\\' || work[0] == '/') && (work[1] == '\\' || work[1] == '/');
    size_t i = (work[0] && work[1] == ':') ? 2 : 0;
    while (work[i] == '\\' || work[i] == '/') ++i;
    for (int part = unc ? 2 : 0; part > 0 && work[i]; --part) {
        while (work[i] && work[i] != '\\' && work[i] != '/') ++i;
        while (work[i] == '\\' || work[i] == '/') ++i;
    }
    for (; work[i]; ++i) {
        if (work[i] != '\\' && work[i] != '/') continue;
        char sep = work[i];
        work[i] = '\0';
        if (!plat_dir_create(work)) return false;
        work[i] = sep;
        while (work[i + 1] == '\\' || work[i + 1] == '/') ++i;
    }
    return plat_dir_create(work);
}

inline void plat_path_join(char* out, size_t out_size, const char* dir, const char* name) {
    size_t dir_len = strlen(dir);
    bool needs_sep = dir_len > 0 && dir[dir_len - 1] != '\\' && dir[dir_len - 1] != '/';
    int n = snprintf(out, out_size, "%s%s%s", dir, needs_sep ? PLAT_PATH_SEP_STR : "", name);
    if (n < 0 || (size_t)n >= out_size) out[out_size - 1] = '\0';
}
