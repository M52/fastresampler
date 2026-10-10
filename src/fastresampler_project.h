#pragma once

#include <stddef.h>
#include "fastresampler_constants.h"
#include "fastresampler_state.h"
#include "fastresampler_resample.h"

struct ProjectEntry {
    char name[FR_MAX_PATH];
    bool is_dir;
};

struct ProjectBrowser {
    char         dir[FR_MAX_PATH];
    ProjectEntry entries[FR_MAX_PROJECTS];
    int          count;
    int          selected;
};

struct ProjectPlugin {
    bool        present;
    int         format;
    char        path[FR_MAX_PATH];
    char        name[FR_PLUGIN_NAME_LEN];
    StateBuffer state;
};

void project_plugin_init(ProjectPlugin* p);
void project_plugin_release(ProjectPlugin* p);

void project_dir(char* out, size_t out_len);

bool project_clean_name(const char* name, char* out, size_t out_len);
void project_name_of_path(const char* path, char* out, size_t out_len);

void project_browser_open(ProjectBrowser* b, const char* dir);
void project_browser_select(ProjectBrowser* b, const char* name);
bool project_browser_parent(const char* dir, char* out, size_t out_len);
void project_browser_entry_path(const ProjectBrowser* b, int index, char* out, size_t out_len);

bool project_save(const ResampleConfig* cfg, const ProjectPlugin* plugin, const char* dir, const char* name,
                  char* path, size_t path_len, char* err, size_t err_len);
bool project_load(ResampleConfig* cfg, ProjectPlugin* plugin, const char* path,
                  char* err, size_t err_len);
