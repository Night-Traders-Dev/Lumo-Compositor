/*
 * protocol_setters.c — UI-state setter functions and layer configuration.
 * Split from protocol.c for maintainability.
 */

#include "protocol_internal.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

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

void lumo_protocol_set_notification_panel_visible(
    struct lumo_compositor *compositor,
    bool visible
) {
    if (compositor == NULL ||
            compositor->notification_panel_visible == visible) {
        return;
    }

    compositor->notification_panel_visible = visible;
    wlr_log(WLR_INFO, "protocol: notification_panel %s",
        visible ? "visible" : "hidden");
    lumo_shell_state_broadcast_launcher_visible(compositor,
        compositor->launcher_visible);
    lumo_protocol_mark_layers_dirty(compositor);
}

void lumo_protocol_push_notification(
    struct lumo_compositor *compositor,
    const char *text
) {
    if (compositor == NULL || text == NULL) return;
    int idx = compositor->notification_count;
    if (idx >= 8) {
        /* shift oldest out */
        for (int i = 0; i < 7; i++) {
            memcpy(compositor->notifications[i],
                compositor->notifications[i + 1], 128);
            compositor->notification_timestamps[i] =
                compositor->notification_timestamps[i + 1];
        }
        idx = 7;
    } else {
        compositor->notification_count++;
    }
    snprintf(compositor->notifications[idx], 128, "%s", text);
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        compositor->notification_timestamps[idx] =
            (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
    }
    wlr_log(WLR_INFO, "protocol: notification pushed: %s", text);
    lumo_shell_state_broadcast_launcher_visible(compositor,
        compositor->launcher_visible);
    lumo_protocol_mark_layers_dirty(compositor);
}

static int lumo_sidebar_auto_hide_timeout(void *data) {
    struct lumo_compositor *compositor = data;
    if (compositor != NULL && compositor->sidebar_visible) {
        wlr_log(WLR_INFO, "protocol: sidebar auto-hide timer fired");
        /* clear the timer pointer BEFORE calling the setter, because
         * the setter tries to remove the timer */
        compositor->sidebar_auto_hide_timer = NULL;
        lumo_protocol_set_sidebar_visible(compositor, false);
        return 0;
    }
    if (compositor != NULL)
        compositor->sidebar_auto_hide_timer = NULL;
    return 0;
}

void lumo_protocol_set_sidebar_visible(
    struct lumo_compositor *compositor, bool visible
) {
    if (compositor == NULL || compositor->sidebar_visible == visible)
        return;
    compositor->sidebar_visible = visible;
    wlr_log(WLR_INFO, "protocol: sidebar %s", visible ? "visible" : "hidden");

    /* cancel any existing auto-hide timer */
    if (compositor->sidebar_auto_hide_timer != NULL) {
        wl_event_source_remove(compositor->sidebar_auto_hide_timer);
        compositor->sidebar_auto_hide_timer = NULL;
    }

    /* start 10-second auto-hide timer when sidebar becomes visible */
    if (visible && compositor->event_loop != NULL) {
        compositor->sidebar_auto_hide_timer = wl_event_loop_add_timer(
            compositor->event_loop, lumo_sidebar_auto_hide_timeout,
            compositor);
        if (compositor->sidebar_auto_hide_timer != NULL)
            wl_event_source_timer_update(
                compositor->sidebar_auto_hide_timer, 10000);
    }

    /* broadcast state so shell shows/hides the sidebar surface */
    lumo_shell_state_broadcast_launcher_visible(compositor,
        compositor->launcher_visible);
    /* re-arrange layers so the sidebar surface gets correct scene position */
    lumo_protocol_mark_layers_dirty(compositor);
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
        compositor->osk_shift_active = false;
        lumo_shell_osk_set_page(0);
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

    lumo_protocol_refresh_shell_hitboxes(compositor);

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
