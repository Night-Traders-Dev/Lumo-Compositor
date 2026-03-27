#include "lumo/compositor.h"
#include "lumo/shell.h"

#include <stdlib.h>
#include <string.h>

#include <wlr/util/box.h>

struct lumo_protocol_state {
    struct wl_listener xdg_new_toplevel;
    struct wl_listener xdg_new_popup;
    struct wl_listener layer_new_surface;
};

struct lumo_scene_object_head {
    struct wl_list link;
    struct lumo_compositor *compositor;
    enum lumo_scene_object_role role;
};

static struct lumo_protocol_state *lumo_protocol_state_from(
    struct lumo_compositor *compositor
) {
    return compositor != NULL
        ? (struct lumo_protocol_state *)compositor->protocol_state
        : NULL;
}

static struct lumo_output *lumo_protocol_first_output(
    struct lumo_compositor *compositor
) {
    struct lumo_output *output;

    if (compositor == NULL || wl_list_empty(&compositor->outputs)) {
        return NULL;
    }

    output = wl_container_of(compositor->outputs.next, output, link);
    return output;
}

static struct lumo_output *lumo_protocol_output_for_wlr(
    struct lumo_compositor *compositor,
    struct wlr_output *wlr_output
) {
    if (compositor == NULL || wlr_output == NULL) {
        return NULL;
    }

    struct lumo_output *output;
    wl_list_for_each(output, &compositor->outputs, link) {
        if (output->wlr_output == wlr_output) {
            return output;
        }
    }

    return NULL;
}

static struct lumo_output *lumo_protocol_output_for_surface(
    struct lumo_compositor *compositor,
    struct wlr_output *preferred
) {
    struct lumo_output *output = lumo_protocol_output_for_wlr(compositor,
        preferred);
    if (output != NULL) {
        return output;
    }

    if (preferred == NULL) {
        return lumo_protocol_first_output(compositor);
    }

    return lumo_protocol_first_output(compositor);
}

static void lumo_protocol_remove_hitbox(struct lumo_hitbox *hitbox) {
    if (hitbox == NULL) {
        return;
    }

    wl_list_remove(&hitbox->link);
    free(hitbox->name);
    free(hitbox);
}

static bool lumo_protocol_hitbox_is_shell_reserved(const struct lumo_hitbox *hitbox) {
    if (hitbox == NULL || hitbox->name == NULL) {
        return false;
    }

    return strncmp(hitbox->name, "shell-", 6) == 0;
}

static void lumo_protocol_clear_shell_hitboxes(struct lumo_compositor *compositor) {
    struct lumo_hitbox *hitbox, *tmp;

    if (compositor == NULL) {
        return;
    }

    wl_list_for_each_safe(hitbox, tmp, &compositor->hitboxes, link) {
        if (lumo_protocol_hitbox_is_shell_reserved(hitbox)) {
            lumo_protocol_remove_hitbox(hitbox);
        }
    }
}

void lumo_protocol_refresh_shell_hitboxes(struct lumo_compositor *compositor) {
    struct wlr_box workarea = {0};
    struct lumo_shell_surface_config shell_config = {0};
    struct lumo_rect rect = {0};

    if (compositor == NULL) {
        return;
    }

    lumo_protocol_clear_shell_hitboxes(compositor);
    if (!lumo_xwayland_collect_workarea(compositor, &workarea)) {
        return;
    }

    if (lumo_shell_surface_config_for_mode(LUMO_SHELL_MODE_GESTURE,
            (uint32_t)workarea.width, (uint32_t)workarea.height,
            &shell_config)) {
        rect.x = workarea.x;
        rect.y = workarea.y + workarea.height - (int)shell_config.height;
        rect.width = workarea.width;
        rect.height = (int)shell_config.height;
        lumo_protocol_register_hitbox(compositor, "shell-gesture", &rect,
            LUMO_HITBOX_EDGE_GESTURE, true, true);
    }

    if (compositor->keyboard_visible &&
            lumo_shell_surface_config_for_mode(LUMO_SHELL_MODE_OSK,
                (uint32_t)workarea.width, (uint32_t)workarea.height,
                &shell_config)) {
        rect.x = workarea.x;
        rect.y = workarea.y + workarea.height - (int)shell_config.height;
        rect.width = workarea.width;
        rect.height = (int)shell_config.height;
        lumo_protocol_register_hitbox(compositor, "shell-osk", &rect,
            LUMO_HITBOX_OSK_KEY, true, true);
    }

    if (compositor->launcher_visible &&
            lumo_shell_surface_config_for_mode(LUMO_SHELL_MODE_LAUNCHER,
                (uint32_t)workarea.width, (uint32_t)workarea.height,
                &shell_config)) {
        rect.x = workarea.x;
        rect.y = workarea.y;
        rect.width = workarea.width;
        rect.height = workarea.height;
        lumo_protocol_register_hitbox(compositor, "shell-launcher", &rect,
            LUMO_HITBOX_SCRIM, true, true);
    }
}

static void lumo_protocol_teardown_toplevel(struct lumo_toplevel *toplevel) {
    if (toplevel == NULL) {
        return;
    }

    if (toplevel->scene_tree != NULL) {
        toplevel->scene_tree->node.data = NULL;
    }

    wl_list_remove(&toplevel->request_maximize.link);
    wl_list_remove(&toplevel->request_fullscreen.link);
    wl_list_remove(&toplevel->request_minimize.link);
    wl_list_remove(&toplevel->request_move.link);
    wl_list_remove(&toplevel->request_resize.link);
    wl_list_remove(&toplevel->request_show_window_menu.link);
    wl_list_remove(&toplevel->set_parent.link);
    wl_list_remove(&toplevel->set_title.link);
    wl_list_remove(&toplevel->set_app_id.link);
    wl_list_remove(&toplevel->destroy.link);
    wl_list_remove(&toplevel->link);
    free(toplevel);
}

static void lumo_protocol_teardown_popup(struct lumo_popup *popup) {
    if (popup == NULL) {
        return;
    }

    if (popup->scene_tree != NULL) {
        popup->scene_tree->node.data = NULL;
    }

    wl_list_remove(&popup->destroy.link);
    wl_list_remove(&popup->link);
    free(popup);
}

static void lumo_protocol_teardown_layer_surface(
    struct lumo_layer_surface *layer_surface
) {
    if (layer_surface == NULL) {
        return;
    }

    if (layer_surface->scene_surface != NULL && layer_surface->scene_surface->tree != NULL) {
        layer_surface->scene_surface->tree->node.data = NULL;
    }

    wl_list_remove(&layer_surface->map.link);
    wl_list_remove(&layer_surface->unmap.link);
    wl_list_remove(&layer_surface->commit.link);
    wl_list_remove(&layer_surface->destroy.link);
    wl_list_remove(&layer_surface->link);
    free(layer_surface);
}

static void lumo_protocol_toplevel_schedule_configure(
    struct lumo_toplevel *toplevel
) {
    if (toplevel == NULL || toplevel->xdg_toplevel == NULL ||
        toplevel->xdg_surface == NULL) {
        return;
    }

    if (toplevel->xdg_toplevel->requested.fullscreen) {
        wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel, true);
        return;
    }

    if (toplevel->xdg_toplevel->requested.maximized) {
        wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, true);
        return;
    }

    if (toplevel->compositor != NULL &&
            toplevel->compositor->output_layout != NULL) {
        struct wlr_box layout_box = {0};
        wlr_output_layout_get_box(toplevel->compositor->output_layout, NULL,
            &layout_box);
        if (!wlr_box_empty(&layout_box)) {
            wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel,
                layout_box.width, layout_box.height);
            return;
        }
    }

    wlr_xdg_surface_schedule_configure(toplevel->xdg_surface);
}

static void lumo_protocol_toplevel_request_maximize(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_toplevel *toplevel =
        wl_container_of(listener, toplevel, request_maximize);
    (void)data;
    wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel,
        toplevel->xdg_toplevel->requested.maximized);
}

static void lumo_protocol_toplevel_request_fullscreen(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_toplevel *toplevel =
        wl_container_of(listener, toplevel, request_fullscreen);
    (void)data;
    wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel,
        toplevel->xdg_toplevel->requested.fullscreen);
}

static void lumo_protocol_toplevel_request_minimize(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_toplevel *toplevel =
        wl_container_of(listener, toplevel, request_minimize);
    (void)data;
    lumo_protocol_toplevel_schedule_configure(toplevel);
}

static void lumo_protocol_toplevel_request_move(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_toplevel *toplevel =
        wl_container_of(listener, toplevel, request_move);
    (void)data;
    lumo_protocol_toplevel_schedule_configure(toplevel);
}

static void lumo_protocol_toplevel_request_resize(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_toplevel *toplevel =
        wl_container_of(listener, toplevel, request_resize);
    (void)data;
    lumo_protocol_toplevel_schedule_configure(toplevel);
}

static void lumo_protocol_toplevel_request_show_window_menu(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_toplevel *toplevel =
        wl_container_of(listener, toplevel, request_show_window_menu);
    (void)data;
    lumo_protocol_toplevel_schedule_configure(toplevel);
}

static void lumo_protocol_toplevel_set_parent(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_toplevel *toplevel =
        wl_container_of(listener, toplevel, set_parent);
    (void)data;
    lumo_protocol_toplevel_schedule_configure(toplevel);
}

static void lumo_protocol_toplevel_set_title(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_toplevel *toplevel =
        wl_container_of(listener, toplevel, set_title);
    const char *title = data;

    if (title != NULL) {
        wlr_log(WLR_INFO, "toplevel title: %s", title);
    }

    (void)toplevel;
}

static void lumo_protocol_toplevel_set_app_id(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_toplevel *toplevel =
        wl_container_of(listener, toplevel, set_app_id);
    const char *app_id = data;

    if (app_id != NULL) {
        wlr_log(WLR_INFO, "toplevel app_id: %s", app_id);
    }

    (void)toplevel;
}

static void lumo_protocol_toplevel_destroy(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_toplevel *toplevel =
        wl_container_of(listener, toplevel, destroy);
    (void)data;
    lumo_protocol_teardown_toplevel(toplevel);
}

static void lumo_protocol_popup_destroy(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_popup *popup = wl_container_of(listener, popup, destroy);
    (void)data;
    lumo_protocol_teardown_popup(popup);
}

static void lumo_protocol_layer_surface_destroy(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_layer_surface *layer_surface =
        wl_container_of(listener, layer_surface, destroy);
    (void)data;
    lumo_protocol_teardown_layer_surface(layer_surface);
}

static void lumo_protocol_new_toplevel(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_compositor *compositor =
        wl_container_of(listener, compositor, xdg_new_toplevel);
    struct wlr_xdg_toplevel *xdg_toplevel = data;
    struct lumo_toplevel *toplevel = calloc(1, sizeof(*toplevel));

    if (toplevel == NULL) {
        wlr_log_errno(WLR_ERROR, "protocol: failed to allocate toplevel");
        return;
    }

    toplevel->compositor = compositor;
    toplevel->role = LUMO_SCENE_OBJECT_TOPLEVEL;
    toplevel->xdg_toplevel = xdg_toplevel;
    toplevel->xdg_surface = xdg_toplevel->base;

    toplevel->scene_tree = wlr_scene_xdg_surface_create(&compositor->scene->tree,
        toplevel->xdg_surface);
    if (toplevel->scene_tree == NULL) {
        wlr_log(WLR_ERROR, "protocol: failed to create xdg scene tree");
        free(toplevel);
        return;
    }

    toplevel->scene_tree->node.data = toplevel;

    toplevel->request_maximize.notify = lumo_protocol_toplevel_request_maximize;
    wl_signal_add(&xdg_toplevel->events.request_maximize,
        &toplevel->request_maximize);
    toplevel->request_fullscreen.notify = lumo_protocol_toplevel_request_fullscreen;
    wl_signal_add(&xdg_toplevel->events.request_fullscreen,
        &toplevel->request_fullscreen);
    toplevel->request_minimize.notify = lumo_protocol_toplevel_request_minimize;
    wl_signal_add(&xdg_toplevel->events.request_minimize,
        &toplevel->request_minimize);
    toplevel->request_move.notify = lumo_protocol_toplevel_request_move;
    wl_signal_add(&xdg_toplevel->events.request_move, &toplevel->request_move);
    toplevel->request_resize.notify = lumo_protocol_toplevel_request_resize;
    wl_signal_add(&xdg_toplevel->events.request_resize,
        &toplevel->request_resize);
    toplevel->request_show_window_menu.notify =
        lumo_protocol_toplevel_request_show_window_menu;
    wl_signal_add(&xdg_toplevel->events.request_show_window_menu,
        &toplevel->request_show_window_menu);
    toplevel->set_parent.notify = lumo_protocol_toplevel_set_parent;
    wl_signal_add(&xdg_toplevel->events.set_parent, &toplevel->set_parent);
    toplevel->set_title.notify = lumo_protocol_toplevel_set_title;
    wl_signal_add(&xdg_toplevel->events.set_title, &toplevel->set_title);
    toplevel->set_app_id.notify = lumo_protocol_toplevel_set_app_id;
    wl_signal_add(&xdg_toplevel->events.set_app_id, &toplevel->set_app_id);
    toplevel->destroy.notify = lumo_protocol_toplevel_destroy;
    wl_signal_add(&xdg_toplevel->events.destroy, &toplevel->destroy);

    wl_list_insert(&compositor->toplevels, &toplevel->link);
    lumo_protocol_toplevel_schedule_configure(toplevel);
    wlr_log(WLR_INFO, "protocol: new toplevel");
}

static void lumo_protocol_new_popup(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_compositor *compositor =
        wl_container_of(listener, compositor, xdg_new_popup);
    struct wlr_xdg_popup *popup_surface = data;
    struct lumo_popup *popup = calloc(1, sizeof(*popup));

    if (popup == NULL) {
        wlr_log_errno(WLR_ERROR, "protocol: failed to allocate popup");
        return;
    }

    popup->compositor = compositor;
    popup->role = LUMO_SCENE_OBJECT_POPUP;
    popup->xdg_popup = popup_surface;
    popup->scene_tree = wlr_scene_xdg_surface_create(&compositor->scene->tree,
        popup_surface->base);
    if (popup->scene_tree == NULL) {
        wlr_log(WLR_ERROR, "protocol: failed to create popup scene tree");
        free(popup);
        return;
    }

    popup->scene_tree->node.data = popup;
    popup->destroy.notify = lumo_protocol_popup_destroy;
    wl_signal_add(&popup_surface->events.destroy, &popup->destroy);

    wl_list_insert(&compositor->popups, &popup->link);
    wlr_log(WLR_INFO, "protocol: new popup");
}

static void lumo_protocol_new_layer_surface(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_compositor *compositor =
        wl_container_of(listener, compositor, layer_new_surface);
    struct wlr_layer_surface_v1 *layer_surface = data;
    struct lumo_layer_surface *surface = calloc(1, sizeof(*surface));

    if (surface == NULL) {
        wlr_log_errno(WLR_ERROR, "protocol: failed to allocate layer surface");
        return;
    }

    surface->compositor = compositor;
    surface->role = LUMO_SCENE_OBJECT_LAYER_SURFACE;
    surface->layer_surface = layer_surface;
    surface->output = lumo_protocol_output_for_surface(compositor,
        layer_surface->output);

    if (surface->output != NULL) {
        layer_surface->output = surface->output->wlr_output;
    }

    surface->scene_surface = wlr_scene_layer_surface_v1_create(
        &compositor->scene->tree,
        layer_surface
    );
    if (surface->scene_surface == NULL) {
        wlr_log(WLR_ERROR,
            "protocol: failed to create layer scene surface");
        free(surface);
        return;
    }

    if (surface->scene_surface->tree != NULL) {
        surface->scene_surface->tree->node.data = surface;
    }

    surface->destroy.notify = lumo_protocol_layer_surface_destroy;
    wl_signal_add(&layer_surface->events.destroy, &surface->destroy);

    wl_list_insert(&compositor->layer_surfaces, &surface->link);
    wlr_log(WLR_INFO, "protocol: new layer surface");
}

static void lumo_protocol_configure_layer_surface_for_output(
    struct lumo_layer_surface *layer_surface,
    struct lumo_output *output,
    const struct wlr_box *full_area,
    struct wlr_box *usable_area
) {
    if (layer_surface == NULL || output == NULL || full_area == NULL ||
        usable_area == NULL || layer_surface->scene_surface == NULL ||
        layer_surface->layer_surface == NULL ||
        layer_surface->layer_surface->output != output->wlr_output) {
        return;
    }

    wlr_scene_layer_surface_v1_configure(layer_surface->scene_surface,
        full_area, usable_area);
}

int lumo_protocol_start(struct lumo_compositor *compositor) {
    struct lumo_protocol_state *state = NULL;

    if (compositor == NULL || compositor->display == NULL ||
            compositor->scene == NULL) {
        return -1;
    }
    if (compositor->protocol_started) {
        return 0;
    }

    state = calloc(1, sizeof(*state));
    if (state == NULL) {
        wlr_log_errno(WLR_ERROR, "protocol: failed to allocate state");
        return -1;
    }

    compositor->xdg_shell = wlr_xdg_shell_create(compositor->display, 6);
    if (compositor->xdg_shell == NULL) {
        wlr_log(WLR_ERROR, "protocol: failed to create xdg shell");
        free(state);
        return -1;
    }

    compositor->layer_shell = wlr_layer_shell_v1_create(compositor->display, 4);
    if (compositor->layer_shell == NULL) {
        wlr_log(WLR_ERROR, "protocol: failed to create layer shell");
        free(state);
        return -1;
    }

    compositor->text_input_manager = wlr_text_input_manager_v3_create(
        compositor->display
    );
    if (compositor->text_input_manager == NULL) {
        wlr_log(WLR_ERROR, "protocol: failed to create text-input manager");
        free(state);
        return -1;
    }

    compositor->input_method_manager = wlr_input_method_manager_v2_create(
        compositor->display
    );
    if (compositor->input_method_manager == NULL) {
        wlr_log(WLR_ERROR, "protocol: failed to create input-method manager");
        free(state);
        return -1;
    }

    compositor->virtual_keyboard_manager = wlr_virtual_keyboard_manager_v1_create(
        compositor->display
    );
    if (compositor->virtual_keyboard_manager == NULL) {
        wlr_log(WLR_ERROR, "protocol: failed to create virtual-keyboard manager");
        free(state);
        return -1;
    }

    compositor->pointer_gestures = wlr_pointer_gestures_v1_create(
        compositor->display
    );
    if (compositor->pointer_gestures == NULL) {
        wlr_log(WLR_ERROR, "protocol: failed to create pointer-gestures manager");
        free(state);
        return -1;
    }

    lumo_xwayland_start(compositor);

    state->xdg_new_toplevel.notify = lumo_protocol_new_toplevel;
    wl_signal_add(&compositor->xdg_shell->events.new_toplevel,
        &state->xdg_new_toplevel);
    state->xdg_new_popup.notify = lumo_protocol_new_popup;
    wl_signal_add(&compositor->xdg_shell->events.new_popup,
        &state->xdg_new_popup);
    state->layer_new_surface.notify = lumo_protocol_new_layer_surface;
    wl_signal_add(&compositor->layer_shell->events.new_surface,
        &state->layer_new_surface);

    compositor->protocol_state = state;
    compositor->protocol_started = true;
    return 0;
}

void lumo_protocol_stop(struct lumo_compositor *compositor) {
    struct lumo_protocol_state *state = lumo_protocol_state_from(compositor);
    struct lumo_hitbox *hitbox, *hitbox_tmp;
    struct lumo_toplevel *toplevel, *toplevel_tmp;
    struct lumo_popup *popup, *popup_tmp;
    struct lumo_layer_surface *layer_surface, *layer_surface_tmp;

    if (compositor == NULL || !compositor->protocol_started) {
        return;
    }

    lumo_xwayland_stop(compositor);

    wl_list_for_each_safe(hitbox, hitbox_tmp, &compositor->hitboxes, link) {
        lumo_protocol_remove_hitbox(hitbox);
    }

    wl_list_for_each_safe(toplevel, toplevel_tmp, &compositor->toplevels, link) {
        lumo_protocol_teardown_toplevel(toplevel);
    }

    wl_list_for_each_safe(popup, popup_tmp, &compositor->popups, link) {
        lumo_protocol_teardown_popup(popup);
    }

    wl_list_for_each_safe(layer_surface, layer_surface_tmp,
            &compositor->layer_surfaces, link) {
        lumo_protocol_teardown_layer_surface(layer_surface);
    }

    if (state != NULL) {
        wl_list_remove(&state->xdg_new_toplevel.link);
        wl_list_remove(&state->xdg_new_popup.link);
        wl_list_remove(&state->layer_new_surface.link);
        free(state);
        compositor->protocol_state = NULL;
    }

    compositor->xdg_shell = NULL;
    compositor->layer_shell = NULL;
    compositor->text_input_manager = NULL;
    compositor->input_method_manager = NULL;
    compositor->virtual_keyboard_manager = NULL;
    compositor->pointer_gestures = NULL;
    compositor->protocol_started = false;
}

int lumo_protocol_register_hitbox(
    struct lumo_compositor *compositor,
    const char *name,
    const struct lumo_rect *rect,
    enum lumo_hitbox_kind kind,
    bool accepts_touch,
    bool accepts_pointer
) {
    struct lumo_hitbox *hitbox = NULL;

    if (compositor == NULL || rect == NULL) {
        return -1;
    }

    hitbox = calloc(1, sizeof(*hitbox));
    if (hitbox == NULL) {
        return -1;
    }

    hitbox->name = name != NULL ? strdup(name) : NULL;
    hitbox->rect = *rect;
    hitbox->kind = kind;
    hitbox->accepts_touch = accepts_touch;
    hitbox->accepts_pointer = accepts_pointer;
    wl_list_insert(compositor->hitboxes.prev, &hitbox->link);
    return 0;
}

const struct lumo_hitbox *lumo_protocol_hitbox_at(
    struct lumo_compositor *compositor,
    double lx,
    double ly
) {
    struct lumo_hitbox *hitbox;

    if (compositor == NULL) {
        return NULL;
    }

    wl_list_for_each_reverse(hitbox, &compositor->hitboxes, link) {
        if (lumo_rect_contains(&hitbox->rect, lx, ly)) {
            return hitbox;
        }
    }

    return NULL;
}

void lumo_protocol_set_gesture_threshold(
    struct lumo_compositor *compositor,
    double threshold
) {
    if (compositor == NULL) {
        return;
    }

    compositor->gesture_threshold = threshold > 0.0 ? threshold : 24.0;
    wlr_log(WLR_INFO, "protocol: gesture threshold set to %.2f",
        compositor->gesture_threshold);
}

void lumo_protocol_set_launcher_visible(
    struct lumo_compositor *compositor,
    bool visible
) {
    if (compositor == NULL) {
        return;
    }

    compositor->launcher_visible = visible;
    if (visible) {
        compositor->scrim_state = LUMO_SCRIM_MODAL;
    } else if (!compositor->keyboard_visible) {
        compositor->scrim_state = LUMO_SCRIM_HIDDEN;
    }

    wlr_log(WLR_INFO, "protocol: launcher %s", visible ? "visible" : "hidden");
    lumo_protocol_refresh_shell_hitboxes(compositor);
}

void lumo_protocol_set_scrim_state(
    struct lumo_compositor *compositor,
    enum lumo_scrim_state state
) {
    if (compositor == NULL) {
        return;
    }

    compositor->scrim_state = state;
    wlr_log(WLR_INFO, "protocol: scrim state=%d", state);
}

void lumo_protocol_ack_keyboard_resize(
    struct lumo_compositor *compositor,
    uint32_t serial
) {
    if (compositor == NULL) {
        return;
    }

    if (compositor->keyboard_resize_pending &&
            compositor->keyboard_resize_serial == serial) {
        compositor->keyboard_resize_pending = false;
        compositor->keyboard_resize_acked = true;
        wlr_log(WLR_INFO, "protocol: keyboard resize ack serial=%u", serial);
        return;
    }

    wlr_log(WLR_INFO,
        "protocol: ignored keyboard resize ack serial=%u pending=%s current=%u",
        serial,
        compositor->keyboard_resize_pending ? "true" : "false",
        compositor->keyboard_resize_serial);
}

void lumo_protocol_set_keyboard_visible(
    struct lumo_compositor *compositor,
    bool visible
) {
    if (compositor == NULL) {
        return;
    }

    if (compositor->keyboard_visible == visible) {
        return;
    }

    compositor->keyboard_visible = visible;
    compositor->keyboard_resize_serial++;
    compositor->keyboard_resize_pending = visible;
    compositor->keyboard_resize_acked = !visible;

    if (visible) {
        compositor->scrim_state = LUMO_SCRIM_DIMMED;
    } else if (!compositor->launcher_visible) {
        compositor->scrim_state = LUMO_SCRIM_HIDDEN;
    }

    wlr_log(WLR_INFO, "protocol: keyboard %s serial=%u",
        visible ? "visible" : "hidden", compositor->keyboard_resize_serial);
    lumo_protocol_refresh_shell_hitboxes(compositor);
}

void lumo_protocol_configure_layers(
    struct lumo_compositor *compositor,
    struct lumo_output *output
) {
    struct lumo_layer_surface *layer_surface;
    struct lumo_layer_surface *tmp;
    struct wlr_box full_area = {0};
    struct wlr_box usable_area = {0};
    int width = 0;
    int height = 0;

    if (compositor == NULL || output == NULL || output->wlr_output == NULL ||
            compositor->scene == NULL) {
        return;
    }

    output->usable_area_valid = false;
    wlr_output_effective_resolution(output->wlr_output, &width, &height);
    full_area.x = 0;
    full_area.y = 0;
    full_area.width = width;
    full_area.height = height;
    usable_area = full_area;

    for (uint32_t layer = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
            layer <= ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY; layer++) {
        wl_list_for_each_safe(layer_surface, tmp, &compositor->layer_surfaces,
                link) {
            if (layer_surface->layer_surface == NULL) {
                continue;
            }

            if (layer_surface->output == NULL) {
                layer_surface->output = output;
                layer_surface->layer_surface->output = output->wlr_output;
            }

            if (layer_surface->output != output) {
                continue;
            }

            if (layer_surface->scene_surface == NULL) {
                continue;
            }

            if (layer_surface->layer_surface->current.layer != layer &&
                    layer_surface->layer_surface->pending.layer != layer) {
                continue;
            }

            lumo_protocol_configure_layer_surface_for_output(layer_surface,
                output, &full_area, &usable_area);
        }
    }

    if (!wlr_box_empty(&usable_area)) {
        output->usable_area = usable_area;
        output->usable_area_valid = true;
    } else {
        output->usable_area = full_area;
        output->usable_area_valid = !wlr_box_empty(&full_area);
    }

    lumo_protocol_refresh_shell_hitboxes(compositor);
}
