// #pragma once (removed for unity build)
// See fastresampler_fingerprint.h. The measurement is fastsampler-shared's;
// this reads the frames it measures.

#include <stdlib.h>
#include <string.h>
#include "fastresampler_fingerprint.h"

bool fingerprint_analyzer_init(FingerprintAnalyzer* a, int64_t max_frames, int channels) {
    memset(a, 0, sizeof(*a));
    if (max_frames <= 0 || channels <= 0) return false;
    a->scratch = (SMFPAnalysisScratch*)malloc(sizeof(SMFPAnalysisScratch));
    a->pcm = (float*)malloc((size_t)max_frames * (size_t)channels * sizeof(float));
    if (!a->scratch || !a->pcm) {
        fingerprint_analyzer_free(a);
        return false;
    }
    smfp_analysis_scratch_init(a->scratch);
    a->capacity = max_frames;
    a->channels = channels;
    return true;
}

void fingerprint_analyzer_free(FingerprintAnalyzer* a) {
    free(a->scratch);
    free(a->pcm);
    memset(a, 0, sizeof(*a));
}

bool fingerprint_zone(FingerprintAnalyzer* a, WavReader* wav, int64_t first_frame, uint32_t total_frames,
                      bool loop, uint32_t loop_start, uint32_t loop_end, int root_note,
                      float* out_band_db, uint32_t* out_analysis_frames) {
    for (int b = 0; b < SMFP_BAND_COUNT; ++b) out_band_db[b] = 0.0f;
    *out_analysis_frames = 0;

    // The whole zone plays, and the automatic range takes its loop.
    uint32_t start, end;
    smfp_resolve_zone_range(total_frames, 0, 0, loop, loop_start, loop_end, nullptr, &start, &end);
    uint32_t frames = end - start;
    if (frames < (uint32_t)SMFP_ANALYSIS_FFT_SIZE) return true;   // flat
    if ((int64_t)frames > a->capacity) return false;

    // Only the measured frames are read, so they are measured from the
    // start of the buffer.
    if (!wav_reader_read(wav, first_frame + (int64_t)start, (int64_t)frames, a->pcm)) return false;
    *out_analysis_frames = smfp_analyze_zone_bands(a->scratch, a->pcm, frames, (uint32_t)a->channels,
                                                   (uint32_t)wav->sample_rate, 0, frames,
                                                   smfp_root_note_hz(root_note), out_band_db);
    return true;
}
