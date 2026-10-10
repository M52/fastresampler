#pragma once
// FastResampler platform abstraction layer.
//
// Everything the host needs from the operating system that is not audio or
// windowing lives behind this API. There is one implementation file per
// platform:
//
//     src/platform/fastresampler_platform_win32.cpp   (Windows)
//     src/platform/fastresampler_platform_macos.cpp   (future)
//     src/platform/fastresampler_platform_linux.cpp   (future)
//
// The audio backends (src/platform/fastresampler_audio_*.cpp) and the window
// glue (src/fastresampler_view_*.cpp) are per-platform files as well; they
// are the only other files that may include OS headers.
//
// Handles are void* so this header stays free of OS headers.

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

// ----------------------------------------------------------------- process ----

// Once at startup and once at exit: COM on Windows, nothing elsewhere.
bool plat_system_init(void);
void plat_system_shutdown(void);

// A line to the debugger output. The log file is written by fastresampler_log.h.
void plat_debug_output(const char* text);

// ----------------------------------------------------------------- time ----

uint64_t plat_ms_now(void);
void     plat_sleep_ms(uint32_t ms);

// ------------------------------------------------------------- threading ----

struct PlatThread {
    void* handle;
};
typedef void (*PlatThreadFn)(void* user);
bool plat_thread_create(PlatThread* t, PlatThreadFn fn, void* user);
void plat_thread_join(PlatThread* t);      // waits, then releases the handle

// ----------------------------------------------------------- file system ----

bool plat_path_exists(const char* path);
bool plat_path_is_dir(const char* path);
bool plat_dir_create(const char* path);      // one level; true when it exists after

// The whole path of a path that may be relative to the working folder.
// False when it does not fit out.
bool plat_path_absolute(const char* path, char* out, size_t out_len);

// The free bytes on the disk that holds path. The folder need not exist
// yet: the nearest folder above it that does is asked. False when none is
// found or the disk does not say.
bool plat_disk_free_bytes(const char* path, uint64_t* out_free);

// Directory of the running executable, with a trailing separator.
void plat_module_dir(char* out_dir, size_t max_len);

// The folder the platform installs VST3 plugins into by default.
void plat_default_plugin_dir(char* out_dir, size_t max_len);

// The folder VST 2 plugins are installed into, from the system's setting
// or the usual places; empty when none exists.
void plat_default_vst2_dir(char* out_dir, size_t max_len);

// The size and the last write time of a file, the time in a unit that only
// has to compare equal for the same file. False when the file is not there.
bool plat_file_stat(const char* path, uint64_t* out_size, uint64_t* out_mtime);

// Deletes a file. True when it is gone afterwards, so a path that was not
// there to begin with counts as deleted.
bool plat_file_delete(const char* path);

// ------------------------------------------------------ native libraries ----

// Loads a native library (a DLL) so its exports can be called. The folder
// of the file is searched first for the libraries it needs. Null, with err
// filled in, when it does not load.
void* plat_library_load(const char* path, char* err, size_t err_len);
void* plat_library_symbol(void* library, const char* name);
void  plat_library_unload(void* library);

// True when the file is a native library of this process's architecture
// that exports one of the names. Read from the file's headers: none of its
// code runs, so a broken library cannot take the caller down.
bool plat_library_exports(const char* path, const char* const* names, int name_count);

// The system's folder picker, modal over the given window; starts in
// start_dir when that exists. True with the chosen folder in out, false when
// the user cancelled. UI thread, outside the ImGui frame.
bool plat_pick_folder(void* parent_window, const char* start_dir, char* out, size_t out_len);

// Directory iteration. plat_dir_open() positions on the first entry; the
// "." and ".." entries are skipped. plat_dir_next() advances. Read name and
// is_dir after either returns true.
struct PlatDirIter {
    void* handle;
    char  name[FR_MAX_PATH];
    bool  is_dir;
    bool  has_pending;   // the entry found by open is still to be reported
};
bool plat_dir_open(PlatDirIter* it, const char* dir);
bool plat_dir_next(PlatDirIter* it);
void plat_dir_close(PlatDirIter* it);

// ---------------------------------------------------------- path helpers ----
// Pure string work, the same on every platform.

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

// Every level of a path, so a folder several deep is made in one call. The
// drive and the leading separators are stepped over: a root is never made.
// True when the whole chain is there afterwards.
inline bool plat_dir_create_all(const char* path) {
    char work[FR_MAX_PATH];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(work)) return false;
    memcpy(work, path, len + 1);
    size_t i = (work[0] && work[1] == ':') ? 2 : 0;
    while (work[i] == '\\' || work[i] == '/') ++i;
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

// dir + separator + name, with the separator added only when dir lacks one.
inline void plat_path_join(char* out, size_t out_size, const char* dir, const char* name) {
    size_t dir_len = strlen(dir);
    bool needs_sep = dir_len > 0 && dir[dir_len - 1] != '\\' && dir[dir_len - 1] != '/';
    int n = snprintf(out, out_size, "%s%s%s", dir, needs_sep ? PLAT_PATH_SEP_STR : "", name);
    if (n < 0 || (size_t)n >= out_size) out[out_size - 1] = '\0';
}
