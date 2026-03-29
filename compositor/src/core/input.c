#include "lumo/compositor.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/util/box.h>
#include <wlr/util/transform.h>

struct lumo_scene_object_head {
    struct wl_list link;
    struct lumo_compositor *compositor;
    enum lumo_scene_object_role role;
};

struct lumo_input_state {
    struct wl_event_source *gesture_timer;
};

struct lumo_input_device {
    struct wl_list link;
    struct lumo_compositor *compositor;
    struct wlr_input_device *device;
    enum wlr_input_device_type type;
    struct wl_listener destroy;
};

struct lumo_surface_target {
    struct wlr_surface *surface;
    struct lumo_scene_object_head *object;
    enum lumo_scene_object_role role;
    double sx;
    double sy;
};

static struct lumo_input_state *lumo_input_state_from(
    struct lumo_compositor *compositor
) {
    return compositor != NULL
        ? (struct lumo_input_state *)compositor->input_state
        : NULL;
}

static uint32_t lumo_input_now_msec(void) {
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint32_t)(now.tv_sec * 1000u + now.tv_nsec / 1000000u);
}

static struct lumo_output *lumo_input_first_output(
    struct lumo_compositor *compositor
) {
    struct lumo_output *output;

    if (compositor == NULL || wl_list_empty(&compositor->outputs)) {
        return NULL;
    }

    output = wl_container_of(compositor->outputs.next, output, link);
    return output;
}

static struct lumo_output *lumo_input_output_from_wlr(
    struct lumo_compositor *compositor,
    struct wlr_output *wlr_output
) {
    struct lumo_output *output;

    if (compositor == NULL || wlr_output == NULL) {
        return NULL;
    }

    wl_list_for_each(output, &compositor->outputs, link) {
        if (output->wlr_output == wlr_output) {
            return output;
        }
    }

    return NULL;
}

static struct lumo_output *lumo_input_output_for_layout_coords(
    struct lumo_compositor *compositor,
    double lx,
    double ly
) {
    struct wlr_output *wlr_output;

    if (compositor == NULL || compositor->output_layout == NULL) {
        return lumo_input_first_output(compositor);
    }

    wlr_output = wlr_output_layout_output_at(compositor->output_layout, lx, ly);
    if (wlr_output != NULL) {
        return lumo_input_output_from_wlr(compositor, wlr_output);
    }

    return lumo_input_first_output(compositor);
}

static bool lumo_input_transform_touch_coords(
    struct lumo_compositor *compositor,
    struct wlr_input_device *device,
    double raw_x,
    double raw_y,
    double *lx,
    double *ly,
    struct lumo_output **output_out
) {
    double mapped_x = raw_x;
    double mapped_y = raw_y;
    struct lumo_output *output;
    struct wlr_box box = {0};

    if (compositor == NULL || lx == NULL || ly == NULL) {
        return false;
    }

    if (compositor->cursor != NULL && compositor->output_layout != NULL) {
        wlr_cursor_absolute_to_layout_coords(compositor->cursor, device,
            raw_x, raw_y, &mapped_x, &mapped_y);
    }

    output = lumo_input_output_for_layout_coords(compositor, mapped_x, mapped_y);
    if (output != NULL && output->wlr_output != NULL &&
            output->wlr_output->transform != WL_OUTPUT_TRANSFORM_NORMAL) {
        wlr_output_layout_get_box(compositor->output_layout, output->wlr_output,
            &box);
        if (!wlr_box_empty(&box) && box.width > 0 && box.height > 0) {
            double norm_x = (mapped_x - box.x) / box.width;
            double norm_y = (mapped_y - box.y) / box.height;
            double out_x = norm_x;
            double out_y = norm_y;

            switch (output->wlr_output->transform) {
            case WL_OUTPUT_TRANSFORM_90:
                out_x = 1.0 - norm_y;
                out_y = norm_x;
                break;
            case WL_OUTPUT_TRANSFORM_180:
                out_x = 1.0 - norm_x;
                out_y = 1.0 - norm_y;
                break;
            case WL_OUTPUT_TRANSFORM_270:
                out_x = norm_y;
                out_y = 1.0 - norm_x;
                break;
            default:
                break;
            }

            mapped_x = box.x + out_x * box.width;
            mapped_y = box.y + out_y * box.height;
        }
    }

    *lx = mapped_x;
    *ly = mapped_y;

    if (output_out != NULL) {
        *output_out = output;
    }

    return true;
}

static struct lumo_scene_object_head *lumo_input_scene_object_from_node(
    struct wlr_scene_node *node
) {
    while (node != NULL) {
        if (node->data != NULL) {
            return node->data;
        }

        node = node->parent != NULL ? &node->parent->node : NULL;
    }

    return NULL;
}

static bool lumo_input_surface_target_at(
    struct lumo_compositor *compositor,
    double lx,
    double ly,
    struct lumo_surface_target *target
) {
    struct wlr_scene_node *node;
    struct wlr_scene_surface *scene_surface = NULL;
    double sx = 0.0;
    double sy = 0.0;

    if (target != NULL) {
        memset(target, 0, sizeof(*target));
    }

    if (compositor == NULL || compositor->scene == NULL || target == NULL) {
        return false;
    }

    node = wlr_scene_node_at(&compositor->scene->tree.node, lx, ly, &sx, &sy);
    if (node == NULL) {
        return false;
    }

    scene_surface = lumo_scene_surface_from_node(node, NULL);
    if (scene_surface != NULL) {
        target->surface = scene_surface->surface;
        target->sx = sx;
        target->sy = sy;
    }

    target->object = lumo_input_scene_object_from_node(node);
    if (target->object != NULL) {
        target->role = target->object->role;
        if (target->surface == NULL) {
            switch (target->role) {
            case LUMO_SCENE_OBJECT_TOPLEVEL: {
                struct lumo_toplevel *toplevel =
                    (struct lumo_toplevel *)target->object;
                if (toplevel->xdg_surface != NULL) {
                    target->surface = toplevel->xdg_surface->surface;
                }
                break;
            }
            case LUMO_SCENE_OBJECT_POPUP: {
                struct lumo_popup *popup = (struct lumo_popup *)target->object;
                if (popup->xdg_popup != NULL && popup->xdg_popup->base != NULL) {
                    target->surface = popup->xdg_popup->base->surface;
                }
                break;
            }
            case LUMO_SCENE_OBJECT_LAYER_SURFACE: {
                struct lumo_layer_surface *layer_surface =
                    (struct lumo_layer_surface *)target->object;
                if (layer_surface->layer_surface != NULL) {
                    target->surface = layer_surface->layer_surface->surface;
                }
                break;
            }
            }
        }
    }

    if (target->surface != NULL && scene_surface == NULL) {
        target->sx = sx;
        target->sy = sy;
    }

    return target->surface != NULL;
}

static bool lumo_input_target_is_shell(const struct lumo_surface_target *target) {
    if (target == NULL) {
        return false;
    }

    return target->role == LUMO_SCENE_OBJECT_LAYER_SURFACE ||
        target->role == LUMO_SCENE_OBJECT_POPUP;
}

static bool lumo_input_hitbox_is_shell_reserved(
    const struct lumo_hitbox *hitbox
) {
    if (hitbox == NULL) {
        return false;
    }

    switch (hitbox->kind) {
    case LUMO_HITBOX_LAUNCHER_TILE:
    case LUMO_HITBOX_OSK_KEY:
    case LUMO_HITBOX_SCRIM:
        return true;
    case LUMO_HITBOX_EDGE_GESTURE:
    case LUMO_HITBOX_CUSTOM:
    default:
        return false;
    }
}

static enum lumo_edge_zone lumo_input_system_edge_zone(
    struct lumo_compositor *compositor,
    const struct lumo_output *output,
    double lx,
    double ly
) {
    struct wlr_box box = {0};
    double threshold;

    if (compositor == NULL || output == NULL || output->wlr_output == NULL) {
        return LUMO_EDGE_NONE;
    }

    wlr_output_layout_get_box(compositor->output_layout, output->wlr_output,
        &box);
    if (wlr_box_empty(&box)) {
        return LUMO_EDGE_NONE;
    }

    threshold = compositor->gesture_threshold > 0.0
        ? compositor->gesture_threshold
        : 24.0;
    return lumo_edge_zone_in_box(&box, lx, ly, threshold);
}

static void lumo_input_touch_audit_log(
    struct lumo_compositor *compositor,
    const struct lumo_touch_point *point,
    const struct lumo_output *output,
    const struct lumo_surface_target *target,
    double raw_x,
    double raw_y
) {
    struct wlr_box box = {0};
    const char *region = "unknown";
    const char *target_name = "none";
    const char *hitbox_name = "none";
    const char *output_name = "(none)";
    double logical_x_pct = 0.0;
    double logical_y_pct = 0.0;
    double threshold = 24.0;

    if (compositor == NULL || point == NULL || output == NULL ||
            output->wlr_output == NULL || compositor->output_layout == NULL) {
        return;
    }

    wlr_output_layout_get_box(compositor->output_layout, output->wlr_output, &box);
    if (wlr_box_empty(&box)) {
        return;
    }

    threshold = compositor->gesture_threshold > 0.0
        ? compositor->gesture_threshold
        : 24.0;
    logical_x_pct = ((point->lx - box.x) / box.width) * 100.0;
    logical_y_pct = ((point->ly - box.y) / box.height) * 100.0;
    region = lumo_touch_region_name_in_box(&box, point->lx, point->ly, threshold);
    output_name = output->wlr_output->name != NULL
        ? output->wlr_output->name
        : "(unnamed)";

    if (point->hitbox != NULL) {
        target_name = "hitbox";
        hitbox_name = point->hitbox->name != NULL
            ? point->hitbox->name
            : lumo_hitbox_kind_name(point->hitbox->kind);
    } else if (target != NULL && lumo_input_target_is_shell(target)) {
        target_name = "shell-surface";
    } else if (target != NULL && target->surface != NULL) {
        target_name = "app-surface";
    }

    wlr_log(WLR_INFO,
        "input: touch %d audit output=%s raw=%.1f%%,%.1f%% logical=%.1f%%,%.1f%% region=%s target=%s hitbox=%s",
        point->touch_id, output_name, raw_x * 100.0, raw_y * 100.0,
        logical_x_pct, logical_y_pct, region, target_name, hitbox_name);
}

static struct lumo_touch_point *lumo_input_touch_point_for_id(
    struct lumo_compositor *compositor,
    int32_t touch_id
) {
    struct lumo_touch_point *point;

    if (compositor == NULL) {
        return NULL;
    }

    wl_list_for_each(point, &compositor->touch_points, link) {
        if (point->touch_id == touch_id) {
            return point;
        }
    }

    return NULL;
}

static void lumo_input_touch_sample_append(
    struct lumo_touch_point *point,
    enum lumo_touch_sample_type type,
    uint32_t time_msec,
    double lx,
    double ly,
    double sx,
    double sy
) {
    struct lumo_touch_sample *sample;

    if (point == NULL) {
        return;
    }

    sample = calloc(1, sizeof(*sample));
    if (sample == NULL) {
        wlr_log_errno(WLR_ERROR, "input: failed to allocate touch sample");
        return;
    }

    sample->type = type;
    sample->time_msec = time_msec;
    sample->lx = lx;
    sample->ly = ly;
    sample->sx = sx;
    sample->sy = sy;
    wl_list_insert(point->samples.prev, &sample->link);
}

static void lumo_input_touch_debug_update(
    struct lumo_compositor *compositor,
    const struct lumo_touch_point *point,
    enum lumo_touch_sample_type phase,
    bool active,
    double lx,
    double ly
) {
    if (compositor == NULL) {
        return;
    }

    compositor->touch_debug_active = active;
    compositor->touch_debug_id = point != NULL ? point->touch_id : -1;
    compositor->touch_debug_lx = lx;
    compositor->touch_debug_ly = ly;
    compositor->touch_debug_phase = phase;
    compositor->touch_debug_target =
        point != NULL ? point->kind : LUMO_TOUCH_TARGET_NONE;
    compositor->touch_debug_hitbox_kind =
        point != NULL && point->hitbox != NULL
            ? point->hitbox->kind
            : LUMO_HITBOX_CUSTOM;
    lumo_shell_state_broadcast_touch_debug(compositor);
}

static void lumo_input_touch_samples_clear(struct lumo_touch_point *point) {
    struct lumo_touch_sample *sample, *tmp;

    if (point == NULL) {
        return;
    }

    wl_list_for_each_safe(sample, tmp, &point->samples, link) {
        wl_list_remove(&sample->link);
        free(sample);
    }
}

static void lumo_input_touch_point_surface_destroy(
    struct wl_listener *listener,
    void *data
);
static int lumo_input_gesture_timeout_cb(void *data);

static void lumo_input_touch_point_bind_surface(
    struct lumo_touch_point *point,
    struct wlr_surface *surface
) {
    if (point == NULL) {
        return;
    }

    if (point->surface_destroy_active) {
        wl_list_remove(&point->surface_destroy.link);
        point->surface_destroy_active = false;
    }

    point->surface = surface;
    if (surface == NULL) {
        return;
    }

    point->surface_destroy.notify = lumo_input_touch_point_surface_destroy;
    wl_signal_add(&surface->events.destroy, &point->surface_destroy);
    point->surface_destroy_active = true;
}

static void lumo_input_touch_point_destroy(struct lumo_touch_point *point) {
    if (point == NULL) {
        return;
    }

    if (point->surface_destroy_active) {
        wl_list_remove(&point->surface_destroy.link);
        point->surface_destroy_active = false;
    }
    lumo_input_touch_samples_clear(point);
    wl_list_remove(&point->link);
    free(point);
}

static void lumo_input_focus_surface(
    struct lumo_compositor *compositor,
    struct wlr_surface *surface
) {
    struct wlr_keyboard *keyboard;
    const uint32_t *keycodes = NULL;
    size_t num_keycodes = 0;
    struct wlr_keyboard_modifiers modifiers = {0};

    if (compositor == NULL || compositor->seat == NULL) {
        return;
    }

    keyboard = wlr_seat_get_keyboard(compositor->seat);
    if (keyboard != NULL) {
        keycodes = keyboard->keycodes;
        num_keycodes = keyboard->num_keycodes;
        modifiers = keyboard->modifiers;
    }

    if (surface != NULL) {
        lumo_xwayland_focus_surface(compositor, surface);
        wlr_seat_keyboard_notify_enter(compositor->seat, surface,
            keycodes, num_keycodes, &modifiers);

        if (!wl_list_empty(&compositor->toplevels) &&
                !compositor->launcher_visible) {
            struct lumo_toplevel *tl;
            bool matched = false;
            wl_list_for_each(tl, &compositor->toplevels, link) {
                if (tl->xdg_surface != NULL &&
                        tl->xdg_surface->surface == surface) {
                    matched = true;
                    /* show keyboard for apps that have text fields
                     * (messages=terminal, notes). Check xdg app_id. */
                    if (!compositor->keyboard_visible &&
                            tl->xdg_toplevel != NULL &&
                            tl->xdg_toplevel->app_id != NULL &&
                            (strstr(tl->xdg_toplevel->app_id, "messages") ||
                                strstr(tl->xdg_toplevel->app_id, "notes"))) {
                        wlr_log(WLR_INFO,
                            "input: auto-show keyboard for %s",
                            tl->xdg_toplevel->app_id);
                        compositor->keyboard_auto_shown = true;
                        lumo_protocol_set_keyboard_visible(compositor, true);
                    }
                    break;
                }
            }
            if (!matched) {
                wlr_log(WLR_INFO,
                    "input: focus_surface surface not in toplevels list");
            }
        } else {
            wlr_log(WLR_INFO,
                "input: focus_surface skipped kbd check "
                "(toplevels_empty=%d launcher=%d)",
                wl_list_empty(&compositor->toplevels),
                compositor->launcher_visible);
        }
    } else {
        wlr_seat_keyboard_notify_clear_focus(compositor->seat);
        if (compositor->keyboard_visible) {
            wlr_log(WLR_INFO,
                "input: focus_surface(NULL) hiding keyboard");
            lumo_protocol_set_keyboard_visible(compositor, false);
        }
    }

    if (compositor->text_input_manager != NULL) {
        struct wl_resource *resource;
        int ti_count = 0;

        wl_list_for_each(resource, &compositor->text_input_manager->text_inputs,
                link) {
            ti_count++;
            struct wlr_text_input_v3 *text_input =
                wl_resource_get_user_data(resource);

            if (text_input == NULL || text_input->seat != compositor->seat) {
                continue;
            }

            if (surface != NULL) {
                /* only send enter if the text-input belongs to the
                 * same client as the focused surface */
                if (wl_resource_get_client(text_input->resource) !=
                        wl_resource_get_client(surface->resource)) {
                    continue;
                }
                if (text_input->focused_surface == surface) {
                    continue;
                }

                if (text_input->focused_surface != NULL) {
                    wlr_text_input_v3_send_leave(text_input);
                }
                wlr_text_input_v3_send_enter(text_input, surface);
                wlr_text_input_v3_send_done(text_input);
                wlr_log(WLR_INFO,
                    "input: sent text-input enter for focused surface");
                continue;
            }

            if (text_input->focused_surface != NULL) {
                wlr_text_input_v3_send_leave(text_input);
                wlr_text_input_v3_send_done(text_input);
            }
        }
        if (surface != NULL && ti_count == 0) {
            wlr_log(WLR_INFO,
                "input: focus_surface no text-inputs registered");
        }
    }

    /* Check if the text-input was already enabled before we sent enter.
     * This handles the case where the app called enable()+commit() on
     * touch BEFORE the enter event was delivered — the focused_surface
     * was set by our send_enter above, and current_enabled may now be
     * valid. We give the client one more frame to process events. */
    /* Note: the keyboard will only show if current_enabled is true AND
     * focused_surface matches. If the client hasn't processed enter yet,
     * the commit listener will call refresh when it does.
     * refresh would immediately undo the show. */
}

static void lumo_input_refresh_capabilities(struct lumo_compositor *compositor) {
    uint32_t caps = 0;

    if (compositor == NULL || compositor->seat == NULL) {
        return;
    }

    if (compositor->pointer_devices > 0) {
        caps |= WL_SEAT_CAPABILITY_POINTER;
    }
    if (compositor->keyboard_devices > 0) {
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    }
    if (compositor->touch_devices > 0) {
        caps |= WL_SEAT_CAPABILITY_TOUCH;
    }

    wlr_seat_set_capabilities(compositor->seat, caps);
}

static bool lumo_input_touch_point_surface_destroyed_cb(
    struct lumo_compositor *compositor,
    struct lumo_touch_point *point,
    uint32_t time_msec
) {
    struct wlr_touch_point *seat_point;

    if (compositor == NULL || compositor->seat == NULL || point == NULL) {
        return false;
    }

    seat_point = wlr_seat_touch_get_point(compositor->seat, point->touch_id);
    if (seat_point != NULL) {
        wlr_seat_touch_notify_cancel(compositor->seat, seat_point->client);
    }

    if (point->surface_destroy_active) {
        wl_list_remove(&point->surface_destroy.link);
        point->surface_destroy_active = false;
    }

    point->surface = NULL;
    wlr_log(WLR_INFO, "input: touch %d surface destroyed at %u", point->touch_id,
        time_msec);
    return true;
}

static void lumo_input_touch_point_surface_destroy(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_touch_point *point =
        wl_container_of(listener, point, surface_destroy);
    struct lumo_compositor *compositor =
        point != NULL ? point->owner : NULL;
    uint32_t time_msec = lumo_input_now_msec();

    (void)data;
    if (point == NULL) {
        return;
    }

    if (point->delivered) {
        lumo_input_touch_point_surface_destroyed_cb(compositor, point, time_msec);
        lumo_input_touch_point_destroy(point);
        return;
    }

    lumo_input_touch_point_surface_destroyed_cb(compositor, point, time_msec);
    /* Always destroy the point when its surface is gone — even if captured
     * but not yet delivered — to prevent a zombie point with surface=NULL. */
    lumo_input_touch_point_destroy(point);
}

static void lumo_input_remove_touch_point(
    struct lumo_compositor *compositor,
    struct lumo_touch_point *point
) {
    if (compositor == NULL || point == NULL) {
        return;
    }

    lumo_input_touch_point_destroy(point);
}

static void lumo_input_replay_touch_point(
    struct lumo_compositor *compositor,
    struct lumo_touch_point *point
) {
    struct lumo_touch_sample *sample;

    if (compositor == NULL || compositor->seat == NULL || point == NULL ||
            point->surface == NULL || point->delivered) {
        return;
    }

    /* if the bound surface belongs to a hidden shell mode (e.g. OSK when
     * keyboard is not visible), re-resolve to find the correct overlay
     * surface (launcher) so the touch reaches the right client */
    {
        struct lumo_layer_surface *ls;
        bool wrong_surface = false;
        wl_list_for_each(ls, &compositor->layer_surfaces, link) {
            if (ls->layer_surface == NULL || ls->scene_surface == NULL) {
                continue;
            }
            if (ls->layer_surface->surface == point->surface &&
                    ls->layer_surface->namespace != NULL &&
                    strcmp(ls->layer_surface->namespace, "osk") == 0 &&
                    !compositor->keyboard_visible) {
                wrong_surface = true;
                break;
            }
        }
        if (wrong_surface) {
            /* find the launcher surface instead */
            wl_list_for_each(ls, &compositor->layer_surfaces, link) {
                if (ls->layer_surface == NULL) continue;
                if (ls->layer_surface->namespace != NULL &&
                        strcmp(ls->layer_surface->namespace, "launcher") == 0) {
                    point->surface = ls->layer_surface->surface;
                    /* fix sample coordinates — the stored sx/sy are from
                     * the OSK surface (0,0). Use the compositor-global
                     * coords instead, which are correct for the fullscreen
                     * launcher surface at (0,0) */
                    {
                        struct lumo_touch_sample *s;
                        wl_list_for_each(s, &point->samples, link) {
                            s->sx = s->lx;
                            s->sy = s->ly;
                        }
                    }
                    wlr_log(WLR_INFO,
                        "input: replay redirected from osk to launcher "
                        "at %.0f,%.0f",
                        point->down_lx, point->down_ly);
                    break;
                }
            }
        }
    }

    lumo_input_focus_surface(compositor, point->surface);
    wl_list_for_each(sample, &point->samples, link) {
        switch (sample->type) {
        case LUMO_TOUCH_SAMPLE_DOWN:
            wlr_seat_touch_notify_down(compositor->seat, point->surface,
                sample->time_msec, point->touch_id, sample->sx, sample->sy);
            break;
        case LUMO_TOUCH_SAMPLE_MOTION:
            wlr_seat_touch_notify_motion(compositor->seat, sample->time_msec,
                point->touch_id, sample->sx, sample->sy);
            break;
        case LUMO_TOUCH_SAMPLE_UP:
        case LUMO_TOUCH_SAMPLE_CANCEL:
            break;
        }
    }

    point->captured = false;
    point->delivered = true;
    lumo_input_touch_samples_clear(point);
    wlr_log(WLR_INFO, "input: touch %d replayed to surface", point->touch_id);
}

static void lumo_input_maybe_start_gesture_timer(struct lumo_compositor *compositor) {
    struct lumo_input_state *state = lumo_input_state_from(compositor);
    struct lumo_touch_point *point;
    uint32_t now;
    uint32_t next_deadline = 0;
    bool have_pending = false;

    if (compositor == NULL || state == NULL) {
        return;
    }

    now = lumo_input_now_msec();
    wl_list_for_each(point, &compositor->touch_points, link) {
        uint32_t deadline;

        if (!point->captured || point->gesture_triggered ||
                point->hitbox == NULL ||
                point->hitbox->kind != LUMO_HITBOX_EDGE_GESTURE) {
            continue;
        }

        deadline = point->down_time_msec + compositor->gesture_timeout_ms;
        if (!have_pending || deadline < next_deadline) {
            next_deadline = deadline;
            have_pending = true;
        }

        if (deadline <= now) {
            next_deadline = now + 1;
            have_pending = true;
            break;
        }
    }

    if (!have_pending) {
        if (state->gesture_timer != NULL) {
            wl_event_source_remove(state->gesture_timer);
            state->gesture_timer = NULL;
        }
        return;
    }

    if (state->gesture_timer == NULL) {
        state->gesture_timer = wl_event_loop_add_timer(compositor->event_loop,
            lumo_input_gesture_timeout_cb, compositor);
        if (state->gesture_timer == NULL) {
            wlr_log(WLR_ERROR, "input: failed to allocate gesture timer");
            return;
        }
    }

    wl_event_source_timer_update(state->gesture_timer,
        next_deadline > now ? (int)(next_deadline - now) : 1);
}

static int lumo_input_gesture_timeout_cb(void *data) {
    struct lumo_compositor *compositor = data;
    struct lumo_touch_point *point;
    struct lumo_touch_point *tmp;
    bool replayed = false;
    uint32_t now = lumo_input_now_msec();

    if (compositor == NULL) {
        return 0;
    }

    wl_list_for_each_safe(point, tmp, &compositor->touch_points, link) {
        uint32_t deadline;

        if (!point->captured || point->gesture_triggered || point->delivered) {
            continue;
        }

        deadline = point->down_time_msec + compositor->gesture_timeout_ms;
        if (deadline > now) {
            continue;
        }

        if (point->surface != NULL && point->capture_edge != LUMO_EDGE_NONE) {
            lumo_input_replay_touch_point(compositor, point);
            replayed = true;
        }
    }

    if (replayed && compositor->seat != NULL) {
        wlr_seat_touch_notify_frame(compositor->seat);
    }

    lumo_input_maybe_start_gesture_timer(compositor);
    return 0;
}

static void lumo_input_touch_point_begin_capture(
    struct lumo_compositor *compositor,
    struct lumo_touch_point *point,
    const struct lumo_surface_target *target,
    uint32_t time_msec
) {
    if (point == NULL) {
        return;
    }

    point->kind = LUMO_TOUCH_TARGET_HITBOX;
    point->captured = true;
    point->delivered = false;
    point->gesture_triggered = false;
    point->sx = target != NULL ? target->sx : 0.0;
    point->sy = target != NULL ? target->sy : 0.0;
    point->down_time_msec = time_msec;

    if (point->hitbox != NULL) {
        wlr_log(WLR_INFO, "input: touch %d captured by hitbox %s (%s)",
            point->touch_id,
            point->hitbox->name != NULL ? point->hitbox->name : "(unnamed)",
            lumo_hitbox_kind_name(point->hitbox->kind));
    } else if (point->capture_edge != LUMO_EDGE_NONE) {
        wlr_log(WLR_INFO, "input: touch %d captured by %s edge zone",
            point->touch_id, lumo_edge_zone_name(point->capture_edge));
    } else {
        wlr_log(WLR_INFO, "input: touch %d captured for gesture", point->touch_id);
    }
    lumo_input_maybe_start_gesture_timer(compositor);
}

static void lumo_input_touch_point_deliver_now(
    struct lumo_compositor *compositor,
    struct lumo_touch_point *point,
    const struct lumo_surface_target *target,
    uint32_t time_msec
) {
    if (compositor == NULL || point == NULL || target == NULL ||
            target->surface == NULL) {
        return;
    }

    point->kind = LUMO_TOUCH_TARGET_SURFACE;
    point->sx = target->sx;
    point->sy = target->sy;
    point->down_time_msec = time_msec;
    point->delivered = true;
    point->captured = false;

    wlr_seat_touch_notify_down(compositor->seat, point->surface, time_msec,
        point->touch_id, point->sx, point->sy);
    lumo_input_touch_samples_clear(point);
    wlr_log(WLR_INFO, "input: touch %d delivered to surface", point->touch_id);
}

static double lumo_input_touch_point_edge_progress(
    const struct lumo_touch_point *point,
    double lx,
    double ly
) {
    if (point == NULL) {
        return 0.0;
    }

    switch (point->capture_edge) {
    case LUMO_EDGE_TOP:
        return ly - point->down_ly;
    case LUMO_EDGE_LEFT:
        return lx - point->down_lx;
    case LUMO_EDGE_RIGHT:
        return point->down_lx - lx;
    case LUMO_EDGE_BOTTOM:
        return point->down_ly - ly;
    case LUMO_EDGE_NONE:
    default:
        return 0.0;
    }
}

/* velocity-based gesture detection (px/second) */
static double lumo_input_touch_point_edge_velocity(
    const struct lumo_touch_point *point,
    double lx,
    double ly,
    uint32_t time_msec
) {
    double dist;
    uint32_t dt;

    if (point == NULL || time_msec <= point->down_time_msec) {
        return 0.0;
    }

    dt = time_msec - point->down_time_msec;
    if (dt == 0) return 0.0;

    switch (point->capture_edge) {
    case LUMO_EDGE_TOP:    dist = ly - point->down_ly; break;
    case LUMO_EDGE_LEFT:   dist = lx - point->down_lx; break;
    case LUMO_EDGE_RIGHT:  dist = point->down_lx - lx; break;
    case LUMO_EDGE_BOTTOM: dist = point->down_ly - ly; break;
    default: return 0.0;
    }

    if (dist <= 0.0) return 0.0;
    return dist * 1000.0 / (double)dt; /* px/sec */
}

/* angle check: swipe must be within 15 degrees of the edge normal.
 * Android AOSP uses OVERVIEW_MIN_DEGREES = 15. */
static bool lumo_input_edge_angle_valid(
    enum lumo_edge_zone edge,
    double dx, double dy
) {
    double primary, orthogonal;
    switch (edge) {
    case LUMO_EDGE_BOTTOM: primary = -dy; orthogonal = dx < 0 ? -dx : dx; break;
    case LUMO_EDGE_TOP:    primary =  dy; orthogonal = dx < 0 ? -dx : dx; break;
    case LUMO_EDGE_LEFT:   primary =  dx; orthogonal = dy < 0 ? -dy : dy; break;
    case LUMO_EDGE_RIGHT:  primary = -dx; orthogonal = dy < 0 ? -dy : dy; break;
    default: return false;
    }
    if (primary <= 0.0) return false;
    /* tan(15deg) = 0.2679 — reject if orthogonal/primary > tan(15) */
    return orthogonal < primary * 0.268;
}

static void lumo_input_touch_point_trigger_edge_action(
    struct lumo_compositor *compositor,
    struct lumo_touch_point *point,
    uint32_t time_msec
) {
    bool closed_focused_app = false;

    if (compositor == NULL || point == NULL || point->gesture_triggered) {
        return;
    }

    point->gesture_triggered = true;

    switch (point->capture_edge) {
    case LUMO_EDGE_TOP: {
        struct lumo_output *top_output = lumo_input_first_output(compositor);
        bool is_right_half = top_output != NULL &&
            top_output->wlr_output != NULL &&
            point->down_lx > (double)top_output->wlr_output->width / 2.0;

        if (is_right_half) {
            if (compositor->time_panel_visible) {
                lumo_protocol_set_time_panel_visible(compositor, false);
            }
            lumo_protocol_set_quick_settings_visible(compositor,
                !compositor->quick_settings_visible);
            wlr_log(WLR_INFO,
                "input: touch %d toggled top-right quick settings at %u",
                point->touch_id, time_msec);
        } else {
            if (compositor->quick_settings_visible) {
                lumo_protocol_set_quick_settings_visible(compositor, false);
            }
            lumo_protocol_set_time_panel_visible(compositor,
                !compositor->time_panel_visible);
            wlr_log(WLR_INFO,
                "input: touch %d toggled top-left time panel at %u",
                point->touch_id, time_msec);
        }
        return;
    }
    case LUMO_EDGE_LEFT:
        if (compositor->touch_audit_active) {
            lumo_touch_audit_set_active(compositor, false);
        } else if (compositor->time_panel_visible) {
            lumo_protocol_set_time_panel_visible(compositor, false);
        } else if (compositor->quick_settings_visible) {
            lumo_protocol_set_quick_settings_visible(compositor, false);
        } else if (compositor->launcher_visible) {
            lumo_protocol_set_launcher_visible(compositor, false);
        } else if (compositor->keyboard_visible) {
            wlr_log(WLR_INFO,
                "input: left-edge dismiss hiding keyboard");
            lumo_protocol_set_keyboard_visible(compositor, false);
        } else {
            lumo_protocol_set_scrim_state(compositor, LUMO_SCRIM_HIDDEN);
        }
        wlr_log(WLR_INFO, "input: touch %d triggered left-edge dismiss at %u",
            point->touch_id, time_msec);
        return;
    case LUMO_EDGE_RIGHT:
    case LUMO_EDGE_BOTTOM:
        if (compositor->touch_audit_active) {
            lumo_touch_audit_set_active(compositor, false);
        }
        /* if an app is focused and the launcher is not visible,
         * the bottom-edge swipe closes the app (like iOS/Android) */
        if (!compositor->launcher_visible &&
                !compositor->touch_audit_active &&
                !wl_list_empty(&compositor->toplevels)) {
            closed_focused_app =
                lumo_protocol_close_focused_app(compositor);
            if (closed_focused_app) {
                /* also hide keyboard if it was showing */
                if (compositor->keyboard_visible) {
                    lumo_protocol_set_keyboard_visible(compositor, false);
                }
                wlr_log(WLR_INFO,
                    "input: touch %d closed focused app from bottom swipe at %u",
                    point->touch_id, time_msec);
                return;
            }
        }
        if (lumo_hitbox_is_shell_gesture(point->hitbox) &&
                compositor->launcher_visible) {
            lumo_protocol_set_launcher_visible(compositor, false);
            wlr_log(WLR_INFO,
                "input: touch %d toggled %s-edge launcher closed at %u",
                point->touch_id, lumo_edge_zone_name(point->capture_edge),
                time_msec);
            return;
        }
        lumo_protocol_set_launcher_visible(compositor, true);
        lumo_protocol_set_scrim_state(compositor, LUMO_SCRIM_MODAL);
        wlr_log(WLR_INFO,
            "input: touch %d triggered %s-edge launcher gesture at %u",
            point->touch_id, lumo_edge_zone_name(point->capture_edge),
            time_msec);
        return;
    case LUMO_EDGE_NONE:
    default:
        wlr_log(WLR_INFO, "input: touch %d edge action skipped at %u",
            point->touch_id, time_msec);
        return;
    }
}

static int lumo_input_gesture_timeout_cb(void *data);

static void lumo_input_touch_down(struct wl_listener *listener, void *data);
static void lumo_input_touch_motion(struct wl_listener *listener, void *data);
static void lumo_input_touch_up(struct wl_listener *listener, void *data);
static void lumo_input_touch_cancel(struct wl_listener *listener, void *data);
static void lumo_input_touch_frame(struct wl_listener *listener, void *data);
static void lumo_input_pointer_motion(struct wl_listener *listener, void *data);
static void lumo_input_pointer_motion_absolute(struct wl_listener *listener, void *data);
static void lumo_input_pointer_button(struct wl_listener *listener, void *data);
static void lumo_input_pointer_axis(struct wl_listener *listener, void *data);
static void lumo_input_pointer_frame(struct wl_listener *listener, void *data);
static void lumo_input_pointer_swipe_begin(struct wl_listener *listener, void *data);
static void lumo_input_pointer_swipe_update(struct wl_listener *listener, void *data);
static void lumo_input_pointer_swipe_end(struct wl_listener *listener, void *data);
static void lumo_input_pointer_pinch_begin(struct wl_listener *listener, void *data);
static void lumo_input_pointer_pinch_update(struct wl_listener *listener, void *data);
static void lumo_input_pointer_pinch_end(struct wl_listener *listener, void *data);
static void lumo_input_pointer_hold_begin(struct wl_listener *listener, void *data);
static void lumo_input_pointer_hold_end(struct wl_listener *listener, void *data);
static void lumo_input_request_set_cursor(struct wl_listener *listener, void *data);
static void lumo_input_request_set_selection(struct wl_listener *listener, void *data);
static void lumo_input_keyboard_key(struct wl_listener *listener, void *data);
static void lumo_input_keyboard_modifiers(struct wl_listener *listener, void *data);
static void lumo_input_keyboard_destroy(struct wl_listener *listener, void *data);
static void lumo_input_device_destroy(struct wl_listener *listener, void *data);

static void lumo_input_pointer_notify_surface(
    struct lumo_compositor *compositor,
    uint32_t time_msec
) {
    struct wlr_surface *focused = NULL;
    struct wlr_scene_node *node = NULL;
    struct wlr_scene_surface *scene_surface = NULL;
    double sx = 0.0;
    double sy = 0.0;

    if (compositor == NULL || compositor->cursor == NULL ||
            compositor->seat == NULL || compositor->scene == NULL) {
        return;
    }

    node = wlr_scene_node_at(&compositor->scene->tree.node, compositor->cursor->x,
        compositor->cursor->y, &sx, &sy);
    if (node != NULL) {
        scene_surface = lumo_scene_surface_from_node(node, NULL);
        if (scene_surface != NULL) {
            focused = scene_surface->surface;
        }
    }

    if (focused == NULL) {
        wlr_seat_pointer_notify_clear_focus(compositor->seat);
        return;
    }

    if (compositor->seat->pointer_state.focused_surface != focused) {
        wlr_seat_pointer_notify_enter(compositor->seat, focused, sx, sy);
    }

    wlr_seat_pointer_notify_motion(compositor->seat, time_msec, sx, sy);
}

static void lumo_input_keyboard_attach(
    struct lumo_compositor *compositor,
    struct wlr_input_device *device
) {
    struct wlr_keyboard *keyboard;
    struct lumo_keyboard *entry;
    struct xkb_keymap *keymap;
    struct xkb_rule_names rules = {
        .layout = "us",
    };

    if (compositor == NULL || device == NULL || compositor->seat == NULL) {
        return;
    }

    keyboard = wlr_keyboard_from_input_device(device);
    if (keyboard == NULL) {
        wlr_log(WLR_ERROR, "input: keyboard device '%s' could not be cast",
            device->name != NULL ? device->name : "(unknown)");
        return;
    }

    keymap = xkb_keymap_new_from_names(compositor->xkb_context, &rules,
        XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (keymap != NULL) {
        if (!wlr_keyboard_set_keymap(keyboard, keymap)) {
            wlr_log(WLR_ERROR, "input: failed to set keymap for '%s'",
                device->name != NULL ? device->name : "(unknown)");
        }
        xkb_keymap_unref(keymap);
    } else {
        wlr_log(WLR_ERROR, "input: failed to build default keymap");
    }

    wlr_keyboard_set_repeat_info(keyboard, 25, 600);
    wlr_seat_set_keyboard(compositor->seat, keyboard);

    entry = calloc(1, sizeof(*entry));
    if (entry == NULL) {
        wlr_log_errno(WLR_ERROR, "input: failed to allocate keyboard wrapper");
        return;
    }

    entry->compositor = compositor;
    entry->wlr_keyboard = keyboard;
    entry->modifiers.notify = lumo_input_keyboard_modifiers;
    wl_signal_add(&keyboard->events.modifiers, &entry->modifiers);
    entry->key.notify = lumo_input_keyboard_key;
    wl_signal_add(&keyboard->events.key, &entry->key);
    entry->destroy.notify = lumo_input_keyboard_destroy;
    wl_signal_add(&device->events.destroy, &entry->destroy);

    wl_list_insert(&compositor->keyboards, &entry->link);
    compositor->keyboard_devices++;
    lumo_input_refresh_capabilities(compositor);
    wlr_log(WLR_INFO, "input: keyboard '%s' ready",
        device->name != NULL ? device->name : "(unknown)");
}

static void lumo_input_pointer_device_attach(
    struct lumo_compositor *compositor,
    struct wlr_input_device *device
) {
    struct lumo_input_device *entry;

    if (compositor == NULL || device == NULL || compositor->cursor == NULL) {
        return;
    }

    entry = calloc(1, sizeof(*entry));
    if (entry == NULL) {
        wlr_log_errno(WLR_ERROR, "input: failed to allocate pointer wrapper");
        return;
    }

    entry->compositor = compositor;
    entry->device = device;
    entry->type = device->type;
    entry->destroy.notify = lumo_input_device_destroy;
    wl_signal_add(&device->events.destroy, &entry->destroy);
    wl_list_insert(&compositor->input_devices, &entry->link);
    wlr_cursor_attach_input_device(compositor->cursor, device);
    if (device->type == WLR_INPUT_DEVICE_POINTER ||
            device->type == WLR_INPUT_DEVICE_TABLET) {
        compositor->pointer_devices++;
    } else if (device->type == WLR_INPUT_DEVICE_TOUCH) {
        compositor->touch_devices++;
    }

    lumo_input_refresh_capabilities(compositor);
    wlr_log(WLR_INFO, "input: device '%s' attached to cursor",
        device->name != NULL ? device->name : "(unknown)");
}

static void lumo_input_device_destroy(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_input_device *entry =
        wl_container_of(listener, entry, destroy);
    struct lumo_compositor *compositor =
        entry != NULL ? entry->compositor : NULL;

    (void)data;
    if (compositor != NULL && compositor->cursor != NULL &&
            entry->device != NULL) {
        wlr_cursor_detach_input_device(compositor->cursor, entry->device);
    }

    if (compositor != NULL) {
        if (entry->type == WLR_INPUT_DEVICE_POINTER ||
                entry->type == WLR_INPUT_DEVICE_TABLET) {
            if (compositor->pointer_devices > 0) {
                compositor->pointer_devices--;
            }
        } else if (entry->type == WLR_INPUT_DEVICE_TOUCH) {
            if (compositor->touch_devices > 0) {
                compositor->touch_devices--;
            }
        }

        lumo_input_refresh_capabilities(compositor);
    }

    wl_list_remove(&entry->destroy.link);
    wl_list_remove(&entry->link);
    free(entry);
}

static void lumo_input_keyboard_destroy(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_keyboard *keyboard = wl_container_of(listener, keyboard, destroy);
    struct lumo_compositor *compositor =
        keyboard != NULL ? keyboard->compositor : NULL;

    (void)data;
    if (compositor != NULL && compositor->keyboard_devices > 0) {
        compositor->keyboard_devices--;
        lumo_input_refresh_capabilities(compositor);
    }

    wl_list_remove(&keyboard->modifiers.link);
    wl_list_remove(&keyboard->key.link);
    wl_list_remove(&keyboard->destroy.link);
    wl_list_remove(&keyboard->link);
    free(keyboard);
}

static void lumo_input_keyboard_modifiers(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);

    if (keyboard == NULL || keyboard->compositor == NULL ||
            keyboard->compositor->seat == NULL || keyboard->wlr_keyboard == NULL) {
        return;
    }

    wlr_seat_keyboard_notify_modifiers(keyboard->compositor->seat,
        &keyboard->wlr_keyboard->modifiers);
    (void)data;
}

static void lumo_input_keyboard_key(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_keyboard *keyboard = wl_container_of(listener, keyboard, key);
    struct wlr_keyboard_key_event *event = data;

    if (keyboard == NULL || keyboard->compositor == NULL ||
            keyboard->compositor->seat == NULL || event == NULL) {
        return;
    }

    wlr_seat_set_keyboard(keyboard->compositor->seat, keyboard->wlr_keyboard);
    wlr_seat_keyboard_notify_key(keyboard->compositor->seat, event->time_msec,
        event->keycode, event->state);
}

static void lumo_input_request_set_cursor(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_compositor *compositor =
        wl_container_of(listener, compositor, seat_request_cursor);
    struct wlr_seat_pointer_request_set_cursor_event *event = data;

    if (compositor == NULL || compositor->cursor == NULL || compositor->seat == NULL ||
            event == NULL) {
        return;
    }

    if (event->seat_client != compositor->seat->pointer_state.focused_client) {
        wlr_log(WLR_INFO, "input: ignored cursor request from unfocused client");
        return;
    }

    wlr_cursor_set_surface(compositor->cursor, event->surface,
        event->hotspot_x, event->hotspot_y);
}

static void lumo_input_request_set_selection(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_compositor *compositor =
        wl_container_of(listener, compositor, seat_request_set_selection);
    struct wlr_seat_request_set_selection_event *event = data;

    if (compositor == NULL || compositor->seat == NULL || event == NULL) {
        return;
    }

    wlr_seat_set_selection(compositor->seat, event->source, event->serial);
}

static void lumo_input_pointer_motion(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_compositor *compositor =
        wl_container_of(listener, compositor, cursor_motion);
    struct wlr_pointer_motion_event *event = data;

    if (compositor == NULL || event == NULL || compositor->cursor == NULL) {
        return;
    }

    wlr_cursor_move(compositor->cursor, &event->pointer->base,
        event->unaccel_dx, event->unaccel_dy);
    lumo_input_pointer_notify_surface(compositor, event->time_msec);
}

static void lumo_input_pointer_motion_absolute(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_compositor *compositor =
        wl_container_of(listener, compositor, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = data;
    double lx = 0.0;
    double ly = 0.0;

    if (compositor == NULL || event == NULL || compositor->cursor == NULL) {
        return;
    }

    lumo_input_transform_touch_coords(compositor, &event->pointer->base,
        event->x, event->y, &lx, &ly, NULL);
    wlr_cursor_warp(compositor->cursor, &event->pointer->base, lx, ly);
    lumo_input_pointer_notify_surface(compositor, event->time_msec);
}

static void lumo_input_pointer_button(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_compositor *compositor =
        wl_container_of(listener, compositor, cursor_button);
    struct wlr_pointer_button_event *event = data;

    if (compositor == NULL || event == NULL || compositor->seat == NULL) {
        return;
    }

    if (compositor->seat->pointer_state.focused_surface == NULL) {
        lumo_input_pointer_notify_surface(compositor, event->time_msec);
    }

    wlr_seat_pointer_notify_button(compositor->seat, event->time_msec,
        event->button, event->state);
}

static void lumo_input_pointer_axis(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_compositor *compositor =
        wl_container_of(listener, compositor, cursor_axis);
    struct wlr_pointer_axis_event *event = data;

    if (compositor == NULL || event == NULL || compositor->seat == NULL) {
        return;
    }

    wlr_seat_pointer_notify_axis(compositor->seat, event->time_msec,
        event->orientation, event->delta, event->delta_discrete,
        event->source, event->relative_direction);
}

static void lumo_input_pointer_frame(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_compositor *compositor =
        wl_container_of(listener, compositor, cursor_frame);

    if (compositor == NULL || compositor->seat == NULL) {
        return;
    }

    wlr_seat_pointer_notify_frame(compositor->seat);
    (void)data;
}

static void lumo_input_pointer_swipe_begin(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_compositor *compositor =
        wl_container_of(listener, compositor, cursor_swipe_begin);
    struct wlr_pointer_swipe_begin_event *event = data;

    if (compositor == NULL || compositor->pointer_gestures == NULL ||
            event == NULL) {
        return;
    }

    wlr_pointer_gestures_v1_send_swipe_begin(compositor->pointer_gestures,
        compositor->seat, event->time_msec, event->fingers);
}

static void lumo_input_pointer_swipe_update(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_compositor *compositor =
        wl_container_of(listener, compositor, cursor_swipe_update);
    struct wlr_pointer_swipe_update_event *event = data;

    if (compositor == NULL || compositor->pointer_gestures == NULL ||
            event == NULL) {
        return;
    }

    wlr_pointer_gestures_v1_send_swipe_update(compositor->pointer_gestures,
        compositor->seat, event->time_msec, event->dx, event->dy);
}

static void lumo_input_pointer_swipe_end(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_compositor *compositor =
        wl_container_of(listener, compositor, cursor_swipe_end);
    struct wlr_pointer_swipe_end_event *event = data;

    if (compositor == NULL || compositor->pointer_gestures == NULL ||
            event == NULL) {
        return;
    }

    wlr_pointer_gestures_v1_send_swipe_end(compositor->pointer_gestures,
        compositor->seat, event->time_msec, event->cancelled);
}

static void lumo_input_pointer_pinch_begin(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_compositor *compositor =
        wl_container_of(listener, compositor, cursor_pinch_begin);
    struct wlr_pointer_pinch_begin_event *event = data;

    if (compositor == NULL || compositor->pointer_gestures == NULL ||
            event == NULL) {
        return;
    }

    wlr_pointer_gestures_v1_send_pinch_begin(compositor->pointer_gestures,
        compositor->seat, event->time_msec, event->fingers);
}

static void lumo_input_pointer_pinch_update(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_compositor *compositor =
        wl_container_of(listener, compositor, cursor_pinch_update);
    struct wlr_pointer_pinch_update_event *event = data;

    if (compositor == NULL || compositor->pointer_gestures == NULL ||
            event == NULL) {
        return;
    }

    wlr_pointer_gestures_v1_send_pinch_update(compositor->pointer_gestures,
        compositor->seat, event->time_msec, event->dx, event->dy,
        event->scale, event->rotation);
}

static void lumo_input_pointer_pinch_end(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_compositor *compositor =
        wl_container_of(listener, compositor, cursor_pinch_end);
    struct wlr_pointer_pinch_end_event *event = data;

    if (compositor == NULL || compositor->pointer_gestures == NULL ||
            event == NULL) {
        return;
    }

    wlr_pointer_gestures_v1_send_pinch_end(compositor->pointer_gestures,
        compositor->seat, event->time_msec, event->cancelled);
}

static void lumo_input_pointer_hold_begin(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_compositor *compositor =
        wl_container_of(listener, compositor, cursor_hold_begin);
    struct wlr_pointer_hold_begin_event *event = data;

    if (compositor == NULL || compositor->pointer_gestures == NULL ||
            event == NULL) {
        return;
    }

    wlr_pointer_gestures_v1_send_hold_begin(compositor->pointer_gestures,
        compositor->seat, event->time_msec, event->fingers);
}

static void lumo_input_pointer_hold_end(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_compositor *compositor =
        wl_container_of(listener, compositor, cursor_hold_end);
    struct wlr_pointer_hold_end_event *event = data;

    if (compositor == NULL || compositor->pointer_gestures == NULL ||
            event == NULL) {
        return;
    }

    wlr_pointer_gestures_v1_send_hold_end(compositor->pointer_gestures,
        compositor->seat, event->time_msec, event->cancelled);
}

static void lumo_input_touch_motion(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_compositor *compositor =
        wl_container_of(listener, compositor, cursor_touch_motion);
    struct wlr_touch_motion_event *event = data;
    struct lumo_touch_point *point;
    struct lumo_surface_target target = {0};
    double lx = 0.0;
    double ly = 0.0;
    double threshold;

    if (compositor == NULL || event == NULL || compositor->seat == NULL) {
        return;
    }

    point = lumo_input_touch_point_for_id(compositor, event->touch_id);
    if (point == NULL) {
        return;
    }

    lumo_input_transform_touch_coords(compositor, &event->touch->base, event->x,
        event->y, &lx, &ly, NULL);
    lumo_input_surface_target_at(compositor, lx, ly, &target);

    threshold = compositor->gesture_threshold > 0.0
        ? compositor->gesture_threshold
        : 24.0;

    if (point->captured && point->capture_edge != LUMO_EDGE_NONE &&
            !point->gesture_triggered) {
        double progress = lumo_input_touch_point_edge_progress(point, lx, ly);
        double velocity = lumo_input_touch_point_edge_velocity(point, lx, ly,
            event->time_msec);
        double dx = lx - point->down_lx;
        double dy = ly - point->down_ly;
        /* trigger on (distance OR velocity) AND angle within 15 deg */
        if ((progress >= threshold || velocity > 800.0) &&
                lumo_input_edge_angle_valid(point->capture_edge, dx, dy)) {
            lumo_input_touch_point_trigger_edge_action(compositor, point,
                event->time_msec);
        }
    }

    if (!point->captured && point->delivered && point->surface != NULL) {
        wlr_seat_touch_notify_motion(compositor->seat, event->time_msec,
            event->touch_id, target.sx, target.sy);
        point->lx = lx;
        point->ly = ly;
        point->sx = target.sx;
        point->sy = target.sy;
        lumo_input_touch_debug_update(compositor, point,
            LUMO_TOUCH_SAMPLE_MOTION, true, point->lx, point->ly);
        return;
    }

    if (point->captured) {
        lumo_input_touch_sample_append(point, LUMO_TOUCH_SAMPLE_MOTION,
            event->time_msec, lx, ly, target.sx, target.sy);
        if (!point->gesture_triggered &&
                point->capture_edge != LUMO_EDGE_NONE) {
            double prog = lumo_input_touch_point_edge_progress(point, lx, ly);
            double vel = lumo_input_touch_point_edge_velocity(point, lx, ly,
                event->time_msec);
            double cdx = lx - point->down_lx;
            double cdy = ly - point->down_ly;
            if ((prog >= threshold || vel > 800.0) &&
                    lumo_input_edge_angle_valid(point->capture_edge,
                        cdx, cdy)) {
                lumo_input_touch_point_trigger_edge_action(compositor, point,
                    event->time_msec);
            }
        }

        lumo_input_maybe_start_gesture_timer(compositor);
        point->lx = lx;
        point->ly = ly;
        point->sx = target.sx;
        point->sy = target.sy;
        lumo_input_touch_debug_update(compositor, point,
            LUMO_TOUCH_SAMPLE_MOTION, true, point->lx, point->ly);
        return;
    }

    if (point->surface != NULL) {
        wlr_seat_touch_notify_motion(compositor->seat, event->time_msec,
            event->touch_id, target.sx, target.sy);
    }

    point->lx = lx;
    point->ly = ly;
    point->sx = target.sx;
    point->sy = target.sy;
    lumo_input_touch_debug_update(compositor, point, LUMO_TOUCH_SAMPLE_MOTION,
        true, point->lx, point->ly);
}

static void lumo_input_touch_down(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_compositor *compositor =
        wl_container_of(listener, compositor, cursor_touch_down);
    struct wlr_touch_down_event *event = data;
    struct lumo_touch_point *point;
    struct lumo_surface_target target = {0};
    struct lumo_output *output = NULL;
    enum lumo_edge_zone edge_zone = LUMO_EDGE_NONE;

    if (compositor == NULL || event == NULL || compositor->seat == NULL) {
        return;
    }

    point = lumo_input_touch_point_for_id(compositor, event->touch_id);
    if (point != NULL) {
        wlr_log(WLR_ERROR, "input: duplicate touch id %d", event->touch_id);
        return;
    }

    /* touch event received */

    point = calloc(1, sizeof(*point));
    if (point == NULL) {
        wlr_log_errno(WLR_ERROR, "input: failed to allocate touch point");
        return;
    }

    point->touch_id = event->touch_id;
    point->owner = compositor;
    point->kind = LUMO_TOUCH_TARGET_NONE;
    point->capture_edge = LUMO_EDGE_NONE;
    wl_list_init(&point->surface_destroy.link);
    wl_list_init(&point->samples);
    wl_list_insert(&compositor->touch_points, &point->link);

    lumo_input_transform_touch_coords(compositor, &event->touch->base, event->x,
        event->y, &point->lx, &point->ly, &output);

    /* touch indicator disabled for debugging */

    wlr_log(WLR_INFO,
        "input: touch DEBUG raw=%.3f,%.3f mapped=%.1f,%.1f transform=%d",
        event->x, event->y, point->lx, point->ly,
        output != NULL && output->wlr_output != NULL ?
            (int)output->wlr_output->transform : -1);

    lumo_input_surface_target_at(compositor, point->lx, point->ly, &target);
    point->down_lx = point->lx;
    point->down_ly = point->ly;
    point->sx = target.sx;
    point->sy = target.sy;
    point->down_time_msec = event->time_msec;
    point->hitbox = lumo_protocol_hitbox_at(compositor, point->lx, point->ly);

    lumo_input_touch_audit_log(compositor, point, output, &target,
        event->x, event->y);
    lumo_touch_audit_note_touch(compositor, output, &event->touch->base, point,
        event->x, event->y);

    if (compositor->touch_audit_active && lumo_input_target_is_shell(&target)) {
        memset(&target, 0, sizeof(target));
        point->sx = 0.0;
        point->sy = 0.0;
    }

    if (compositor->quick_settings_visible || compositor->time_panel_visible) {
        bool in_panel = false;

        if (compositor->quick_settings_visible) {
            struct lumo_output *o = lumo_input_first_output(compositor);
            if (o != NULL && o->wlr_output != NULL) {
                int ow = 0, oh = 0;
                wlr_output_effective_resolution(o->wlr_output, &ow, &oh);
                int pw = ow / 2;
                if (point->lx >= ow - pw - 8 && point->ly >= 48 &&
                        point->ly < oh) {
                    in_panel = true;
                }
            }
        }
        if (compositor->time_panel_visible) {
            struct lumo_output *o = lumo_input_first_output(compositor);
            if (o != NULL && o->wlr_output != NULL) {
                int ow = 0, oh = 0;
                wlr_output_effective_resolution(o->wlr_output, &ow, &oh);
                int pw = ow / 2;
                if (point->lx >= 0 && point->lx <= pw + 16 &&
                        point->ly >= 48 && point->ly < 250) {
                    in_panel = true;
                }
            }
        }

        if (!in_panel && !lumo_input_target_is_shell(&target)) {
            if (compositor->quick_settings_visible) {
                lumo_protocol_set_quick_settings_visible(compositor, false);
            }
            if (compositor->time_panel_visible) {
                lumo_protocol_set_time_panel_visible(compositor, false);
            }
            wlr_log(WLR_INFO,
                "input: touch %d dismissed panel (outside tap)",
                point->touch_id);
            lumo_input_remove_touch_point(compositor, point);
            return;
        }
    }

    lumo_input_touch_sample_append(point, LUMO_TOUCH_SAMPLE_DOWN,
        event->time_msec, point->lx, point->ly, point->sx, point->sy);
    lumo_input_touch_point_bind_surface(point, target.surface);

    if (lumo_touch_hitbox_uses_immediate_launcher_toggle(point->hitbox)) {
        point->capture_edge = lumo_hitbox_edge_zone(point->hitbox);
        lumo_input_touch_point_begin_capture(compositor, point, &target,
            event->time_msec);
        lumo_input_touch_point_trigger_edge_action(compositor, point,
            event->time_msec);
        wlr_log(WLR_INFO,
            "input: touch %d toggled launcher from gesture handle",
            point->touch_id);
        lumo_input_touch_debug_update(compositor, point, LUMO_TOUCH_SAMPLE_DOWN,
            true, point->lx, point->ly);
        return;
    }

    /* --- hitbox checks FIRST (edges, gestures, OSK) --- */

    if (lumo_input_hitbox_is_shell_reserved(point->hitbox)) {
        lumo_input_touch_point_begin_capture(compositor, point, &target,
            event->time_msec);
        lumo_input_touch_debug_update(compositor, point, LUMO_TOUCH_SAMPLE_DOWN,
            true, point->lx, point->ly);
        return;
    }

    if (!compositor->touch_audit_active &&
            point->hitbox != NULL &&
            point->hitbox->kind == LUMO_HITBOX_EDGE_GESTURE) {
        point->capture_edge = lumo_hitbox_edge_zone(point->hitbox);
        lumo_input_touch_point_begin_capture(compositor, point, &target,
            event->time_msec);
        lumo_input_touch_debug_update(compositor, point, LUMO_TOUCH_SAMPLE_DOWN,
            true, point->lx, point->ly);
        return;
    }

    edge_zone = lumo_input_system_edge_zone(compositor, output, point->lx,
        point->ly);
    if (compositor->touch_audit_active && edge_zone != LUMO_EDGE_LEFT) {
        edge_zone = LUMO_EDGE_NONE;
    }
    if (edge_zone != LUMO_EDGE_NONE) {
        point->capture_edge = edge_zone;
        lumo_input_touch_point_begin_capture(compositor, point, &target,
            event->time_msec);
        lumo_input_touch_debug_update(compositor, point, LUMO_TOUCH_SAMPLE_DOWN,
            true, point->lx, point->ly);
        return;
    }

    /* --- shell surface redirect (only after hitboxes) --- */

    if (lumo_input_target_is_shell(&target)) {
        bool shell_ui_active = compositor->launcher_visible ||
            compositor->quick_settings_visible ||
            compositor->time_panel_visible ||
            compositor->touch_audit_active;
        if (!shell_ui_active && !wl_list_empty(&compositor->toplevels)) {
            struct lumo_toplevel *tl;
            wl_list_for_each(tl, &compositor->toplevels, link) {
                if (tl->xdg_surface != NULL &&
                        tl->xdg_surface->surface != NULL) {
                    struct lumo_surface_target tl_target = {0};
                    tl_target.surface = tl->xdg_surface->surface;
                    tl_target.sx = point->lx;
                    tl_target.sy = point->ly;
                    if (tl->scene_tree != NULL) {
                        int sx = 0, sy = 0;
                        wlr_scene_node_coords(&tl->scene_tree->node,
                            &sx, &sy);
                        tl_target.sx = point->lx - (double)sx;
                        tl_target.sy = point->ly - (double)sy;
                    }
                    lumo_input_touch_point_bind_surface(point,
                        tl->xdg_surface->surface);
                    lumo_input_touch_point_deliver_now(compositor, point,
                        &tl_target, event->time_msec);
                    lumo_input_focus_surface(compositor,
                        tl->xdg_surface->surface);
                    lumo_input_touch_debug_update(compositor, point,
                        LUMO_TOUCH_SAMPLE_DOWN, true, point->lx, point->ly);
                    return;
                }
            }
        }
        /* Safe: bind_surface was already called at line 1718 above with
         * target.surface, so point->surface is valid before deliver_now. */
        lumo_input_touch_point_deliver_now(compositor, point, &target,
            event->time_msec);
        lumo_input_focus_surface(compositor, point->surface);
        lumo_input_touch_debug_update(compositor, point, LUMO_TOUCH_SAMPLE_DOWN,
            true, point->lx, point->ly);
        return;
    }

    if (target.surface != NULL) {
        bool is_app_toplevel = false;
        struct lumo_toplevel *tl;

        wl_list_for_each(tl, &compositor->toplevels, link) {
            if (tl->xdg_surface != NULL &&
                    tl->xdg_surface->surface == target.surface) {
                is_app_toplevel = true;
                break;
            }
        }

        wlr_log(WLR_INFO,
            "input: touch %d surface found, is_toplevel=%d role=%d",
            point->touch_id, is_app_toplevel,
            target.object != NULL ? (int)target.role : -1);

        lumo_input_touch_point_deliver_now(compositor, point, &target,
            event->time_msec);
        lumo_input_focus_surface(compositor,
            is_app_toplevel ? target.surface : point->surface);
        lumo_input_touch_debug_update(compositor, point, LUMO_TOUCH_SAMPLE_DOWN,
            true, point->lx, point->ly);
        return;
    }

    if (!wl_list_empty(&compositor->toplevels) &&
            !compositor->launcher_visible) {
        struct lumo_toplevel *tl;
        wl_list_for_each(tl, &compositor->toplevels, link) {
            if (tl->xdg_surface != NULL && tl->xdg_surface->surface != NULL) {
                lumo_input_touch_point_bind_surface(point,
                    tl->xdg_surface->surface);
                lumo_input_touch_point_deliver_now(compositor, point, &target,
                    event->time_msec);
                lumo_input_focus_surface(compositor, tl->xdg_surface->surface);
                wlr_log(WLR_INFO,
                    "input: touch %d delivered to focused toplevel",
                    point->touch_id);
                return;
            }
        }
    }

    wlr_log(WLR_INFO, "input: touch %d ignored outside shell/app regions",
        point->touch_id);
    lumo_input_touch_debug_update(compositor, point, LUMO_TOUCH_SAMPLE_DOWN,
        false, point->lx, point->ly);
    lumo_input_remove_touch_point(compositor, point);
}

static void lumo_input_touch_up(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_compositor *compositor =
        wl_container_of(listener, compositor, cursor_touch_up);
    struct wlr_touch_up_event *event = data;
    struct lumo_touch_point *point;

    if (compositor == NULL || event == NULL || compositor->seat == NULL) {
        return;
    }

    point = lumo_input_touch_point_for_id(compositor, event->touch_id);
    if (point == NULL) {
        return;
    }

    if (point->captured && !point->delivered) {
        if (!point->gesture_triggered &&
                point->capture_edge != LUMO_EDGE_NONE) {
            double progress = lumo_input_touch_point_edge_progress(point,
                point->lx, point->ly);
            double velocity = lumo_input_touch_point_edge_velocity(point,
                point->lx, point->ly, event->time_msec);
            double threshold = compositor->gesture_threshold > 0.0
                ? compositor->gesture_threshold : 24.0;
            /* iOS-style projection: where would the finger end up
             * if it kept moving at current velocity for 150ms? */
            double projected = progress + velocity * 0.15;

            if (progress < 12.0) {
                /* very short movement = tap on the edge/handle */
                lumo_input_touch_point_trigger_edge_action(compositor, point,
                    event->time_msec);
                if (lumo_hitbox_is_shell_gesture(point->hitbox)) {
                    wlr_log(WLR_INFO,
                        "input: touch %d tapped gesture handle",
                        point->touch_id);
                } else {
                    wlr_log(WLR_INFO, "input: touch %d tapped %s edge",
                        point->touch_id,
                        lumo_edge_zone_name(point->capture_edge));
                }
            } else if (projected >= threshold) {
                /* projection says this swipe would have crossed the
                 * threshold — trigger the gesture (iOS fluid model) */
                lumo_input_touch_point_trigger_edge_action(compositor, point,
                    event->time_msec);
                wlr_log(WLR_INFO,
                    "input: touch %d projected swipe triggered "
                    "(prog=%.0f vel=%.0f proj=%.0f)",
                    point->touch_id, progress, velocity, projected);
            }
        }

        if (!point->gesture_triggered && point->surface != NULL &&
                point->capture_edge != LUMO_EDGE_NONE) {
            lumo_input_replay_touch_point(compositor, point);
        }

        if (point->gesture_triggered) {
            wlr_log(WLR_INFO, "input: touch %d gesture completed", point->touch_id);
        } else if (point->hitbox != NULL &&
                lumo_input_hitbox_is_shell_reserved(point->hitbox)) {
            struct wlr_surface *shell_surface = NULL;
            struct lumo_layer_surface *ls;
            wl_list_for_each(ls, &compositor->layer_surfaces, link) {
                if (ls->layer_surface != NULL &&
                        ls->layer_surface->current.layer ==
                            ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY) {
                    shell_surface = ls->layer_surface->surface;
                    break;
                }
            }
            if (shell_surface == NULL) {
                wl_list_for_each(ls, &compositor->layer_surfaces, link) {
                    if (ls->layer_surface != NULL &&
                            ls->layer_surface->current.layer ==
                                ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
                        shell_surface = ls->layer_surface->surface;
                        break;
                    }
                }
            }
            if (shell_surface != NULL) {
                lumo_input_touch_point_bind_surface(point, shell_surface);
                lumo_input_replay_touch_point(compositor, point);
                wlr_log(WLR_INFO, "input: touch %d replayed to shell from hitbox %s",
                    point->touch_id,
                    point->hitbox->name != NULL ? point->hitbox->name : "(unnamed)");
            } else {
                wlr_log(WLR_INFO, "input: touch %d consumed by hitbox %s",
                    point->touch_id,
                    point->hitbox->name != NULL ? point->hitbox->name : "(unnamed)");
            }
        }
    }

    if (point->delivered && point->surface != NULL) {
        wlr_seat_touch_notify_up(compositor->seat, event->time_msec,
            event->touch_id);
    }

    if (point->delivered || point->captured) {
        wlr_seat_touch_notify_frame(compositor->seat);
    }

    lumo_input_touch_debug_update(compositor, point, LUMO_TOUCH_SAMPLE_UP,
        false, point->lx, point->ly);

    lumo_input_remove_touch_point(compositor, point);
    lumo_input_maybe_start_gesture_timer(compositor);
}

static void lumo_input_touch_cancel(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_compositor *compositor =
        wl_container_of(listener, compositor, cursor_touch_cancel);
    struct wlr_touch_cancel_event *event = data;
    struct lumo_touch_point *point;

    if (compositor == NULL || event == NULL || compositor->seat == NULL) {
        return;
    }

    point = lumo_input_touch_point_for_id(compositor, event->touch_id);
    if (point != NULL) {
        lumo_input_touch_debug_update(compositor, point,
            LUMO_TOUCH_SAMPLE_CANCEL, false, point->lx, point->ly);
        if (point->delivered && point->surface != NULL) {
            struct wlr_touch_point *seat_point =
                wlr_seat_touch_get_point(compositor->seat, point->touch_id);
            if (seat_point != NULL) {
                wlr_seat_touch_notify_cancel(compositor->seat,
                    seat_point->client);
            }
        }

        lumo_input_remove_touch_point(compositor, point);
    }
}

static void lumo_input_touch_frame(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_compositor *compositor =
        wl_container_of(listener, compositor, cursor_touch_frame);

    if (compositor == NULL || compositor->seat == NULL) {
        return;
    }

    wlr_seat_touch_notify_frame(compositor->seat);
    (void)data;
}

static void lumo_input_backend_new_input(
    struct wl_listener *listener,
    void *data
) {
    struct lumo_compositor *compositor =
        wl_container_of(listener, compositor, backend_new_input);
    struct wlr_input_device *device = data;

    if (compositor == NULL || device == NULL) {
        return;
    }

    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
        lumo_input_keyboard_attach(compositor, device);
        return;
    case WLR_INPUT_DEVICE_POINTER:
    case WLR_INPUT_DEVICE_TOUCH:
    case WLR_INPUT_DEVICE_TABLET:
        lumo_input_pointer_device_attach(compositor, device);
        return;
    case WLR_INPUT_DEVICE_TABLET_PAD:
    case WLR_INPUT_DEVICE_SWITCH:
    default:
        wlr_log(WLR_INFO, "input: ignoring unsupported device '%s'",
            device->name != NULL ? device->name : "(unknown)");
        return;
    }
}

int lumo_input_start(struct lumo_compositor *compositor) {
    struct lumo_input_state *state;

    if (compositor == NULL || compositor->display == NULL ||
            compositor->output_layout == NULL) {
        return -1;
    }
    if (compositor->input_started) {
        return 0;
    }

    state = calloc(1, sizeof(*state));
    if (state == NULL) {
        wlr_log_errno(WLR_ERROR, "input: failed to allocate input state");
        return -1;
    }

    compositor->seat = wlr_seat_create(compositor->display, "seat0");
    if (compositor->seat == NULL) {
        wlr_log(WLR_ERROR, "input: failed to create seat");
        free(state);
        return -1;
    }

#if LUMO_ENABLE_XWAYLAND
    if (compositor->xwayland != NULL) {
        wlr_xwayland_set_seat(compositor->xwayland, compositor->seat);
    }
#endif

    compositor->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (compositor->xkb_context == NULL) {
        wlr_log(WLR_ERROR, "input: failed to create xkb context");
        wlr_seat_destroy(compositor->seat);
        compositor->seat = NULL;
        free(state);
        return -1;
    }

    compositor->cursor = wlr_cursor_create();
    if (compositor->cursor == NULL) {
        wlr_log(WLR_ERROR, "input: failed to create cursor");
        xkb_context_unref(compositor->xkb_context);
        compositor->xkb_context = NULL;
        wlr_seat_destroy(compositor->seat);
        compositor->seat = NULL;
        free(state);
        return -1;
    }

    compositor->cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
    if (compositor->cursor_mgr == NULL) {
        wlr_log(WLR_ERROR, "input: failed to create cursor theme manager");
        wlr_cursor_destroy(compositor->cursor);
        compositor->cursor = NULL;
        xkb_context_unref(compositor->xkb_context);
        compositor->xkb_context = NULL;
        wlr_seat_destroy(compositor->seat);
        compositor->seat = NULL;
        free(state);
        return -1;
    }

    wlr_cursor_attach_output_layout(compositor->cursor, compositor->output_layout);

    compositor->backend_new_input.notify = lumo_input_backend_new_input;
    wl_signal_add(&compositor->backend->events.new_input,
        &compositor->backend_new_input);

    compositor->cursor_motion.notify = lumo_input_pointer_motion;
    wl_signal_add(&compositor->cursor->events.motion, &compositor->cursor_motion);
    compositor->cursor_motion_absolute.notify = lumo_input_pointer_motion_absolute;
    wl_signal_add(&compositor->cursor->events.motion_absolute,
        &compositor->cursor_motion_absolute);
    compositor->cursor_button.notify = lumo_input_pointer_button;
    wl_signal_add(&compositor->cursor->events.button, &compositor->cursor_button);
    compositor->cursor_axis.notify = lumo_input_pointer_axis;
    wl_signal_add(&compositor->cursor->events.axis, &compositor->cursor_axis);
    compositor->cursor_frame.notify = lumo_input_pointer_frame;
    wl_signal_add(&compositor->cursor->events.frame, &compositor->cursor_frame);
    compositor->cursor_swipe_begin.notify = lumo_input_pointer_swipe_begin;
    wl_signal_add(&compositor->cursor->events.swipe_begin,
        &compositor->cursor_swipe_begin);
    compositor->cursor_swipe_update.notify = lumo_input_pointer_swipe_update;
    wl_signal_add(&compositor->cursor->events.swipe_update,
        &compositor->cursor_swipe_update);
    compositor->cursor_swipe_end.notify = lumo_input_pointer_swipe_end;
    wl_signal_add(&compositor->cursor->events.swipe_end,
        &compositor->cursor_swipe_end);
    compositor->cursor_pinch_begin.notify = lumo_input_pointer_pinch_begin;
    wl_signal_add(&compositor->cursor->events.pinch_begin,
        &compositor->cursor_pinch_begin);
    compositor->cursor_pinch_update.notify = lumo_input_pointer_pinch_update;
    wl_signal_add(&compositor->cursor->events.pinch_update,
        &compositor->cursor_pinch_update);
    compositor->cursor_pinch_end.notify = lumo_input_pointer_pinch_end;
    wl_signal_add(&compositor->cursor->events.pinch_end,
        &compositor->cursor_pinch_end);
    compositor->cursor_hold_begin.notify = lumo_input_pointer_hold_begin;
    wl_signal_add(&compositor->cursor->events.hold_begin,
        &compositor->cursor_hold_begin);
    compositor->cursor_hold_end.notify = lumo_input_pointer_hold_end;
    wl_signal_add(&compositor->cursor->events.hold_end,
        &compositor->cursor_hold_end);
    compositor->cursor_touch_down.notify = lumo_input_touch_down;
    wl_signal_add(&compositor->cursor->events.touch_down,
        &compositor->cursor_touch_down);
    compositor->cursor_touch_motion.notify = lumo_input_touch_motion;
    wl_signal_add(&compositor->cursor->events.touch_motion,
        &compositor->cursor_touch_motion);
    compositor->cursor_touch_up.notify = lumo_input_touch_up;
    wl_signal_add(&compositor->cursor->events.touch_up,
        &compositor->cursor_touch_up);
    compositor->cursor_touch_cancel.notify = lumo_input_touch_cancel;
    wl_signal_add(&compositor->cursor->events.touch_cancel,
        &compositor->cursor_touch_cancel);
    compositor->cursor_touch_frame.notify = lumo_input_touch_frame;
    wl_signal_add(&compositor->cursor->events.touch_frame,
        &compositor->cursor_touch_frame);

    compositor->seat_request_cursor.notify = lumo_input_request_set_cursor;
    wl_signal_add(&compositor->seat->events.request_set_cursor,
        &compositor->seat_request_cursor);
    compositor->seat_request_set_selection.notify = lumo_input_request_set_selection;
    wl_signal_add(&compositor->seat->events.request_set_selection,
        &compositor->seat_request_set_selection);

    compositor->input_state = state;
    compositor->input_started = true;
    lumo_input_refresh_capabilities(compositor);
    lumo_input_maybe_start_gesture_timer(compositor);
    wlr_log(WLR_INFO, "input: ready for touchscreen and pointer devices");
    return 0;
}

void lumo_input_stop(struct lumo_compositor *compositor) {
    struct lumo_input_state *state;
    struct lumo_touch_point *point, *point_tmp;

    if (compositor == NULL || !compositor->input_started) {
        return;
    }

    state = lumo_input_state_from(compositor);
    if (state != NULL && state->gesture_timer != NULL) {
        wl_event_source_remove(state->gesture_timer);
        state->gesture_timer = NULL;
    }

    wl_list_for_each_safe(point, point_tmp, &compositor->touch_points, link) {
        lumo_input_remove_touch_point(compositor, point);
    }

    if (compositor->cursor != NULL) {
        wl_list_remove(&compositor->cursor_motion.link);
        wl_list_remove(&compositor->cursor_motion_absolute.link);
        wl_list_remove(&compositor->cursor_button.link);
        wl_list_remove(&compositor->cursor_axis.link);
        wl_list_remove(&compositor->cursor_frame.link);
        wl_list_remove(&compositor->cursor_swipe_begin.link);
        wl_list_remove(&compositor->cursor_swipe_update.link);
        wl_list_remove(&compositor->cursor_swipe_end.link);
        wl_list_remove(&compositor->cursor_pinch_begin.link);
        wl_list_remove(&compositor->cursor_pinch_update.link);
        wl_list_remove(&compositor->cursor_pinch_end.link);
        wl_list_remove(&compositor->cursor_hold_begin.link);
        wl_list_remove(&compositor->cursor_hold_end.link);
        wl_list_remove(&compositor->cursor_touch_down.link);
        wl_list_remove(&compositor->cursor_touch_motion.link);
        wl_list_remove(&compositor->cursor_touch_up.link);
        wl_list_remove(&compositor->cursor_touch_cancel.link);
        wl_list_remove(&compositor->cursor_touch_frame.link);
        wlr_cursor_destroy(compositor->cursor);
        compositor->cursor = NULL;
    }

    if (compositor->cursor_mgr != NULL) {
        wlr_xcursor_manager_destroy(compositor->cursor_mgr);
        compositor->cursor_mgr = NULL;
    }

    if (compositor->seat != NULL) {
        wl_list_remove(&compositor->seat_request_cursor.link);
        wl_list_remove(&compositor->seat_request_set_selection.link);
        wlr_seat_destroy(compositor->seat);
        compositor->seat = NULL;
    }

    if (compositor->xkb_context != NULL) {
        xkb_context_unref(compositor->xkb_context);
        compositor->xkb_context = NULL;
    }

    if (compositor->backend_new_input.link.prev != NULL &&
            compositor->backend_new_input.link.next != NULL) {
        wl_list_remove(&compositor->backend_new_input.link);
    }

    free(state);
    compositor->input_state = NULL;
    compositor->input_started = false;
    compositor->pointer_devices = 0;
    compositor->touch_devices = 0;
    compositor->keyboard_devices = 0;
}

void lumo_input_set_rotation(
    struct lumo_compositor *compositor,
    enum lumo_rotation rotation
) {
    if (compositor == NULL) {
        return;
    }

    compositor->active_rotation = rotation;
    lumo_shell_state_broadcast_rotation(compositor, rotation);
}
