#pragma once
// The container a plugin's own state is kept in. A VST3 plugin writes two
// streams (the component's and the controller's), a VST 2 plugin one chunk
// or a list of parameter values, so the block holds a header and any number
// of parts, each with a kind and a length. The block is this program's own;
// what is inside every part is the plugin's and is never read here.
//
// Everything is little endian, the one byte order this program is built for.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FR_STATE_MAGIC       0x53505246u   // "FRPS", read as a number
#define FR_STATE_VERSION     1
#define FR_STATE_HEADER_SIZE 24
#define FR_STATE_PART_HEADER 8

// The kinds of part. A block holds the ones its format wrote.
enum StatePartKind {
    STATE_PART_VST3_COMPONENT = 1,   // IComponent::getState
    STATE_PART_VST3_CONTROLLER = 2,  // IEditController::getState
    STATE_PART_VST2_CHUNK = 3,       // effGetChunk of the whole bank
    STATE_PART_VST2_PARAMS = 4,      // one float per parameter, in order
    STATE_PART_VST2_PROGRAM = 5,     // the program number, one int32
    // The class the state was written for, the 16 bytes of the VST3 TUID.
    // A VST 2 plugin says the same thing with the unique id in the header.
    // A block from before this part existed holds none and is taken as is.
    STATE_PART_VST3_CLASS_UID = 6
};

#define FR_STATE_CLASS_UID_SIZE 16

// A block being written: the bytes grow as parts are added.
struct StateBuffer {
    uint8_t* data;
    size_t   size;
    size_t   capacity;
};

inline void state_buffer_init(StateBuffer* b) {
    b->data = NULL;
    b->size = 0;
    b->capacity = 0;
}

inline void state_buffer_release(StateBuffer* b) {
    free(b->data);
    state_buffer_init(b);
}

// Room for that many more bytes. False when the memory is not there; then
// the buffer keeps what it had.
inline bool state_buffer_reserve(StateBuffer* b, size_t extra) {
    if (b->size + extra <= b->capacity) return true;
    size_t want = b->capacity ? b->capacity * 2 : 4096;
    while (want < b->size + extra) want *= 2;
    uint8_t* grown = (uint8_t*)realloc(b->data, want);
    if (!grown) return false;
    b->data = grown;
    b->capacity = want;
    return true;
}

inline bool state_buffer_append(StateBuffer* b, const void* bytes, size_t count) {
    if (!state_buffer_reserve(b, count)) return false;
    if (count) memcpy(b->data + b->size, bytes, count);
    b->size += count;
    return true;
}

inline void state_write_u32(uint8_t* at, uint32_t value) {
    at[0] = (uint8_t)(value & 0xFF);
    at[1] = (uint8_t)((value >> 8) & 0xFF);
    at[2] = (uint8_t)((value >> 16) & 0xFF);
    at[3] = (uint8_t)((value >> 24) & 0xFF);
}

inline uint32_t state_read_u32(const uint8_t* at) {
    return (uint32_t)at[0] | ((uint32_t)at[1] << 8) | ((uint32_t)at[2] << 16) | ((uint32_t)at[3] << 24);
}

// Starts a block. format is a HostPluginFormat and unique_id the VST 2
// plugin's own id, 0 for a VST3: both are checked before a block is put
// back, so a state never reaches the wrong plugin.
inline bool state_begin(StateBuffer* b, int format, uint32_t unique_id) {
    state_buffer_release(b);
    if (!state_buffer_reserve(b, FR_STATE_HEADER_SIZE)) return false;
    uint8_t* h = b->data;
    memset(h, 0, FR_STATE_HEADER_SIZE);
    state_write_u32(h + 0, FR_STATE_MAGIC);
    state_write_u32(h + 4, FR_STATE_VERSION);
    state_write_u32(h + 8, (uint32_t)format);
    state_write_u32(h + 12, unique_id);
    state_write_u32(h + 16, 0);   // the parts, counted up as they are added
    state_write_u32(h + 20, 0);   // reserved
    b->size = FR_STATE_HEADER_SIZE;
    return true;
}

// One part onto the end. An empty one is left out, so a plugin that wrote
// nothing does not add a part that says so.
inline bool state_add_part(StateBuffer* b, int kind, const void* bytes, size_t count) {
    if (b->size < FR_STATE_HEADER_SIZE || count == 0) return false;
    uint8_t header[FR_STATE_PART_HEADER];
    state_write_u32(header + 0, (uint32_t)kind);
    state_write_u32(header + 4, (uint32_t)count);
    if (!state_buffer_append(b, header, sizeof(header))) return false;
    if (!state_buffer_append(b, bytes, count)) {
        b->size -= sizeof(header);
        return false;
    }
    state_write_u32(b->data + 16, state_read_u32(b->data + 16) + 1);
    return true;
}

inline uint32_t state_part_count(const StateBuffer* b) {
    if (!b->data || b->size < FR_STATE_HEADER_SIZE) return 0;
    return state_read_u32(b->data + 16);
}

// --- Reading -------------------------------------------------------------------

// The header of a block. False when the bytes are not one of ours.
inline bool state_read_header(const void* data, size_t size, int* format, uint32_t* unique_id) {
    const uint8_t* p = (const uint8_t*)data;
    if (!p || size < FR_STATE_HEADER_SIZE) return false;
    if (state_read_u32(p + 0) != FR_STATE_MAGIC) return false;
    if (state_read_u32(p + 4) > FR_STATE_VERSION) return false;
    if (format) *format = (int)state_read_u32(p + 8);
    if (unique_id) *unique_id = state_read_u32(p + 12);
    return true;
}

// The first part of that kind. False when the block does not hold one; a
// part that runs past the end of the block ends the walk.
inline bool state_find_part(const void* data, size_t size, int kind,
                            const uint8_t** out_bytes, size_t* out_size) {
    if (out_bytes) *out_bytes = NULL;
    if (out_size) *out_size = 0;
    if (!state_read_header(data, size, NULL, NULL)) return false;
    const uint8_t* p = (const uint8_t*)data;
    uint32_t count = state_read_u32(p + 16);
    size_t at = FR_STATE_HEADER_SIZE;
    for (uint32_t i = 0; i < count; ++i) {
        if (at + FR_STATE_PART_HEADER > size) return false;
        uint32_t part_kind = state_read_u32(p + at);
        uint32_t part_size = state_read_u32(p + at + 4);
        at += FR_STATE_PART_HEADER;
        if (part_size > size - at) return false;
        if ((int)part_kind == kind) {
            if (out_bytes) *out_bytes = p + at;
            if (out_size) *out_size = part_size;
            return true;
        }
        at += part_size;
    }
    return false;
}
