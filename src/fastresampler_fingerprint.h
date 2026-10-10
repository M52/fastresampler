#pragma once

#include <stdint.h>
#include "fastresampler_wav.h"
#include "fastsampler_smfp_analysis.h"

struct FingerprintAnalyzer {
    SMFPAnalysisScratch* scratch;
    float*  pcm;
    int64_t capacity;
    int     channels;
};

bool fingerprint_analyzer_init(FingerprintAnalyzer* a, int64_t max_frames, int channels);
void fingerprint_analyzer_free(FingerprintAnalyzer* a);

bool fingerprint_zone(FingerprintAnalyzer* a, WavReader* wav, int64_t first_frame, uint32_t total_frames,
                      bool loop, uint32_t loop_start, uint32_t loop_end, int root_note,
                      float* out_band_db, uint32_t* out_analysis_frames);
