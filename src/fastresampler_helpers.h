#pragma once
// Small string helpers shared by every module.

#include <string.h>
#include <stdio.h>

// Bounded copy that always terminates.
inline void fr_strcpy(char* dst, size_t dst_size, const char* src) {
    if (dst_size == 0) return;
    size_t i = 0;
    for (; i + 1 < dst_size && src[i]; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

inline char fr_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

// Case-insensitive compare, ASCII only. Same contract as strcmp.
inline int fr_stricmp(const char* a, const char* b) {
    for (;; ++a, ++b) {
        int d = (int)(unsigned char)fr_lower(*a) - (int)(unsigned char)fr_lower(*b);
        if (d != 0 || *a == '\0') return d;
    }
}

// Removes a trailing ".vst3" (any case) from a file name, in place.
inline void fr_strip_vst3_ext(char* name) {
    size_t len = strlen(name);
    if (len >= 5 && fr_stricmp(name + len - 5, ".vst3") == 0) name[len - 5] = '\0';
}

// UTF-16 to UTF-8 for the SDK's String128 names. Non-BMP characters are
// written as '?', which is fine for a display name.
inline void fr_utf16_to_utf8(const char16_t* src, char* dst, size_t dst_size) {
    size_t o = 0;
    for (size_t i = 0; src[i] && o + 4 < dst_size; ++i) {
        unsigned c = src[i];
        if (c < 0x80) {
            dst[o++] = (char)c;
        } else if (c < 0x800) {
            dst[o++] = (char)(0xC0 | (c >> 6));
            dst[o++] = (char)(0x80 | (c & 0x3F));
        } else if (c >= 0xD800 && c <= 0xDFFF) {
            dst[o++] = '?';
            if (c <= 0xDBFF && src[i + 1]) ++i;
        } else {
            dst[o++] = (char)(0xE0 | (c >> 12));
            dst[o++] = (char)(0x80 | ((c >> 6) & 0x3F));
            dst[o++] = (char)(0x80 | (c & 0x3F));
        }
    }
    dst[o] = '\0';
}

inline float fr_clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline int fr_clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
