#ifndef LUMO_COMPOSITOR_H
#define LUMO_COMPOSITOR_H

#include <stdbool.h>
#include <stddef.h>

enum lumo_rotation {
    LUMO_ROTATION_NORMAL = 0,
    LUMO_ROTATION_90 = 1,
    LUMO_ROTATION_180 = 2,
    LUMO_ROTATION_270 = 3,
};

enum lumo_hitbox_kind {
    LUMO_HITBOX_CUSTOM = 0,
    LUMO_HITBOX_LAUNCHER_TILE,
    LUMO_HITBOX_OSK_KEY,
    LUMO_HITBOX_EDGE_GESTURE,
    LUMO_HITBOX_SCRIM,
};

struct lumo_rect {
    int x;
    int y;
    int width;
    int height;
};

struct lumo_compositor_config {
    const char *session_name;
    const char *socket_name;
};

struct lumo_compositor {
    const struct lumo_compositor_config *config;
    bool running;
    bool keyboard_visible;
    enum lumo_rotation active_rotation;
};

struct lumo_compositor *lumo_compositor_create(
    const struct lumo_compositor_config *config
);
void lumo_compositor_destroy(struct lumo_compositor *compositor);
int lumo_compositor_run(struct lumo_compositor *compositor);
void lumo_compositor_stop(struct lumo_compositor *compositor);

int lumo_backend_start(struct lumo_compositor *compositor);
void lumo_backend_stop(struct lumo_compositor *compositor);

int lumo_input_start(struct lumo_compositor *compositor);
void lumo_input_stop(struct lumo_compositor *compositor);
void lumo_input_set_rotation(
    struct lumo_compositor *compositor,
    enum lumo_rotation rotation
);

int lumo_output_start(struct lumo_compositor *compositor);
void lumo_output_stop(struct lumo_compositor *compositor);
void lumo_output_set_rotation(
    struct lumo_compositor *compositor,
    const char *output_name,
    enum lumo_rotation rotation
);

int lumo_protocol_start(struct lumo_compositor *compositor);
void lumo_protocol_stop(struct lumo_compositor *compositor);
int lumo_protocol_register_hitbox(
    struct lumo_compositor *compositor,
    const char *name,
    const struct lumo_rect *rect,
    enum lumo_hitbox_kind kind,
    bool accepts_touch,
    bool accepts_pointer
);
void lumo_protocol_set_keyboard_visible(
    struct lumo_compositor *compositor,
    bool visible
);

#endif
