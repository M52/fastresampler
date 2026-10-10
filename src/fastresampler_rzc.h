#pragma once

#include "fastresampler_constants.h"
#include "fastsampler.pb.h"

#define FR_RZC_VERSION 5

bool rzc_write(const char* path, const FSIZone* zones,
               const char sample_paths[][FR_MAX_PATH], int zone_count);
