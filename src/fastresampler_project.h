#pragma once
// A resampling project: everything the Resample tab holds, in one YAML file
// under the Projects folder next to the exe. Saving writes the file, loading
// puts the configuration back, and a field the file does not hold keeps its
// default. The reader is src/fastresampler_yaml.h.

#include <stddef.h>
#include "fastresampler_constants.h"
#include "fastresampler_resample.h"

// The projects found in the folder, by name and without the extension.
struct ProjectList {
    char names[FR_MAX_PROJECTS][FR_PROJECT_NAME_LEN];
    int  count;
};

// The Projects folder next to the exe, with a trailing separator. The folder
// is not made here; project_save() makes it.
void project_dir(char* out, size_t out_len);

// <Projects folder>\<name>.yaml, with the name cleaned for a file name.
// False when nothing is left of the name.
bool project_path(const char* name, char* out, size_t out_len);

// The name with the characters a file name cannot hold replaced by '-', and
// the outer spaces and dots removed. False when nothing is left.
bool project_clean_name(const char* name, char* out, size_t out_len);

// The .yaml files in the Projects folder, sorted by name.
void project_scan(ProjectList* list);

// The configuration to a file and back. False with the reason in err.
bool project_save(const ResampleConfig* cfg, const char* name, char* err, size_t err_len);
bool project_load(ResampleConfig* cfg, const char* name, char* err, size_t err_len);
