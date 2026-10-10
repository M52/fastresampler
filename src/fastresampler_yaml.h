#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

enum YamlNodeType {
    YAML_NONE = 0,
    YAML_SCALAR,
    YAML_MAPPING,
    YAML_SEQUENCE
};

struct YamlNode {
    YamlNodeType type;
    const char* key;
    const char* value;

    YamlNode* first_child;
    YamlNode* next_sibling;
};

struct YamlArena {
    uint8_t* base;
    size_t size;
    size_t offset;
};

inline void yaml_arena_init(YamlArena* arena, void* buffer, size_t size) {
    arena->base = (uint8_t*)buffer;
    arena->size = size;
    arena->offset = 0;
}

inline void* yaml_arena_alloc(YamlArena* arena, size_t bytes) {
    size_t aligned = (bytes + 7) & ~(size_t)7;
    if (arena->offset + aligned > arena->size) return NULL;
    void* ptr = arena->base + arena->offset;
    arena->offset += aligned;
    return ptr;
}

inline const char* yaml_arena_strdup(YamlArena* arena, const char* str, size_t len) {
    char* dst = (char*)yaml_arena_alloc(arena, len + 1);
    if (dst) {
        memcpy(dst, str, len);
        dst[len] = '\0';
    }
    return dst;
}

inline YamlNode* yaml_find_child(YamlNode* node, const char* key) {
    if (!node || node->type != YAML_MAPPING) return NULL;
    for (YamlNode* child = node->first_child; child; child = child->next_sibling) {
        if (child->key && strcmp(child->key, key) == 0) {
            return child;
        }
    }
    return NULL;
}

inline const char* yaml_get_string(YamlNode* node, const char* key, const char* default_val = "") {
    YamlNode* child = yaml_find_child(node, key);
    if (child && child->type == YAML_SCALAR && child->value) {
        return child->value;
    }
    return default_val;
}

inline int yaml_get_int(YamlNode* node, const char* key, int default_val = 0) {
    YamlNode* child = yaml_find_child(node, key);
    if (child && child->type == YAML_SCALAR && child->value) {
        return atoi(child->value);
    }
    return default_val;
}

inline float yaml_get_float(YamlNode* node, const char* key, float default_val = 0.0f) {
    YamlNode* child = yaml_find_child(node, key);
    if (child && child->type == YAML_SCALAR && child->value) {
        return (float)atof(child->value);
    }
    return default_val;
}

inline bool yaml_get_bool(YamlNode* node, const char* key, bool default_val = false) {
    YamlNode* child = yaml_find_child(node, key);
    if (child && child->type == YAML_SCALAR && child->value) {
        if (strcmp(child->value, "true") == 0 || strcmp(child->value, "1") == 0 || strcmp(child->value, "yes") == 0) {
            return true;
        }
        if (strcmp(child->value, "false") == 0 || strcmp(child->value, "0") == 0 || strcmp(child->value, "no") == 0) {
            return false;
        }
    }
    return default_val;
}

YamlNode* yaml_parse(const char* yaml_str, YamlArena* arena);

struct YamlFile {
    YamlNode* root;
    YamlArena arena;
    void*     heap;
};

inline void yaml_file_release(YamlFile* file) {
    if (file->heap) free(file->heap);
    memset(file, 0, sizeof(*file));
}

#define YAML_ARENA_SLACK (2 * 1024 * 1024)

#define YAML_FILE_MAX_BYTES (16 * 1024 * 1024)

inline bool yaml_load_file(const char* path, YamlFile* out) {
    memset(out, 0, sizeof(*out));

    FILE* f = fopen(path, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > YAML_FILE_MAX_BYTES) {
        fclose(f);
        return false;
    }

    size_t buf_size = (size_t)size + YAML_ARENA_SLACK;
    void* buf = malloc(buf_size);
    if (!buf) {
        fclose(f);
        return false;
    }
    out->heap = buf;

    yaml_arena_init(&out->arena, buf, buf_size);
    char* content = (char*)yaml_arena_alloc(&out->arena, (size_t)size + 1);
    if (content) {
        size_t read_bytes = fread(content, 1, (size_t)size, f);
        content[read_bytes] = '\0';
        out->root = yaml_parse(content, &out->arena);
    }
    fclose(f);

    if (!out->root) {
        yaml_file_release(out);
        return false;
    }
    return true;
}
