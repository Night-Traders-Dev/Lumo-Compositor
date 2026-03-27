#include "lumo/compositor.h"

#include <stdio.h>
#include <stdlib.h>

struct lumo_compositor *lumo_compositor_create(
    const struct lumo_compositor_config *config
) {
    struct lumo_compositor *compositor = calloc(1, sizeof(*compositor));
    if (compositor == NULL) {
        return NULL;
    }

    compositor->config = config;
    compositor->running = false;
    compositor->keyboard_visible = false;
    compositor->active_rotation = LUMO_ROTATION_NORMAL;
    return compositor;
}

void lumo_compositor_stop(struct lumo_compositor *compositor) {
    if (compositor == NULL) {
        return;
    }

    compositor->running = false;
    lumo_protocol_stop(compositor);
    lumo_input_stop(compositor);
    lumo_output_stop(compositor);
    lumo_backend_stop(compositor);
}

void lumo_compositor_destroy(struct lumo_compositor *compositor) {
    if (compositor == NULL) {
        return;
    }

    lumo_compositor_stop(compositor);
    free(compositor);
}

int lumo_compositor_run(struct lumo_compositor *compositor) {
    const char *session_name = "lumo";
    const char *socket_name = "lumo-shell";

    if (compositor == NULL) {
        return -1;
    }

    if (compositor->config != NULL) {
        if (compositor->config->session_name != NULL) {
            session_name = compositor->config->session_name;
        }
        if (compositor->config->socket_name != NULL) {
            socket_name = compositor->config->socket_name;
        }
    }

    if (lumo_backend_start(compositor) != 0) {
        return -1;
    }
    if (lumo_output_start(compositor) != 0) {
        lumo_compositor_stop(compositor);
        return -1;
    }
    if (lumo_input_start(compositor) != 0) {
        lumo_compositor_stop(compositor);
        return -1;
    }
    if (lumo_protocol_start(compositor) != 0) {
        lumo_compositor_stop(compositor);
        return -1;
    }

    compositor->running = true;

    printf("lumo compositor skeleton\n");
    printf("session=%s socket=%s\n", session_name, socket_name);
    printf("modules=backend,input,output,protocol\n");
    printf("next=wire wlroots backend, input seats, output rotation, shell protocol\n");

    lumo_compositor_stop(compositor);
    return 0;
}
