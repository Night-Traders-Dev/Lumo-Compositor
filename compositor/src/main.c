#include "lumo/compositor.h"

#include <stdio.h>

int main(void) {
    const struct lumo_compositor_config config = {
        .session_name = "lumo",
        .socket_name = "lumo-shell",
    };

    struct lumo_compositor *compositor = lumo_compositor_create(&config);
    if (compositor == NULL) {
        fputs("failed to allocate compositor\n", stderr);
        return 1;
    }

    if (lumo_compositor_run(compositor) != 0) {
        lumo_compositor_destroy(compositor);
        return 1;
    }

    lumo_compositor_destroy(compositor);
    return 0;
}

