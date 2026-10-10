#pragma once
// Spectral fingerprints of the zones, for FastSampler's spectral morph filter.
//
// A run with dynamic layers crossfades them on one controller: each layer's
// gain rises off the floor as the controller comes up to it. A layer that is
// brighter than the one below is heard as soon as it leaves the floor, well
// before the controller reaches it, so its high end arrives early and the
// crossfade is heard as a second, brighter note fading in rather than as
// one note getting louder.
//
// The morph filter takes that out. It sits on every collection of a layer
// and bends each voice's spectrum toward the layer the controller is at:
// the upper layer comes in sounding like the lower one and turns into
// itself as the controller reaches its own place, while the lower one
// brightens as it leaves. To do that it needs the spectrum of every layer.
// FastSampler measures the collection that carries the filter itself, when
// it loads it, and reads every other layer from a fingerprint file (.smfp)
// named in the instrument. This file measures those.
//
// The measurement is fastsampler-shared's (fastsampler_smfp_analysis.h),
// the code FastSampler measures with: the same windows over the same frames
// of each zone, the same envelope and the same bands. A target measured any
// other way would not compare with FastSampler's own reading of the
// collection, and the filter would bend every voice by the difference
// between the two measurements as well as between the layers. This file
// only reads the frames of each zone from the WAV for it.

#include <stdint.h>
#include "fastresampler_wav.h"
#include "fastsampler_smfp_analysis.h"

// The buffers the analysis works in, sized once for the longest zone and
// reused for the rest.
struct FingerprintAnalyzer {
    SMFPAnalysisScratch* scratch;
    float*  pcm;        // the frames of one zone being measured, interleaved
    int64_t capacity;   // frames pcm holds
    int     channels;
};

// False when the buffers could not be had; the caller then writes no
// fingerprints, and the instrument crossfades its layers on gain alone.
bool fingerprint_analyzer_init(FingerprintAnalyzer* a, int64_t max_frames, int channels);
void fingerprint_analyzer_free(FingerprintAnalyzer* a);

// Measures one zone into its SMFP bands: first_frame and total_frames are
// its span in the full WAV, the loop is relative to first_frame, root_note
// the note it plays at. The zone plays whole, so it is measured over its
// loop when it has one, else 15 % to 85 % of it. A zone too short for one
// window is written flat, with analysis_frames 0, as FastSampler writes it.
// False when the audio could not be read.
bool fingerprint_zone(FingerprintAnalyzer* a, WavReader* wav, int64_t first_frame, uint32_t total_frames,
                      bool loop, uint32_t loop_start, uint32_t loop_end, int root_note,
                      float* out_band_db, uint32_t* out_analysis_frames);
