#include "lumo/compositor.h"

int lumo_output_start(struct lumo_compositor *compositor) {
    (void)compositor;
    return 0;
}

void lumo_output_stop(struct lumo_compositor *compositor) {
    (void)compositor;
}

void lumo_output_set_rotation(
    struct lumo_compositor *compositor,
    const char *output_name,
    enum lumo_rotation rotation
) {
    (void)output_name;
    if (compositor == NULL) {
        return;
    }

    compositor->active_rotation = rotation;
}

