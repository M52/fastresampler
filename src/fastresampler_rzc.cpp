// #pragma once (removed for unity build)
#include <stdio.h>
#include <string.h>
#include "fastresampler_rzc.h"
#include "fastresampler_helpers.h"
#include "platform/fastresampler_platform.h"

// FastSampler names a zone by its file without the folder or the extension,
// through get_filename_without_ext(); the zone names here already carry the
// sample's ".wav", so the same trimming applies.
static void rzc_name_without_ext(const char* name, char* out, size_t out_len) {
    fr_strcpy(out, out_len, plat_path_basename(name));
    char* dot = strrchr(out, '.');
    if (dot) *dot = '\0';
}

// The crossfade shapes, in the order FastSampler's reader matches them.
static const char* rzc_xfade_name(uint32_t xfadetype) {
    if (xfadetype == 1) return "S-Curves";
    if (xfadetype == 2) return "Equal Power";
    return "Linear";
}

bool rzc_write(const char* path, const FSIZone* zones,
               const char sample_paths[][FR_MAX_PATH], int zone_count) {
    FILE* f = fopen(path, "w");
    if (!f) return false;

    fprintf(f, "version: %d\n", FR_RZC_VERSION);
    fprintf(f, "zones:\n");

    for (int i = 0; i < zone_count; ++i) {
        const FSIZone* z = &zones[i];
        char no_ext[sizeof(z->name)];
        rzc_name_without_ext(z->name, no_ext, sizeof(no_ext));

        fprintf(f, "  - name: \"%s\"\n", no_ext);
        fprintf(f, "    file: \"%s\"\n", sample_paths[i]);
        fprintf(f, "    attack: %.6f\n", z->attack);
        fprintf(f, "    decay: %.6f\n", z->decay);
        fprintf(f, "    sustain: %.6f\n", z->sustain);
        fprintf(f, "    release: %.6f\n", z->release);
        fprintf(f, "    startframe: %u\n", z->startframe);
        fprintf(f, "    endframe: %u\n", z->endframe);
        fprintf(f, "    inframe: %u\n", z->inframe);
        fprintf(f, "    outframe: %u\n", z->outframe);
        fprintf(f, "    loopstart: %u\n", z->loopstart);
        fprintf(f, "    loopend: %u\n", z->loopend);
        fprintf(f, "    xfadeframes: %u\n", z->xfadeframes);
        fprintf(f, "    xfadetype: \"%s\"\n", rzc_xfade_name(z->xfadetype));
        fprintf(f, "    volume: %.6f\n", z->volume);
        fprintf(f, "    pitch: %.6f\n", z->pitch);
        fprintf(f, "    flag_loop: %d\n", z->loop_enabled ? 1 : 0);
        fprintf(f, "    flag_oneshot: %d\n", z->oneshot ? 1 : 0);
        fprintf(f, "    flag_release: %d\n", z->is_release_sample ? 1 : 0);
        fprintf(f, "    flag_ignore_vel: %d\n", z->ignore_vel_mapping ? 1 : 0);
        fprintf(f, "    pitchshift_type: %d\n", (int)z->pitchshift_type);
        fprintf(f, "    flip_lr: %s\n", z->flip_lr ? "true" : "false");
        // Only when the zone has one, as FastSampler writes it: a collection
        // with no legato in it keeps the lines it always had.
        if (z->arrivalframe > 0) fprintf(f, "    arrivalframe: %u\n", z->arrivalframe);
        fprintf(f, "    zoom_v: %.6f\n", z->zoom_v);
        if (z->zoom_end > 0) {
            fprintf(f, "    zoom_start: %u\n", z->zoom_start);
            fprintf(f, "    zoom_end: %u\n", z->zoom_end);
        }
        fprintf(f, "    low_note: %d\n", (int)z->low_note);
        fprintf(f, "    high_note: %d\n", (int)z->high_note);
        fprintf(f, "    low_vel: %d\n", (int)z->low_velocity);
        fprintf(f, "    high_vel: %d\n", (int)z->high_velocity);
        fprintf(f, "    root_note: %d\n", (int)z->root_note);
    }

    return fclose(f) == 0;
}
