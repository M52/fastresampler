#pragma once

#include <string.h>
#include <stdlib.h>
#include <time.h>

inline void fr_strcpy(char* dst, size_t dst_size, const char* src) {
    if (dst_size == 0) return;
    size_t i = 0;
    for (; i + 1 < dst_size && src[i]; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

inline char fr_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

inline int fr_stricmp(const char* a, const char* b) {
    for (;; ++a, ++b) {
        int d = (int)(unsigned char)fr_lower(*a) - (int)(unsigned char)fr_lower(*b);
        if (d != 0 || *a == '\0') return d;
    }
}

inline void fr_strip_plugin_ext(char* name) {
    size_t len = strlen(name);
    if (len >= 5 && fr_stricmp(name + len - 5, ".vst3") == 0) {
        name[len - 5] = '\0';
    } else if (len >= 4 && fr_stricmp(name + len - 4, ".dll") == 0) {
        name[len - 4] = '\0';
    }
}

inline int fr_parse_floats(const char* text, float* out, int max_count) {
    int count = 0;
    const char* s = text;
    while (count < max_count) {
        char* end = nullptr;
        double v = strtod(s, &end);
        if (end == s) break;
        out[count++] = (float)v;
        s = end;
    }
    return count;
}

inline bool fr_localtime(const time_t* when, struct tm* out) {
#if defined(_WIN32)
    return localtime_s(out, when) == 0;
#else
    return localtime_r(when, out) != nullptr;
#endif
}

inline float fr_clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline int fr_clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
