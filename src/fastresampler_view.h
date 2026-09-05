#pragma once
// The native window, its renderer and the message loop, one implementation
// per platform:
//
//     src/fastresampler_view_win32.cpp   (Windows: Win32 + Direct3D 11)
//
// The view owns the ImGui context, calls ui_draw_frame() every frame, then
// app_process_requests(), and keeps the plugin editor's child window placed
// in the main area to the right of the side panel.

struct App;
struct PlatView;

PlatView* view_create(App* app, int width, int height);
void      view_run(PlatView* v);          // returns when the window closes
void      view_destroy(PlatView* v);

void*     view_native_window(PlatView* v); // the handle drivers and plugins get
float     view_dpi_scale(PlatView* v);

// The child window the plugin editor attaches to. Create sizes and places it
// at the editor spot; place moves it there again with a new size.
void*     view_editor_create(PlatView* v, int width, int height);
void      view_editor_place(PlatView* v, int width, int height);
void      view_editor_destroy(PlatView* v);
