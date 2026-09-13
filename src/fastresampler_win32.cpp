// FastResampler: the Windows unity translation unit root.
//
// build.bat compiles this one file. Everything else is #included below in
// dependency order: OS headers, third-party code (ImGui, the VST3 SDK hosting
// helpers) with warnings off, then the first-party modules and WinMain.

#include "fastresampler_build.h"

// INITGUID makes the Core Audio property keys and D3D interface ids real
// definitions in this TU, so no import library has to supply them.
#define INITGUID
#include <initguid.h>
#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <d3d11.h>
#include <dxgi.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <atomic>
#include <string>

// --- Third-party ---------------------------------------------------------------
#pragma warning(push, 0)

// imgui.cpp pulls in imgui_internal.h, which wants this set before imgui.h.
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"
#include "imgui.cpp"
#include "imgui_draw.cpp"
#include "imgui_tables.cpp"
#include "imgui_widgets.cpp"
#include "backends/imgui_impl_win32.cpp"
#include "backends/imgui_impl_dx11.cpp"

// VST3 SDK: the interfaces a host needs, then the hosting helpers and the
// base classes they compile against.
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/pluginterfacesupport.h"
#include "public.sdk/source/vst/hosting/connectionproxy.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "public.sdk/source/common/memorystream.h"

#include "pluginterfaces/base/coreiids.cpp"
#include "pluginterfaces/base/funknown.cpp"
#include "pluginterfaces/base/ustring.cpp"
#include "base/source/baseiids.cpp"
#include "base/source/fobject.cpp"
#include "base/source/fstring.cpp"
#include "base/source/updatehandler.cpp"
#include "base/thread/source/flock.cpp"
#include "public.sdk/source/common/commoniids.cpp"
#include "public.sdk/source/common/commonstringconvert.cpp"
#include "public.sdk/source/common/memorystream.cpp"
#include "public.sdk/source/common/threadchecker_win32.cpp"
#include "public.sdk/source/vst/vstinitiids.cpp"
#include "public.sdk/source/vst/utility/stringconvert.cpp"
#include "public.sdk/source/vst/hosting/module.cpp"
#include "public.sdk/source/vst/hosting/module_win32.cpp"
#include "public.sdk/source/vst/hosting/pluginterfacesupport.cpp"
#include "public.sdk/source/vst/hosting/hostclasses.cpp"
#include "public.sdk/source/vst/hosting/connectionproxy.cpp"
#include "public.sdk/source/vst/hosting/eventlist.cpp"
#include "public.sdk/source/vst/hosting/parameterchanges.cpp"
#include "public.sdk/source/vst/hosting/processdata.cpp"

// FastSampler's file formats, from the FastSampler checkout (see build.bat):
// the nanopb runtime, the generated schema code, the bank header and the
// magic of the instrument file.
#include "pb.h"
#include "pb_common.h"
#include "pb_encode.h"
#include "pb_common.c"
#include "pb_encode.c"
#include "fastsampler.pb.h"
#include "fastsampler.pb.c"
#include "fileformats/fastsampler_fsb.h"
#include "fileformats/fastsampler_pb.h"
// The collection effect ids and parameter slots, for the round robin effect
// the instrument builder writes.
#include "fx/collection/fastsampler_cfx.h"
#include "fx/collection/legato/fastsampler_cfx_legato.h"
// WavPack, from FastSampler's third-party folder: the bank builder
// compresses the streamed part of every sample with it. The DLL sits next
// to the exe, copied there by build.bat.
#include "wavpack.h"

#pragma warning(pop)

// --- First-party ---------------------------------------------------------------
#include "fastresampler_constants.h"
#include "platform/fastresampler_platform.h"
#include "platform/fastresampler_platform_win32.cpp"
#include "fastresampler_helpers.h"
#include "fastresampler_log.h"
#include "fastresampler_state.h"
#include "fastresampler_midi.h"
#include "fastresampler_resample.h"
#include "fastresampler_yaml.h"
#include "fastresampler_yaml.cpp"
#include "fastresampler_project.h"
#include "fastresampler_project.cpp"
#include "fastresampler_settings.h"
#include "fastresampler_settings.cpp"
#include "fastresampler_host.h"
#include "fastresampler_vst2.h"
#include "fastresampler_host_vst2.h"
#include "fastresampler_host_vst2.cpp"
#include "fastresampler_scan.h"
#include "fastresampler_scan.cpp"
#include "fastresampler_audio.h"
#include "fastresampler_audio.cpp"
#include "platform/fastresampler_audio_wasapi.cpp"
#include "platform/fastresampler_audio_asio.cpp"
#include "fastresampler_host.cpp"
#include "fastresampler_wav.h"
#include "fastresampler_wav.cpp"
#include "fastresampler_resample.cpp"
#include "fastresampler_rzc.h"
#include "fastresampler_rzc.cpp"
#include "fastresampler_loop.h"
#include "fastresampler_loop.cpp"
#include "fastresampler_legato.h"
#include "fastresampler_legato.cpp"
#include "fastresampler_instrument.h"
#include "fastresampler_instrument.cpp"
#include "fastresampler_app.h"
#include "fastresampler_view.h"
#include "fastresampler_app.cpp"
#include "ui/fastresampler_palette.h"
#include "ui/fastresampler_ui.h"
#include "ui/fastresampler_ui_keyboard.cpp"
#include "ui/fastresampler_ui_cc_curve.cpp"
#include "ui/fastresampler_ui_dragslider.h"
#include "ui/fastresampler_ui_dragslider.cpp"
#include "ui/fastresampler_ui.cpp"
#include "fastresampler_view_win32.cpp"

// --- Entry point ---------------------------------------------------------------

int WINAPI WinMain(HINSTANCE instance, HINSTANCE prev_instance, LPSTR cmd_line, int show_cmd) {
    fr_log_init();
    fr_log("FastResampler build %d starting", FASTRESAMPLER_BUILD_NUMBER);

    if (!plat_system_init()) {
        MessageBoxW(NULL, L"COM could not be initialized.", L"FastResampler", MB_OK | MB_ICONERROR);
        return 1;
    }

    // One instance for the life of the process; static so its atomics and
    // its big fixed arrays never touch the stack.
    static App app;
    app_init(&app);

    PlatView* view = view_create(&app, app.settings.window_width, app.settings.window_height);
    if (!view) {
        app_shutdown(&app);
        plat_system_shutdown();
        return 1;
    }
    app_start_audio(&app);

    // An optional path on the command line, a .vst3 module or a VST 2
    // instrument .dll, loads right away.
    if (cmd_line && cmd_line[0]) {
        char path[FR_MAX_PATH];
        fr_strcpy(path, sizeof(path), cmd_line);
        size_t len = strlen(path);
        while (len > 0 && (path[len - 1] == ' ' || path[len - 1] == '\t')) path[--len] = '\0';
        if (len >= 2 && path[0] == '"' && path[len - 1] == '"') {
            memmove(path, path + 1, len - 2);
            path[len - 2] = '\0';
        }
        app_request_load_path(&app, path);
    }

    view_run(view);

    // The close handler already shut the app down; this covers a loop that
    // ended some other way.
    if (app.host) app_shutdown(&app);
    view_destroy(view);
    plat_system_shutdown();
    fr_log("FastResampler exited");
    return 0;
}
