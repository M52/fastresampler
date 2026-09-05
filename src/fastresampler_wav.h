#pragma once
// A WAV writer for the offline bounce: 32 bit float, interleaved, sizes
// patched into the header when the file closes.

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
bool wav_writer_close(WavWriter* w);
