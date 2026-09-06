// #pragma once (removed for unity build)
// Windows implementation of fastresampler_platform.h. The unity root has
// already included <windows.h>.

#include <stdio.h>
#include <stdlib.h>
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

// ------------------------------------------------------------- threading ----

struct PlatThreadStart {
    PlatThreadFn fn;
    void* user;
};

static DWORD WINAPI plat_thread_trampoline(LPVOID param) {
    PlatThreadStart start = *(PlatThreadStart*)param;
    free(param);
    start.fn(start.user);
    return 0;
}

bool plat_thread_create(PlatThread* t, PlatThreadFn fn, void* user) {
    t->handle = NULL;
    PlatThreadStart* start = (PlatThreadStart*)malloc(sizeof(PlatThreadStart));
    if (!start) return false;
    start->fn = fn;
    start->user = user;
    HANDLE h = CreateThread(NULL, 0, plat_thread_trampoline, start, 0, NULL);
    if (!h) {
        free(start);
        return false;
    }
    t->handle = h;
    return true;
}

void plat_thread_join(PlatThread* t) {
    if (!t->handle) return;
    WaitForSingleObject((HANDLE)t->handle, INFINITE);
    CloseHandle((HANDLE)t->handle);
    t->handle = NULL;
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

bool plat_dir_create(const char* path) {
    wchar_t wpath[FR_MAX_PATH];
    if (!plat_utf8_to_wide(path, wpath, FR_MAX_PATH)) return false;
    if (CreateDirectoryW(wpath, NULL)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS && plat_path_is_dir(path);
}

bool plat_disk_free_bytes(const char* path, uint64_t* out_free) {
    char probe[FR_MAX_PATH];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(probe)) return false;
    memcpy(probe, path, len + 1);
    for (;;) {
        wchar_t wpath[FR_MAX_PATH];
        if (plat_utf8_to_wide(probe, wpath, FR_MAX_PATH)) {
            ULARGE_INTEGER free_bytes;
            if (GetDiskFreeSpaceExW(wpath, &free_bytes, NULL, NULL)) {
                *out_free = (uint64_t)free_bytes.QuadPart;
                return true;
            }
        }
        // Up one level, keeping the separator so a drive root stays "D:\".
        while (len > 0 && (probe[len - 1] == '\\' || probe[len - 1] == '/')) probe[--len] = '\0';
        size_t cut = 0;
        for (size_t i = 0; i < len; ++i) {
            if (probe[i] == '\\' || probe[i] == '/') cut = i + 1;
        }
        if (cut == 0) return false;
        probe[cut] = '\0';
        len = cut;
    }
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

bool plat_pick_folder(void* parent_window, const char* start_dir, char* out, size_t out_len) {
    IFileDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || !dialog) return false;

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"Output folder");
    if (start_dir && start_dir[0] && plat_path_is_dir(start_dir)) {
        wchar_t wdir[FR_MAX_PATH];
        if (plat_utf8_to_wide(start_dir, wdir, FR_MAX_PATH)) {
            IShellItem* start = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(wdir, NULL, IID_PPV_ARGS(&start))) && start) {
                dialog->SetFolder(start);
                start->Release();
            }
        }
    }

    bool ok = false;
    if (SUCCEEDED(dialog->Show((HWND)parent_window))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item) {
            PWSTR wpath = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &wpath)) && wpath) {
                ok = plat_wide_to_utf8(wpath, out, (int)out_len);
                CoTaskMemFree(wpath);
            }
            item->Release();
        }
    }
    dialog->Release();
    return ok;
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
