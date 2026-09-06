// #pragma once (removed for unity build)
#include "fastresampler_yaml.h"

// Deepest mapping/sequence nesting the parser follows; deeper lines attach to
// the last node that fit.
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

// True for the value part of "key: |" or "key: |-", the literal block scalar
// that carries the following indented lines as one string with line breaks.
static bool is_block_scalar(const char* value, size_t len) {
    if (len == 1 && value[0] == '|') return true;
    if (len == 2 && value[0] == '|' && value[1] == '-') return true;
    return false;
}

// Reads the body of a literal block scalar. `cursor` points at the line after
// the "key: |", and every following line indented deeper than `key_indent`
// belongs to the block, as does a blank line inside it. The deepest common
// indent is removed, the lines are joined with '\n', and trailing blank lines
// are dropped. Returns the joined string and leaves `cursor` on the first line
// that is not part of the block.
static const char* parse_block_scalar(char** cursor, int key_indent, YamlArena* arena) {
    char* p = *cursor;
    char* body_start = p;
    char* body_end = p;          // one past the last line kept
    int   block_indent = -1;

    while (p && *p) {
        char* line_end = p;
        while (*line_end && *line_end != '\n' && *line_end != '\r') line_end++;

        // Indent of this line, and whether it holds anything at all.
        int indent = 0;
        char* text = p;
        while (text < line_end && (*text == ' ' || *text == '\t')) {
            indent += (*text == ' ') ? 1 : 4;
            text++;
        }
        bool blank = (text == line_end);

        if (!blank && indent <= key_indent) break;
        // The first line with text sets the indent to strip, and starts the
        // body: blank lines before it are separation, not content.
        if (!blank && block_indent < 0) {
            block_indent = indent;
            body_start = p;
        }

        char* next = line_end;
        if (*next == '\r' && *(next + 1) == '\n') next += 2;
        else if (*next) next += 1;
        else next = line_end;    // last line, no terminator

        if (!blank) body_end = line_end;
        p = (next == line_end && !*next) ? NULL : next;
        *cursor = p;
    }

    if (block_indent < 0) return NULL;   // "key: |" with nothing under it

    // Second pass over the kept span, stripping the block indent per line.
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

    // Create root node
    YamlNode* root = (YamlNode*)yaml_arena_alloc(arena, sizeof(YamlNode));
    if (!root) return NULL;
    memset(root, 0, sizeof(YamlNode));
    root->type = YAML_MAPPING;

    YamlStackEntry stack[YAML_MAX_NESTING_DEPTH];
    int stack_depth = 1;
    stack[0].node = root;
    stack[0].indent = -1;

    // Make a mutable copy of the string to safely tokenise line-by-line
    size_t total_len = strlen(yaml_str);
    char* src = (char*)yaml_arena_alloc(arena, total_len + 1);
    if (!src) return NULL;
    memcpy(src, yaml_str, total_len + 1);

    char* line = src;
    char* next_line = NULL;

    while (line && *line != '\0') {
        // Find end of current line
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

        // Count leading spaces
        int indent = 0;
        char* start = line;
        while (*start == ' ' || *start == '\t') {
            if (*start == ' ') indent++;
            else indent += 4; // tab fallback
            start++;
        }

        // Skip empty lines
        if (*start == '\0') {
            line = next_line;
            continue;
        }

        // Check list item
        bool is_list_item = false;
        if (start[0] == '-' && (start[1] == ' ' || start[1] == '\t' || start[1] == '\0')) {
            is_list_item = true;
            start++;
            while (*start == ' ' || *start == '\t') start++;
        }

        // Pop stack based on indentation
        // If it's a list item, the '-' character defines the item's level, which matches the list's parent indent level.
        int target_indent = indent;
        while (stack_depth > 1 && stack[stack_depth - 1].indent >= target_indent) {
            stack_depth--;
        }

        YamlNode* parent = stack[stack_depth - 1].node;

        if (is_list_item) {
            // Ensure parent is SEQUENCE
            if (parent->type != YAML_SEQUENCE) {
                parent->type = YAML_SEQUENCE;
            }

            // The list item can contain a mapping (e.g. "- file: a") or a scalar (e.g. "- a")
            const char* colon = find_yaml_colon(start);
            if (colon) {
                // List item is a mapping
                YamlNode* item_node = (YamlNode*)yaml_arena_alloc(arena, sizeof(YamlNode));
                if (!item_node) return NULL;
                memset(item_node, 0, sizeof(YamlNode));
                item_node->type = YAML_MAPPING;
                add_child(parent, item_node);

                // Parse the mapping inside this list item
                size_t k_len;
                const char* k_str = strip_quotes_and_spaces(start, k_len);
                k_len = colon - k_str;
                // trim trailing spaces of key
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

                // Push the list item mapping node to stack so nested fields can be added under it
                if (stack_depth < YAML_MAX_NESTING_DEPTH) {
                    stack[stack_depth].node = item_node;
                    stack[stack_depth].indent = indent; // List item indent is same as '-' indent
                    stack_depth++;
                }

                // If it was just "key:" with no value, push the child node itself as a mapping block
                if (v_len == 0 && stack_depth < YAML_MAX_NESTING_DEPTH) {
                    child_node->type = YAML_MAPPING;
                    stack[stack_depth].node = child_node;
                    stack[stack_depth].indent = indent + 2; // Child expects more indent
                    stack_depth++;
                }
            } else {
                // List item is a scalar
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
            // Normal key-value mapping or block mapping
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

                // "key: |" takes the indented lines below it as one string with
                // the line breaks kept. The body is consumed here, so the outer
                // loop resumes after it and the node gets no children.
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

                // If it is a block mapping (no value), push it to stack
                if (v_len == 0) {
                    child_node->type = YAML_MAPPING; // default
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
