#pragma once

#include <stdint.h>
#include <atomic>
#include "fastsampler_provenance.h"
#include "fastresampler_constants.h"
#include "fastresampler_resample.h"

struct InstrumentTarget {
    char full_wav_path[FR_MAX_PATH];
    char out_dir[FR_MAX_PATH];
    char prefix[RESAMPLE_FILE_NAME_LEN];
    char bus_name[RESAMPLE_NAME_LEN];
};

struct InstrumentJob {
    std::atomic<bool> running;
    std::atomic<bool> cancel;
    std::atomic<bool> finished;
    std::atomic<int>  progress;
    std::atomic<int>  progress_total;
    int               progress_done;
    std::atomic<int>  bus_done;
    PlatThread        thread;

    const ResampleScript* script;
    ResampleConfig cfg;
    FSBProvenanceFields provenance{};
    InstrumentTarget targets[FR_MAX_OUTPUT_BUSSES];
    int  target_count;

    int  target;
    char full_wav_path[FR_MAX_PATH];
    char fsi_path[FR_MAX_PATH];
    char fsb_path[FR_MAX_PATH];
    char out_dir[FR_MAX_PATH];
    char prefix[RESAMPLE_FILE_NAME_LEN];

    char result[FR_STATUS_LEN];
    bool result_is_error;
    bool full_wav_removed;
    int  rzc_written;
    int  zones_written;
    int  instruments_written;

    char bus_result[FR_MAX_OUTPUT_BUSSES][FR_STATUS_LEN];
    int  bus_result_count;
};

bool instrument_start(InstrumentJob* job, const ResampleScript* script, const ResampleConfig* cfg,
                      const InstrumentTarget* targets, int target_count, const FSBProvenanceFields* provenance = nullptr);
void instrument_cancel(InstrumentJob* job);

bool instrument_collect(InstrumentJob* job);
