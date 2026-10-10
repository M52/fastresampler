#include "fastresampler_yaml.h"

#define YAML_MAX_NESTING_DEPTH 64

struct YamlStackEntry {
    YamlNode* node;
    int indent;
};

static void strip_yaml_comments(char* line) {
    bool in_double_quote = false;
    bool in_single_quote = false;
    for (char* p = line; *p; ++p) {
        if (*p == '"' && (p == line || *(p - 1) != '\\')) {
            in_double_quote = !in_double_quote;
        } else if (*p == '\'' && (p == line || *(p - 1) != '\\')) {
            in_single_quote = !in_single_quote;
        } else if (*p == '#' && !in_double_quote && !in_single_quote) {
            if (p == line || *(p - 1) == ' ' || *(p - 1) == '\t') {
                *p = '\0';
                break;
            }
        }
    }
}

static const char* strip_quotes_and_spaces(const char* str, size_t& out_len) {
    while (*str == ' ' || *str == '\t') {
        str++;
    }
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\t' || str[len - 1] == '\r' || str[len - 1] == '\n')) {
        len--;
    }
    if (len >= 2 && ((str[0] == '"' && str[len - 1] == '"') || (str[0] == '\'' && str[len - 1] == '\''))) {
        str++;
        len -= 2;
    }
    out_len = len;
    return str;
}

static const char* find_yaml_colon(const char* str) {
    bool in_double_quote = false;
    bool in_single_quote = false;
    for (const char* p = str; *p; ++p) {
        if (*p == '"' && (p == str || *(p - 1) != '\\')) {
            in_double_quote = !in_double_quote;
        } else if (*p == '\'' && (p == str || *(p - 1) != '\\')) {
            in_single_quote = !in_single_quote;
        } else if (*p == ':' && !in_double_quote && !in_single_quote) {
            if (*(p + 1) == ' ' || *(p + 1) == '\t' || *(p + 1) == '\0' || *(p + 1) == '\r' || *(p + 1) == '\n') {
                return p;
            }
        }
    }
    return NULL;
}

static bool is_block_scalar(const char* value, size_t len) {
    if (len == 1 && value[0] == '|') return true;
    if (len == 2 && value[0] == '|' && value[1] == '-') return true;
    return false;
}

static const char* parse_block_scalar(char** cursor, int key_indent, YamlArena* arena) {
    char* p = *cursor;
    char* body_start = p;
    char* body_end = p;
    int   block_indent = -1;

    while (p && *p) {
        char* line_end = p;
        while (*line_end && *line_end != '\n' && *line_end != '\r') line_end++;

        int indent = 0;
        char* text = p;
        while (text < line_end && (*text == ' ' || *text == '\t')) {
            indent += (*text == ' ') ? 1 : 4;
            text++;
        }
        bool blank = (text == line_end);

        if (!blank && indent <= key_indent) break;
        if (!blank && block_indent < 0) {
            block_indent = indent;
            body_start = p;
        }

        char* next = line_end;
        if (*next == '\r' && *(next + 1) == '\n') next += 2;
        else if (*next) next += 1;
        else next = line_end;

        if (!blank) body_end = line_end;
        p = (next == line_end && !*next) ? NULL : next;
        *cursor = p;
    }

    if (block_indent < 0) return NULL;

    size_t span = (size_t)(body_end - body_start);
    char* out = (char*)yaml_arena_alloc(arena, span + 1);
    if (!out) return NULL;

    size_t written = 0;
    for (char* line = body_start; line < body_end; ) {
        char* line_end = line;
        while (line_end < body_end && *line_end != '\n' && *line_end != '\r') line_end++;

        char* text = line;
        int skipped = 0;
        while (text < line_end && skipped < block_indent && (*text == ' ' || *text == '\t')) {
            skipped += (*text == ' ') ? 1 : 4;
            text++;
        }
        if (written > 0) out[written++] = '\n';
        size_t len = (size_t)(line_end - text);
        memcpy(out + written, text, len);
        written += len;

        line = line_end;
        if (line < body_end && *line == '\r' && *(line + 1) == '\n') line += 2;
        else if (line < body_end) line += 1;
    }
    out[written] = '\0';
    return out;
}

static void add_child(YamlNode* parent, YamlNode* child) {
    if (!parent->first_child) {
        parent->first_child = child;
    } else {
        YamlNode* curr = parent->first_child;
        while (curr->next_sibling) {
            curr = curr->next_sibling;
        }
        curr->next_sibling = child;
    }
}

YamlNode* yaml_parse(const char* yaml_str, YamlArena* arena) {
    if (!yaml_str || !arena) return NULL;

    YamlNode* root = (YamlNode*)yaml_arena_alloc(arena, sizeof(YamlNode));
    if (!root) return NULL;
    memset(root, 0, sizeof(YamlNode));
    root->type = YAML_MAPPING;

    YamlStackEntry stack[YAML_MAX_NESTING_DEPTH];
    int stack_depth = 1;
    stack[0].node = root;
    stack[0].indent = -1;

    size_t total_len = strlen(yaml_str);
    char* src = (char*)yaml_arena_alloc(arena, total_len + 1);
    if (!src) return NULL;
    memcpy(src, yaml_str, total_len + 1);

    char* line = src;
    char* next_line = NULL;

    while (line && *line != '\0') {
        char* p = line;
        while (*p && *p != '\n' && *p != '\r') p++;
        if (*p) {
            if (*p == '\r' && *(p + 1) == '\n') {
                *p = '\0';
                next_line = p + 2;
            } else {
                *p = '\0';
                next_line = p + 1;
            }
        } else {
            next_line = NULL;
        }

        strip_yaml_comments(line);

        int indent = 0;
        char* start = line;
        while (*start == ' ' || *start == '\t') {
            if (*start == ' ') indent++;
            else indent += 4;
            start++;
        }

        if (*start == '\0') {
            line = next_line;
            continue;
        }

        bool is_list_item = false;
        if (start[0] == '-' && (start[1] == ' ' || start[1] == '\t' || start[1] == '\0')) {
            is_list_item = true;
            start++;
            while (*start == ' ' || *start == '\t') start++;
        }

        int target_indent = indent;
        while (stack_depth > 1 && stack[stack_depth - 1].indent >= target_indent) {
            stack_depth--;
        }

        YamlNode* parent = stack[stack_depth - 1].node;

        if (is_list_item) {
            if (parent->type != YAML_SEQUENCE) {
                parent->type = YAML_SEQUENCE;
            }

            const char* colon = find_yaml_colon(start);
            if (colon) {
                YamlNode* item_node = (YamlNode*)yaml_arena_alloc(arena, sizeof(YamlNode));
                if (!item_node) return NULL;
                memset(item_node, 0, sizeof(YamlNode));
                item_node->type = YAML_MAPPING;
                add_child(parent, item_node);

                size_t k_len;
                const char* k_str = strip_quotes_and_spaces(start, k_len);
                k_len = colon - k_str;
                while (k_len > 0 && (k_str[k_len - 1] == ' ' || k_str[k_len - 1] == '\t')) {
                    k_len--;
                }

                YamlNode* child_node = (YamlNode*)yaml_arena_alloc(arena, sizeof(YamlNode));
                if (!child_node) return NULL;
                memset(child_node, 0, sizeof(YamlNode));
                child_node->key = yaml_arena_strdup(arena, k_str, k_len);

                const char* val_part = colon + 1;
                size_t v_len;
                const char* v_str = strip_quotes_and_spaces(val_part, v_len);
                if (v_len > 0) {
                    child_node->type = YAML_SCALAR;
                    child_node->value = yaml_arena_strdup(arena, v_str, v_len);
                } else {
                    child_node->type = YAML_NONE;
                }

                add_child(item_node, child_node);

                if (stack_depth < YAML_MAX_NESTING_DEPTH) {
                    stack[stack_depth].node = item_node;
                    stack[stack_depth].indent = indent;
                    stack_depth++;
                }

                if (v_len == 0 && stack_depth < YAML_MAX_NESTING_DEPTH) {
                    child_node->type = YAML_MAPPING;
                    stack[stack_depth].node = child_node;
                    stack[stack_depth].indent = indent + 2;
                    stack_depth++;
                }
            } else {
                size_t v_len;
                const char* v_str = strip_quotes_and_spaces(start, v_len);

                YamlNode* child_node = (YamlNode*)yaml_arena_alloc(arena, sizeof(YamlNode));
                if (!child_node) return NULL;
                memset(child_node, 0, sizeof(YamlNode));
                child_node->type = YAML_SCALAR;
                child_node->value = yaml_arena_strdup(arena, v_str, v_len);

                add_child(parent, child_node);
            }
        } else {
            const char* colon = find_yaml_colon(start);
            if (colon) {
                if (parent->type != YAML_MAPPING) {
                    parent->type = YAML_MAPPING;
                }

                size_t k_len;
                const char* k_str = strip_quotes_and_spaces(start, k_len);
                k_len = colon - k_str;
                while (k_len > 0 && (k_str[k_len - 1] == ' ' || k_str[k_len - 1] == '\t')) {
                    k_len--;
                }

                YamlNode* child_node = (YamlNode*)yaml_arena_alloc(arena, sizeof(YamlNode));
                if (!child_node) return NULL;
                memset(child_node, 0, sizeof(YamlNode));
                child_node->key = yaml_arena_strdup(arena, k_str, k_len);

                const char* val_part = colon + 1;
                size_t v_len;
                const char* v_str = strip_quotes_and_spaces(val_part, v_len);

                if (is_block_scalar(v_str, v_len)) {
                    const char* block = parse_block_scalar(&next_line, indent, arena);
                    child_node->type = YAML_SCALAR;
                    child_node->value = block ? block : "";
                    add_child(parent, child_node);
                    line = next_line;
                    continue;
                }

                if (v_len > 0) {
                    child_node->type = YAML_SCALAR;
                    child_node->value = yaml_arena_strdup(arena, v_str, v_len);
                } else {
                    child_node->type = YAML_NONE;
                }

                add_child(parent, child_node);

                if (v_len == 0) {
                    child_node->type = YAML_MAPPING;
                    if (stack_depth < YAML_MAX_NESTING_DEPTH) {
                        stack[stack_depth].node = child_node;
                        stack[stack_depth].indent = indent;
                        stack_depth++;
                    }
                }
            }
        }

        line = next_line;
    }

    return root;
}
