// #pragma once (removed for unity build)
// Windows implementation of fastresampler_platform.h. The unity root has
// already included <windows.h>.

#include <stdio.h>
#include "platform/fastresampler_platform.h"

// ----------------------------------------------------------------- process ----

bool plat_system_init(void) {
    // ASIO drivers and the WASAPI enumerator are COM objects; drivers expect
    // an apartment on the thread that talks to them.
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
}

void plat_system_shutdown(void) {
    CoUninitialize();
}

void plat_debug_output(const char* text) {
    OutputDebugStringA(text);
}

// ----------------------------------------------------------------- time ----

uint64_t plat_ms_now(void) {
    return (uint64_t)GetTickCount64();
}

void plat_sleep_ms(uint32_t ms) {
    Sleep(ms);
}

// ----------------------------------------------------------- file system ----

// UTF-8 in, UTF-16 out. Returns false when the path does not fit.
static bool plat_utf8_to_wide(const char* utf8, wchar_t* out, int out_len) {
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, out_len);
    return n > 0;
}

static bool plat_wide_to_utf8(const wchar_t* wide, char* out, int out_len) {
    int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, out_len, NULL, NULL);
    return n > 0;
}

bool plat_path_exists(const char* path) {
    wchar_t wpath[FR_MAX_PATH];
    if (!plat_utf8_to_wide(path, wpath, FR_MAX_PATH)) return false;
    return GetFileAttributesW(wpath) != INVALID_FILE_ATTRIBUTES;
}

bool plat_path_is_dir(const char* path) {
    wchar_t wpath[FR_MAX_PATH];
    if (!plat_utf8_to_wide(path, wpath, FR_MAX_PATH)) return false;
    DWORD attr = GetFileAttributesW(wpath);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

void plat_module_dir(char* out_dir, size_t max_len) {
    wchar_t wpath[FR_MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, wpath, FR_MAX_PATH);
    if (n == 0 || n >= FR_MAX_PATH) {
        out_dir[0] = '\0';
        return;
    }
    // Cut after the last separator so the directory keeps its trailing slash.
    for (int i = (int)n - 1; i >= 0; --i) {
        if (wpath[i] == L'\\' || wpath[i] == L'/') {
            wpath[i + 1] = L'\0';
            break;
        }
    }
    if (!plat_wide_to_utf8(wpath, out_dir, (int)max_len)) out_dir[0] = '\0';
}

void plat_default_plugin_dir(char* out_dir, size_t max_len) {
    wchar_t wpath[FR_MAX_PATH];
    wpath[0] = L'\0';
    SHGetFolderPathW(NULL, CSIDL_PROGRAM_FILES_COMMON, NULL, SHGFP_TYPE_CURRENT, wpath);
    wcscat_s(wpath, FR_MAX_PATH, L"\\VST3");
    if (!plat_wide_to_utf8(wpath, out_dir, (int)max_len)) out_dir[0] = '\0';
}

// Copies one search result into the iterator, or reports that it was a dot entry.
static bool plat_dir_store(PlatDirIter* it, const WIN32_FIND_DATAW* fd) {
    if (wcscmp(fd->cFileName, L".") == 0 || wcscmp(fd->cFileName, L"..") == 0) return false;
    if (!plat_wide_to_utf8(fd->cFileName, it->name, FR_MAX_PATH)) return false;
    it->is_dir = (fd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    return true;
}

bool plat_dir_open(PlatDirIter* it, const char* dir) {
    memset(it, 0, sizeof(*it));
    char pattern[FR_MAX_PATH];
    plat_path_join(pattern, sizeof(pattern), dir, "*");
    wchar_t wpattern[FR_MAX_PATH];
    if (!plat_utf8_to_wide(pattern, wpattern, FR_MAX_PATH)) return false;

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(wpattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    it->handle = h;

    // Walk past the dot entries so open lands on a real one, like next does.
    while (!plat_dir_store(it, &fd)) {
        if (!FindNextFileW(h, &fd)) {
            plat_dir_close(it);
            return false;
        }
    }
    it->has_pending = true;
    return true;
}

bool plat_dir_next(PlatDirIter* it) {
    if (!it->handle) return false;
    if (it->has_pending) {
        // The entry found by plat_dir_open() is the current one already.
        it->has_pending = false;
        return true;
    }
    WIN32_FIND_DATAW fd;
    while (FindNextFileW((HANDLE)it->handle, &fd)) {
        if (plat_dir_store(it, &fd)) return true;
    }
    return false;
}

void plat_dir_close(PlatDirIter* it) {
    if (it->handle) FindClose((HANDLE)it->handle);
    it->handle = NULL;
}
