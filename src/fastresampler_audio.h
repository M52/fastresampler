#pragma once

#include <atomic>
#include "fastresampler_constants.h"

enum AudioApi {
    AUDIO_API_WASAPI = 0,
    AUDIO_API_ASIO   = 1
};

struct AudioDriverInfo {
    int  api;
    char name[FR_DRIVER_NAME_LEN];
    char id[FR_DRIVER_ID_LEN];
};

struct AudioDriverList {
    AudioDriverInfo drivers[FR_MAX_AUDIO_DRIVERS];
    int count;
};

typedef void (*AudioRenderFn)(void* user, float* const* outputs, int channel_count, int frames);

struct AudioDevice {
    bool   open;
    int    api;
    char   name[FR_DRIVER_NAME_LEN];

    double sample_rate;
    int    block_size;
    int    channel_count;
    int    latency_frames;

    AudioRenderFn render;
    void*  user;
    void*  backend;

    std::atomic<bool> reset_requested;

    char   error[FR_ERROR_LEN];

    float  scratch[FR_OUTPUT_CHANNELS][FR_MAX_BLOCK_SIZE];
    float* scratch_ptrs[FR_OUTPUT_CHANNELS];
};

const char* audio_api_name(int api);

void audio_enumerate(AudioDriverList* out);
void audio_add_driver(AudioDriverList* out, int api, const char* name, const char* id);
bool audio_open(AudioDevice* dev, const AudioDriverInfo* driver, int block_size,
                AudioRenderFn render, void* user, void* sys_window);
void audio_close(AudioDevice* dev);
bool audio_has_control_panel(const AudioDevice* dev);
void audio_control_panel(AudioDevice* dev);

void audio_render_block(AudioDevice* dev, int frames);

void wasapi_enumerate(AudioDriverList* out);
bool wasapi_open(AudioDevice* dev, const AudioDriverInfo* driver, int block_size);
void wasapi_close(AudioDevice* dev);

void asio_enumerate(AudioDriverList* out);
bool asio_open(AudioDevice* dev, const AudioDriverInfo* driver, int block_size, void* sys_window);
void asio_close(AudioDevice* dev);
void asio_control_panel(AudioDevice* dev);
