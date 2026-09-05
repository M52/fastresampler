#pragma once
// Audio output. One device is open at a time; it calls the render function
// from its own thread once per block with planar float buffers to fill.
//
// The generic part (this header and fastresampler_audio.cpp) dispatches on the
// API. Each backend is a per-platform file:
//
//     src/platform/fastresampler_audio_wasapi.cpp   (Windows, always present)
//     src/platform/fastresampler_audio_asio.cpp     (Windows, Steinberg ASIO)
//
// A future platform adds its backend and lists it in audio_enumerate().

#include <stdint.h>
#include <atomic>
#include "fastresampler_constants.h"

enum AudioApi {
    AUDIO_API_WASAPI = 0,
    AUDIO_API_ASIO   = 1,
    AUDIO_API_COUNT
};

struct AudioDriverInfo {
    int  api;                         // AudioApi
    char name[FR_DRIVER_NAME_LEN];    // shown to the user, saved in settings
    char id[FR_DRIVER_ID_LEN];        // endpoint id or CLSID; empty = default
};

struct AudioDriverList {
    AudioDriverInfo drivers[FR_MAX_AUDIO_DRIVERS];
    int count;
};

// Called on the audio thread. outputs[c][0..frames) are to be filled; they
// are zeroed before the call.
typedef void (*AudioRenderFn)(void* user, float* const* outputs, int channel_count, int frames);

struct AudioDevice {
    bool   open;
    int    api;
    char   name[FR_DRIVER_NAME_LEN];

    double sample_rate;
    int    block_size;          // frames per render call
    int    channel_count;       // always FR_OUTPUT_CHANNELS today
    int    latency_frames;      // output latency the backend reports

    AudioRenderFn render;
    void*  user;
    void*  backend;             // WasapiState* or AsioState*, owned by the backend

    // The driver asked for a restart (ASIO reset, buffer size change). The UI
    // thread polls this and reopens the device.
    std::atomic<bool> reset_requested;

    char   error[FR_ERROR_LEN];

    // Planar scratch the backends render into before converting to the
    // device's interleaved sample format.
    float  scratch[FR_OUTPUT_CHANNELS][FR_MAX_BLOCK_SIZE];
    float* scratch_ptrs[FR_OUTPUT_CHANNELS];
};

const char* audio_api_name(int api);

void audio_enumerate(AudioDriverList* out);
bool audio_open(AudioDevice* dev, const AudioDriverInfo* driver, int block_size,
                AudioRenderFn render, void* user, void* sys_window);
void audio_close(AudioDevice* dev);
bool audio_has_control_panel(const AudioDevice* dev);
void audio_control_panel(AudioDevice* dev);

// Shared by the backends: zero the scratch, call the render function.
void audio_render_block(AudioDevice* dev, int frames);

// --- Backend entry points, one set per API ----------------------------------
void wasapi_enumerate(AudioDriverList* out);
bool wasapi_open(AudioDevice* dev, const AudioDriverInfo* driver, int block_size);
void wasapi_close(AudioDevice* dev);

void asio_enumerate(AudioDriverList* out);
bool asio_open(AudioDevice* dev, const AudioDriverInfo* driver, int block_size, void* sys_window);
void asio_close(AudioDevice* dev);
void asio_control_panel(AudioDevice* dev);
