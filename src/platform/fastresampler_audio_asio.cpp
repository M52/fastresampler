// #pragma once (removed for unity build)
// ASIO output. Drivers are found in the registry under HKLM\SOFTWARE\ASIO,
// created through COM, and drive us from their own thread through the
// callbacks below. The callbacks carry no user pointer, so one ASIO device is
// open at a time and lives in g_asio.
//
// The unity root has already included <windows.h>.

#include <stdlib.h>
#include <string.h>
#include "platform/fastresampler_asio.h"
#include "fastresampler_audio.h"
#include "fastresampler_helpers.h"
#include "fastresampler_log.h"

#define ASIO_REGISTRY_KEY "SOFTWARE\\ASIO"
#define ASIO_ENGINE_VERSION 2

struct AsioState {
    IASIO*          driver;
    ASIOBufferInfo  buffers[FR_OUTPUT_CHANNELS];
    ASIOChannelInfo channels[FR_OUTPUT_CHANNELS];
    ASIOCallbacks   callbacks;
    long            channel_count;    // outputs in use, at most FR_OUTPUT_CHANNELS
    long            buffer_size;
    bool            buffers_created;
    bool            started;
    bool            output_ready;     // the driver wants outputReady() after each switch
    AudioDevice*    owner;
};

static AsioState* g_asio = NULL;

static void asio_add_driver(AudioDriverList* out, const char* name, const char* clsid) {
    if (out->count >= FR_MAX_AUDIO_DRIVERS) return;
    AudioDriverInfo* d = &out->drivers[out->count++];
    d->api = AUDIO_API_ASIO;
    fr_strcpy(d->name, FR_DRIVER_NAME_LEN, name);
    fr_strcpy(d->id, FR_DRIVER_ID_LEN, clsid);
}

void asio_enumerate(AudioDriverList* out) {
    HKEY root;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, ASIO_REGISTRY_KEY, 0, KEY_READ, &root) != ERROR_SUCCESS) return;

    for (DWORD i = 0;; ++i) {
        char sub[FR_DRIVER_NAME_LEN];
        DWORD sub_len = sizeof(sub);
        if (RegEnumKeyExA(root, i, sub, &sub_len, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;

        HKEY key;
        if (RegOpenKeyExA(root, sub, 0, KEY_READ, &key) != ERROR_SUCCESS) continue;

        char clsid[FR_DRIVER_ID_LEN];
        DWORD clsid_len = sizeof(clsid);
        DWORD type = 0;
        if (RegQueryValueExA(key, "CLSID", NULL, &type, (LPBYTE)clsid, &clsid_len) == ERROR_SUCCESS && type == REG_SZ) {
            char desc[FR_DRIVER_NAME_LEN];
            DWORD desc_len = sizeof(desc);
            if (RegQueryValueExA(key, "Description", NULL, &type, (LPBYTE)desc, &desc_len) != ERROR_SUCCESS || type != REG_SZ) {
                fr_strcpy(desc, sizeof(desc), sub);
            }
            asio_add_driver(out, desc, clsid);
        }
        RegCloseKey(key);
    }
    RegCloseKey(root);
}

static bool asio_sample_type_supported(ASIOSampleType t) {
    switch (t) {
        case ASIOSTInt16LSB: case ASIOSTInt24LSB: case ASIOSTInt32LSB:
        case ASIOSTFloat32LSB: case ASIOSTFloat64LSB:
        case ASIOSTInt32LSB16: case ASIOSTInt32LSB18: case ASIOSTInt32LSB20: case ASIOSTInt32LSB24:
            return true;
        default:
            return false;
    }
}

// One channel of planar float scratch into the driver's buffer of that type.
static void asio_convert_channel(const float* src, void* dst, long frames, ASIOSampleType type) {
    switch (type) {
        case ASIOSTFloat32LSB: {
            float* d = (float*)dst;
            for (long i = 0; i < frames; ++i) d[i] = src[i];
            break;
        }
        case ASIOSTFloat64LSB: {
            double* d = (double*)dst;
            for (long i = 0; i < frames; ++i) d[i] = (double)src[i];
            break;
        }
        case ASIOSTInt16LSB: {
            int16_t* d = (int16_t*)dst;
            for (long i = 0; i < frames; ++i) d[i] = (int16_t)(fr_clampf(src[i], -1.0f, 1.0f) * 32767.0f);
            break;
        }
        case ASIOSTInt24LSB: {
            uint8_t* d = (uint8_t*)dst;
            for (long i = 0; i < frames; ++i) {
                int32_t q = (int32_t)(fr_clampf(src[i], -1.0f, 1.0f) * 8388607.0f);
                d[i * 3 + 0] = (uint8_t)(q & 0xFF);
                d[i * 3 + 1] = (uint8_t)((q >> 8) & 0xFF);
                d[i * 3 + 2] = (uint8_t)((q >> 16) & 0xFF);
            }
            break;
        }
        case ASIOSTInt32LSB: {
            int32_t* d = (int32_t*)dst;
            for (long i = 0; i < frames; ++i) d[i] = (int32_t)((double)fr_clampf(src[i], -1.0f, 1.0f) * 2147483647.0);
            break;
        }
        case ASIOSTInt32LSB16: case ASIOSTInt32LSB18: case ASIOSTInt32LSB20: case ASIOSTInt32LSB24: {
            // 32 bit words carrying a narrower sample in the low bits.
            int bits = type == ASIOSTInt32LSB16 ? 16 : type == ASIOSTInt32LSB18 ? 18 : type == ASIOSTInt32LSB20 ? 20 : 24;
            double scale = (double)((1u << (bits - 1)) - 1u);
            int32_t* d = (int32_t*)dst;
            for (long i = 0; i < frames; ++i) d[i] = (int32_t)((double)fr_clampf(src[i], -1.0f, 1.0f) * scale);
            break;
        }
        default:
            break;
    }
}

static void asio_render_switch(long index) {
    AsioState* s = g_asio;
    if (!s || !s->buffers_created || index < 0 || index > 1) return;
    AudioDevice* dev = s->owner;
    long frames = s->buffer_size;
    audio_render_block(dev, (int)frames);
    for (long c = 0; c < s->channel_count; ++c) {
        void* dst = s->buffers[c].buffers[index];
        if (dst) asio_convert_channel(dev->scratch[c], dst, frames, s->channels[c].type);
    }
    if (s->output_ready) s->driver->outputReady();
}

static void asio_cb_buffer_switch(long index, ASIOBool direct) {
    asio_render_switch(index);
}

static ASIOTime* asio_cb_buffer_switch_time_info(ASIOTime* time, long index, ASIOBool direct) {
    asio_render_switch(index);
    return time;
}

static void asio_cb_sample_rate_changed(ASIOSampleRate rate) {
    if (g_asio) g_asio->owner->reset_requested.store(true);
}

static long asio_cb_message(long selector, long value, void* message, double* opt) {
    switch (selector) {
        case kAsioSelectorSupported:
            return (value == kAsioResetRequest || value == kAsioEngineVersion ||
                    value == kAsioSupportsTimeInfo || value == kAsioBufferSizeChange ||
                    value == kAsioResyncRequest || value == kAsioLatenciesChanged) ? 1 : 0;
        case kAsioEngineVersion:
            return ASIO_ENGINE_VERSION;
        case kAsioResetRequest:
        case kAsioBufferSizeChange:
        case kAsioResyncRequest:
            if (g_asio) g_asio->owner->reset_requested.store(true);
            return 1;
        case kAsioLatenciesChanged:
        case kAsioSupportsTimeInfo:
            return 1;
        default:
            return 0;
    }
}

// The block the driver accepts nearest to the request. A granularity of -1
// means powers of two, 0 means the preferred size only.
static long asio_pick_buffer_size(long want, long min_size, long max_size, long preferred, long granularity) {
    if (want < min_size) want = min_size;
    if (want > max_size) want = max_size;
    if (granularity == 0) return preferred;
    if (granularity == -1) {
        long p = 1;
        while (p < want) p <<= 1;
        if (p > max_size) p >>= 1;
        return p < min_size ? min_size : p;
    }
    long steps = (want - min_size + granularity / 2) / granularity;
    long v = min_size + steps * granularity;
    if (v > max_size) v = max_size;
    return v;
}

static void asio_driver_error(AudioDevice* dev, AsioState* s, const char* what) {
    char msg[128];
    msg[0] = '\0';
    if (s->driver) s->driver->getErrorMessage(msg);
    snprintf(dev->error, FR_ERROR_LEN, "%s%s%s", what, msg[0] ? ": " : "", msg);
}

bool asio_open(AudioDevice* dev, const AudioDriverInfo* driver, int block_size, void* sys_window) {
    if (g_asio) {
        fr_strcpy(dev->error, FR_ERROR_LEN, "An ASIO device is already open");
        return false;
    }
    AsioState* s = (AsioState*)calloc(1, sizeof(AsioState));
    if (!s) {
        fr_strcpy(dev->error, FR_ERROR_LEN, "Out of memory");
        return false;
    }
    s->owner = dev;
    dev->backend = s;

    wchar_t wid[FR_DRIVER_ID_LEN];
    MultiByteToWideChar(CP_UTF8, 0, driver->id, -1, wid, FR_DRIVER_ID_LEN);
    CLSID clsid;
    if (FAILED(CLSIDFromString(wid, &clsid))) {
        fr_strcpy(dev->error, FR_ERROR_LEN, "The driver's CLSID is not valid");
        asio_close(dev);
        return false;
    }
    HRESULT hr = CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, clsid, (void**)&s->driver);
    if (FAILED(hr) || !s->driver) {
        snprintf(dev->error, FR_ERROR_LEN, "Could not create the ASIO driver (0x%08lx)", (unsigned long)hr);
        asio_close(dev);
        return false;
    }
    if (!s->driver->init(sys_window)) {
        asio_driver_error(dev, s, "The ASIO driver did not initialize");
        asio_close(dev);
        return false;
    }

    long in_count = 0, out_count = 0;
    if (s->driver->getChannels(&in_count, &out_count) != ASE_OK || out_count < 1) {
        asio_driver_error(dev, s, "The ASIO driver has no outputs");
        asio_close(dev);
        return false;
    }

    long min_size = 0, max_size = 0, preferred = 0, granularity = 0;
    if (s->driver->getBufferSize(&min_size, &max_size, &preferred, &granularity) != ASE_OK) {
        asio_driver_error(dev, s, "getBufferSize failed");
        asio_close(dev);
        return false;
    }
    long size = asio_pick_buffer_size(block_size, min_size, max_size, preferred, granularity);
    if (size > FR_MAX_BLOCK_SIZE || size < 1) {
        snprintf(dev->error, FR_ERROR_LEN, "The driver's buffer size %ld is not supported", size);
        asio_close(dev);
        return false;
    }

    ASIOSampleRate rate = 0.0;
    if (s->driver->getSampleRate(&rate) != ASE_OK || rate < 8000.0 || rate > 384000.0) {
        s->driver->setSampleRate(FR_DEFAULT_SAMPLE_RATE);
        if (s->driver->getSampleRate(&rate) != ASE_OK || rate < 8000.0 || rate > 384000.0) {
            asio_driver_error(dev, s, "The ASIO driver has no usable sample rate");
            asio_close(dev);
            return false;
        }
    }

    s->channel_count = out_count < FR_OUTPUT_CHANNELS ? out_count : FR_OUTPUT_CHANNELS;
    for (long c = 0; c < s->channel_count; ++c) {
        s->buffers[c].isInput = ASIOFalse;
        s->buffers[c].channelNum = c;
        s->buffers[c].buffers[0] = NULL;
        s->buffers[c].buffers[1] = NULL;
    }
    s->callbacks.bufferSwitch = asio_cb_buffer_switch;
    s->callbacks.sampleRateDidChange = asio_cb_sample_rate_changed;
    s->callbacks.asioMessage = asio_cb_message;
    s->callbacks.bufferSwitchTimeInfo = asio_cb_buffer_switch_time_info;

    // The callbacks read g_asio, so it is set before the driver can call them.
    g_asio = s;
    if (s->driver->createBuffers(s->buffers, s->channel_count, size, &s->callbacks) != ASE_OK) {
        asio_driver_error(dev, s, "createBuffers failed");
        asio_close(dev);
        return false;
    }
    s->buffers_created = true;
    s->buffer_size = size;

    for (long c = 0; c < s->channel_count; ++c) {
        s->channels[c].channel = c;
        s->channels[c].isInput = ASIOFalse;
        if (s->driver->getChannelInfo(&s->channels[c]) != ASE_OK || !asio_sample_type_supported(s->channels[c].type)) {
            snprintf(dev->error, FR_ERROR_LEN, "Output channel %ld has an unsupported sample type (%ld)",
                     c, (long)s->channels[c].type);
            asio_close(dev);
            return false;
        }
    }

    long lat_in = 0, lat_out = 0;
    if (s->driver->getLatencies(&lat_in, &lat_out) == ASE_OK) dev->latency_frames = (int)lat_out;
    s->output_ready = s->driver->outputReady() == ASE_OK;

    dev->sample_rate = rate;
    dev->block_size = (int)size;

    if (s->driver->start() != ASE_OK) {
        asio_driver_error(dev, s, "The ASIO driver did not start");
        asio_close(dev);
        return false;
    }
    s->started = true;

    char name[64];
    name[0] = '\0';
    s->driver->getDriverName(name);
    fr_log("asio: '%s' running, %ld outputs, type %ld, buffer %ld (min %ld max %ld pref %ld gran %ld)",
           name, s->channel_count, (long)s->channels[0].type, size, min_size, max_size, preferred, granularity);
    return true;
}

void asio_close(AudioDevice* dev) {
    AsioState* s = (AsioState*)dev->backend;
    if (!s) return;
    if (s->driver) {
        if (s->started) s->driver->stop();
        if (s->buffers_created) s->driver->disposeBuffers();
        s->driver->Release();
    }
    if (g_asio == s) g_asio = NULL;
    free(s);
    dev->backend = NULL;
}

void asio_control_panel(AudioDevice* dev) {
    AsioState* s = (AsioState*)dev->backend;
    if (s && s->driver) s->driver->controlPanel();
}
