// #pragma once (removed for unity build)
// The Windows window: Win32 message loop, Direct3D 11 swap chain for ImGui,
// and the child window the plugin editor attaches to. The unity root has
// included <windows.h>, <d3d11.h>, ImGui and its two backends before this.

#include <stdlib.h>
#include "fastresampler_view.h"
#include "fastresampler_app.h"
#include "fastresampler_log.h"
#include "ui/fastresampler_ui.h"
#include "ui/fastresampler_palette.h"

struct PlatView {
    App*  app;
    HWND  hwnd;
    HWND  editor_hwnd;
    IDXGISwapChain*         swap_chain;
    ID3D11Device*           device;
    ID3D11DeviceContext*    context;
    ID3D11RenderTargetView* rtv;
    ImGuiContext* imgui;
    int   width;             // client size in pixels
    int   height;
    float dpi_scale;
    bool  in_frame;          // guards against a re-entered WM_PAINT
    int   last_avail_w;      // editor area last offered to the plugin
    int   last_avail_h;
    bool  editor_fit_pending;
    bool  closed;            // app_shutdown has run
};

static const wchar_t* g_main_class_name = L"FastResamplerMainWindow";
static const wchar_t* g_editor_class_name = L"FastResamplerEditorHost";

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// --- Direct3D ------------------------------------------------------------------

static void view_release_rtv(PlatView* v) {
    if (v->rtv) {
        v->rtv->Release();
        v->rtv = nullptr;
    }
}

static bool view_create_rtv(PlatView* v) {
    ID3D11Texture2D* back_buffer = nullptr;
    HRESULT hr = v->swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
    if (FAILED(hr) || !back_buffer) return false;
    hr = v->device->CreateRenderTargetView(back_buffer, nullptr, &v->rtv);
    back_buffer->Release();
    return SUCCEEDED(hr) && v->rtv != nullptr;
}

static void view_shutdown_d3d(PlatView* v) {
    view_release_rtv(v);
    if (v->swap_chain) { v->swap_chain->Release(); v->swap_chain = nullptr; }
    if (v->context) { v->context->Release(); v->context = nullptr; }
    if (v->device) { v->device->Release(); v->device = nullptr; }
}

static bool view_init_d3d(PlatView* v) {
    // The bitblt swap effect keeps the plugin editor's child window composited
    // over our surface without surprises; flip model presents are for windows
    // with no children.
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = (UINT)v->width;
    sd.BufferDesc.Height = (UINT)v->height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = v->hwnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL level;
    const D3D_FEATURE_LEVEL levels[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                               D3D11_SDK_VERSION, &sd, &v->swap_chain, &v->device, &level, &v->context);
    if (FAILED(hr)) {
        fr_log("view: hardware D3D11 failed (0x%08lx), trying WARP", (unsigned long)hr);
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2,
                                           D3D11_SDK_VERSION, &sd, &v->swap_chain, &v->device, &level, &v->context);
    }
    if (FAILED(hr)) {
        fr_log("view: D3D11CreateDeviceAndSwapChain failed (0x%08lx)", (unsigned long)hr);
        view_shutdown_d3d(v);
        return false;
    }
    if (!view_create_rtv(v)) {
        fr_log("view: CreateRenderTargetView failed");
        view_shutdown_d3d(v);
        return false;
    }
    return true;
}

static void view_resize_swap_chain(PlatView* v) {
    if (!v->swap_chain || v->width <= 0 || v->height <= 0) return;
    view_release_rtv(v);
    HRESULT hr = v->swap_chain->ResizeBuffers(0, (UINT)v->width, (UINT)v->height, DXGI_FORMAT_UNKNOWN, 0);
    if (SUCCEEDED(hr)) view_create_rtv(v);
}

// --- Editor placement ----------------------------------------------------------

// The editor spot: right of the side panel, under the main header.
static void view_editor_origin(PlatView* v, int* x, int* y) {
    *x = (int)(UI_SIDEPANEL_WIDTH * v->dpi_scale + 0.5f);
    *y = (int)(UI_MAIN_HEADER_HEIGHT * v->dpi_scale + 0.5f);
}

static void view_editor_available(PlatView* v, int* w, int* h) {
    int x, y;
    view_editor_origin(v, &x, &y);
    *w = v->width - x;
    *h = v->height - y - (int)(UI_KEYBOARD_HEIGHT * v->dpi_scale + 0.5f);
    if (*w < 1) *w = 1;
    if (*h < 1) *h = 1;
}

void* view_editor_create(PlatView* v, int width, int height) {
    view_editor_destroy(v);
    int x, y;
    view_editor_origin(v, &x, &y);
    v->editor_hwnd = CreateWindowExW(0, g_editor_class_name, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                     x, y, width, height, v->hwnd, NULL, GetModuleHandleW(NULL), NULL);
    if (!v->editor_hwnd) fr_log("view: editor child window failed (%lu)", (unsigned long)GetLastError());
    v->editor_fit_pending = true;
    return v->editor_hwnd;
}

// The child is clipped to the editor area: a plugin larger than the space
// keeps its own size and shows what fits until the window grows.
void view_editor_place(PlatView* v, int width, int height) {
    if (!v->editor_hwnd) return;
    int x, y, avail_w, avail_h;
    view_editor_origin(v, &x, &y);
    view_editor_available(v, &avail_w, &avail_h);
    int w = width < avail_w ? width : avail_w;
    int h = height < avail_h ? height : avail_h;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    RECT r;
    GetWindowRect(v->editor_hwnd, &r);
    MapWindowPoints(HWND_DESKTOP, v->hwnd, (POINT*)&r, 2);
    if (r.left == x && r.top == y && r.right - r.left == w && r.bottom - r.top == h) return;
    SetWindowPos(v->editor_hwnd, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

void view_editor_destroy(PlatView* v) {
    if (!v->editor_hwnd) return;
    DestroyWindow(v->editor_hwnd);
    v->editor_hwnd = NULL;
}

// Once per frame: offer the plugin the area it has when that area changed,
// then place the child window at the size the plugin settled on.
static void view_update_editor(PlatView* v) {
    App* app = v->app;
    if (!app->plugin || !app->editor_open || !v->editor_hwnd) return;
    int avail_w, avail_h;
    view_editor_available(v, &avail_w, &avail_h);
    if (avail_w != v->last_avail_w || avail_h != v->last_avail_h || v->editor_fit_pending) {
        v->last_avail_w = avail_w;
        v->last_avail_h = avail_h;
        v->editor_fit_pending = false;
        int w = app->editor_width, h = app->editor_height;
        host_plugin_editor_fit(app->plugin, avail_w, avail_h, &w, &h);
        if (w > 0 && h > 0) {
            app->editor_width = w;
            app->editor_height = h;
        }
    }
    view_editor_place(v, app->editor_width, app->editor_height);
}

// --- Frame ---------------------------------------------------------------------

static void view_render_frame(PlatView* v) {
    if (v->in_frame || !v->rtv || v->closed) return;
    v->in_frame = true;
    ImGui::SetCurrentContext(v->imgui);

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ui_draw_frame(v->app, (float)v->width, (float)v->height, v->dpi_scale);
    ImGui::Render();

    ImVec4 cc = CODE_COLOR_WINDOW_BG;
    float clear_color[4] = { cc.x, cc.y, cc.z, cc.w };
    v->context->OMSetRenderTargets(1, &v->rtv, nullptr);
    v->context->ClearRenderTargetView(v->rtv, clear_color);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    v->swap_chain->Present(1, 0);

    // Plugin loads, unloads and device changes happen here, outside the frame,
    // so a plugin that pumps messages while it loads cannot re-enter ImGui.
    app_process_requests(v->app);
    view_update_editor(v);
    v->in_frame = false;
}

static void view_on_close(PlatView* v) {
    if (!v->closed) {
        v->closed = true;
        app_shutdown(v->app);
    }
    DestroyWindow(v->hwnd);
}

static LRESULT CALLBACK view_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    PlatView* v = (PlatView*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!v) {
        if (msg == WM_NCCREATE) {
            CREATESTRUCTW* cs = (CREATESTRUCTW*)lparam;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    // A click on the plugin editor gives its child window the keyboard focus,
    // and Windows leaves it there. A press in our own area takes it back, or
    // the side panel's text fields never see a keystroke again.
    if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN) {
        if (GetFocus() != hwnd) SetFocus(hwnd);
    }

    if (v->imgui) {
        ImGui::SetCurrentContext(v->imgui);
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) return 1;
    }

    switch (msg) {
        case WM_SIZE: {
            if (wparam == SIZE_MINIMIZED) return 0;
            int w = LOWORD(lparam), h = HIWORD(lparam);
            if (w == 0 || h == 0) return 0;
            v->width = w;
            v->height = h;
            view_resize_swap_chain(v);
            v->app->settings.window_width = (int)((float)w / v->dpi_scale + 0.5f);
            v->app->settings.window_height = (int)((float)h / v->dpi_scale + 0.5f);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            MINMAXINFO* mmi = (MINMAXINFO*)lparam;
            mmi->ptMinTrackSize.x = (LONG)(WINDOW_MIN_WIDTH * v->dpi_scale);
            mmi->ptMinTrackSize.y = (LONG)(WINDOW_MIN_HEIGHT * v->dpi_scale);
            return 0;
        }
        case WM_DPICHANGED: {
            v->dpi_scale = (float)HIWORD(wparam) / (float)WIN_BASE_DPI;
            RECT* r = (RECT*)lparam;
            SetWindowPos(hwnd, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            if (v->imgui) {
                ImGui::SetCurrentContext(v->imgui);
                apply_neon_theme(v->dpi_scale);
            }
            if (v->app->plugin) host_plugin_editor_set_scale(v->app->plugin, v->dpi_scale);
            v->editor_fit_pending = true;
            return 0;
        }
        case WM_TIMER: {
            if (wparam == UI_TIMER_ID) InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            view_render_frame(v);
            return 0;
        }
        case WM_ERASEBKGND: {
            return 1;
        }
        case WM_CLOSE: {
            view_on_close(v);
            return 0;
        }
        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

// --- Lifetime ------------------------------------------------------------------

static bool view_register_classes(HINSTANCE instance) {
    static bool registered = false;
    if (registered) return true;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = view_wndproc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = g_main_class_name;
    if (!RegisterClassExW(&wc)) return false;

    // The editor host: plain default handling, a dark background until the
    // plugin's own window covers it.
    WNDCLASSEXW ec = {};
    ec.cbSize = sizeof(ec);
    ec.style = CS_DBLCLKS;
    ec.lpfnWndProc = DefWindowProcW;
    ec.hInstance = instance;
    ec.hCursor = LoadCursor(NULL, IDC_ARROW);
    ec.hbrBackground = CreateSolidBrush(RGB(18, 18, 20));
    ec.lpszClassName = g_editor_class_name;
    if (!RegisterClassExW(&ec)) return false;

    registered = true;
    return true;
}

PlatView* view_create(App* app, int width, int height) {
    ImGui_ImplWin32_EnableDpiAwareness();

    PlatView* v = (PlatView*)calloc(1, sizeof(PlatView));
    if (!v) return nullptr;
    v->app = app;
    app->view = v;

    HINSTANCE instance = GetModuleHandleW(NULL);
    if (!view_register_classes(instance)) {
        fr_log("view: RegisterClassEx failed (%lu)", (unsigned long)GetLastError());
        free(v);
        return nullptr;
    }

    v->dpi_scale = (float)GetDpiForSystem() / (float)WIN_BASE_DPI;
    if (v->dpi_scale <= 0.0f) v->dpi_scale = 1.0f;
    DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    RECT r = { 0, 0, (LONG)((float)width * v->dpi_scale), (LONG)((float)height * v->dpi_scale) };
    AdjustWindowRectEx(&r, style, FALSE, 0);
    v->hwnd = CreateWindowExW(0, g_main_class_name, L"FastResampler", style, CW_USEDEFAULT, CW_USEDEFAULT,
                              r.right - r.left, r.bottom - r.top, NULL, NULL, instance, v);
    if (!v->hwnd) {
        fr_log("view: CreateWindowEx failed (%lu)", (unsigned long)GetLastError());
        free(v);
        return nullptr;
    }
    v->dpi_scale = ImGui_ImplWin32_GetDpiScaleForHwnd(v->hwnd);
    if (v->dpi_scale <= 0.0f) v->dpi_scale = 1.0f;
    RECT client;
    GetClientRect(v->hwnd, &client);
    v->width = client.right - client.left;
    v->height = client.bottom - client.top;

    IMGUI_CHECKVERSION();
    v->imgui = ImGui::CreateContext();
    ImGui::SetCurrentContext(v->imgui);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.Fonts->AddFontDefault();
    apply_neon_theme(v->dpi_scale);

    if (!view_init_d3d(v)) {
        MessageBoxW(v->hwnd, L"Direct3D 11 could not be initialized.", L"FastResampler", MB_OK | MB_ICONERROR);
        ImGui::DestroyContext(v->imgui);
        v->imgui = nullptr;
        DestroyWindow(v->hwnd);
        free(v);
        return nullptr;
    }
    ImGui_ImplWin32_Init(v->hwnd);
    ImGui_ImplDX11_Init(v->device, v->context);

    ShowWindow(v->hwnd, SW_SHOW);
    SetTimer(v->hwnd, UI_TIMER_ID, UI_FRAME_INTERVAL_MS, NULL);
    fr_log("view: window %dx%d at scale %.2f", v->width, v->height, v->dpi_scale);
    return v;
}

void view_run(PlatView* v) {
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void view_destroy(PlatView* v) {
    if (!v) return;
    if (v->imgui) {
        ImGui::SetCurrentContext(v->imgui);
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext(v->imgui);
        v->imgui = nullptr;
    }
    view_shutdown_d3d(v);
    if (v->hwnd && IsWindow(v->hwnd)) {
        KillTimer(v->hwnd, UI_TIMER_ID);
        DestroyWindow(v->hwnd);
    }
    v->hwnd = NULL;
    if (v->app) v->app->view = nullptr;
    free(v);
}

void* view_native_window(PlatView* v) {
    return v ? (void*)v->hwnd : nullptr;
}

float view_dpi_scale(PlatView* v) {
    return v ? v->dpi_scale : 1.0f;
}
