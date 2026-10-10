#pragma once

#include <stdint.h>
#include "fastresampler_wav.h"

#define LEGATO_HARMONICS 6

struct LegatoBounds {
    int64_t departure;
    int64_t arrival;
    float   score;
    bool    found;
};

struct LegatoAnalyzer {
    float*  mono;
    float*  work;
    int64_t capacity;
    int     sample_rate;
};

bool legato_analyzer_init(LegatoAnalyzer* a, int64_t max_frames, int sample_rate);
void legato_analyzer_free(LegatoAnalyzer* a);

bool legato_find_bounds(LegatoAnalyzer* a, WavReader* wav, int64_t first_frame, int64_t frame_count,
                        int source_note, int target_note, int64_t note_on,
                        int64_t nominal_departure, int64_t nominal_arrival,
                        LegatoBounds* out);
