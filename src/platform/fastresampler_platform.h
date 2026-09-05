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

// ----------------------------------------------------------- file system ----

bool plat_path_exists(const char* path);
bool plat_path_is_dir(const char* path);

// Directory of the running executable, with a trailing separator.
void plat_module_dir(char* out_dir, size_t max_len);

// The folder the platform installs VST3 plugins into by default.
void plat_default_plugin_dir(char* out_dir, size_t max_len);

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

// dir + separator + name, with the separator added only when dir lacks one.
inline void plat_path_join(char* out, size_t out_size, const char* dir, const char* name) {
    size_t dir_len = strlen(dir);
    bool needs_sep = dir_len > 0 && dir[dir_len - 1] != '\\' && dir[dir_len - 1] != '/';
    int n = snprintf(out, out_size, "%s%s%s", dir, needs_sep ? PLAT_PATH_SEP_STR : "", name);
    if (n < 0 || (size_t)n >= out_size) out[out_size - 1] = '\0';
}
