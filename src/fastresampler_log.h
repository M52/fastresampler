#pragma once
// Logging: one line to the debugger and to fastresampler.log next to the exe.
// Not for the audio thread.

#include <stdio.h>
#include <stdarg.h>
#include "fastresampler_constants.h"
#include "platform/fastresampler_platform.h"

static char g_log_path[FR_MAX_PATH];

inline void fr_log_init(void) {
    char dir[FR_MAX_PATH];
    plat_module_dir(dir, sizeof(dir));
    plat_path_join(g_log_path, sizeof(g_log_path), dir, FR_LOG_FILE);
    FILE* f = fopen(g_log_path, "w");
    if (f) fclose(f);
}

inline void fr_log(const char* fmt, ...) {
    char line[1024];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(line, sizeof(line) - 2, fmt, args);
    va_end(args);
    if (n < 0) return;
    if (n > (int)sizeof(line) - 2) n = (int)sizeof(line) - 2;
    line[n] = '\n';
    line[n + 1] = '\0';

    plat_debug_output(line);
    if (g_log_path[0] == '\0') return;
    FILE* f = fopen(g_log_path, "a");
    if (!f) return;
    fputs(line, f);
    fclose(f);
}
