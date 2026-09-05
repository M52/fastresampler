#pragma once
// WAV files for the offline bounce: a writer for 32 bit float interleaved
// audio with sizes patched into the header at close, and a copy of a span
// of one such file into a new one, for the split into samples.

#include <stdio.h>
#include <stdint.h>

struct WavWriter {
    FILE*    file;
    int      channels;
    int      sample_rate;
    uint64_t frames_written;
};

bool wav_writer_open(WavWriter* w, const char* path, int channels, int sample_rate);
bool wav_writer_write(WavWriter* w, float* const* planar, int channels, int frames);
bool wav_writer_write_interleaved(WavWriter* w, const float* interleaved, int frames);
bool wav_writer_close(WavWriter* w);

// Copies frame_count frames from start_frame of a file this writer made into
// a new file at dst_path. The source header is read for channels and rate.
bool wav_copy_segment(const char* src_path, int64_t start_frame, int64_t frame_count, const char* dst_path);
