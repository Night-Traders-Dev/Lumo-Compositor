#ifndef LUMO_SHELL_PROTOCOL_H
#define LUMO_SHELL_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LUMO_SHELL_PROTOCOL_VERSION 1u
#define LUMO_SHELL_PROTOCOL_MAX_NAME_LENGTH 32u
#define LUMO_SHELL_PROTOCOL_MAX_KEY_LENGTH 32u
#define LUMO_SHELL_PROTOCOL_MAX_VALUE_LENGTH 128u
#define LUMO_SHELL_PROTOCOL_MAX_FIELDS 16u
#define LUMO_SHELL_PROTOCOL_LINE_BUFFER_SIZE 256u

enum lumo_shell_protocol_frame_kind {
    LUMO_SHELL_PROTOCOL_FRAME_EVENT = 0,
    LUMO_SHELL_PROTOCOL_FRAME_REQUEST,
    LUMO_SHELL_PROTOCOL_FRAME_RESPONSE,
    LUMO_SHELL_PROTOCOL_FRAME_ERROR,
};

struct lumo_shell_protocol_field {
    char key[LUMO_SHELL_PROTOCOL_MAX_KEY_LENGTH];
    char value[LUMO_SHELL_PROTOCOL_MAX_VALUE_LENGTH];
};

struct lumo_shell_protocol_frame {
    enum lumo_shell_protocol_frame_kind kind;
    char name[LUMO_SHELL_PROTOCOL_MAX_NAME_LENGTH];
    uint32_t id;
    size_t field_count;
    struct lumo_shell_protocol_field fields[LUMO_SHELL_PROTOCOL_MAX_FIELDS];
};

typedef void (*lumo_shell_protocol_frame_callback)(
    const struct lumo_shell_protocol_frame *frame,
    void *user_data
);

struct lumo_shell_protocol_stream {
    bool active;
    char line_buffer[LUMO_SHELL_PROTOCOL_LINE_BUFFER_SIZE];
    size_t line_used;
    struct lumo_shell_protocol_frame frame;
};

const char *lumo_shell_protocol_frame_kind_name(
    enum lumo_shell_protocol_frame_kind kind
);
bool lumo_shell_protocol_frame_kind_parse(
    const char *value,
    enum lumo_shell_protocol_frame_kind *kind
);
bool lumo_shell_protocol_frame_init(
    struct lumo_shell_protocol_frame *frame,
    enum lumo_shell_protocol_frame_kind kind,
    const char *name,
    uint32_t id
);
bool lumo_shell_protocol_frame_add_field(
    struct lumo_shell_protocol_frame *frame,
    const char *key,
    const char *value
);
bool lumo_shell_protocol_frame_add_bool(
    struct lumo_shell_protocol_frame *frame,
    const char *key,
    bool value
);
bool lumo_shell_protocol_frame_add_u32(
    struct lumo_shell_protocol_frame *frame,
    const char *key,
    uint32_t value
);
bool lumo_shell_protocol_frame_add_double(
    struct lumo_shell_protocol_frame *frame,
    const char *key,
    double value
);
bool lumo_shell_protocol_frame_add_string(
    struct lumo_shell_protocol_frame *frame,
    const char *key,
    const char *value
);
bool lumo_shell_protocol_frame_get(
    const struct lumo_shell_protocol_frame *frame,
    const char *key,
    const char **value
);
bool lumo_shell_protocol_frame_get_bool(
    const struct lumo_shell_protocol_frame *frame,
    const char *key,
    bool *value
);
bool lumo_shell_protocol_frame_get_u32(
    const struct lumo_shell_protocol_frame *frame,
    const char *key,
    uint32_t *value
);
bool lumo_shell_protocol_frame_get_double(
    const struct lumo_shell_protocol_frame *frame,
    const char *key,
    double *value
);
size_t lumo_shell_protocol_frame_format(
    const struct lumo_shell_protocol_frame *frame,
    char *buffer,
    size_t buffer_size
);
void lumo_shell_protocol_stream_init(struct lumo_shell_protocol_stream *stream);
void lumo_shell_protocol_stream_reset(struct lumo_shell_protocol_stream *stream);
bool lumo_shell_protocol_stream_feed(
    struct lumo_shell_protocol_stream *stream,
    const char *chunk,
    size_t length,
    lumo_shell_protocol_frame_callback callback,
    void *user_data
);

#endif
