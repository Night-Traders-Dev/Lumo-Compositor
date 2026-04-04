#include "lumo/compositor.h"

#include <float.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/input-event-codes.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/util/box.h>

#include "input_internal.h"

static void lumo_input_touch_point_surface_destroy(
    struct wl_listener *listener,
    void *data
);
static int lumo_input_gesture_timeout_cb(void *data);

void lumo_input_touch_audit_log(
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

struct lumo_touch_point *lumo_input_touch_point_for_id(
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

void lumo_input_touch_sample_append(
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

void lumo_input_touch_debug_update(
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

void lumo_input_touch_samples_clear(struct lumo_touch_point *point) {
    struct lumo_touch_sample *sample, *tmp;

    if (point == NULL) {
        return;
    }

    wl_list_for_each_safe(sample, tmp, &point->samples, link) {
        wl_list_remove(&sample->link);
        free(sample);
    }
}

void lumo_input_touch_point_bind_surface(
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

void lumo_input_touch_point_destroy(struct lumo_touch_point *point) {
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

void lumo_input_focus_surface(
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
                    /* only auto-show for the terminal app.
                     * Notes enables text-input when a note enters edit mode,
                     * which keeps the OSK aligned with actual editability. */
                    if (!compositor->keyboard_visible &&
                            tl->xdg_toplevel != NULL &&
                            tl->xdg_toplevel->app_id != NULL &&
                            strstr(tl->xdg_toplevel->app_id, "messages")) {
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

void lumo_input_refresh_capabilities(struct lumo_compositor *compositor) {
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

void lumo_input_remove_touch_point(
    struct lumo_compositor *compositor,
    struct lumo_touch_point *point
) {
    if (compositor == NULL || point == NULL) {
        return;
    }

    lumo_input_touch_point_destroy(point);
}

struct wlr_surface *lumo_input_shell_surface_for_hitbox(
    struct lumo_compositor *compositor,
    const struct lumo_hitbox *hitbox
) {
    const char *preferred_namespace;
    struct lumo_layer_surface *ls;

    if (compositor == NULL) {
        return NULL;
    }

    preferred_namespace = lumo_hitbox_shell_namespace(hitbox);
    if (preferred_namespace != NULL) {
        wl_list_for_each(ls, &compositor->layer_surfaces, link) {
            if (ls->layer_surface == NULL ||
                    ls->layer_surface->surface == NULL ||
                    ls->layer_surface->namespace == NULL) {
                continue;
            }
            if (strcmp(ls->layer_surface->namespace, preferred_namespace) == 0) {
                return ls->layer_surface->surface;
            }
        }
    }

    wl_list_for_each(ls, &compositor->layer_surfaces, link) {
        if (ls->layer_surface != NULL &&
                ls->layer_surface->surface != NULL &&
                ls->layer_surface->current.layer ==
                    ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY) {
            return ls->layer_surface->surface;
        }
    }

    wl_list_for_each(ls, &compositor->layer_surfaces, link) {
        if (ls->layer_surface != NULL &&
                ls->layer_surface->surface != NULL &&
                ls->layer_surface->current.layer ==
                    ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
            return ls->layer_surface->surface;
        }
    }

    return NULL;
}

void lumo_input_replay_touch_point(
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
        bool launcher_surface = false;
        bool launcher_coords_invalid = false;

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
            if (ls->layer_surface->surface == point->surface &&
                    ls->layer_surface->namespace != NULL &&
                    strcmp(ls->layer_surface->namespace, "launcher") == 0) {
                launcher_surface = true;
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
        if (launcher_surface) {
            struct lumo_touch_sample *s;
            bool launcher_coords_corrected = false;

            wl_list_for_each(s, &point->samples, link) {
                if (s->sx != s->lx || s->sy != s->ly) {
                    launcher_coords_invalid = true;
                    break;
                }
            }
            if (launcher_coords_invalid) {
                wl_list_for_each(s, &point->samples, link) {
                    s->sx = s->lx;
                    s->sy = s->ly;
                }
                launcher_coords_corrected = true;
            }
            if (launcher_coords_corrected) {
                wlr_log(WLR_INFO,
                    "input: replay corrected launcher coords at %.0f,%.0f",
                    point->down_lx, point->down_ly);
            }
        }
    }

    /* don't move keyboard focus when replaying to a shell layer surface
     * (OSK/launcher) — the keyboard must stay focused on the app toplevel
     * so that wlr_seat_keyboard_notify_key delivers keys to the app */
    {
        bool is_shell_surface = false;
        struct lumo_layer_surface *ls;
        wl_list_for_each(ls, &compositor->layer_surfaces, link) {
            if (ls->layer_surface != NULL &&
                    ls->layer_surface->surface == point->surface) {
                is_shell_surface = true;
                break;
            }
        }
        if (!is_shell_surface) {
            lumo_input_focus_surface(compositor, point->surface);
        }
    }
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

void lumo_input_maybe_start_gesture_timer(struct lumo_compositor *compositor) {
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
            /* only replay to shell surface if the touch was captured by
             * the gesture handle hitbox — system edge zone captures
             * should wait for touch-up to trigger the edge action,
             * not replay as a tap on the shell surface */
            if (lumo_hitbox_is_shell_gesture(point->hitbox)) {
                lumo_input_replay_touch_point(compositor, point);
                replayed = true;
            }
        }
    }

    if (replayed && compositor->seat != NULL) {
        wlr_seat_touch_notify_frame(compositor->seat);
    }

    lumo_input_maybe_start_gesture_timer(compositor);
    return 0;
}

void lumo_input_touch_point_begin_capture(
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

void lumo_input_touch_point_deliver_now(
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
    wlr_seat_touch_notify_frame(compositor->seat);

    lumo_input_touch_samples_clear(point);
    wlr_log(WLR_INFO, "input: touch %d delivered to surface",
        point->touch_id);
}

double lumo_input_touch_point_edge_progress(
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
double lumo_input_touch_point_edge_velocity(
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
bool lumo_input_edge_angle_valid(
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

void lumo_input_touch_point_trigger_edge_action(
    struct lumo_compositor *compositor,
    struct lumo_touch_point *point,
    uint32_t time_msec
) {
    bool closed_focused_app = false;
    bool is_tap = false;
    double progress;

    if (compositor == NULL || point == NULL || point->gesture_triggered) {
        return;
    }

    point->gesture_triggered = true;
    progress = lumo_input_touch_point_edge_progress(point,
        point->lx, point->ly);
    is_tap = progress < 12.0;

    /* block all edge gestures during boot splash */
    if (access("/run/user/1001/lumo-boot-active", F_OK) == 0) {
        wlr_log(WLR_INFO, "input: edge gesture blocked (boot splash active)");
        return;
    }

    switch (point->capture_edge) {
    case LUMO_EDGE_TOP: {
        if (compositor->launcher_visible) {
            lumo_protocol_set_launcher_visible(compositor, false);
        }

        struct lumo_output *top_output = lumo_input_first_output(compositor);
        struct wlr_box out_box = {0};
        if (top_output != NULL && top_output->wlr_output != NULL) {
            wlr_output_layout_get_box(compositor->output_layout,
                top_output->wlr_output, &out_box);
        }
        /* Use layout box width — this is the rotated/effective width.
         * point->down_lx is in layout coordinates, so it matches. */
        double out_w = out_box.width > 0 ? (double)out_box.width : 1280.0;
        double third = out_w / 3.0;
        int zone = (point->down_lx < third) ? 0 :
            (point->down_lx < third * 2.0) ? 1 : 2;

        /* close all other panels before toggling the target */
        if (zone != 0 && compositor->notification_panel_visible)
            lumo_protocol_set_notification_panel_visible(compositor, false);
        if (zone != 1 && compositor->time_panel_visible)
            lumo_protocol_set_time_panel_visible(compositor, false);
        if (zone != 2 && compositor->quick_settings_visible)
            lumo_protocol_set_quick_settings_visible(compositor, false);

        if (zone == 0) {
            /* left third: notifications */
            lumo_protocol_set_notification_panel_visible(compositor,
                !compositor->notification_panel_visible);
            wlr_log(WLR_INFO,
                "input: touch %d toggled notifications at %u",
                point->touch_id, time_msec);
        } else if (zone == 1) {
            /* center third: date/time/weather */
            lumo_protocol_set_time_panel_visible(compositor,
                !compositor->time_panel_visible);
            wlr_log(WLR_INFO,
                "input: touch %d toggled time panel at %u",
                point->touch_id, time_msec);
        } else {
            /* right third: quick settings */
            lumo_protocol_set_quick_settings_visible(compositor,
                !compositor->quick_settings_visible);
            wlr_log(WLR_INFO,
                "input: touch %d toggled quick settings at %u",
                point->touch_id, time_msec);
        }
        return;
    }
    case LUMO_EDGE_LEFT:
        /* left edge → toggle sidebar (running apps multitasking bar) */
        if (compositor->touch_audit_active) {
            lumo_touch_audit_set_active(compositor, false);
        } else if (compositor->notification_panel_visible) {
            lumo_protocol_set_notification_panel_visible(compositor, false);
        } else if (compositor->time_panel_visible) {
            lumo_protocol_set_time_panel_visible(compositor, false);
        } else if (compositor->quick_settings_visible) {
            lumo_protocol_set_quick_settings_visible(compositor, false);
        } else if (compositor->launcher_visible) {
            lumo_protocol_set_launcher_visible(compositor, false);
        } else {
            /* show sidebar (auto-hide timer handles closing) */
            lumo_protocol_set_sidebar_visible(compositor, true);
        }
        wlr_log(WLR_INFO, "input: touch %d triggered left-edge at %u",
            point->touch_id, time_msec);
        return;
    case LUMO_EDGE_RIGHT: {
        /* right edge → back gesture.  If an app is focused, inject
         * KEY_BACK (keycode 158) so apps can handle in-app navigation.
         * If the app doesn't handle it or we're at the home screen
         * of an app, this effectively goes back to home. */
        if (compositor->touch_audit_active) {
            lumo_touch_audit_set_active(compositor, false);
            return;
        }
        /* dismiss any open panels first */
        if (compositor->notification_panel_visible)
            lumo_protocol_set_notification_panel_visible(compositor, false);
        if (compositor->time_panel_visible)
            lumo_protocol_set_time_panel_visible(compositor, false);
        if (compositor->quick_settings_visible)
            lumo_protocol_set_quick_settings_visible(compositor, false);
        if (compositor->sidebar_visible)
            lumo_protocol_set_sidebar_visible(compositor, false);
        if (compositor->keyboard_visible) {
            lumo_protocol_set_keyboard_visible(compositor, false);
            wlr_log(WLR_INFO, "input: right-edge back hid keyboard at %u",
                time_msec);
            return;
        }
        if (compositor->launcher_visible) {
            lumo_protocol_set_launcher_visible(compositor, false);
            wlr_log(WLR_INFO, "input: right-edge back closed launcher at %u",
                time_msec);
            return;
        }
        /* close focused app → go home.  Sends xdg close request so
         * GTK4/WebKit apps actually dismiss (just hiding the scene node
         * isn't enough — GTK re-presents the window). */
        if (!wl_list_empty(&compositor->toplevels)) {
            struct lumo_toplevel *tl;
            wl_list_for_each(tl, &compositor->toplevels, link) {
                if (tl->scene_tree->node.enabled) {
                    wlr_xdg_toplevel_send_close(tl->xdg_toplevel);
                    tl->scene_tree->node.enabled = false;
                    wlr_seat_keyboard_clear_focus(compositor->seat);
                    lumo_protocol_set_scrim_state(compositor, LUMO_SCRIM_HIDDEN);
                    wlr_log(WLR_INFO,
                        "input: touch %d back-closed app at %u",
                        point->touch_id, time_msec);
                    return;
                }
            }
        }
        wlr_log(WLR_INFO, "input: touch %d right-edge back (no-op) at %u",
            point->touch_id, time_msec);
        return;
    }
    case LUMO_EDGE_BOTTOM:
        /* bottom edge → go home (minimize all, show desktop) */
        if (compositor->touch_audit_active) {
            lumo_touch_audit_set_active(compositor, false);
        }
        /* close launcher if open */
        if (compositor->launcher_visible) {
            lumo_protocol_set_launcher_visible(compositor, false);
            if (compositor->keyboard_visible)
                lumo_protocol_set_keyboard_visible(compositor, false);
            wlr_log(WLR_INFO,
                "input: touch %d bottom swipe closed launcher at %u",
                point->touch_id, time_msec);
            return;
        }
        /* close sidebar if open */
        if (compositor->sidebar_visible)
            lumo_protocol_set_sidebar_visible(compositor, false);
        /* hide keyboard if visible */
        if (compositor->keyboard_visible) {
            lumo_protocol_set_keyboard_visible(compositor, false);
            wlr_log(WLR_INFO,
                "input: touch %d bottom swipe closed keyboard at %u",
                point->touch_id, time_msec);
            return;
        }
        /* close all running apps (go to home screen) */
        if (!wl_list_empty(&compositor->toplevels)) {
            struct lumo_toplevel *tl;
            wl_list_for_each(tl, &compositor->toplevels, link) {
                wlr_xdg_toplevel_send_close(tl->xdg_toplevel);
                tl->scene_tree->node.enabled = false;
            }
            wlr_seat_keyboard_clear_focus(compositor->seat);
            lumo_protocol_set_scrim_state(compositor, LUMO_SCRIM_HIDDEN);
            wlr_log(WLR_INFO,
                "input: touch %d bottom swipe → home at %u",
                point->touch_id, time_msec);
        }
        return;
    case LUMO_EDGE_NONE:
    default:
        wlr_log(WLR_INFO, "input: touch %d edge action skipped at %u",
            point->touch_id, time_msec);
        return;
    }
}
