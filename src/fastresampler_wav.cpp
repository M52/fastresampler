// #pragma once (removed for unity build)
#include <string.h>
#include "fastresampler_wav.h"
#include "fastresampler_constants.h"

#define WAV_FORMAT_IEEE_FLOAT 3
#define WAV_BITS_PER_SAMPLE   32

// The header: RIFF (0..11), fmt chunk (12..35), fact chunk (36..47), data
// chunk header (48..55). Three fields in it are patched at close.
#define WAV_RIFF_SIZE_OFFSET   4
#define WAV_FACT_FRAMES_OFFSET 44
#define WAV_DATA_SIZE_OFFSET   52
#define WAV_HEADER_SIZE        56

static void wav_put_u32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void wav_put_u16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

bool wav_writer_open(WavWriter* w, const char* path, int channels, int sample_rate) {
    memset(w, 0, sizeof(*w));
    if (channels < 1 || channels > FR_OUTPUT_CHANNELS) return false;
    w->file = fopen(path, "wb");
    if (!w->file) return false;
    w->channels = channels;
    w->sample_rate = sample_rate;

    uint32_t block_align = (uint32_t)channels * (WAV_BITS_PER_SAMPLE / 8);
    uint8_t h[WAV_HEADER_SIZE];
    memcpy(h + 0, "RIFF", 4);
    wav_put_u32(h + 4, 0);                 // patched at close
    memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4);
    wav_put_u32(h + 16, 16);
    wav_put_u16(h + 20, WAV_FORMAT_IEEE_FLOAT);
    wav_put_u16(h + 22, (uint16_t)channels);
    wav_put_u32(h + 24, (uint32_t)sample_rate);
    wav_put_u32(h + 28, (uint32_t)sample_rate * block_align);
    wav_put_u16(h + 32, (uint16_t)block_align);
    wav_put_u16(h + 34, WAV_BITS_PER_SAMPLE);
    memcpy(h + 36, "fact", 4);             // non-PCM files carry the frame count
    wav_put_u32(h + 40, 4);
    wav_put_u32(h + 44, 0);                // patched at close
    memcpy(h + 48, "data", 4);
    wav_put_u32(h + 52, 0);                // patched at close

    if (fwrite(h, 1, WAV_HEADER_SIZE, w->file) != WAV_HEADER_SIZE) {
        fclose(w->file);
        w->file = NULL;
        return false;
    }
    return true;
}

bool wav_writer_write(WavWriter* w, float* const* planar, int channels, int frames) {
    if (!w->file || frames <= 0) return false;
    float buf[FR_MAX_BLOCK_SIZE * FR_OUTPUT_CHANNELS];
    int done = 0;
    while (done < frames) {
        int n = frames - done;
        if (n > FR_MAX_BLOCK_SIZE) n = FR_MAX_BLOCK_SIZE;
        for (int i = 0; i < n; ++i) {
            for (int c = 0; c < w->channels; ++c) {
                int src = c < channels ? c : channels - 1;
                buf[i * w->channels + c] = planar[src][done + i];
            }
        }
        size_t count = (size_t)n * (size_t)w->channels;
        if (fwrite(buf, sizeof(float), count, w->file) != count) return false;
        done += n;
    }
    w->frames_written += (uint64_t)frames;
    return true;
}

bool wav_writer_write_interleaved(WavWriter* w, const float* interleaved, int frames) {
    if (!w->file || frames <= 0) return false;
    size_t count = (size_t)frames * (size_t)w->channels;
    if (fwrite(interleaved, sizeof(float), count, w->file) != count) return false;
    w->frames_written += (uint64_t)frames;
    return true;
}

static uint32_t wav_get_u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t wav_get_u16(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

bool wav_copy_segment(const char* src_path, int64_t start_frame, int64_t frame_count, const char* dst_path) {
    if (start_frame < 0 || frame_count <= 0) return false;
    FILE* src = fopen(src_path, "rb");
    if (!src) return false;

    // Our own layout: the fmt chunk at 20, the data at WAV_HEADER_SIZE.
    uint8_t h[WAV_HEADER_SIZE];
    if (fread(h, 1, WAV_HEADER_SIZE, src) != WAV_HEADER_SIZE || memcmp(h, "RIFF", 4) != 0 ||
        wav_get_u16(h + 20) != WAV_FORMAT_IEEE_FLOAT || wav_get_u16(h + 34) != WAV_BITS_PER_SAMPLE) {
        fclose(src);
        return false;
    }
    int channels = wav_get_u16(h + 22);
    int sample_rate = (int)wav_get_u32(h + 24);
    uint64_t data_bytes = wav_get_u32(h + WAV_DATA_SIZE_OFFSET);
    uint64_t src_frames = data_bytes / ((uint64_t)channels * (WAV_BITS_PER_SAMPLE / 8));
    if (channels < 1 || channels > FR_OUTPUT_CHANNELS || (uint64_t)start_frame >= src_frames) {
        fclose(src);
        return false;
    }
    if ((uint64_t)(start_frame + frame_count) > src_frames) frame_count = (int64_t)(src_frames - (uint64_t)start_frame);

    WavWriter dst;
    if (!wav_writer_open(&dst, dst_path, channels, sample_rate)) {
        fclose(src);
        return false;
    }
    int64_t frame_bytes = (int64_t)channels * (WAV_BITS_PER_SAMPLE / 8);
    bool ok = _fseeki64(src, (int64_t)WAV_HEADER_SIZE + start_frame * frame_bytes, SEEK_SET) == 0;
    float buf[FR_MAX_BLOCK_SIZE * FR_OUTPUT_CHANNELS];
    int64_t done = 0;
    while (ok && done < frame_count) {
        int64_t n = frame_count - done;
        if (n > FR_MAX_BLOCK_SIZE) n = FR_MAX_BLOCK_SIZE;
        size_t count = (size_t)n * (size_t)channels;
        ok = fread(buf, sizeof(float), count, src) == count;
        ok = ok && wav_writer_write_interleaved(&dst, buf, (int)n);
        done += n;
    }
    ok = wav_writer_close(&dst) && ok;
    fclose(src);
    return ok;
}

bool wav_writer_close(WavWriter* w) {
    if (!w->file) return false;
    uint64_t data_bytes = w->frames_written * (uint64_t)w->channels * (WAV_BITS_PER_SAMPLE / 8);
    if (data_bytes > 0xFFFFFFFFull - WAV_HEADER_SIZE) data_bytes = 0xFFFFFFFFull - WAV_HEADER_SIZE;
    uint8_t v[4];

    bool ok = true;
    wav_put_u32(v, (uint32_t)(data_bytes + WAV_HEADER_SIZE - 8));
    ok = ok && fseek(w->file, WAV_RIFF_SIZE_OFFSET, SEEK_SET) == 0 && fwrite(v, 1, 4, w->file) == 4;
    wav_put_u32(v, (uint32_t)w->frames_written);
    ok = ok && fseek(w->file, WAV_FACT_FRAMES_OFFSET, SEEK_SET) == 0 && fwrite(v, 1, 4, w->file) == 4;
    wav_put_u32(v, (uint32_t)data_bytes);
    ok = ok && fseek(w->file, WAV_DATA_SIZE_OFFSET, SEEK_SET) == 0 && fwrite(v, 1, 4, w->file) == 4;

    ok = fclose(w->file) == 0 && ok;
    w->file = NULL;
    return ok;
}
