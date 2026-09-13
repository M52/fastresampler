// Exercise the production instrument writer without loading a plugin.
#include "../src/fastresampler_win32.cpp"
#include <memory>
#include <vector>
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)
int main() {
    const char* folder = "build/format-fixtures";
    CreateDirectoryA(folder, nullptr);
    auto cfg = std::make_unique<ResampleConfig>();
    resample_config_defaults(cfg.get()); strcpy(cfg->instrument, "Synthetic"); strcpy(cfg->articulation, "Test"); cfg->bus_count = 1; strcpy(cfg->busses[0].name, "Main");
    strcpy(cfg->low_note, "C3"); strcpy(cfg->high_note, "D3"); cfg->step = 2;
    strcpy(cfg->velocities, "64, 127"); cfg->round_robins = 2;
    cfg->hold_seconds = 0.1f; cfg->gap_seconds = 0.05f;
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
        puts(job->result);
    }
    return 0;
}
