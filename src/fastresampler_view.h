#pragma once

struct App;
struct PlatView;

PlatView* view_create(App* app, int width, int height);
void      view_run(PlatView* v);
void      view_destroy(PlatView* v);

void*     view_native_window(PlatView* v);
float     view_dpi_scale(PlatView* v);

void*     view_editor_create(PlatView* v, int width, int height);
void      view_editor_place(PlatView* v, int width, int height);
void      view_editor_destroy(PlatView* v);
