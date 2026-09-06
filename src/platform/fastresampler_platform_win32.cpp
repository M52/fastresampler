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

void plat_default_vst2_dir(char* out_dir, size_t max_len) {
    out_dir[0] = '\0';
    wchar_t wpath[FR_MAX_PATH];
    // The folder the installers of VST 2 plugins register.
    DWORD size = sizeof(wpath);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\VST", L"VSTPluginsPath", RRF_RT_REG_SZ, NULL, wpath, &size) == ERROR_SUCCESS &&
        wpath[0] && plat_wide_to_utf8(wpath, out_dir, (int)max_len) && plat_path_is_dir(out_dir)) {
        return;
    }
    // Else the usual folders, the first that exists.
    static const int roots[3] = { CSIDL_PROGRAM_FILES, CSIDL_PROGRAM_FILES, CSIDL_PROGRAM_FILES_COMMON };
    static const wchar_t* const tails[3] = { L"\\VstPlugins", L"\\Steinberg\\VstPlugins", L"\\VST2" };
    for (int i = 0; i < 3; ++i) {
        wpath[0] = L'\0';
        SHGetFolderPathW(NULL, roots[i], NULL, SHGFP_TYPE_CURRENT, wpath);
        if (!wpath[0]) continue;
        wcscat_s(wpath, FR_MAX_PATH, tails[i]);
        if (plat_wide_to_utf8(wpath, out_dir, (int)max_len) && plat_path_is_dir(out_dir)) return;
    }
    out_dir[0] = '\0';
}

bool plat_file_stat(const char* path, uint64_t* out_size, uint64_t* out_mtime) {
    wchar_t wpath[FR_MAX_PATH];
    if (!plat_utf8_to_wide(path, wpath, FR_MAX_PATH)) return false;
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(wpath, GetFileExInfoStandard, &fad)) return false;
    *out_size = ((uint64_t)fad.nFileSizeHigh << 32) | (uint64_t)fad.nFileSizeLow;
    *out_mtime = ((uint64_t)fad.ftLastWriteTime.dwHighDateTime << 32) | (uint64_t)fad.ftLastWriteTime.dwLowDateTime;
    return true;
}

bool plat_file_delete(const char* path) {
    wchar_t wpath[FR_MAX_PATH];
    if (!plat_utf8_to_wide(path, wpath, FR_MAX_PATH)) return false;
    if (DeleteFileW(wpath)) return true;
    return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
}

// ------------------------------------------------------ native libraries ----

void* plat_library_load(const char* path, char* err, size_t err_len) {
    wchar_t wpath[FR_MAX_PATH];
    if (!plat_utf8_to_wide(path, wpath, FR_MAX_PATH)) {
        snprintf(err, err_len, "The path is too long");
        return nullptr;
    }
    HMODULE h = LoadLibraryExW(wpath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (h) return (void*)h;
    DWORD code = GetLastError();
    char message[256];
    DWORD n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code,
                             MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), message, sizeof(message), NULL);
    while (n > 0 && (message[n - 1] == '\r' || message[n - 1] == '\n' || message[n - 1] == '.' || message[n - 1] == ' ')) message[--n] = '\0';
    if (n > 0) {
        snprintf(err, err_len, "The DLL did not load: %s (%lu)", message, (unsigned long)code);
    } else {
        snprintf(err, err_len, "The DLL did not load (%lu)", (unsigned long)code);
    }
    return nullptr;
}

void* plat_library_symbol(void* library, const char* name) {
    if (!library) return nullptr;
    return (void*)GetProcAddress((HMODULE)library, name);
}

void plat_library_unload(void* library) {
    if (library) FreeLibrary((HMODULE)library);
}

// The PE headers, read piece by piece: a plugin DLL with samples inside can
// be hundreds of megabytes.
static bool plat_pe_read(FILE* f, uint64_t offset, void* out, size_t len) {
    if (_fseeki64(f, (long long)offset, SEEK_SET) != 0) return false;
    return fread(out, 1, len, f) == len;
}

// A virtual address of the loaded image to an offset in the file.
static bool plat_pe_rva_to_offset(const IMAGE_SECTION_HEADER* sections, int count, DWORD rva, uint64_t* out) {
    for (int i = 0; i < count; ++i) {
        const IMAGE_SECTION_HEADER* s = &sections[i];
        DWORD size = s->Misc.VirtualSize ? s->Misc.VirtualSize : s->SizeOfRawData;
        if (rva >= s->VirtualAddress && rva < s->VirtualAddress + size) {
            *out = (uint64_t)s->PointerToRawData + (uint64_t)(rva - s->VirtualAddress);
            return true;
        }
    }
    return false;
}

#define PLAT_PE_MAX_SECTIONS 96
#define PLAT_PE_MAX_NAMES    65536
#define PLAT_PE_NAME_LEN     64

bool plat_library_exports(const char* path, const char* const* names, int name_count) {
    wchar_t wpath[FR_MAX_PATH];
    if (!plat_utf8_to_wide(path, wpath, FR_MAX_PATH)) return false;
    FILE* f = _wfopen(wpath, L"rb");
    if (!f) return false;

    bool found = false;
    DWORD* name_rvas = nullptr;
    IMAGE_DOS_HEADER dos;
    DWORD signature = 0;
    IMAGE_FILE_HEADER fh;
    IMAGE_SECTION_HEADER sections[PLAT_PE_MAX_SECTIONS];
    IMAGE_DATA_DIRECTORY export_dir = {};
    IMAGE_EXPORT_DIRECTORY ed;
    uint64_t nt_offset, opt_offset, sections_offset, offset;
    int section_count;
    WORD opt_magic = 0;
#if defined(_M_X64)
    const WORD machine = IMAGE_FILE_MACHINE_AMD64;
#elif defined(_M_ARM64)
    const WORD machine = IMAGE_FILE_MACHINE_ARM64;
#else
    const WORD machine = IMAGE_FILE_MACHINE_I386;
#endif

    if (!plat_pe_read(f, 0, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0) goto done;
    nt_offset = (uint64_t)dos.e_lfanew;
    if (!plat_pe_read(f, nt_offset, &signature, sizeof(signature)) || signature != IMAGE_NT_SIGNATURE) goto done;
    if (!plat_pe_read(f, nt_offset + 4, &fh, sizeof(fh))) goto done;
    if (fh.Machine != machine || !(fh.Characteristics & IMAGE_FILE_DLL)) goto done;

    // The export table's place, from whichever optional header the file has.
    opt_offset = nt_offset + 4 + sizeof(IMAGE_FILE_HEADER);
    if (!plat_pe_read(f, opt_offset, &opt_magic, sizeof(opt_magic))) goto done;
    if (opt_magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        IMAGE_OPTIONAL_HEADER64 opt;
        if (!plat_pe_read(f, opt_offset, &opt, sizeof(opt))) goto done;
        export_dir = opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    } else if (opt_magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        IMAGE_OPTIONAL_HEADER32 opt;
        if (!plat_pe_read(f, opt_offset, &opt, sizeof(opt))) goto done;
        export_dir = opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    } else {
        goto done;
    }
    if (export_dir.VirtualAddress == 0 || export_dir.Size == 0) goto done;

    section_count = fh.NumberOfSections < PLAT_PE_MAX_SECTIONS ? fh.NumberOfSections : PLAT_PE_MAX_SECTIONS;
    sections_offset = opt_offset + fh.SizeOfOptionalHeader;
    if (!plat_pe_read(f, sections_offset, sections, sizeof(IMAGE_SECTION_HEADER) * (size_t)section_count)) goto done;

    if (!plat_pe_rva_to_offset(sections, section_count, export_dir.VirtualAddress, &offset)) goto done;
    if (!plat_pe_read(f, offset, &ed, sizeof(ed))) goto done;
    if (ed.NumberOfNames == 0 || ed.NumberOfNames > PLAT_PE_MAX_NAMES) goto done;
    if (!plat_pe_rva_to_offset(sections, section_count, ed.AddressOfNames, &offset)) goto done;
    name_rvas = (DWORD*)malloc(sizeof(DWORD) * ed.NumberOfNames);
    if (!name_rvas || !plat_pe_read(f, offset, name_rvas, sizeof(DWORD) * ed.NumberOfNames)) goto done;

    for (DWORD i = 0; i < ed.NumberOfNames && !found; ++i) {
        uint64_t name_offset;
        if (!plat_pe_rva_to_offset(sections, section_count, name_rvas[i], &name_offset)) continue;
        if (_fseeki64(f, (long long)name_offset, SEEK_SET) != 0) continue;
        char name[PLAT_PE_NAME_LEN];
        size_t n = fread(name, 1, sizeof(name) - 1, f);
        name[n] = '\0';
        for (int k = 0; k < name_count; ++k) {
            if (strcmp(name, names[k]) == 0) {
                found = true;
                break;
            }
        }
    }
done:
    free(name_rvas);
    fclose(f);
    return found;
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
