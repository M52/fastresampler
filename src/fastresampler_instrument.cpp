// #pragma once (removed for unity build)
// See fastresampler_instrument.h. The FastSampler format headers and the
// nanopb runtime are included by the unity root before this file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "fastresampler_instrument.h"
#include "fastresampler_wav.h"
#include "fastresampler_helpers.h"
#include "fastresampler_log.h"

// The bank is written at 24 bit, which is what FastSampler's own export picks
// for float sources. Three little-endian bytes per sample.
#define INSTRUMENT_FSB_BITS 24
#define INSTRUMENT_FSB_BYTES_PER_SAMPLE 3
#define INSTRUMENT_PCM_MAX_24  8388607
#define INSTRUMENT_PCM_MIN_24 (-8388608)

// A zone's release in seconds. Zones in FastSampler default to none, which
// cuts a note at the key-up; a short tail keeps a mapped instrument playable
// before the user has touched anything.
#define INSTRUMENT_ZONE_RELEASE_SECONDS 0.25f

// The mapping of one zone: its span of keys and velocities.
struct InstrumentZoneMap {
    int segment;        // index into the script's segments
    int low_note, high_note, root_note;
    int low_velocity, high_velocity;
    int round_robin;    // 1-based; the collection
};

// Sorted distinct values of one field over the segments.
static int instrument_distinct(const ResampleScript* script, bool velocities, int* out, int max_out) {
    int count = 0;
    for (int i = 0; i < script->segment_count; ++i) {
        int v = velocities ? script->segments[i].velocity : script->segments[i].note;
        bool seen = false;
        for (int j = 0; j < count; ++j) seen = seen || out[j] == v;
        if (seen) continue;
        if (count >= max_out) break;
        int k = count++;
        while (k > 0 && out[k - 1] > v) {
            out[k] = out[k - 1];
            --k;
        }
        out[k] = v;
    }
    return count;
}

// The key range a sampled note covers. A key between two samples goes to the
// nearer one; the middle key of an even gap goes up, so it is pitched down.
static void instrument_note_span(const int* notes, int count, int index, int* low, int* high) {
    if (index == 0) {
        *low = notes[0];
    } else {
        int gap = notes[index] - notes[index - 1];
        *low = notes[index - 1] + (gap + 1) / 2;
    }
    if (index == count - 1) {
        *high = notes[count - 1];
    } else {
        int gap = notes[index + 1] - notes[index];
        *high = notes[index] + (gap + 1) / 2 - 1;
    }
}

static void instrument_velocity_span(const int* velocities, int count, int index, int* low, int* high) {
    *low = index == 0 ? 0 : velocities[index - 1] + 1;
    *high = index == count - 1 ? MIDI_VALUE_MAX : velocities[index];
}

// The zones in file order: round robin major, then note, then velocity. Each
// collection's zones are contiguous, as FSICollection.first_zone_index needs.
static int instrument_map(const ResampleScript* script, InstrumentZoneMap* out, int* out_rr_count) {
    int notes[MIDI_NOTE_COUNT];
    int velocities[RESAMPLE_MAX_VELOCITIES];
    int note_count = instrument_distinct(script, false, notes, MIDI_NOTE_COUNT);
    int velocity_count = instrument_distinct(script, true, velocities, RESAMPLE_MAX_VELOCITIES);
    int rr_count = script->round_robin_count;

    int zone_count = 0;
    for (int rr = 1; rr <= rr_count; ++rr) {
        for (int n = 0; n < note_count; ++n) {
            for (int v = 0; v < velocity_count; ++v) {
                int seg = -1;
                for (int i = 0; i < script->segment_count; ++i) {
                    const ResampleSegment* s = &script->segments[i];
                    if (s->note == notes[n] && s->velocity == velocities[v] && s->round_robin == rr) {
                        seg = i;
                        break;
                    }
                }
                if (seg < 0) continue;   // a run that was cut short
                InstrumentZoneMap* z = &out[zone_count++];
                z->segment = seg;
                z->root_note = notes[n];
                instrument_note_span(notes, note_count, n, &z->low_note, &z->high_note);
                instrument_velocity_span(velocities, velocity_count, v, &z->low_velocity, &z->high_velocity);
                z->round_robin = rr;
            }
        }
    }
    *out_rr_count = rr_count;
    return zone_count;
}

// The zone's name, which is also its bank row's name: the sample file name,
// shortened from the front when it would not fit the format's 63 characters.
static void instrument_zone_name(const char* prefix, const ResampleSegment* seg, char* out, size_t out_len) {
    char note[8];
    midi_note_name(seg->note, note, sizeof(note));
    char suffix[32];
    snprintf(suffix, sizeof(suffix), "_%s_%d_RR%d.wav", note, (int)seg->velocity, (int)seg->round_robin);
    size_t room = MAX_NAME_LENGTH - 1 - strlen(suffix);
    char short_prefix[MAX_NAME_LENGTH];
    size_t n = strlen(prefix);
    if (n > room) n = room;
    memcpy(short_prefix, prefix, n);
    short_prefix[n] = '\0';
    snprintf(out, out_len, "%s%s", short_prefix, suffix);
}

static void instrument_fill_zone(FSIZone* dst, const char* name, const InstrumentZoneMap* map, uint32_t frames) {
    memset(dst, 0, sizeof(*dst));
    fr_strcpy(dst->name, sizeof(dst->name), name);
    dst->attack = 0.0f;
    dst->decay = 0.0f;
    dst->sustain = 1.0f;
    dst->release = INSTRUMENT_ZONE_RELEASE_SECONDS;
    dst->startframe = 0;
    dst->endframe = frames;
    dst->loopstart = 0;
    dst->loopend = frames;
    dst->xfadeframes = 0;
    dst->xfadetype = 0;
    dst->volume = 1.0f;
    dst->pitch = 0.0f;
    dst->has_loop_enabled = true;       dst->loop_enabled = false;
    dst->has_oneshot = true;            dst->oneshot = false;
    dst->has_is_release_sample = true;  dst->is_release_sample = false;
    dst->has_ignore_vel_mapping = true; dst->ignore_vel_mapping = false;
    dst->has_low_note = true;           dst->low_note = (uint32_t)map->low_note;
    dst->has_high_note = true;          dst->high_note = (uint32_t)map->high_note;
    dst->has_root_note = true;          dst->root_note = (uint32_t)map->root_note;
    dst->has_low_velocity = true;       dst->low_velocity = (uint32_t)map->low_velocity;
    dst->has_high_velocity = true;      dst->high_velocity = (uint32_t)map->high_velocity;
    dst->has_pitchshift_type = true;    dst->pitchshift_type = 0;
    dst->has_flip_lr = true;            dst->flip_lr = false;
    dst->has_zoom_start = true;         dst->zoom_start = 0;
    dst->has_zoom_end = true;           dst->zoom_end = frames;
    dst->has_zoom_v = true;             dst->zoom_v = 1.0f;
    dst->has_inframe = true;            dst->inframe = 0;
    dst->has_outframe = true;           dst->outframe = frames;
    dst->has_arrivalframe = true;       dst->arrivalframe = 0;
}

// --- The bank ------------------------------------------------------------------

// One float sample as the bank's 24 bit integer.
static int32_t instrument_sample_24(float s) {
    int32_t v = (int32_t)lrintf(fr_clampf(s, -1.0f, 1.0f) * PCM_ENCODE_24BIT);
    if (v > INSTRUMENT_PCM_MAX_24) v = INSTRUMENT_PCM_MAX_24;
    if (v < INSTRUMENT_PCM_MIN_24) v = INSTRUMENT_PCM_MIN_24;
    return v;
}

// Float frames to the bank's packed 24 bit little-endian samples.
static void instrument_pcm_to_24(const float* src, uint8_t* dst, size_t sample_count) {
    for (size_t i = 0; i < sample_count; ++i) {
        int32_t v = instrument_sample_24(src[i]);
        dst[i * 3 + 0] = (uint8_t)(v & 0xFF);
        dst[i * 3 + 1] = (uint8_t)((v >> 8) & 0xFF);
        dst[i * 3 + 2] = (uint8_t)((v >> 16) & 0xFF);
    }
}

// --- WavPack ---------------------------------------------------------------------
// The streamed part of a sample is compressed the way FastSampler's own
// export compresses it: 24 bit, normal mode, packed in blocks of frames.

#define INSTRUMENT_PACK_BLOCK_FRAMES     4096
#define INSTRUMENT_PACK_FIRST_CAPACITY   65536
// WAVE channel masks: front-centre for mono, front-left and front-right for stereo.
#define INSTRUMENT_WP_CHANNEL_MASK_MONO   4
#define INSTRUMENT_WP_CHANNEL_MASK_STEREO 3

// Where the encoder puts its blocks: one growing buffer per stream.
struct InstrumentPackBuffer {
    uint8_t* data;
    size_t   size;
    size_t   capacity;
    bool     failed;
};

static int instrument_pack_output(void* id, void* data, int32_t count) {
    InstrumentPackBuffer* buf = (InstrumentPackBuffer*)id;
    if (count <= 0) return 1;
    if (buf->size + (size_t)count > buf->capacity) {
        size_t capacity = buf->capacity ? buf->capacity * 2 : INSTRUMENT_PACK_FIRST_CAPACITY;
        while (capacity < buf->size + (size_t)count) capacity *= 2;
        uint8_t* grown = (uint8_t*)realloc(buf->data, capacity);
        if (!grown) {
            buf->failed = true;
            return 0;
        }
        buf->data = grown;
        buf->capacity = capacity;
    }
    memcpy(buf->data + buf->size, data, (size_t)count);
    buf->size += (size_t)count;
    return 1;
}

// Float frames to one WavPack stream. False, with nothing kept, when the
// library refused; the log says what it said.
static bool instrument_compress(const float* pcm, int frames, int channels, int sample_rate, InstrumentPackBuffer* out) {
    memset(out, 0, sizeof(*out));
    if (frames <= 0) return false;
    WavpackContext* wpc = WavpackOpenFileOutput(instrument_pack_output, out, NULL);
    if (!wpc) {
        fr_log("instrument: WavPack could not open an output stream");
        return false;
    }
    WavpackConfig config = {};
    config.bits_per_sample = INSTRUMENT_FSB_BITS;
    config.bytes_per_sample = INSTRUMENT_FSB_BYTES_PER_SAMPLE;
    config.num_channels = channels;
    config.sample_rate = sample_rate;
    config.channel_mask = channels == 1 ? INSTRUMENT_WP_CHANNEL_MASK_MONO : INSTRUMENT_WP_CHANNEL_MASK_STEREO;

    size_t samples = (size_t)frames * (size_t)channels;
    int32_t* ints = (int32_t*)malloc(samples * sizeof(int32_t));
    bool ok = ints != nullptr && WavpackSetConfiguration(wpc, &config, (uint32_t)frames) && WavpackPackInit(wpc);
    if (ok) {
        for (size_t i = 0; i < samples; ++i) ints[i] = instrument_sample_24(pcm[i]);
        for (int done = 0; done < frames && ok; done += INSTRUMENT_PACK_BLOCK_FRAMES) {
            int block = frames - done < INSTRUMENT_PACK_BLOCK_FRAMES ? frames - done : INSTRUMENT_PACK_BLOCK_FRAMES;
            ok = WavpackPackSamples(wpc, ints + (size_t)done * (size_t)channels, (uint32_t)block) != 0;
        }
        ok = ok && WavpackFlushSamples(wpc) != 0;
    }
    if (!ok) fr_log("instrument: WavPack: %s", WavpackGetErrorMessage(wpc));
    WavpackCloseFile(wpc);
    free(ints);
    ok = ok && !out->failed && out->size > 0;
    if (!ok) {
        free(out->data);
        memset(out, 0, sizeof(*out));
    }
    return ok;
}

// The bank: every zone is its own stored sample. In RAM mode each sample is
// held whole in the preload block and nothing streams. Otherwise each sample
// keeps its first preload frames in the block and the rest follows, WavPack
// compressed, in a stream of its own: FastSampler's own layout for a sample
// one zone plays. Rows are written in zone order because the loader pairs
// row i with zone i.
static bool instrument_write_fsb(InstrumentJob* job, WavReader* wav, const InstrumentZoneMap* map, int zone_count,
                                 FSBZoneEntry* rows, const char (*names)[MAX_NAME_LENGTH]) {
    FILE* f = fopen(job->fsb_path, "wb");
    if (!f) {
        snprintf(job->result, sizeof(job->result), "Could not create %s", job->fsb_path);
        return false;
    }
    bool ram_only = job->cfg.preload_frames <= 0;
    int channels = wav->channels;
    FSBHeader header = {};
    header.magic = FSB_MAGIC;
    header.version = FSB_VERSION;
    header.sample_rate = (uint32_t)wav->sample_rate;
    header.channels = (uint32_t)channels;
    header.preload_frames = ram_only ? FSB_PRELOAD_ALL_FRAMES : (uint32_t)job->cfg.preload_frames;
    header.zone_count = (uint32_t)zone_count;
    header.bps = INSTRUMENT_FSB_BITS;
    header.preload_offset = sizeof(FSBHeader) + sizeof(FSBZoneEntry) * (uint64_t)zone_count;

    // Placeholders; the header and the rows are rewritten once the sizes are known.
    bool ok = fwrite(&header, sizeof(header), 1, f) == 1;
    ok = ok && fwrite(rows, sizeof(FSBZoneEntry), (size_t)zone_count, f) == (size_t)zone_count;

    // The preload block: the resident head of every sample, one after the other.
    const ResampleScript* script = job->script;
    uint64_t preload_frame = 0;
    for (int z = 0; z < zone_count && ok; ++z) {
        if (job->cancel.load()) break;
        const ResampleSegment* seg = &script->segments[map[z].segment];
        int64_t frames = seg->end_frame - seg->start_frame;
        int64_t preload = ram_only || frames < (int64_t)job->cfg.preload_frames ? frames : (int64_t)job->cfg.preload_frames;
        size_t samples = (size_t)preload * (size_t)channels;
        float* pcm = (float*)malloc(samples * sizeof(float));
        uint8_t* packed = (uint8_t*)malloc(samples * INSTRUMENT_FSB_BYTES_PER_SAMPLE);
        ok = pcm && packed && wav_reader_read(wav, seg->start_frame, preload, pcm);
        if (ok) {
            instrument_pcm_to_24(pcm, packed, samples);
            ok = fwrite(packed, INSTRUMENT_FSB_BYTES_PER_SAMPLE, samples, f) == samples;
        }
        free(pcm);
        free(packed);
        if (!ok) {
            snprintf(job->result, sizeof(job->result), "Could not write sample %d of %d to the bank", z + 1, zone_count);
            break;
        }
        FSBZoneEntry* row = &rows[z];
        memset(row, 0, sizeof(*row));
        fr_strcpy(row->name, sizeof(row->name), names[z]);
        row->preload_frame_offset = preload_frame;
        row->preload_frame_count = (uint64_t)preload;
        row->total_frames = (uint64_t)frames;
        row->sample_frame_offset = 0;
        row->sample_index = (uint32_t)z;
        row->sample_frames = (uint32_t)frames;
        row->stream_offset = 0;
        row->stream_size = 0;
        row->stream_first_frame = (uint64_t)preload;   // the stream, when there is one, starts past the preload
        preload_frame += (uint64_t)preload;
    }
    header.preload_size = preload_frame * (uint64_t)channels * INSTRUMENT_FSB_BYTES_PER_SAMPLE;

    // The streams: what each sample holds past its preload. The file position
    // is counted here rather than asked, so a bank past 2 GB keeps its offsets.
    uint64_t write_pos = header.preload_offset + header.preload_size;
    for (int z = 0; z < zone_count && ok; ++z) {
        if (job->cancel.load()) break;
        FSBZoneEntry* row = &rows[z];
        int64_t stream_frames = (int64_t)(row->total_frames - row->preload_frame_count);
        if (stream_frames > 0) {
            const ResampleSegment* seg = &script->segments[map[z].segment];
            size_t samples = (size_t)stream_frames * (size_t)channels;
            float* pcm = (float*)malloc(samples * sizeof(float));
            InstrumentPackBuffer stream = {};
            ok = pcm && wav_reader_read(wav, seg->start_frame + (int64_t)row->preload_frame_count, stream_frames, pcm) &&
                 instrument_compress(pcm, (int)stream_frames, channels, wav->sample_rate, &stream);
            free(pcm);
            if (ok) {
                ok = fwrite(stream.data, 1, stream.size, f) == stream.size;
                row->stream_offset = write_pos;
                row->stream_size = (uint64_t)stream.size;
                write_pos += (uint64_t)stream.size;
            }
            free(stream.data);
            if (!ok) {
                snprintf(job->result, sizeof(job->result), "Could not compress sample %d of %d for the bank", z + 1, zone_count);
                break;
            }
        }
        job->progress.store(z + 1);
    }

    if (ok && !job->cancel.load()) {
        ok = fseek(f, 0, SEEK_SET) == 0 &&
             fwrite(&header, sizeof(header), 1, f) == 1 &&
             fwrite(rows, sizeof(FSBZoneEntry), (size_t)zone_count, f) == (size_t)zone_count;
        if (!ok) snprintf(job->result, sizeof(job->result), "Could not finish %s", job->fsb_path);
    }
    fclose(f);
    return ok;
}

// --- The instrument ---------------------------------------------------------------

static bool instrument_pb_write_callback(pb_ostream_t* stream, const uint8_t* buf, size_t count) {
    FILE* f = (FILE*)stream->state;
    return fwrite(buf, 1, count, f) == count;
}

// magic(4) + payload size(4) + the encoded FSIFile, as fastsampler_pb.cpp writes it.
static bool instrument_write_fsi(InstrumentJob* job, const InstrumentZoneMap* map, int zone_count, int rr_count,
                                 const char (*names)[MAX_NAME_LENGTH]) {
    FSIFile* msg = (FSIFile*)calloc(1, sizeof(FSIFile));
    if (!msg) {
        fr_strcpy(job->result, sizeof(job->result), "Out of memory for the instrument file");
        return false;
    }
    snprintf(msg->instrument_name, sizeof(msg->instrument_name), "%s %s", job->cfg.instrument, job->cfg.articulation);
    fr_strcpy(msg->fsb_path, sizeof(msg->fsb_path), plat_path_basename(job->fsb_path));

    // One collection per round robin, each over a contiguous run of zones.
    msg->collections_count = 0;
    for (int rr = 1; rr <= rr_count; ++rr) {
        int first = -1, count = 0;
        for (int z = 0; z < zone_count; ++z) {
            if (map[z].round_robin != rr) continue;
            if (first < 0) first = z;
            count++;
        }
        if (count == 0) continue;
        FSICollection* col = &msg->collections[msg->collections_count++];
        snprintf(col->name, sizeof(col->name), "RR%d", rr);
        col->zone_count = (uint32_t)count;
        col->modulator_count = 0;
        col->first_zone_index = (uint32_t)first;
        col->has_flags = true;      col->flags = 0;
        col->has_stable_id = true;  col->stable_id = (uint32_t)rr;
        col->has_output_bus = true; col->output_bus = 0;
    }

    // With more than one round robin, every collection gets FastSampler's
    // round robin effect on one shared instrument counter: each note advances
    // the counter, and collection k sounds when it stands at k. That is how
    // FastSampler links round robin collections; no link rows are involved.
    msg->collection_fx_count = 0;
    if (msg->collections_count > 1) {
        for (pb_size_t ci = 0; ci < msg->collections_count; ++ci) {
            FSICollectionFX* fx = &msg->collection_fx[msg->collection_fx_count++];
            memset(fx, 0, sizeof(*fx));
            fx->type = (uint32_t)COLLECTION_FX_ROUND_ROBIN;
            fx->collection_index = (uint32_t)ci;
            fx->params_count = CFX_RR_MODE + 1;
            fx->params[CFX_STRENGTH] = 1.0f;
            fx->params[CFX_RR_START] = 1.0f;
            fx->params[CFX_RR_END] = (float)msg->collections_count;
            fx->params[CFX_RR_ACTIVATE_STEP] = (float)(ci + 1);
            fx->params[CFX_RR_GLOBAL_COUNTER] = 1.0f;
            fx->params[CFX_RR_MODE] = 0.0f;   // sequence
        }
    }

    msg->zones_count = (pb_size_t)zone_count;
    for (int z = 0; z < zone_count; ++z) {
        const ResampleSegment* seg = &job->script->segments[map[z].segment];
        uint32_t frames = (uint32_t)(seg->end_frame - seg->start_frame);
        instrument_fill_zone(&msg->zones[z], names[z], &map[z], frames);
    }

    FILE* f = fopen(job->fsi_path, "wb");
    if (!f) {
        snprintf(job->result, sizeof(job->result), "Could not create %s", job->fsi_path);
        free(msg);
        return false;
    }
    uint32_t magic = FSI_MAGIC_PB;
    uint32_t size_placeholder = 0;
    bool ok = fwrite(&magic, sizeof(magic), 1, f) == 1 && fwrite(&size_placeholder, sizeof(size_placeholder), 1, f) == 1;

    pb_ostream_t stream = {};
    stream.callback = instrument_pb_write_callback;
    stream.state = f;
    stream.max_size = SIZE_MAX;
    ok = ok && pb_encode(&stream, FSIFile_fields, msg);
    if (!ok) {
        snprintf(job->result, sizeof(job->result), "Encoding the instrument failed: %s", PB_GET_ERROR(&stream));
    } else {
        uint32_t payload_size = (uint32_t)stream.bytes_written;
        ok = fseek(f, sizeof(uint32_t), SEEK_SET) == 0 && fwrite(&payload_size, sizeof(payload_size), 1, f) == 1;
        if (!ok) snprintf(job->result, sizeof(job->result), "Could not finish %s", job->fsi_path);
    }
    fclose(f);
    free(msg);
    return ok;
}

// --- The worker ------------------------------------------------------------------

static void instrument_thread_main(void* user) {
    InstrumentJob* job = (InstrumentJob*)user;
    const ResampleScript* script = job->script;

    InstrumentZoneMap* map = (InstrumentZoneMap*)calloc((size_t)script->segment_count, sizeof(InstrumentZoneMap));
    FSBZoneEntry* rows = (FSBZoneEntry*)calloc((size_t)script->segment_count, sizeof(FSBZoneEntry));
    char (*names)[MAX_NAME_LENGTH] = (char (*)[MAX_NAME_LENGTH])calloc((size_t)script->segment_count, MAX_NAME_LENGTH);
    WavReader wav = {};
    bool ok = map && rows && names;
    if (!ok) fr_strcpy(job->result, sizeof(job->result), "Out of memory for the mapping");

    int rr_count = 0;
    int zone_count = 0;
    if (ok) {
        zone_count = instrument_map(script, map, &rr_count);
        for (int z = 0; z < zone_count; ++z) {
            instrument_zone_name(job->prefix, &script->segments[map[z].segment], names[z], MAX_NAME_LENGTH);
        }
        ok = zone_count > 0;
        if (!ok) fr_strcpy(job->result, sizeof(job->result), "The run holds no samples to map");
    }
    if (ok) {
        ok = wav_reader_open(&wav, job->full_wav_path);
        if (!ok) snprintf(job->result, sizeof(job->result), "Could not read %s", plat_path_basename(job->full_wav_path));
    }
    if (ok) ok = instrument_write_fsb(job, &wav, map, zone_count, rows, names);
    if (ok && !job->cancel.load()) ok = instrument_write_fsi(job, map, zone_count, rr_count, names);
    wav_reader_close(&wav);

    if (ok && job->cancel.load()) {
        fr_strcpy(job->result, sizeof(job->result), "Cancelled; the instrument files are incomplete.");
        job->result_is_error = true;
    } else if (ok) {
        // The bank now holds every sample the full WAV was rendered for, and
        // the reader is closed, so the file can go when the tab asked for it.
        if (job->cfg.delete_full_wav) {
            if (plat_file_delete(job->full_wav_path)) {
                job->full_wav_removed = true;
            } else {
                fr_log("instrument: could not remove %s", job->full_wav_path);
            }
        }
        char preload[32];
        resample_preload_label(job->cfg.preload_frames, preload, sizeof(preload));
        snprintf(job->result, sizeof(job->result), "Wrote %s and %s: %d zones in %d collection%s%s, preload %s.%s",
                 plat_path_basename(job->fsi_path), plat_path_basename(job->fsb_path), zone_count,
                 rr_count, rr_count == 1 ? "" : "s", rr_count > 1 ? " linked by a round robin effect" : "", preload,
                 job->full_wav_removed ? " The full WAV was removed." : "");
        job->result_is_error = false;
    } else {
        job->result_is_error = true;
    }
    free(map);
    free(rows);
    free(names);
    fr_log("instrument: %s", job->result);
    job->finished.store(true);
}

bool instrument_start(InstrumentJob* job, const ResampleScript* script, const ResampleConfig* cfg,
                      const char* full_wav_path, const char* out_dir, const char* prefix) {
    if (job->running.load()) return false;
    if (!script || !script->built || script->segment_count == 0) return false;
    job->script = script;
    job->cfg = *cfg;
    fr_strcpy(job->full_wav_path, sizeof(job->full_wav_path), full_wav_path);
    fr_strcpy(job->prefix, sizeof(job->prefix), prefix);
    char name[RESAMPLE_FILE_NAME_LEN];
    snprintf(name, sizeof(name), "%s.fsi", prefix);
    plat_path_join(job->fsi_path, sizeof(job->fsi_path), out_dir, name);
    snprintf(name, sizeof(name), "%s.fsb", prefix);
    plat_path_join(job->fsb_path, sizeof(job->fsb_path), out_dir, name);

    job->result[0] = '\0';
    job->result_is_error = false;
    job->full_wav_removed = false;
    job->cancel.store(false);
    job->finished.store(false);
    job->progress.store(0);
    job->running.store(true);
    if (!plat_thread_create(&job->thread, instrument_thread_main, job)) {
        job->running.store(false);
        return false;
    }
    fr_log("instrument: building %s from %d samples", job->fsi_path, script->segment_count);
    return true;
}

void instrument_cancel(InstrumentJob* job) {
    if (job->running.load()) job->cancel.store(true);
}

bool instrument_collect(InstrumentJob* job) {
    if (!job->running.load() || !job->finished.load()) return false;
    plat_thread_join(&job->thread);
    job->running.store(false);
    return true;
}
