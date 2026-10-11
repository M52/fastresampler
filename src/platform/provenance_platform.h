#pragma once
// Platform collection belongs to the application; encryption and layout are shared.
#include "../../3rdparty/fastsampler-shared/include/fastsampler_provenance.h"
#include <ctime>
#include <cstring>
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "advapi32.lib")
static bool provenance_random(void*, uint8_t* out, size_t size) {
    if (size > ULONG_MAX) return false;
    return BCryptGenRandom(nullptr, out, (ULONG)size, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}
static bool provenance_account(FSBProvenanceFields* fields) {
    memset(fields->display_name, 0, sizeof(fields->display_name));
    wchar_t name[1024] = {}; DWORD size = 1024; char utf8[4096] = {};
    if (!GetUserNameW(name, &size) || !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name, -1,
                                                        utf8, sizeof(utf8), nullptr, nullptr)) return false;
    if (!fsb_provenance_text(fields->account_name, sizeof(fields->account_name), utf8)) return false;
    // The OS display name is optional and is not available for every account.
    HMODULE security = LoadLibraryW(L"secur32.dll");
    if (security) {
        using NameFn = BOOLEAN (WINAPI*)(int, LPWSTR, PULONG);
        auto get_name = (NameFn)GetProcAddress(security, "GetUserNameExW");
        ULONG length = 1024;
        if (get_name && get_name(3, name, &length) &&
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name, -1, utf8, sizeof(utf8), nullptr, nullptr))
            fsb_provenance_text(fields->display_name, sizeof(fields->display_name), utf8);
        FreeLibrary(security);
    }
    return true;
}
#else
#include <unistd.h>
#include <pwd.h>
#ifdef __linux__
#include <sys/random.h>
#include <errno.h>
#endif
static bool provenance_random(void*, uint8_t* out, size_t size) {
#ifdef __linux__
    while (size) {
        ssize_t n = getrandom(out, size, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        out += n; size -= (size_t)n;
    }
    return true;
#else
    while (size) {
        size_t n = size > 256 ? 256 : size;
        if (getentropy(out, n) != 0) return false;
        out += n; size -= n;
    }
    return true;
#endif
}
static bool provenance_account(FSBProvenanceFields* fields) {
    memset(fields->display_name, 0, sizeof(fields->display_name));
    passwd entry = {}; passwd* result = nullptr; char buffer[32768];
    if (getpwuid_r(geteuid(), &entry, buffer, sizeof(buffer), &result) != 0 || !result || !entry.pw_name) return false;
    if (!fsb_provenance_text(fields->account_name, sizeof(fields->account_name), entry.pw_name)) return false;
    if (entry.pw_gecos) {
        char display[4096] = {}; size_t n = strcspn(entry.pw_gecos, ",");
        if (n < sizeof(display)) { memcpy(display, entry.pw_gecos, n);
            fsb_provenance_text(fields->display_name, sizeof(fields->display_name), display); }
    }
    return true;
}
#endif
static bool provenance_new(FSBProvenanceFields* fields, const char* creator, const char* version, const char* kind) {
    *fields = FSBProvenanceFields{};
    fields->version = FSB_PROVENANCE_VERSION;
    time_t now = time(nullptr);
    if (now < 0) return false;
    fields->created_utc = (uint64_t)now;
    return fsb_provenance_text(fields->creator, sizeof(fields->creator), creator) &&
           fsb_provenance_text(fields->creator_version, sizeof(fields->creator_version), version) &&
           fsb_provenance_text(fields->source_kind, sizeof(fields->source_kind), kind) && provenance_account(fields);
}
