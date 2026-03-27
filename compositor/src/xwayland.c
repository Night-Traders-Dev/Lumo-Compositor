#include "lumo/compositor.h"

#include <stdlib.h>
#include <string.h>

struct lumo_xwayland_surface {
    struct wl_list link;
    struct lumo_compositor *compositor;
    struct wlr_xwayland_surface *xsurface;
    struct wlr_scene_tree *scene_tree;
    struct wl_listener associate;
    struct wl_listener dissociate;
    struct wl_listener destroy;
    struct wl_listener request_configure;
    struct wl_listener request_activate;
    struct wl_listener set_title;
    struct wl_listener set_class;
    struct wl_listener set_parent;
    struct wl_listener set_startup_id;
};

struct lumo_xwayland_state {
    struct lumo_compositor *compositor;
    struct wl_list surfaces;
    struct wl_listener ready;
    struct wl_listener new_surface;
    struct wl_listener remove_startup_info;
};

bool lumo_xwayland_collect_workarea(
    struct lumo_compositor *compositor,
    struct wlr_box *workarea
) {
    struct lumo_output *output;

    if (workarea == NULL) {
        return false;
    }

    memset(workarea, 0, sizeof(*workarea));

    if (compositor == NULL) {
        return false;
    }

    wl_list_for_each(output, &compositor->outputs, link) {
        if (!output->usable_area_valid) {
            continue;
        }

        *workarea = output->usable_area;
        return !wlr_box_empty(workarea);
    }

    if (compositor->output_layout != NULL) {
        wlr_output_layout_get_box(compositor->output_layout, NULL, workarea);
        return !wlr_box_empty(workarea);
    }

    return false;
}

void lumo_xwayland_sync_workareas(struct lumo_compositor *compositor) {
    struct wlr_box workarea = {0};

    if (compositor == NULL || compositor->xwayland == NULL) {
        return;
    }

    if (!lumo_xwayland_collect_workarea(compositor, &workarea)) {
        wlr_log(WLR_DEBUG, "xwayland: skipped workarea sync, no usable geometry");
        return;
    }

    if (compositor->xwayland_workarea_valid &&
            compositor->xwayland_workarea.x == workarea.x &&
            compositor->xwayland_workarea.y == workarea.y &&
            compositor->xwayland_workarea.width == workarea.width &&
            compositor->xwayland_workarea.height == workarea.height) {
        return;
    }

    wlr_xwayland_set_workareas(compositor->xwayland, &workarea, 1);
    compositor->xwayland_workarea = workarea;
    compositor->xwayland_workarea_valid = true;
    wlr_log(WLR_INFO, "xwayland workarea: %d,%d %dx%d",
        workarea.x, workarea.y, workarea.width, workarea.height);
}

void lumo_xwayland_focus_surface(
    struct lumo_compositor *compositor,
    struct wlr_surface *surface
) {
    struct wlr_xwayland_surface *xsurface;

    if (compositor == NULL || surface == NULL || compositor->xwayland == NULL) {
        return;
    }

    xsurface = wlr_xwayland_surface_try_from_wlr_surface(surface);
    if (xsurface == NULL) {
        return;
    }

    if (!wlr_xwayland_or_surface_wants_focus(xsurface)) {
        wlr_log(WLR_DEBUG, "xwayland: skipping focus for %s",
            xsurface->title != NULL ? xsurface->title : "(unnamed)");
        return;
    }

    wlr_xwayland_surface_activate(xsurface, true);
}

static void lumo_xwayland_surface_clear_scene(
    struct lumo_xwayland_surface *surface
) {
    if (surface == NULL || surface->scene_tree == NULL) {
        return;
    }

    surface->scene_tree->node.data = NULL;
    wlr_scene_node_destroy(&surface->scene_tree->node);
    surface->scene_tree = NULL;
}

static void lumo_xwayland_surface_teardown(
    struct lumo_xwayland_surface *surface
) {
    if (surface == NULL) {
        return;
    }

    lumo_xwayland_surface_clear_scene(surface);

    wl_list_remove(&surface->associate.link);
    wl_list_remove(&surface->dissociate.link);
    wl_list_remove(&surface->destroy.link);
    wl_list_remove(&surface->request_configure.link);
    wl_list_remove(&surface->request_activate.link);
    wl_list_remove(&surface->set_title.link);
    wl_list_remove(&surface->set_class.link);
    wl_list_remove(&surface->set_parent.link);
    wl_list_remove(&surface->set_startup_id.link);
    wl_list_remove(&surface->link);
    free(surface);
}

static void lumo_xwayland_surface_associate(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_xwayland_surface *surface =
        wl_container_of(listener, surface, associate);
    struct lumo_compositor *compositor =
        surface != NULL ? surface->compositor : NULL;

    (void)data;
    if (surface == NULL || compositor == NULL || compositor->scene == NULL ||
            surface->xsurface == NULL || surface->xsurface->surface == NULL ||
            surface->scene_tree != NULL) {
        return;
    }

    surface->scene_tree = wlr_scene_tree_create(&compositor->scene->tree);
    if (surface->scene_tree == NULL) {
        wlr_log(WLR_ERROR, "xwayland: failed to create scene tree");
        return;
    }

    surface->scene_tree->node.data = surface;
    if (wlr_scene_surface_create(surface->scene_tree, surface->xsurface->surface) == NULL) {
        wlr_log(WLR_ERROR, "xwayland: failed to create scene surface");
        lumo_xwayland_surface_clear_scene(surface);
        return;
    }

    wlr_log(WLR_INFO, "xwayland: associated surface");
}

static void lumo_xwayland_surface_dissociate(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_xwayland_surface *surface =
        wl_container_of(listener, surface, dissociate);

    (void)data;
    if (surface == NULL) {
        return;
    }

    lumo_xwayland_surface_clear_scene(surface);
}

static void lumo_xwayland_surface_destroy(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_xwayland_surface *surface =
        wl_container_of(listener, surface, destroy);

    (void)data;
    if (surface == NULL) {
        return;
    }

    lumo_xwayland_surface_teardown(surface);
}

static void lumo_xwayland_surface_request_configure(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_xwayland_surface *surface =
        wl_container_of(listener, surface, request_configure);
    const struct wlr_xwayland_surface_configure_event *event = data;

    if (surface == NULL || surface->xsurface == NULL || event == NULL) {
        return;
    }

    wlr_xwayland_surface_configure(surface->xsurface, event->x, event->y,
        event->width, event->height);
}

static void lumo_xwayland_surface_request_activate(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_xwayland_surface *surface =
        wl_container_of(listener, surface, request_activate);

    (void)data;
    if (surface == NULL || surface->xsurface == NULL ||
            !wlr_xwayland_or_surface_wants_focus(surface->xsurface)) {
        return;
    }

    wlr_xwayland_surface_activate(surface->xsurface, true);
}

static void lumo_xwayland_surface_set_title(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_xwayland_surface *surface =
        wl_container_of(listener, surface, set_title);
    const char *title = data;

    (void)surface;
    if (title != NULL) {
        wlr_log(WLR_INFO, "xwayland title: %s", title);
    }
}

static void lumo_xwayland_surface_set_class(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_xwayland_surface *surface =
        wl_container_of(listener, surface, set_class);
    const char *class_name = data;

    (void)surface;
    if (class_name != NULL) {
        wlr_log(WLR_INFO, "xwayland class: %s", class_name);
    }
}

static void lumo_xwayland_surface_set_parent(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_xwayland_surface *surface =
        wl_container_of(listener, surface, set_parent);

    (void)data;
    (void)surface;
    wlr_log(WLR_INFO, "xwayland: parent updated");
}

static void lumo_xwayland_surface_set_startup_id(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_xwayland_surface *surface =
        wl_container_of(listener, surface, set_startup_id);
    const char *startup_id = data;

    (void)surface;
    if (startup_id != NULL) {
        wlr_log(WLR_INFO, "xwayland startup_id: %s", startup_id);
    }
}

static void lumo_xwayland_ready(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_xwayland_state *state =
        wl_container_of(listener, state, ready);
    struct lumo_compositor *compositor = state != NULL ? state->compositor : NULL;

    (void)data;
    if (compositor == NULL || compositor->xwayland == NULL ||
            compositor->xwayland->display_name == NULL) {
        return;
    }

    if (setenv("DISPLAY", compositor->xwayland->display_name, true) != 0) {
        wlr_log_errno(WLR_ERROR, "xwayland: failed to export DISPLAY");
    }

    wlr_log(WLR_INFO, "xwayland ready on display %s",
        compositor->xwayland->display_name);
    lumo_xwayland_sync_workareas(compositor);
}

static void lumo_xwayland_new_surface(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_xwayland_state *state =
        wl_container_of(listener, state, new_surface);
    struct lumo_compositor *compositor = state != NULL ? state->compositor : NULL;
    struct wlr_xwayland_surface *xsurface = data;
    struct lumo_xwayland_surface *surface;

    if (state == NULL || compositor == NULL || xsurface == NULL) {
        return;
    }

    surface = calloc(1, sizeof(*surface));
    if (surface == NULL) {
        wlr_log_errno(WLR_ERROR, "xwayland: failed to allocate surface");
        return;
    }

    surface->compositor = compositor;
    surface->xsurface = xsurface;

    surface->associate.notify = lumo_xwayland_surface_associate;
    wl_signal_add(&xsurface->events.associate, &surface->associate);
    surface->dissociate.notify = lumo_xwayland_surface_dissociate;
    wl_signal_add(&xsurface->events.dissociate, &surface->dissociate);
    surface->destroy.notify = lumo_xwayland_surface_destroy;
    wl_signal_add(&xsurface->events.destroy, &surface->destroy);
    surface->request_configure.notify = lumo_xwayland_surface_request_configure;
    wl_signal_add(&xsurface->events.request_configure,
        &surface->request_configure);
    surface->request_activate.notify = lumo_xwayland_surface_request_activate;
    wl_signal_add(&xsurface->events.request_activate, &surface->request_activate);
    surface->set_title.notify = lumo_xwayland_surface_set_title;
    wl_signal_add(&xsurface->events.set_title, &surface->set_title);
    surface->set_class.notify = lumo_xwayland_surface_set_class;
    wl_signal_add(&xsurface->events.set_class, &surface->set_class);
    surface->set_parent.notify = lumo_xwayland_surface_set_parent;
    wl_signal_add(&xsurface->events.set_parent, &surface->set_parent);
    surface->set_startup_id.notify = lumo_xwayland_surface_set_startup_id;
    wl_signal_add(&xsurface->events.set_startup_id, &surface->set_startup_id);

    wl_list_insert(&state->surfaces, &surface->link);
    xsurface->data = surface;
    wlr_log(WLR_INFO, "xwayland: new surface");

    if (xsurface->surface != NULL) {
        lumo_xwayland_surface_associate(&surface->associate, NULL);
    }
}

static void lumo_xwayland_remove_startup_info(
    struct wl_listener *listener,
    void *data
) {
    const struct wlr_xwayland_remove_startup_info_event *event = data;

    (void)listener;
    if (event != NULL && event->id != NULL) {
        wlr_log(WLR_INFO, "xwayland startup info removed: %s", event->id);
    }
}

int lumo_xwayland_start(struct lumo_compositor *compositor) {
    struct lumo_xwayland_state *state;

    if (compositor == NULL || compositor->display == NULL ||
            compositor->compositor_protocol == NULL) {
        return -1;
    }

    if (compositor->xwayland != NULL) {
        return 0;
    }

    compositor->xwayland = wlr_xwayland_create(compositor->display,
        compositor->compositor_protocol, true);
    if (compositor->xwayland == NULL) {
        wlr_log(WLR_ERROR, "xwayland: unavailable, continuing without X11");
        return 0;
    }

    state = calloc(1, sizeof(*state));
    if (state == NULL) {
        wlr_log_errno(WLR_ERROR, "xwayland: failed to allocate state");
        wlr_xwayland_destroy(compositor->xwayland);
        compositor->xwayland = NULL;
        return 0;
    }

    state->compositor = compositor;
    wl_list_init(&state->surfaces);
    state->ready.notify = lumo_xwayland_ready;
    wl_signal_add(&compositor->xwayland->events.ready, &state->ready);
    state->new_surface.notify = lumo_xwayland_new_surface;
    wl_signal_add(&compositor->xwayland->events.new_surface, &state->new_surface);
    state->remove_startup_info.notify = lumo_xwayland_remove_startup_info;
    wl_signal_add(&compositor->xwayland->events.remove_startup_info,
        &state->remove_startup_info);

    compositor->xwayland->data = state;
    compositor->xwayland_workarea_valid = false;
    wlr_log(WLR_INFO, "xwayland: created");
    return 0;
}

void lumo_xwayland_stop(struct lumo_compositor *compositor) {
    struct lumo_xwayland_state *state;
    struct lumo_xwayland_surface *surface, *tmp;

    if (compositor == NULL || compositor->xwayland == NULL) {
        return;
    }

    state = compositor->xwayland->data;
    if (state != NULL) {
        wl_list_for_each_safe(surface, tmp, &state->surfaces, link) {
            lumo_xwayland_surface_teardown(surface);
        }

        wl_list_remove(&state->ready.link);
        wl_list_remove(&state->new_surface.link);
        wl_list_remove(&state->remove_startup_info.link);
        free(state);
        compositor->xwayland->data = NULL;
    }

    wlr_xwayland_destroy(compositor->xwayland);
    compositor->xwayland = NULL;
    compositor->xwayland_workarea_valid = false;
}
