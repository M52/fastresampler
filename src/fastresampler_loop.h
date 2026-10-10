#pragma once

#include <stdint.h>
#include "fastresampler_wav.h"

struct LoopBounds {
    uint32_t loop_start;
    uint32_t loop_end;
    uint32_t xfade_frames;
};

struct LoopResult {
    LoopBounds bounds;
    float      score;
    bool       aligned;
    int        start_shift;
    int        end_shift;
};

struct LoopAnalyzer {
    float*  mono;
    float*  coarse;
    float*  envelope;
    int64_t capacity;
    int     env_capacity;
    int     sample_rate;
};

bool loop_analyzer_init(LoopAnalyzer* a, int64_t max_frames, int sample_rate);
void loop_analyzer_free(LoopAnalyzer* a);

bool loop_refine(LoopAnalyzer* a, WavReader* wav, int64_t first_frame, int64_t frame_count,
                 int root_note, const LoopBounds* nominal, LoopResult* out);
