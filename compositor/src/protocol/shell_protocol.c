#include "lumo/shell_protocol.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool lumo_shell_protocol_is_token_char(char ch) {
    switch (ch) {
    case '\0':
    case ' ':
    case '\t':
    case '\n':
    case '\r':
    case '=':
        return false;
    default:
        return true;
    }
}

static bool lumo_shell_protocol_copy_token(
    char *buffer,
    size_t buffer_size,
    const char *value
) {
    size_t length;

    if (buffer == NULL || buffer_size == 0 || value == NULL || value[0] == '\0') {
        return false;
    }

    for (const char *cursor = value; *cursor != '\0'; cursor++) {
        if (!lumo_shell_protocol_is_token_char(*cursor)) {
            return false;
        }
    }

    length = strlen(value);
    if (length + 1 > buffer_size) {
        return false;
    }

    memcpy(buffer, value, length + 1);
    return true;
}

static bool lumo_shell_protocol_append(
    char *buffer,
    size_t buffer_size,
    size_t *used,
    const char *format,
    ...
) {
    va_list args;
    int written;

    if (buffer == NULL || used == NULL || format == NULL ||
            *used >= buffer_size) {
        return false;
    }

    va_start(args, format);
    written = vsnprintf(buffer + *used, buffer_size - *used, format, args);
    va_end(args);

    if (written < 0 || (size_t)written >= buffer_size - *used) {
        return false;
    }

    *used += (size_t)written;
    return true;
}

const char *lumo_shell_protocol_frame_kind_name(
    enum lumo_shell_protocol_frame_kind kind
) {
    switch (kind) {
    case LUMO_SHELL_PROTOCOL_FRAME_REQUEST:
        return "request";
    case LUMO_SHELL_PROTOCOL_FRAME_RESPONSE:
        return "response";
    case LUMO_SHELL_PROTOCOL_FRAME_ERROR:
        return "error";
    case LUMO_SHELL_PROTOCOL_FRAME_EVENT:
    default:
        return "event";
    }
}

bool lumo_shell_protocol_frame_kind_parse(
    const char *value,
    enum lumo_shell_protocol_frame_kind *kind
) {
    if (value == NULL || kind == NULL) {
        return false;
    }

    if (strcmp(value, "event") == 0) {
        *kind = LUMO_SHELL_PROTOCOL_FRAME_EVENT;
        return true;
    }
    if (strcmp(value, "request") == 0) {
        *kind = LUMO_SHELL_PROTOCOL_FRAME_REQUEST;
        return true;
    }
    if (strcmp(value, "response") == 0) {
        *kind = LUMO_SHELL_PROTOCOL_FRAME_RESPONSE;
        return true;
    }
    if (strcmp(value, "error") == 0) {
        *kind = LUMO_SHELL_PROTOCOL_FRAME_ERROR;
        return true;
    }

    return false;
}

bool lumo_shell_protocol_frame_init(
    struct lumo_shell_protocol_frame *frame,
    enum lumo_shell_protocol_frame_kind kind,
    const char *name,
    uint32_t id
) {
    if (frame == NULL || name == NULL) {
        return false;
    }

    switch (kind) {
    case LUMO_SHELL_PROTOCOL_FRAME_EVENT:
    case LUMO_SHELL_PROTOCOL_FRAME_REQUEST:
    case LUMO_SHELL_PROTOCOL_FRAME_RESPONSE:
    case LUMO_SHELL_PROTOCOL_FRAME_ERROR:
        break;
    default:
        return false;
    }

    memset(frame, 0, sizeof(*frame));
    frame->kind = kind;
    frame->id = id;
    return lumo_shell_protocol_copy_token(frame->name, sizeof(frame->name),
        name);
}

bool lumo_shell_protocol_frame_add_field(
    struct lumo_shell_protocol_frame *frame,
    const char *key,
    const char *value
) {
    struct lumo_shell_protocol_field *field;

    if (frame == NULL || key == NULL || value == NULL ||
            frame->field_count >= LUMO_SHELL_PROTOCOL_MAX_FIELDS) {
        return false;
    }

    field = &frame->fields[frame->field_count];
    if (!lumo_shell_protocol_copy_token(field->key, sizeof(field->key), key) ||
            !lumo_shell_protocol_copy_token(field->value, sizeof(field->value),
                value)) {
        return false;
    }

    frame->field_count++;
    return true;
}

bool lumo_shell_protocol_frame_add_bool(
    struct lumo_shell_protocol_frame *frame,
    const char *key,
    bool value
) {
    return lumo_shell_protocol_frame_add_field(frame, key, value ? "1" : "0");
}

bool lumo_shell_protocol_frame_add_u32(
    struct lumo_shell_protocol_frame *frame,
    const char *key,
    uint32_t value
) {
    char value_buffer[32];

    {
        int written = snprintf(value_buffer, sizeof(value_buffer), "%u", value);
        if (written < 0 || (size_t)written >= sizeof(value_buffer))
            return false;
    }

    return lumo_shell_protocol_frame_add_field(frame, key, value_buffer);
}

bool lumo_shell_protocol_frame_add_double(
    struct lumo_shell_protocol_frame *frame,
    const char *key,
    double value
) {
    char value_buffer[32];

    {
        int written = snprintf(value_buffer, sizeof(value_buffer), "%.2f", value);
        if (written < 0 || (size_t)written >= sizeof(value_buffer))
            return false;
    }

    return lumo_shell_protocol_frame_add_field(frame, key, value_buffer);
}

bool lumo_shell_protocol_frame_add_string(
    struct lumo_shell_protocol_frame *frame,
    const char *key,
    const char *value
) {
    return lumo_shell_protocol_frame_add_field(frame, key, value);
}

bool lumo_shell_protocol_frame_get(
    const struct lumo_shell_protocol_frame *frame,
    const char *key,
    const char **value
) {
    if (frame == NULL || key == NULL || value == NULL) {
        return false;
    }

    for (size_t i = 0; i < frame->field_count; i++) {
        if (strcmp(frame->fields[i].key, key) == 0) {
            *value = frame->fields[i].value;
            return true;
        }
    }

    return false;
}

bool lumo_shell_protocol_frame_get_bool(
    const struct lumo_shell_protocol_frame *frame,
    const char *key,
    bool *value
) {
    const char *field_value = NULL;

    if (!lumo_shell_protocol_frame_get(frame, key, &field_value) || value == NULL) {
        return false;
    }

    if (strcmp(field_value, "1") == 0 || strcmp(field_value, "true") == 0) {
        *value = true;
        return true;
    }
    if (strcmp(field_value, "0") == 0 || strcmp(field_value, "false") == 0) {
        *value = false;
        return true;
    }

    return false;
}

bool lumo_shell_protocol_frame_get_u32(
    const struct lumo_shell_protocol_frame *frame,
    const char *key,
    uint32_t *value
) {
    const char *field_value = NULL;
    char *end = NULL;
    unsigned long parsed;

    if (!lumo_shell_protocol_frame_get(frame, key, &field_value) || value == NULL) {
        return false;
    }

    errno = 0;
    parsed = strtoul(field_value, &end, 10);
    if (errno != 0 || end == field_value || *end != '\0' ||
            parsed > UINT32_MAX) {
        return false;
    }

    *value = (uint32_t)parsed;
    return true;
}

bool lumo_shell_protocol_frame_get_double(
    const struct lumo_shell_protocol_frame *frame,
    const char *key,
    double *value
) {
    const char *field_value = NULL;
    char *end = NULL;
    double parsed;

    if (!lumo_shell_protocol_frame_get(frame, key, &field_value) || value == NULL) {
        return false;
    }

    errno = 0;
    parsed = strtod(field_value, &end);
    if (errno != 0 || end == field_value || *end != '\0') {
        return false;
    }

    *value = parsed;
    return true;
}

size_t lumo_shell_protocol_frame_format(
    const struct lumo_shell_protocol_frame *frame,
    char *buffer,
    size_t buffer_size
) {
    size_t used = 0;

    if (frame == NULL || buffer == NULL || buffer_size == 0) {
        return 0;
    }

    if (!lumo_shell_protocol_append(buffer, buffer_size, &used,
            "LUMO/%u %s %s id=%u\n",
            LUMO_SHELL_PROTOCOL_VERSION,
            lumo_shell_protocol_frame_kind_name(frame->kind),
            frame->name,
            frame->id)) {
        return 0;
    }

    for (size_t i = 0; i < frame->field_count; i++) {
        if (!lumo_shell_protocol_append(buffer, buffer_size, &used,
                "%s=%s\n",
                frame->fields[i].key,
                frame->fields[i].value)) {
            return 0;
        }
    }

    if (!lumo_shell_protocol_append(buffer, buffer_size, &used, "\n")) {
        return 0;
    }

    return used;
}

void lumo_shell_protocol_stream_reset(struct lumo_shell_protocol_stream *stream) {
    if (stream == NULL) {
        return;
    }

    stream->active = false;
    stream->line_used = 0;
    memset(&stream->frame, 0, sizeof(stream->frame));
}

void lumo_shell_protocol_stream_init(struct lumo_shell_protocol_stream *stream) {
    if (stream == NULL) {
        return;
    }

    memset(stream->line_buffer, 0, sizeof(stream->line_buffer));
    lumo_shell_protocol_stream_reset(stream);
}

static bool lumo_shell_protocol_parse_header_line(
    struct lumo_shell_protocol_stream *stream,
    char *line
) {
    char *cursor = line;
    char *token = NULL;
    enum lumo_shell_protocol_frame_kind kind;
    uint32_t id = 0;
    bool have_id = false;

    while (*cursor == ' ') {
        cursor++;
    }

    token = cursor;
    while (*cursor != '\0' && *cursor != ' ') {
        cursor++;
    }
    if (*cursor == ' ') {
        *cursor++ = '\0';
    }
    if (token == NULL || strcmp(token, "LUMO/1") != 0) {
        return false;
    }

    while (*cursor == ' ') {
        cursor++;
    }

    token = cursor;
    while (*cursor != '\0' && *cursor != ' ') {
        cursor++;
    }
    if (*cursor == ' ') {
        *cursor++ = '\0';
    }
    if (token == NULL || !lumo_shell_protocol_frame_kind_parse(token, &kind)) {
        return false;
    }

    while (*cursor == ' ') {
        cursor++;
    }

    token = cursor;
    while (*cursor != '\0' && *cursor != ' ') {
        cursor++;
    }
    if (*cursor == ' ') {
        *cursor++ = '\0';
    }
    if (token == NULL || token[0] == '\0') {
        return false;
    }

    if (!lumo_shell_protocol_frame_init(&stream->frame, kind, token, 0)) {
        return false;
    }

    while (*cursor == ' ') {
        cursor++;
    }

    while (*cursor != '\0') {
        token = cursor;
        while (*cursor != '\0' && *cursor != ' ') {
            cursor++;
        }
        if (*cursor == ' ') {
            *cursor++ = '\0';
        }

        if (token[0] == '\0') {
            while (*cursor == ' ') {
                cursor++;
            }
            continue;
        }

        if (strncmp(token, "id=", 3) == 0) {
            char *end = NULL;
            unsigned long parsed;

            errno = 0;
            parsed = strtoul(token + 3, &end, 10);
            if (errno != 0 || end == token + 3 || *end != '\0' ||
                    parsed > UINT32_MAX) {
                return false;
            }
            id = (uint32_t)parsed;
            have_id = true;
            while (*cursor == ' ') {
                cursor++;
            }
            continue;
        }

        return false;
    }

    stream->frame.id = have_id ? id : 0;
    stream->active = true;
    return true;
}

static bool lumo_shell_protocol_parse_field_line(
    struct lumo_shell_protocol_stream *stream,
    const char *line
) {
    const char *equals;
    size_t key_length;
    char key[LUMO_SHELL_PROTOCOL_MAX_KEY_LENGTH];

    if (stream == NULL || line == NULL || !stream->active) {
        return false;
    }

    equals = strchr(line, '=');
    if (equals == NULL || equals == line || *(equals + 1) == '\0') {
        return false;
    }

    key_length = (size_t)(equals - line);
    if (key_length >= sizeof(key)) {
        return false;
    }

    memcpy(key, line, key_length);
    key[key_length] = '\0';
    return lumo_shell_protocol_frame_add_field(&stream->frame, key, equals + 1);
}

bool lumo_shell_protocol_stream_feed(
    struct lumo_shell_protocol_stream *stream,
    const char *chunk,
    size_t length,
    lumo_shell_protocol_frame_callback callback,
    void *user_data
) {
    if (stream == NULL || chunk == NULL) {
        return false;
    }

    for (size_t i = 0; i < length; i++) {
        char ch = chunk[i];

        if (ch == '\r') {
            continue;
        }

        if (ch != '\n') {
            if (stream->line_used + 1 >= sizeof(stream->line_buffer)) {
                fprintf(stderr,
                    "shell_protocol: line exceeds buffer (%zu bytes), "
                    "dropping stream\n",
                    sizeof(stream->line_buffer));
                lumo_shell_protocol_stream_reset(stream);
                return false;
            }
            stream->line_buffer[stream->line_used++] = ch;
            continue;
        }

        stream->line_buffer[stream->line_used] = '\0';
        if (stream->line_used == 0) {
            if (stream->active) {
                if (callback != NULL) {
                    callback(&stream->frame, user_data);
                }
                lumo_shell_protocol_stream_reset(stream);
            }
            continue;
        }

        if (!stream->active) {
            if (!lumo_shell_protocol_parse_header_line(stream,
                    stream->line_buffer)) {
                lumo_shell_protocol_stream_reset(stream);
                return false;
            }
        } else if (!lumo_shell_protocol_parse_field_line(stream,
                stream->line_buffer)) {
            lumo_shell_protocol_stream_reset(stream);
            return false;
        }

        stream->line_used = 0;
        memset(stream->line_buffer, 0, sizeof(stream->line_buffer));
    }

    return true;
}
