#include "lumo/compositor.h"

int lumo_input_start(struct lumo_compositor *compositor) {
    (void)compositor;
    return 0;
}

void lumo_input_stop(struct lumo_compositor *compositor) {
    (void)compositor;
}

void lumo_input_set_rotation(
    struct lumo_compositor *compositor,
    enum lumo_rotation rotation
) {
    if (compositor == NULL) {
        return;
    }

    compositor->active_rotation = rotation;
}

