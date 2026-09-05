// #pragma once (removed for unity build)
#include <string.h>
#include "fastresampler_audio.h"
#include "fastresampler_helpers.h"
#include "fastresampler_log.h"

const char* audio_api_name(int api) {
    switch (api) {
        case AUDIO_API_WASAPI: return "WASAPI";
        case AUDIO_API_ASIO:   return "ASIO";
        default:               return "?";
    }
}

void audio_enumerate(AudioDriverList* out) {
    out->count = 0;
    wasapi_enumerate(out);
    asio_enumerate(out);
    fr_log("audio: %d drivers found", out->count);
}

void audio_render_block(AudioDevice* dev, int frames) {
    if (frames > FR_MAX_BLOCK_SIZE) frames = FR_MAX_BLOCK_SIZE;
    for (int c = 0; c < dev->channel_count; ++c) {
        memset(dev->scratch[c], 0, (size_t)frames * sizeof(float));
    }
    if (dev->render) dev->render(dev->user, dev->scratch_ptrs, dev->channel_count, frames);
}

bool audio_open(AudioDevice* dev, const AudioDriverInfo* driver, int block_size,
                AudioRenderFn render, void* user, void* sys_window) {
    if (dev->open) audio_close(dev);

    dev->api = driver->api;
    fr_strcpy(dev->name, FR_DRIVER_NAME_LEN, driver->name);
    dev->render = render;
    dev->user = user;
    dev->channel_count = FR_OUTPUT_CHANNELS;
    dev->block_size = fr_clampi(block_size, FR_MIN_BLOCK_SIZE, FR_MAX_BLOCK_SIZE);
    dev->sample_rate = FR_DEFAULT_SAMPLE_RATE;
    dev->latency_frames = 0;
    dev->error[0] = '\0';
    dev->reset_requested.store(false);
    for (int c = 0; c < FR_OUTPUT_CHANNELS; ++c) dev->scratch_ptrs[c] = dev->scratch[c];

    bool ok = false;
    switch (driver->api) {
        case AUDIO_API_WASAPI: ok = wasapi_open(dev, driver, dev->block_size); break;
        case AUDIO_API_ASIO:   ok = asio_open(dev, driver, dev->block_size, sys_window); break;
        default: fr_strcpy(dev->error, FR_ERROR_LEN, "Unknown audio API"); break;
    }
    dev->open = ok;
    if (ok) {
        fr_log("audio: opened %s '%s' at %.0f Hz, %d frames, latency %d frames",
               audio_api_name(dev->api), dev->name, dev->sample_rate, dev->block_size, dev->latency_frames);
    } else {
        fr_log("audio: open of %s '%s' failed: %s", audio_api_name(dev->api), dev->name, dev->error);
    }
    return ok;
}

void audio_close(AudioDevice* dev) {
    if (!dev->open) return;
    switch (dev->api) {
        case AUDIO_API_WASAPI: wasapi_close(dev); break;
        case AUDIO_API_ASIO:   asio_close(dev); break;
        default: break;
    }
    dev->open = false;
    dev->backend = nullptr;
    fr_log("audio: closed");
}

bool audio_has_control_panel(const AudioDevice* dev) {
    return dev->open && dev->api == AUDIO_API_ASIO;
}

void audio_control_panel(AudioDevice* dev) {
    if (audio_has_control_panel(dev)) asio_control_panel(dev);
}
