#pragma once
// FastSampler's readable zone collection, the .rzc: a YAML list of zones,
// each with its properties and the path of the sample file it plays. One
// file holds one collection, so a resample run writes one per round robin.
//
// The format is FastSampler's, written there by rzc_save_file() in
// src/fileformats/fastsampler_rzc.cpp. That file cannot be compiled into
// this build: it is written against FastSampler's own YAML parser and its
// platform layer, both of which FastResampler deliberately forked, so the
// two collide on Yaml* and plat_* alike. The writer below therefore mirrors
// it, key for key, off the FSIZone records the .fsi is built from -- and
// those come from FastSampler's schema, compiled straight out of its
// checkout, so a field added to a zone arrives here on its own.
//
// What does not arrive on its own is the version and the key names. When
// FastSampler bumps RZC_VERSION, compare this file against that one.

#include "fastresampler_constants.h"
#include "fastsampler.pb.h"

// The version FastSampler's rzc_save_file() writes, and the oldest it reads.
// A file written at this version loads in any FastSampler that has it.
#define FR_RZC_VERSION 5

// One collection to one file. The zones and the sample paths are parallel
// arrays of zone_count entries; each path is the WAV that zone plays, and
// FastSampler opens it as it is written here, so it has to be absolute.
// False when the file could not be written.
bool rzc_write(const char* path, const FSIZone* zones,
               const char sample_paths[][FR_MAX_PATH], int zone_count);
