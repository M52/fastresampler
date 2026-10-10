#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <stdlib.h>
#include <string.h>
#include "fastresampler_audio.h"
#include "fastresampler_helpers.h"
#include "fastresampler_log.h"

#ifndef AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
#define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM 0x80000000
#endif
#ifndef AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY
#define AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY 0x08000000
#endif

#define WASAPI_BUFFER_BLOCKS 4

enum WasapiSampleKind {
    WASAPI_SAMPLE_FLOAT32 = 0,
    WASAPI_SAMPLE_INT16,
    WASAPI_SAMPLE_INT24,
    WASAPI_SAMPLE_INT32
};

struct WasapiState {
    IMMDevice*          device;
    IAudioClient*       client;
    IAudioRenderClient* render;
    HANDLE              event;
    HANDLE              stop_event;
    HANDLE              thread;
    UINT32              buffer_frames;
    int                 device_channels;
    int                 bytes_per_sample;
    int                 sample_kind;
    int                 valid_bits;
    AudioDevice*        owner;
};

static IMMDeviceEnumerator* wasapi_create_enumerator(void) {
    IMMDeviceEnumerator* en = NULL;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), (void**)&en);
    if (FAILED(hr)) {
        fr_log("wasapi: MMDeviceEnumerator failed (0x%08lx)", (unsigned long)hr);
        return NULL;
    }
    return en;
}

void wasapi_enumerate(AudioDriverList* out) {
    IMMDeviceEnumerator* en = wasapi_create_enumerator();
    if (!en) return;

    audio_add_driver(out, AUDIO_API_WASAPI, "Default output device", "");

    IMMDeviceCollection* coll = NULL;
    if (SUCCEEDED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &coll)) && coll) {
        UINT n = 0;
        coll->GetCount(&n);
        for (UINT i = 0; i < n; ++i) {
            IMMDevice* d = NULL;
            if (FAILED(coll->Item(i, &d)) || !d) continue;

            char name[FR_DRIVER_NAME_LEN];
            fr_strcpy(name, sizeof(name), "Unknown device");
            IPropertyStore* props = NULL;
            if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, &props)) && props) {
                PROPVARIANT pv;
                PropVariantInit(&pv);
                if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR) {
                    WideCharToMultiByte(CP_UTF8, 0, pv.pwszVal, -1, name, sizeof(name), NULL, NULL);
                }
                PropVariantClear(&pv);
                props->Release();
            }

            char id[FR_DRIVER_ID_LEN];
            id[0] = '\0';
            LPWSTR wid = NULL;
            if (SUCCEEDED(d->GetId(&wid)) && wid) {
                WideCharToMultiByte(CP_UTF8, 0, wid, -1, id, sizeof(id), NULL, NULL);
                CoTaskMemFree(wid);
            }
            if (id[0]) audio_add_driver(out, AUDIO_API_WASAPI, name, id);
            d->Release();
        }
        coll->Release();
    }
    en->Release();
}

static bool wasapi_parse_format(WasapiState* s, const WAVEFORMATEX* fmt) {
    s->device_channels = fmt->nChannels;
    s->bytes_per_sample = fmt->wBitsPerSample / 8;
    s->valid_bits = fmt->wBitsPerSample;
    bool is_float = false;
    if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE && fmt->cbSize >= 22) {
        const WAVEFORMATEXTENSIBLE* ext = (const WAVEFORMATEXTENSIBLE*)fmt;
        is_float = IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != 0;
        if (!is_float && !IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_PCM)) return false;
        if (ext->Samples.wValidBitsPerSample) s->valid_bits = ext->Samples.wValidBitsPerSample;
    } else if (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        is_float = true;
    } else if (fmt->wFormatTag != WAVE_FORMAT_PCM) {
        return false;
    }

    if (is_float && fmt->wBitsPerSample == 32) {
        s->sample_kind = WASAPI_SAMPLE_FLOAT32;
    } else if (!is_float && fmt->wBitsPerSample == 16) {
        s->sample_kind = WASAPI_SAMPLE_INT16;
    } else if (!is_float && fmt->wBitsPerSample == 24) {
        s->sample_kind = WASAPI_SAMPLE_INT24;
    } else if (!is_float && fmt->wBitsPerSample == 32) {
        s->sample_kind = WASAPI_SAMPLE_INT32;
    } else {
        return false;
    }
    return true;
}

static void wasapi_convert(WasapiState* s, BYTE* dst, int frames) {
    AudioDevice* dev = s->owner;
    int ch = s->device_channels;
    for (int i = 0; i < frames; ++i) {
        for (int c = 0; c < ch; ++c) {
            float v = c < FR_OUTPUT_CHANNELS ? fr_clampf(dev->scratch[c][i], -1.0f, 1.0f) : 0.0f;
            switch (s->sample_kind) {
                case WASAPI_SAMPLE_FLOAT32: {
                    *(float*)dst = v;
                    break;
                }
                case WASAPI_SAMPLE_INT16: {
                    *(int16_t*)dst = (int16_t)(v * 32767.0f);
                    break;
                }
                case WASAPI_SAMPLE_INT24: {
                    int32_t q = (int32_t)(v * 8388607.0f);
                    dst[0] = (BYTE)(q & 0xFF);
                    dst[1] = (BYTE)((q >> 8) & 0xFF);
                    dst[2] = (BYTE)((q >> 16) & 0xFF);
                    break;
                }
                case WASAPI_SAMPLE_INT32: {
                    double scale = (double)((1u << (s->valid_bits - 1)) - 1u);
                    int32_t q = (int32_t)(v * scale);
                    *(int32_t*)dst = q << (32 - s->valid_bits);
                    break;
                }
            }
            dst += s->bytes_per_sample;
        }
    }
}

static void wasapi_fill(WasapiState* s) {
    UINT32 padding = 0;
    if (FAILED(s->client->GetCurrentPadding(&padding))) return;
    UINT32 avail = s->buffer_frames - padding;
    UINT32 block = (UINT32)s->owner->block_size;
    while (avail >= block) {
        BYTE* data = NULL;
        if (FAILED(s->render->GetBuffer(block, &data)) || !data) return;
        audio_render_block(s->owner, (int)block);
        wasapi_convert(s, data, (int)block);
        s->render->ReleaseBuffer(block, 0);
        avail -= block;
    }
}

static DWORD WINAPI wasapi_thread_main(LPVOID param) {
    WasapiState* s = (WasapiState*)param;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    wasapi_fill(s);
    s->client->Start();

    HANDLE waits[2] = { s->stop_event, s->event };
    for (;;) {
        DWORD r = WaitForMultipleObjects(2, waits, FALSE, 2000);
        if (r == WAIT_OBJECT_0) break;
        if (r == WAIT_OBJECT_0 + 1 || r == WAIT_TIMEOUT) {
            wasapi_fill(s);
            continue;
        }
        break;
    }

    s->client->Stop();
    CoUninitialize();
    return 0;
}

static void wasapi_fail(AudioDevice* dev, const char* what, HRESULT hr) {
    snprintf(dev->error, FR_ERROR_LEN, "%s (0x%08lx)", what, (unsigned long)hr);
}

static void wasapi_float_stereo_format(WAVEFORMATEXTENSIBLE* f, DWORD rate) {
    memset(f, 0, sizeof(*f));
    f->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    f->Format.nChannels = FR_OUTPUT_CHANNELS;
    f->Format.nSamplesPerSec = rate;
    f->Format.wBitsPerSample = 32;
    f->Format.nBlockAlign = (WORD)(FR_OUTPUT_CHANNELS * 4);
    f->Format.nAvgBytesPerSec = rate * f->Format.nBlockAlign;
    f->Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    f->Samples.wValidBitsPerSample = 32;
    f->dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    f->SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
}

bool wasapi_open(AudioDevice* dev, const AudioDriverInfo* driver, int block_size) {
    WasapiState* s = (WasapiState*)calloc(1, sizeof(WasapiState));
    if (!s) {
        fr_strcpy(dev->error, FR_ERROR_LEN, "Out of memory");
        return false;
    }
    s->owner = dev;
    dev->backend = s;

    IMMDeviceEnumerator* en = wasapi_create_enumerator();
    if (!en) {
        fr_strcpy(dev->error, FR_ERROR_LEN, "Could not create the device enumerator");
        wasapi_close(dev);
        return false;
    }
    HRESULT hr;
    if (driver->id[0]) {
        wchar_t wid[FR_DRIVER_ID_LEN];
        MultiByteToWideChar(CP_UTF8, 0, driver->id, -1, wid, FR_DRIVER_ID_LEN);
        hr = en->GetDevice(wid, &s->device);
    } else {
        hr = en->GetDefaultAudioEndpoint(eRender, eConsole, &s->device);
    }
    en->Release();
    if (FAILED(hr) || !s->device) {
        wasapi_fail(dev, "Audio device not found", hr);
        wasapi_close(dev);
        return false;
    }

    hr = s->device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&s->client);
    if (FAILED(hr) || !s->client) {
        wasapi_fail(dev, "IAudioClient activation failed", hr);
        wasapi_close(dev);
        return false;
    }

    WAVEFORMATEX* mix = NULL;
    hr = s->client->GetMixFormat(&mix);
    if (FAILED(hr) || !mix) {
        wasapi_fail(dev, "GetMixFormat failed", hr);
        wasapi_close(dev);
        return false;
    }
    DWORD rate = mix->nSamplesPerSec;
    REFERENCE_TIME duration = (REFERENCE_TIME)((double)block_size * WASAPI_BUFFER_BLOCKS * 10000000.0 / (double)rate);

    WAVEFORMATEXTENSIBLE want;
    wasapi_float_stereo_format(&want, rate);
    DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                  AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    hr = s->client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, duration, 0, &want.Format, NULL);
    bool parsed = SUCCEEDED(hr) && wasapi_parse_format(s, &want.Format);

    if (!parsed) {
        fr_log("wasapi: float32 stereo refused (0x%08lx), using the mix format", (unsigned long)hr);
        s->client->Release();
        s->client = NULL;
        hr = s->device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&s->client);
        if (SUCCEEDED(hr) && s->client) {
            hr = s->client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                       duration, 0, mix, NULL);
        }
        parsed = SUCCEEDED(hr) && wasapi_parse_format(s, mix);
        if (!parsed) {
            wasapi_fail(dev, "IAudioClient::Initialize failed", hr);
            CoTaskMemFree(mix);
            wasapi_close(dev);
            return false;
        }
    }
    CoTaskMemFree(mix);

    s->event = CreateEventW(NULL, FALSE, FALSE, NULL);
    s->stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!s->event || !s->stop_event) {
        fr_strcpy(dev->error, FR_ERROR_LEN, "CreateEvent failed");
        wasapi_close(dev);
        return false;
    }
    hr = s->client->SetEventHandle(s->event);
    if (FAILED(hr)) {
        wasapi_fail(dev, "SetEventHandle failed", hr);
        wasapi_close(dev);
        return false;
    }
    hr = s->client->GetBufferSize(&s->buffer_frames);
    if (FAILED(hr) || s->buffer_frames < (UINT32)block_size) {
        wasapi_fail(dev, "Endpoint buffer too small", hr);
        wasapi_close(dev);
        return false;
    }
    hr = s->client->GetService(__uuidof(IAudioRenderClient), (void**)&s->render);
    if (FAILED(hr) || !s->render) {
        wasapi_fail(dev, "IAudioRenderClient failed", hr);
        wasapi_close(dev);
        return false;
    }

    dev->sample_rate = (double)rate;
    dev->block_size = block_size;
    dev->latency_frames = (int)s->buffer_frames;

    s->thread = CreateThread(NULL, 0, wasapi_thread_main, s, 0, NULL);
    if (!s->thread) {
        fr_strcpy(dev->error, FR_ERROR_LEN, "CreateThread failed");
        wasapi_close(dev);
        return false;
    }
    fr_log("wasapi: %d device channels, kind %d, %d valid bits, endpoint buffer %u frames",
           s->device_channels, s->sample_kind, s->valid_bits, (unsigned)s->buffer_frames);
    return true;
}

void wasapi_close(AudioDevice* dev) {
    WasapiState* s = (WasapiState*)dev->backend;
    if (!s) return;
    if (s->thread) {
        SetEvent(s->stop_event);
        WaitForSingleObject(s->thread, 5000);
        CloseHandle(s->thread);
    }
    if (s->render) s->render->Release();
    if (s->client) s->client->Release();
    if (s->device) s->device->Release();
    if (s->event) CloseHandle(s->event);
    if (s->stop_event) CloseHandle(s->stop_event);
    free(s);
    dev->backend = NULL;
}
