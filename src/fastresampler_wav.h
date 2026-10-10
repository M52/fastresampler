#pragma once

#include <stdio.h>
#include <stdint.h>

#define WAV_HEADER_SIZE 56

struct WavWriter {
    FILE*    file;
    int      channels;
    uint64_t frames_written;
};

bool wav_writer_open(WavWriter* w, const char* path, int channels, int sample_rate);
bool wav_writer_write(WavWriter* w, float* const* planar, int channels, int frames);
bool wav_writer_write_interleaved(WavWriter* w, const float* interleaved, int frames);
bool wav_writer_close(WavWriter* w);

struct WavReader {
    FILE*    file;
    int      channels;
    int      sample_rate;
    uint64_t frames;
};

bool wav_reader_open(WavReader* r, const char* path);
bool wav_reader_read(WavReader* r, int64_t start_frame, int64_t frame_count, float* interleaved_out);
bool wav_reader_read_mono(WavReader* r, int64_t start_frame, int64_t frame_count, float* mono_out);
void wav_reader_close(WavReader* r);

bool wav_copy_segment(const char* src_path, int64_t start_frame, int64_t frame_count, const char* dst_path);
