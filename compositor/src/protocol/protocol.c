#include "lumo/compositor.h"
#include "lumo/shell.h"

#include <stdlib.h>
#include <string.h>

#include <wlr/util/box.h>

struct lumo_scene_object_head {
    struct wl_list link;
    struct lumo_compositor *compositor;
    enum lumo_scene_object_role role;
};

static struct lumo_protocol_state *lumo_protocol_state_from(
    struct lumo_compositor *compositor
) {
    return compositor != NULL ? compositor->protocol_state : NULL;
}

struct lumo_compositor *lumo_protocol_listener_compositor(
    struct wl_listener *listener,
    enum lumo_protocol_listener_kind kind
) {
    struct lumo_protocol_state *state = NULL;

    if (listener == NULL) {
        return NULL;
    }

    switch (kind) {
    case LUMO_PROTOCOL_LISTENER_XDG_TOPLEVEL:
        state = wl_container_of(listener, state, xdg_new_toplevel);
        break;
    case LUMO_PROTOCOL_LISTENER_XDG_POPUP:
        state = wl_container_of(listener, state, xdg_new_popup);
        break;
    case LUMO_PROTOCOL_LISTENER_LAYER_SURFACE:
        state = wl_container_of(listener, state, layer_new_surface);
        break;
    default:
        return NULL;
    }

    return state != NULL ? state->compositor : NULL;
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

void lumo_protocol_mark_layers_dirty(struct lumo_compositor *compositor) {
    struct lumo_output *output;

    if (compositor == NULL) {
        return;
    }

    compositor->layer_config_dirty = true;
    wl_list_for_each(output, &compositor->outputs, link) {
        if (output->wlr_output != NULL) {
            wlr_output_schedule_frame(output->wlr_output);
        }
    }
}

static struct wlr_xdg_surface *lumo_protocol_root_xdg_surface_from_surface(
    struct wlr_surface *surface
) {
    struct wlr_xdg_surface *xdg_surface;

    xdg_surface = surface != NULL
        ? wlr_xdg_surface_try_from_wlr_surface(surface)
        : NULL;
    while (xdg_surface != NULL &&
            xdg_surface->role == WLR_XDG_SURFACE_ROLE_POPUP &&
            xdg_surface->popup != NULL &&
            xdg_surface->popup->parent != NULL) {
        xdg_surface = wlr_xdg_surface_try_from_wlr_surface(
            xdg_surface->popup->parent);
    }

    return xdg_surface;
}

static bool lumo_protocol_close_surface_app(
    struct lumo_compositor *compositor,
    struct wlr_surface *surface
) {
    struct wlr_xdg_surface *xdg_surface;

    if (compositor == NULL || surface == NULL) {
        return false;
    }

    xdg_surface = lumo_protocol_root_xdg_surface_from_surface(surface);
    if (xdg_surface != NULL &&
            xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL &&
            xdg_surface->toplevel != NULL) {
        wlr_log(WLR_INFO, "protocol: closing xdg toplevel %s",
            xdg_surface->toplevel->title != NULL
                ? xdg_surface->toplevel->title
                : "(unnamed)");
        wlr_xdg_toplevel_send_close(xdg_surface->toplevel);
        return true;
    }

    return lumo_xwayland_close_surface(compositor, surface);
}

void lumo_protocol_refresh_keyboard_visibility(
    struct lumo_compositor *compositor
) {
    bool visible = false;

    if (compositor == NULL || compositor->seat == NULL) {
        lumo_protocol_set_keyboard_visible(compositor, false);
        return;
    }

    /* check text-input-v3 enabled state */
    if (compositor->text_input_manager != NULL) {
        struct wl_resource *resource;
        wl_list_for_each(resource,
                &compositor->text_input_manager->text_inputs, link) {
            struct wlr_text_input_v3 *text_input =
                wl_resource_get_user_data(resource);

            if (text_input == NULL ||
                    text_input->seat != compositor->seat) {
                continue;
            }

            /* check both current_enabled (set after commit) and
             * pending_enabled (set after enable, before commit).
             * The pending flag covers the race where the client called
             * enable() but commit() was a no-op because focused_surface
             * wasn't set yet (enter event still in the Wayland queue) */
            if (text_input->current_enabled ||
                    text_input->pending_enabled) {
                visible = true;
                break;
            }
        }
    }

    /* keyboard is only visible when a client has explicitly enabled
     * text-input-v3 (meaning a text field is focused, not just any
     * toplevel).  Apps that want the OSK must implement text-input. */

    /* don't let refresh hide the keyboard when it was auto-shown
     * by app_id match — the text-input protocol race makes
     * current_enabled and pending_enabled unreliable.
     * BUT: if a text-input explicitly disabled (current_enabled went
     * false after being true), that's a real user action, so let it
     * override auto-show. */
    if (!visible && compositor->keyboard_auto_shown) {
        bool explicit_disable = false;
        if (compositor->text_input_manager != NULL) {
            struct wl_resource *resource;
            wl_list_for_each(resource,
                    &compositor->text_input_manager->text_inputs, link) {
                struct wlr_text_input_v3 *ti =
                    wl_resource_get_user_data(resource);
                if (ti != NULL && ti->seat == compositor->seat &&
                        !ti->current_enabled && !ti->pending_enabled &&
                        ti->focused_surface != NULL) {
                    explicit_disable = true;
                    break;
                }
            }
        }
        if (!explicit_disable) {
            return;
        }
        wlr_log(WLR_INFO,
            "protocol: explicit text-input disable overrides auto-show");
    }
    if (compositor->keyboard_visible && !visible) {
        wlr_log(WLR_INFO,
            "protocol: refresh_keyboard hiding kbd "
            "(text_enabled=%d focused=%p toplevels_empty=%d)",
            0, (void *)compositor->seat->keyboard_state.focused_surface,
            wl_list_empty(&compositor->toplevels));
    }
    lumo_protocol_set_keyboard_visible(compositor, visible);
}

bool lumo_protocol_close_focused_app(struct lumo_compositor *compositor) {
    struct wlr_surface *focused_surface = NULL;

    if (compositor == NULL || compositor->seat == NULL) {
        return false;
    }

    focused_surface = compositor->seat->keyboard_state.focused_surface;
    if (focused_surface == NULL) {
        focused_surface = compositor->seat->pointer_state.focused_surface;
    }

    if (focused_surface != NULL) {
        return lumo_protocol_close_surface_app(compositor, focused_surface);
    }

    /* fallback: if no surface is focused but toplevels exist,
     * close the most recently added one. This handles GTK apps
     * that lose focus during fullscreen transitions. */
    if (!wl_list_empty(&compositor->toplevels)) {
        struct lumo_toplevel *tl;
        wl_list_for_each(tl, &compositor->toplevels, link) {
            if (tl->xdg_toplevel != NULL) {
                wlr_log(WLR_INFO,
                    "protocol: closing unfocused toplevel %s",
                    tl->xdg_toplevel->title != NULL
                        ? tl->xdg_toplevel->title : "(unnamed)");
                wlr_xdg_toplevel_send_close(tl->xdg_toplevel);
                return true;
            }
        }
    }

    return false;
}

static void lumo_protocol_text_input_binding_destroy(
    struct lumo_text_input_binding *binding
) {
    if (binding == NULL) {
        return;
    }

    wl_list_remove(&binding->enable.link);
    wl_list_remove(&binding->commit.link);
    wl_list_remove(&binding->disable.link);
    wl_list_remove(&binding->destroy.link);
    wl_list_remove(&binding->link);
    free(binding);
}

static void lumo_protocol_text_input_enable(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_text_input_binding *binding =
        wl_container_of(listener, binding, enable);

    (void)data;
    if (binding != NULL) {
        wlr_log(WLR_INFO, "protocol: text_input_enable fired");
        lumo_protocol_refresh_keyboard_visibility(binding->compositor);
    }
}

static void lumo_protocol_text_input_commit(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_text_input_binding *binding =
        wl_container_of(listener, binding, commit);

    (void)data;
    if (binding != NULL) {
        wlr_log(WLR_INFO, "protocol: text_input_commit fired, enabled=%d",
            binding->text_input != NULL ?
                binding->text_input->current_enabled : -1);
        lumo_protocol_refresh_keyboard_visibility(binding->compositor);
    }
}

static void lumo_protocol_text_input_disable(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_text_input_binding *binding =
        wl_container_of(listener, binding, disable);

    (void)data;
    if (binding != NULL) {
        wlr_log(WLR_INFO, "protocol: text_input_disable fired");
        lumo_protocol_refresh_keyboard_visibility(binding->compositor);
    }
}

static void lumo_protocol_text_input_destroy(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_text_input_binding *binding =
        wl_container_of(listener, binding, destroy);
    struct lumo_compositor *compositor = binding != NULL ? binding->compositor : NULL;

    (void)data;
    lumo_protocol_text_input_binding_destroy(binding);
    lumo_protocol_refresh_keyboard_visibility(compositor);
}

static void lumo_protocol_new_text_input(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_protocol_state *state =
        wl_container_of(listener, state, text_input_new);
    struct lumo_text_input_binding *binding;
    struct wlr_text_input_v3 *text_input = data;

    if (state == NULL || state->compositor == NULL || text_input == NULL) {
        return;
    }

    binding = calloc(1, sizeof(*binding));
    if (binding == NULL) {
        wlr_log_errno(WLR_ERROR, "protocol: failed to track text-input");
        return;
    }

    binding->compositor = state->compositor;
    binding->text_input = text_input;
    binding->enable.notify = lumo_protocol_text_input_enable;
    binding->commit.notify = lumo_protocol_text_input_commit;
    binding->disable.notify = lumo_protocol_text_input_disable;
    binding->destroy.notify = lumo_protocol_text_input_destroy;

    wl_signal_add(&text_input->events.enable, &binding->enable);
    wl_signal_add(&text_input->events.commit, &binding->commit);
    wl_signal_add(&text_input->events.disable, &binding->disable);
    wl_signal_add(&text_input->events.destroy, &binding->destroy);
    wl_list_insert(&state->text_input_bindings, &binding->link);

    /* if a surface already has keyboard focus AND belongs to the same
     * Wayland client that created this text-input, send enter so it
     * can enable itself — the original enter was sent before this
     * text-input object existed */
    {
        struct wlr_surface *focused =
            state->compositor->seat != NULL
                ? state->compositor->seat->keyboard_state.focused_surface
                : NULL;
        if (focused != NULL &&
                wl_resource_get_client(text_input->resource) ==
                    wl_resource_get_client(focused->resource)) {
            wlr_text_input_v3_send_enter(text_input, focused);
            wlr_text_input_v3_send_done(text_input);
            wlr_log(WLR_INFO,
                "protocol: sent enter to new text-input for focused surface");
        }
    }
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

static bool lumo_protocol_layer_surface_layout_state_equal(
    const struct wlr_layer_surface_v1_state *left,
    const struct wlr_layer_surface_v1_state *right
);

bool lumo_protocol_layer_surface_commit_needs_reconfigure(
    const struct wlr_layer_surface_v1_state *previous,
    bool previous_valid,
    const struct wlr_layer_surface_v1_state *current,
    bool initialized
) {
    if (!initialized || !previous_valid) {
        return true;
    }

    return !lumo_protocol_layer_surface_layout_state_equal(previous, current);
}

static bool lumo_protocol_layer_surface_layout_state_equal(
    const struct wlr_layer_surface_v1_state *left,
    const struct wlr_layer_surface_v1_state *right
) {
    if (left == NULL || right == NULL) {
        return false;
    }

    return left->anchor == right->anchor &&
        left->exclusive_zone == right->exclusive_zone &&
        left->margin.top == right->margin.top &&
        left->margin.right == right->margin.right &&
        left->margin.bottom == right->margin.bottom &&
        left->margin.left == right->margin.left &&
        left->keyboard_interactive == right->keyboard_interactive &&
        left->desired_width == right->desired_width &&
        left->desired_height == right->desired_height &&
        left->layer == right->layer;
}

static bool lumo_protocol_layer_surface_layout_unchanged(
    const struct lumo_layer_surface *layer_surface,
    const struct lumo_output *output,
    const struct wlr_box *full_area,
    const struct wlr_box *usable_area
) {
    if (layer_surface == NULL || output == NULL || full_area == NULL ||
            usable_area == NULL || layer_surface->layer_surface == NULL ||
            !layer_surface->layout_snapshot_valid ||
            layer_surface->last_configured_output != output) {
        return false;
    }

    return wlr_box_equal(&layer_surface->last_full_area, full_area) &&
        wlr_box_equal(&layer_surface->last_usable_area, usable_area) &&
        lumo_protocol_layer_surface_layout_state_equal(
            &layer_surface->last_current_state,
            &layer_surface->layer_surface->current) &&
        lumo_protocol_layer_surface_layout_state_equal(
            &layer_surface->last_pending_state,
            &layer_surface->layer_surface->pending);
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
        wlr_log(WLR_INFO, "protocol: hitbox refresh skipped (no workarea)");
        return;
    }

    if (lumo_shell_surface_config_for_mode(LUMO_SHELL_MODE_GESTURE,
            (uint32_t)workarea.width, (uint32_t)workarea.height,
            &shell_config)) {
        /* Guard: shell_config.height > workarea.height would wrap rect.y */
        if (shell_config.height <= (uint32_t)workarea.height) {
            rect.x = workarea.x;
            rect.y = workarea.y + workarea.height - (int)shell_config.height;
            rect.width = workarea.width;
            rect.height = (int)shell_config.height;
            lumo_protocol_register_hitbox(compositor, "shell-gesture", &rect,
                LUMO_HITBOX_EDGE_GESTURE, true, true);
        }
    }

    if (lumo_shell_surface_config_for_mode(LUMO_SHELL_MODE_STATUS,
            (uint32_t)workarea.width, (uint32_t)workarea.height,
            &shell_config)) {
        rect.x = workarea.x;
        rect.y = 0;
        rect.width = workarea.width;
        rect.height = (int)shell_config.height;
        lumo_protocol_register_hitbox(compositor, "shell-edge-top", &rect,
            LUMO_HITBOX_EDGE_GESTURE, true, true);
    }

    if (compositor->keyboard_visible) {
        if (lumo_shell_surface_config_for_mode(LUMO_SHELL_MODE_OSK,
                (uint32_t)workarea.width, (uint32_t)workarea.height,
                &shell_config) &&
                shell_config.height <= (uint32_t)workarea.height) {
            rect.x = workarea.x;
            rect.y = workarea.y + workarea.height - (int)shell_config.height;
            rect.width = workarea.width;
            rect.height = (int)shell_config.height;
            lumo_protocol_register_hitbox(compositor, "shell-osk", &rect,
                LUMO_HITBOX_OSK_KEY, true, true);
            wlr_log(WLR_INFO,
                "protocol: registered shell-osk hitbox at %d,%d %dx%d",
                rect.x, rect.y, rect.width, rect.height);
        } else {
            wlr_log(WLR_INFO,
                "protocol: keyboard visible but osk hitbox NOT registered "
                "(workarea=%dx%d)",
                workarea.width, workarea.height);
        }
    }

    if (compositor->launcher_visible && !compositor->touch_audit_active &&
            lumo_shell_surface_config_for_mode(LUMO_SHELL_MODE_LAUNCHER,
                (uint32_t)workarea.width, (uint32_t)workarea.height,
                &shell_config) &&
            lumo_shell_launcher_panel_rect((uint32_t)workarea.width,
                (uint32_t)workarea.height, &rect)) {
        rect.x += workarea.x;
        rect.y += workarea.y;
        lumo_protocol_register_hitbox(compositor, "shell-launcher", &rect,
            LUMO_HITBOX_SCRIM, true, true);
    }
}

static void lumo_protocol_teardown_toplevel(struct lumo_toplevel *toplevel) {
    if (toplevel == NULL) {
        return;
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
    struct lumo_compositor *compositor =
        layer_surface != NULL ? layer_surface->compositor : NULL;
    (void)data;
    lumo_protocol_teardown_layer_surface(layer_surface);

    if (compositor != NULL && compositor->protocol_started) {
        lumo_protocol_mark_layers_dirty(compositor);
    }
}

static void lumo_protocol_layer_surface_commit(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_layer_surface *layer_surface =
        wl_container_of(listener, layer_surface, commit);
    struct wlr_layer_surface_v1 *wlr_layer_surface;

    (void)data;
    if (layer_surface == NULL || layer_surface->compositor == NULL ||
            !layer_surface->compositor->protocol_started) {
        return;
    }

    wlr_layer_surface = layer_surface->layer_surface;
    if (wlr_layer_surface == NULL) {
        return;
    }

    if (lumo_protocol_layer_surface_commit_needs_reconfigure(
            &layer_surface->last_committed_state,
            layer_surface->commit_snapshot_valid,
            &wlr_layer_surface->current,
            wlr_layer_surface->initialized)) {
        lumo_protocol_mark_layers_dirty(layer_surface->compositor);
    }

    layer_surface->last_committed_state = wlr_layer_surface->current;
    layer_surface->commit_snapshot_valid = true;
}

static void lumo_protocol_new_toplevel(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_compositor *compositor = lumo_protocol_listener_compositor(
        listener,
        LUMO_PROTOCOL_LISTENER_XDG_TOPLEVEL
    );
    struct wlr_xdg_toplevel *xdg_toplevel = data;
    struct lumo_toplevel *toplevel = calloc(1, sizeof(*toplevel));

    if (compositor == NULL) {
        wlr_log(WLR_ERROR, "protocol: missing compositor for new toplevel");
        free(toplevel);
        return;
    }

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
    struct lumo_compositor *compositor = lumo_protocol_listener_compositor(
        listener,
        LUMO_PROTOCOL_LISTENER_XDG_POPUP
    );
    struct wlr_xdg_popup *popup_surface = data;
    struct lumo_popup *popup = calloc(1, sizeof(*popup));

    if (compositor == NULL) {
        wlr_log(WLR_ERROR, "protocol: missing compositor for new popup");
        free(popup);
        return;
    }

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
    struct lumo_compositor *compositor = lumo_protocol_listener_compositor(
        listener,
        LUMO_PROTOCOL_LISTENER_LAYER_SURFACE
    );
    struct wlr_layer_surface_v1 *layer_surface = data;
    struct lumo_layer_surface *surface = calloc(1, sizeof(*surface));

    if (compositor == NULL) {
        wlr_log(WLR_ERROR, "protocol: missing compositor for new layer surface");
        free(surface);
        return;
    }

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

    /* disable the OSK scene node at creation so its bootstrap 1px
     * surface does not intercept wlr_scene_node_at; it will be
     * enabled when keyboard_visible becomes true */
    if (layer_surface->namespace != NULL &&
            strcmp(layer_surface->namespace, "osk") == 0 &&
            surface->scene_surface->tree != NULL) {
        wlr_scene_node_set_enabled(
            &surface->scene_surface->tree->node, false);
        wlr_log(WLR_INFO, "protocol: osk scene node disabled at creation");
    }

    surface->commit.notify = lumo_protocol_layer_surface_commit;
    wl_signal_add(&layer_surface->surface->events.commit, &surface->commit);
    surface->destroy.notify = lumo_protocol_layer_surface_destroy;
    wl_signal_add(&layer_surface->events.destroy, &surface->destroy);

    wl_list_insert(&compositor->layer_surfaces, &surface->link);
    lumo_protocol_mark_layers_dirty(compositor);
    wlr_log(WLR_INFO, "protocol: new layer surface '%s'",
        layer_surface->namespace != NULL ? layer_surface->namespace : "");
}

static void lumo_protocol_configure_layer_surface_for_output(
    struct lumo_layer_surface *layer_surface,
    struct lumo_output *output,
    const struct wlr_box *full_area,
    struct wlr_box *usable_area
) {
    struct wlr_box incoming_usable_area;

    if (layer_surface == NULL || output == NULL || full_area == NULL ||
        usable_area == NULL || layer_surface->scene_surface == NULL ||
        layer_surface->layer_surface == NULL ||
        layer_surface->layer_surface->output != output->wlr_output) {
        return;
    }

    incoming_usable_area = *usable_area;
    if (lumo_protocol_layer_surface_layout_unchanged(layer_surface, output,
            full_area, &incoming_usable_area)) {
        return;
    }

    wlr_scene_layer_surface_v1_configure(layer_surface->scene_surface,
        full_area, usable_area);
    layer_surface->last_configured_output = output;
    layer_surface->layout_snapshot_valid = true;
    layer_surface->last_full_area = *full_area;
    layer_surface->last_usable_area = incoming_usable_area;
    layer_surface->last_current_state = layer_surface->layer_surface->current;
    layer_surface->last_pending_state = layer_surface->layer_surface->pending;
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

    state->compositor = compositor;
    wl_list_init(&state->text_input_bindings);

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

    compositor->screencopy_manager = wlr_screencopy_manager_v1_create(
        compositor->display
    );
    if (compositor->screencopy_manager == NULL) {
        wlr_log(WLR_ERROR, "protocol: failed to create screencopy manager");
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
    state->text_input_new.notify = lumo_protocol_new_text_input;
    wl_signal_add(&compositor->text_input_manager->events.text_input,
        &state->text_input_new);

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
    struct lumo_text_input_binding *binding, *binding_tmp;

    lumo_protocol_clear_shell_hitboxes(compositor);
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
        wl_list_for_each_safe(binding, binding_tmp, &state->text_input_bindings, link) {
            lumo_protocol_text_input_binding_destroy(binding);
        }
        wl_list_remove(&state->xdg_new_toplevel.link);
        wl_list_remove(&state->xdg_new_popup.link);
        wl_list_remove(&state->layer_new_surface.link);
        wl_list_remove(&state->text_input_new.link);
        free(state);
        compositor->protocol_state = NULL;
    }

    compositor->xdg_shell = NULL;
    compositor->layer_shell = NULL;
    compositor->text_input_manager = NULL;
    compositor->input_method_manager = NULL;
    compositor->virtual_keyboard_manager = NULL;
    compositor->pointer_gestures = NULL;
    compositor->screencopy_manager = NULL;
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
    lumo_shell_state_broadcast_gesture_threshold(compositor,
        compositor->gesture_threshold,
        compositor->gesture_timeout_ms);
}

void lumo_protocol_set_launcher_visible(
    struct lumo_compositor *compositor,
    bool visible
) {
    if (compositor == NULL) {
        return;
    }

    if (compositor->launcher_visible == visible) {
        return;
    }

    compositor->launcher_visible = visible;
    /* clear search query when drawer closes */
    if (!visible) {
        compositor->toast_message[0] = '\0';
    }
    /* hide keyboard when launcher opens (unless search activates it) */
    if (visible && compositor->keyboard_visible) {
        lumo_protocol_set_keyboard_visible(compositor, false);
    }
    if (visible) {
        compositor->scrim_state = LUMO_SCRIM_MODAL;
    } else if (!compositor->keyboard_visible) {
        compositor->scrim_state = LUMO_SCRIM_HIDDEN;
    }

    wlr_log(WLR_INFO, "protocol: launcher %s", visible ? "visible" : "hidden");
    lumo_shell_state_broadcast_launcher_visible(compositor, visible);
    lumo_shell_state_broadcast_scrim_state(compositor, compositor->scrim_state);
    lumo_protocol_refresh_shell_hitboxes(compositor);
    compositor->hitboxes_dirty = false;
    lumo_protocol_mark_layers_dirty(compositor);
}

void lumo_protocol_set_quick_settings_visible(
    struct lumo_compositor *compositor,
    bool visible
) {
    if (compositor == NULL) {
        return;
    }

    if (compositor->quick_settings_visible == visible) {
        return;
    }

    compositor->quick_settings_visible = visible;
    wlr_log(WLR_INFO, "protocol: quick_settings %s",
        visible ? "visible" : "hidden");
    lumo_shell_state_broadcast_launcher_visible(compositor,
        compositor->launcher_visible);
    lumo_protocol_mark_layers_dirty(compositor);
}

void lumo_protocol_set_time_panel_visible(
    struct lumo_compositor *compositor,
    bool visible
) {
    if (compositor == NULL || compositor->time_panel_visible == visible) {
        return;
    }

    compositor->time_panel_visible = visible;
    wlr_log(WLR_INFO, "protocol: time_panel %s",
        visible ? "visible" : "hidden");
    lumo_shell_state_broadcast_launcher_visible(compositor,
        compositor->launcher_visible);
    lumo_protocol_mark_layers_dirty(compositor);
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
    lumo_shell_state_broadcast_scrim_state(compositor, state);
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
    if (!visible) {
        compositor->keyboard_auto_shown = false;
    }
    compositor->keyboard_resize_serial++;
    compositor->keyboard_resize_pending = visible;
    compositor->keyboard_resize_acked = !visible;

    /* toggle the OSK scene node visibility so the bootstrap 1px surface
     * doesn't intercept wlr_scene_node_at when the keyboard is hidden */
    {
        struct lumo_layer_surface *ls;
        wl_list_for_each(ls, &compositor->layer_surfaces, link) {
            if (ls->layer_surface == NULL || ls->scene_surface == NULL ||
                    ls->scene_surface->tree == NULL) {
                continue;
            }
            const char *ns = ls->layer_surface->namespace;
            if (ns != NULL && strcmp(ns, "osk") == 0) {
                wlr_scene_node_set_enabled(
                    &ls->scene_surface->tree->node, visible);
                if (visible) {
                    wlr_scene_node_raise_to_top(
                        &ls->scene_surface->tree->node);
                }
            }
        }
    }

    if (visible) {
        compositor->scrim_state = LUMO_SCRIM_DIMMED;
    } else if (!compositor->launcher_visible) {
        compositor->scrim_state = LUMO_SCRIM_HIDDEN;
    }

    wlr_log(WLR_INFO, "protocol: keyboard %s serial=%u",
        visible ? "visible" : "hidden", compositor->keyboard_resize_serial);
    lumo_shell_state_broadcast_keyboard_visible(compositor, visible);
    lumo_shell_state_broadcast_scrim_state(compositor, compositor->scrim_state);
    lumo_protocol_refresh_shell_hitboxes(compositor);
    compositor->hitboxes_dirty = false;
    lumo_protocol_mark_layers_dirty(compositor);
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

    if (compositor->hitboxes_dirty) {
        lumo_protocol_refresh_shell_hitboxes(compositor);
        compositor->hitboxes_dirty = false;
    }

    /* wlr_scene_layer_surface_v1_configure re-enables nodes based on
     * surface->mapped.  Re-disable the OSK node when the keyboard is
     * hidden so its 1px bootstrap surface doesn't intercept touches. */
    if (!compositor->keyboard_visible) {
        wl_list_for_each_safe(layer_surface, tmp,
                &compositor->layer_surfaces, link) {
            if (layer_surface->layer_surface != NULL &&
                    layer_surface->layer_surface->namespace != NULL &&
                    strcmp(layer_surface->layer_surface->namespace, "osk") == 0 &&
                    layer_surface->scene_surface != NULL &&
                    layer_surface->scene_surface->tree != NULL) {
                wlr_scene_node_set_enabled(
                    &layer_surface->scene_surface->tree->node, false);
            }
        }
    }
}

void lumo_protocol_configure_all_layers(struct lumo_compositor *compositor) {
    struct lumo_output *output;

    if (compositor == NULL) {
        return;
    }

    if (wl_list_empty(&compositor->outputs)) {
        compositor->layer_config_dirty = true;
        return;
    }

    compositor->layer_config_dirty = false;
    wl_list_for_each(output, &compositor->outputs, link) {
        lumo_protocol_configure_layers(compositor, output);
    }
}
