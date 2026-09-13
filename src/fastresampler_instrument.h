#pragma once
// Builds a FastSampler instrument from a finished resample run: one .fsb
// sound bank holding every sample, one .fsi that maps them, and, when the
// configuration asks, one .rzc per round robin -- FastSampler's readable
// zone collection, naming the sample files the split wrote.
//
// The public formats are pinned in the fastsampler-shared submodule (see
// FORMATS.md): fastsampler.pb.h for the .fsi message, fastsampler_fsb.h for
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
//
// A run with dynamic layers builds a crossfading sustain instrument instead.
// Every sample is mapped twice: the held note, from the note-on to the
// note-off, as a looped sustain in the collection of its layer (dyn1, dyn2,
// ...), and what rings out past the note-off as a release sample in a
// collection of its own (dyn1-release, ...), which FastSampler plays when
// the key comes up. Every zone ignores velocity for its level. Every
// collection carries FastSampler's gain effect, driven by one controller
// through a curve that puts its layer at 0 dB at its own part of the range
// and fades it to the floor either side, equal power against its
// neighbours, so the controller moves through the layers.

#include <stdint.h>
#include <atomic>
#include "fastresampler_constants.h"
#include "fastresampler_resample.h"

// One instrument to build: the full WAV of one output bus, and where its
// files go. A run with three microphones has three of these and the worker
// builds them one after the other, each into the folder of its own bus.
struct InstrumentTarget {
    char full_wav_path[FR_MAX_PATH];
    char out_dir[FR_MAX_PATH];         // where all of them go, the .rzc included
    char prefix[RESAMPLE_FILE_NAME_LEN];
    char bus_name[RESAMPLE_NAME_LEN];  // what a message calls this one
};

struct InstrumentJob {
    std::atomic<bool> running;
    std::atomic<bool> cancel;
    std::atomic<bool> finished;
    std::atomic<int>  progress;        // zones written to the bank, over every bus
    std::atomic<int>  progress_total;  // zones every bank will hold together
    std::atomic<int>  bus_done;        // instruments finished, so the tab can say "2 / 3"
    PlatThread        thread;

    const ResampleScript* script;
    ResampleConfig cfg;                // a copy, so the tab may change meanwhile
    InstrumentTarget targets[FR_MAX_OUTPUT_BUSSES];
    int  target_count;

    // The one being built: the worker's own writers read these, so a build
    // of one bus reads exactly what it did before the busses existed.
    int  target;
    char full_wav_path[FR_MAX_PATH];
    char fsi_path[FR_MAX_PATH];
    char fsb_path[FR_MAX_PATH];
    char out_dir[FR_MAX_PATH];
    char prefix[RESAMPLE_FILE_NAME_LEN];

    char result[FR_STATUS_LEN];
    bool result_is_error;
    bool full_wav_removed;             // the build deleted one, as the configuration asked
    int  rzc_written;                  // zone collection files written beside the banks
    int  zones_written;                // zones of every instrument together
    int  instruments_written;

    // What each bus's own build reported, in order. The worker cannot write
    // to the output log from its thread, so the UI thread reads these out
    // once the build is joined and puts one line in the ledger per bus.
    char bus_result[FR_MAX_OUTPUT_BUSSES][FR_STATUS_LEN];
    int  bus_result_count;
};

// Starts the worker over one target per output bus. The script is the one
// every full WAV was rendered from, so one mapping serves them all.
bool instrument_start(InstrumentJob* job, const ResampleScript* script, const ResampleConfig* cfg,
                      const InstrumentTarget* targets, int target_count);
void instrument_cancel(InstrumentJob* job);

// Joins the worker once finished is set. Returns true when a result is ready.
bool instrument_collect(InstrumentJob* job);
