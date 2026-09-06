#pragma once
// WAV files for the offline bounce: a writer for 32 bit float interleaved
// audio with sizes patched into the header at close, a reader for the files
// that writer made, and a copy of a span of one into a new file.

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

// Reads the files the writer makes: our own float32 layout only.
struct WavReader {
    FILE*    file;
    int      channels;
    int      sample_rate;
    uint64_t frames;
};

bool wav_reader_open(WavReader* r, const char* path);
bool wav_reader_read(WavReader* r, int64_t start_frame, int64_t frame_count, float* interleaved_out);
void wav_reader_close(WavReader* r);

// Copies frame_count frames from start_frame of a file this writer made into
// a new file at dst_path.
bool wav_copy_segment(const char* src_path, int64_t start_frame, int64_t frame_count, const char* dst_path);
