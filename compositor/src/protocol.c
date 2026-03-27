#include "lumo/compositor.h"

int lumo_protocol_start(struct lumo_compositor *compositor) {
    (void)compositor;
    return 0;
}

void lumo_protocol_stop(struct lumo_compositor *compositor) {
    (void)compositor;
}

int lumo_protocol_register_hitbox(
    struct lumo_compositor *compositor,
    const char *name,
    const struct lumo_rect *rect,
    enum lumo_hitbox_kind kind,
    bool accepts_touch,
    bool accepts_pointer
) {
    (void)compositor;
    (void)name;
    (void)rect;
    (void)kind;
    (void)accepts_touch;
    (void)accepts_pointer;
    return 0;
}

void lumo_protocol_set_keyboard_visible(
    struct lumo_compositor *compositor,
    bool visible
) {
    if (compositor == NULL) {
        return;
    }

    compositor->keyboard_visible = visible;
}

