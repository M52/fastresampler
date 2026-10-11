#include "../src/fastresampler_win32.cpp"
#include "fastsampler_fsb.cpp"
#pragma warning(push, 0)
#include "pb_decode.h"
#include "pb_decode.c"
#pragma warning(pop)
#include <memory>
#include <vector>
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

static const FSICollectionFX* find_fx(const FSIFile* msg, uint32_t collection, uint32_t type) {
    for (pb_size_t i = 0; i < msg->collection_fx_count; ++i) {
        if (msg->collection_fx[i].collection_index == collection && msg->collection_fx[i].type == type) return &msg->collection_fx[i];
    }
    return nullptr;
}

static bool write_layered_input(const ResampleScript* script, const char* path) {
    std::vector<float> pcm((size_t)script->total_frames * 2, 0.0f);
    for (int s = 0; s < script->segment_count; ++s) {
        const ResampleSegment* seg = &script->segments[s];
        float f0 = 440.0f * powf(2.0f, ((float)seg->note - 69.0f) / 12.0f);
        float slope = 3.0f - (float)seg->round_robin;
        for (int64_t f = seg->start_frame; f < seg->end_frame; ++f) {
            float t = (float)(f - seg->start_frame) / 44100.0f;
            float env = f < seg->off_frame ? 1.0f : exp(-(float)(f - seg->off_frame) / (0.1f * 44100.0f));
            float v = 0.0f;
            for (int h = 1; h <= 16 && f0 * (float)h < 20000.0f; ++h) {
                v += powf(1.0f / (float)h, slope) * sinf(6.28318530718f * f0 * (float)h * t);
            }
            pcm[(size_t)f * 2] = pcm[(size_t)f * 2 + 1] = 0.05f * env * v;
        }
    }
    WavWriter writer = {};
    return wav_writer_open(&writer, path, 2, 44100) &&
           wav_writer_write_interleaved(&writer, pcm.data(), (int)script->total_frames) &&
           wav_writer_close(&writer);
}

int main() {
    const char* folder = "build/format-fixtures";
    CreateDirectoryA(folder, nullptr);
    auto settings = std::make_unique<Settings>(); settings_defaults(settings.get());
    settings->disclaimer_accepted = settings->disclaimer_hidden = true;
    REQUIRE(settings_save(settings.get(), "build/format-fixtures/old-settings.txt"));
    REQUIRE(settings_load(settings.get(), "build/format-fixtures/old-settings.txt"));
    REQUIRE(!settings->disclaimer_accepted && !settings->disclaimer_hidden);
    strcpy(settings->disclaimer_revision, FR_DISCLAIMER_REVISION);
    settings->disclaimer_accepted = settings->disclaimer_hidden = true;
    REQUIRE(settings_save(settings.get(), "build/format-fixtures/new-settings.txt"));
    REQUIRE(settings_load(settings.get(), "build/format-fixtures/new-settings.txt"));
    REQUIRE(settings->disclaimer_accepted && settings->disclaimer_hidden);
    auto cfg = std::make_unique<ResampleConfig>();
    resample_config_defaults(cfg.get());
    strcpy(cfg->instrument, "Synthetic"); strcpy(cfg->articulation, "Test"); cfg->bus_count = 1; strcpy(cfg->busses[0].name, "Main");
    strcpy(cfg->low_note, "C3"); strcpy(cfg->high_note, "D3"); cfg->step = 2;
    strcpy(cfg->velocities, "64, 127"); cfg->round_robins = 2;
    cfg->hold_seconds = 0.3f; cfg->gap_seconds = 0.05f;
    cfg->preroll = false; cfg->legato = false; cfg->dynamic_layers = false;
    cfg->delete_full_wav = false; cfg->split_samples = false; cfg->export_rzc = false;
    cfg->bit_depth = 24;
    auto script = std::make_unique<ResampleScript>();
    char error[512] = {};
    if (!resample_build(script.get(), cfg.get(), 44100, error, sizeof(error))) { fprintf(stderr, "%s\n", error); return 1; }
    WavWriter writer = {};
    REQUIRE(wav_writer_open(&writer, "build/format-fixtures/input.wav", 2, 44100));
    std::vector<float> pcm((size_t)script->total_frames * 2);
    for (size_t f = 0; f < pcm.size() / 2; ++f) {
        pcm[f * 2] = 0.25f * sinf(6.28318530718f * 440.0f * (float)f / 44100.0f);
        pcm[f * 2 + 1] = pcm[f * 2];
    }
    REQUIRE(wav_writer_write_interleaved(&writer, pcm.data(), (int)script->total_frames));
    REQUIRE(wav_writer_close(&writer));
    for (int preload : {0, 1024}) {
        auto job = std::make_unique<InstrumentJob>();
        job->cfg = *cfg; job->cfg.preload_frames = preload;
        job->script = script.get(); job->target_count = 1;
        strcpy(job->full_wav_path, "build/format-fixtures/input.wav");
        strcpy(job->out_dir, folder);
        snprintf(job->prefix, sizeof(job->prefix), "resampler-%d", preload);
        snprintf(job->fsi_path, sizeof(job->fsi_path), "%s/%s.fsi", folder, job->prefix);
        snprintf(job->fsb_path, sizeof(job->fsb_path), "%s/%s.fsb", folder, job->prefix);
        REQUIRE(instrument_build_one(job.get()));
        REQUIRE(job->zones_written == script->segment_count);
        FILE* bank_file = fopen(job->fsb_path, "rb"); REQUIRE(bank_file);
        REQUIRE(fseek(bank_file, 0, SEEK_END) == 0); long bank_size = ftell(bank_file); REQUIRE(bank_size > 0);
        std::vector<uint8_t> bank((size_t)bank_size); REQUIRE(fseek(bank_file, 0, SEEK_SET) == 0);
        REQUIRE(fread(bank.data(), 1, bank.size(), bank_file) == bank.size()); fclose(bank_file);
        FSBHeader header = {}; FSBProvenanceView origins = {};
        std::vector<FSBZoneEntry> rows((size_t)job->zones_written);
        REQUIRE(fsb_load_headers(bank.data(), bank.size(), &header, rows.data(), (uint32_t)rows.size(), (uint32_t)rows.size()));
        REQUIRE(fsb_read_provenance(bank.data(), bank.size(), &origins) && origins.count == 1);
        REQUIRE(strcmp(origins.records[0].key_id, "fsb-provenance-2026-01") == 0 && header.version == 3);

        puts(job->result);
    }

    const int layers = 3;
    auto dyn_cfg = std::make_unique<ResampleConfig>(*cfg);
    dyn_cfg->dynamic_layers = true; dyn_cfg->round_robins = layers; strcpy(dyn_cfg->velocities, "100");
    dyn_cfg->hold_seconds = 0.6f; dyn_cfg->gap_seconds = 0.4f;
    auto layered = std::make_unique<ResampleScript>();
    if (!resample_build(layered.get(), dyn_cfg.get(), 44100, error, sizeof(error))) { fprintf(stderr, "%s\n", error); return 1; }
    REQUIRE(layered->dynamic_layers && layered->round_robin_count == layers);
    REQUIRE(write_layered_input(layered.get(), "build/format-fixtures/layers.wav"));
    static SMFPZoneEntry entries[64];
    for (int preload : {0, 1024}) {
        auto job = std::make_unique<InstrumentJob>();
        job->cfg = *dyn_cfg; job->cfg.preload_frames = preload;
        job->script = layered.get(); job->target_count = 1;
        strcpy(job->full_wav_path, "build/format-fixtures/layers.wav");
        strcpy(job->out_dir, folder);
        snprintf(job->prefix, sizeof(job->prefix), "resampler-layers-%d", preload);
        snprintf(job->fsi_path, sizeof(job->fsi_path), "%s/%s.fsi", folder, job->prefix);
        snprintf(job->fsb_path, sizeof(job->fsb_path), "%s/%s.fsb", folder, job->prefix);
        REQUIRE(instrument_build_one(job.get()));
        REQUIRE(job->zones_written == layered->segment_count * 2);
        REQUIRE(job->progress.load() == job->progress_total.load());
        puts(job->result);

        FILE* f = fopen(job->fsi_path, "rb");
        REQUIRE(f);
        std::vector<uint8_t> bytes;
        uint8_t chunk[65536];
        for (size_t n; (n = fread(chunk, 1, sizeof(chunk), f)) > 0;) bytes.insert(bytes.end(), chunk, chunk + n);
        fclose(f);
        REQUIRE(bytes.size() > 8);
        uint32_t magic = 0;
        memcpy(&magic, bytes.data(), 4);
        REQUIRE(magic == FSI_MAGIC_PB);
        FSIFile* msg = (FSIFile*)calloc(1, sizeof(FSIFile));
        REQUIRE(msg);
        pb_istream_t in = pb_istream_from_buffer(bytes.data() + 8, bytes.size() - 8);
        REQUIRE(pb_decode(&in, FSIFile_fields, msg));

        REQUIRE(msg->collections_count == (pb_size_t)(layers * 2));
        REQUIRE(msg->smf_fingerprints_count == msg->collections_count);
        for (pb_size_t c = 0; c < msg->collections_count; ++c) {
            const FSICollection* col = &msg->collections[c];
            bool release = strstr(col->name, "-release") != nullptr;
            int layer = atoi(col->name + 3);
            REQUIRE(layer >= 1 && layer <= layers);

            const FSICollectionFX* gain = find_fx(msg, c, COLLECTION_FX_GAIN);
            const FSICollectionFX* smart = find_fx(msg, c, COLLECTION_FX_SMART_RELEASE);
            const FSICollectionFX* morph = find_fx(msg, c, COLLECTION_FX_SPECTRAL_MORPH);
            REQUIRE(morph);
            if (release) {
                REQUIRE(!gain && smart);
                REQUIRE(smart->params[CFX_SREL_LEVEL_ON] == 1.0f && smart->params[CFX_SREL_LEVEL_DB] == SREL_MAX_MATCH_DB);
                REQUIRE(smart->params[CFX_SREL_ATTACK_ON] == 1.0f && smart->drivers_count == 0);
            } else {
                REQUIRE(gain && !smart);
                REQUIRE(gain->drivers_count == 1 && gain->drivers[0].out_min == -50.0f && gain->drivers[0].out_max == 0.0f);
            }
            REQUIRE(morph->params[CFX_SMF_SELF_POS] == (float)layer / (float)layers);
            REQUIRE(morph->drivers_count == 1 && morph->drivers[0].target_param_idx == CFX_SMF_MORPH);
            REQUIRE(morph->drivers[0].source_type == FX_DRIVER_SOURCE_CC && !morph->drivers[0].use_curve);
            REQUIRE(morph->drivers[0].out_min == 0.0f && morph->drivers[0].out_max == 1.0f);

            const FSISMFFingerprints* fp = nullptr;
            for (pb_size_t i = 0; i < msg->smf_fingerprints_count; ++i) {
                if (msg->smf_fingerprints[i].collection_index == c) fp = &msg->smf_fingerprints[i];
            }
            REQUIRE(fp && fp->target_paths_count == (pb_size_t)(layers - 1) && fp->target_positions_count == fp->target_paths_count);
            for (pb_size_t t = 0; t < fp->target_paths_count; ++t) {
                char expected[128];
                int target_layer = (int)(fp->target_positions[t] * (float)layers + 0.5f);
                REQUIRE(target_layer != layer);
                snprintf(expected, sizeof(expected), "%s_dyn%d%s.smfp", job->prefix, target_layer, release ? "-release" : "");
                REQUIRE(strcmp(plat_path_basename(fp->target_paths[t]), expected) == 0);
                REQUIRE(fp->target_paths[t][1] == ':');
                SMFPHeader header = {};
                uint32_t count = 0;
                REQUIRE(smfp_load_file(fp->target_paths[t], &header, entries, 64, &count));
                REQUIRE(count == col->zone_count && header.band_count == SMFP_BAND_COUNT);
                for (uint32_t e = 0; e < count; ++e) REQUIRE(entries[e].analysis_frames > 0);
            }
        }

        const int high_band = SMFP_REF_BAND + 2;
        float below = 0.0f;
        for (int k = 1; k <= layers; ++k) {
            char path[FR_MAX_PATH];
            snprintf(path, sizeof(path), "%s/%s_dyn%d.smfp", folder, job->prefix, k);
            SMFPHeader header = {};
            uint32_t count = 0;
            REQUIRE(smfp_load_file(path, &header, entries, 64, &count) && count > 0);
            float level = entries[0].band_db[high_band];
            printf("%s: %.1f dB at %.0f Hz\n", header.collection_name, level, smfp_band_center_hz(high_band));
            if (k > 1) REQUIRE(level > below + 6.0f);
            below = level;
        }
        free(msg);
    }
    return 0;
}
