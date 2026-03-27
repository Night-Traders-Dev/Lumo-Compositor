#include "lumo/compositor.h"

#include <stdio.h>
#include <stdlib.h>
#include <wlr/backend/session.h>

int lumo_backend_start(struct lumo_compositor *compositor) {
    if (compositor == NULL || compositor->display == NULL) {
        return -1;
    }

    compositor->backend = wlr_backend_autocreate(
        compositor->event_loop,
        &compositor->session
    );
    if (compositor->backend == NULL) {
        wlr_log(WLR_ERROR, "backend: failed to autocreate wlroots backend");
        return -1;
    }

    compositor->renderer = wlr_renderer_autocreate(compositor->backend);
    if (compositor->renderer == NULL) {
        wlr_log(WLR_ERROR, "backend: failed to create renderer");
        goto fail;
    }

    if (!wlr_renderer_init_wl_display(compositor->renderer, compositor->display)) {
        wlr_log(WLR_ERROR, "backend: failed to initialize wl_display renderer support");
        goto fail;
    }

    compositor->allocator = wlr_allocator_autocreate(
        compositor->backend,
        compositor->renderer
    );
    if (compositor->allocator == NULL) {
        wlr_log(WLR_ERROR, "backend: failed to create allocator");
        goto fail;
    }

    compositor->compositor_protocol = wlr_compositor_create(
        compositor->display,
        5,
        compositor->renderer
    );
    if (compositor->compositor_protocol == NULL) {
        wlr_log(WLR_ERROR, "backend: failed to create compositor global");
        goto fail;
    }

    compositor->subcompositor = wlr_subcompositor_create(compositor->display);
    if (compositor->subcompositor == NULL) {
        wlr_log(WLR_ERROR, "backend: failed to create subcompositor global");
        goto fail;
    }

    compositor->data_device_manager = wlr_data_device_manager_create(
        compositor->display
    );
    if (compositor->data_device_manager == NULL) {
        wlr_log(WLR_ERROR, "backend: failed to create data-device manager");
        goto fail;
    }

    return 0;

fail:
    lumo_backend_stop(compositor);
    return -1;
}

void lumo_backend_stop(struct lumo_compositor *compositor) {
    if (compositor == NULL) {
        return;
    }

    compositor->data_device_manager = NULL;
    compositor->subcompositor = NULL;
    compositor->compositor_protocol = NULL;

    if (compositor->allocator != NULL) {
        wlr_allocator_destroy(compositor->allocator);
        compositor->allocator = NULL;
    }

    if (compositor->renderer != NULL) {
        wlr_renderer_destroy(compositor->renderer);
        compositor->renderer = NULL;
    }

    if (compositor->backend != NULL) {
        wlr_backend_destroy(compositor->backend);
        compositor->backend = NULL;
    }

    if (compositor->session != NULL) {
        wlr_session_destroy(compositor->session);
        compositor->session = NULL;
    }
}
