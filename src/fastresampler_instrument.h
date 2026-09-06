#pragma once
// Builds a FastSampler instrument from a finished resample run: one .fsb
// sound bank holding every sample, and one .fsi that maps them.
//
// The formats are FastSampler's and are compiled from its checkout (see
// build.bat): fastsampler.pb.h for the .fsi message, fastsampler_fsb.h for
// the bank header and rows. Nothing about their layout is repeated here.
//
// The mapping: one zone collection per round robin, named RR1, RR2, ..., each
// carrying FastSampler's round robin effect on a shared counter so the
// collections take turns. Inside a collection the zones cover the sampled
// range without holes. Every
// key between two sampled notes goes to the nearer one, a key exactly in the
// middle to the upper one, so it plays pitched down. Velocities split the
// same way: each sampled velocity takes the range from the one below it up to
// itself, the lowest reaching down to 0 and the highest up to 127.

#include <stdint.h>
#include <atomic>
#include "fastresampler_constants.h"
#include "fastresampler_resample.h"

struct InstrumentJob {
    std::atomic<bool> running;
    std::atomic<bool> cancel;
    std::atomic<bool> finished;
    std::atomic<int>  progress;        // zones written to the bank
    PlatThread        thread;

    const ResampleScript* script;
    ResampleConfig cfg;                // a copy, so the tab may change meanwhile
    char full_wav_path[FR_MAX_PATH];
    char fsi_path[FR_MAX_PATH];
    char fsb_path[FR_MAX_PATH];
    char prefix[RESAMPLE_FILE_NAME_LEN];

    char result[FR_STATUS_LEN];
    bool result_is_error;
    bool full_wav_removed;             // the build deleted it, as the configuration asked
};

// Starts the worker. The script is the one the full WAV was rendered from.
bool instrument_start(InstrumentJob* job, const ResampleScript* script, const ResampleConfig* cfg,
                      const char* full_wav_path, const char* out_dir, const char* prefix);
void instrument_cancel(InstrumentJob* job);

// Joins the worker once finished is set. Returns true when a result is ready.
bool instrument_collect(InstrumentJob* job);
