#include "lumo/compositor.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <linux/input-event-codes.h>

#ifndef KEY_ZOOMIN
#define KEY_ZOOMIN 0x1a2
#endif
#ifndef KEY_ZOOMOUT
#define KEY_ZOOMOUT 0x1a3
#endif

#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/util/box.h>
#include <wlr/util/transform.h>

#include "input_internal.h"

struct lumo_input_state *lumo_input_state_from(
    struct lumo_compositor *compositor
) {
    return compositor != NULL
        ? (struct lumo_input_state *)compositor->input_state
        : NULL;
}

uint32_t lumo_input_now_msec(void) {
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint32_t)(now.tv_sec * 1000u + now.tv_nsec / 1000000u);
}

struct lumo_output *lumo_input_first_output(
    struct lumo_compositor *compositor
) {
    struct lumo_output *output;

    if (compositor == NULL || wl_list_empty(&compositor->outputs)) {
        return NULL;
    }

    output = wl_container_of(compositor->outputs.next, output, link);
    return output;
}

struct lumo_output *lumo_input_output_from_wlr(
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

struct lumo_output *lumo_input_output_for_layout_coords(
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

bool lumo_input_transform_touch_coords(
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

struct lumo_scene_object_head *lumo_input_scene_object_from_node(
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

bool lumo_input_surface_target_at(
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

bool lumo_input_target_is_shell(const struct lumo_surface_target *target) {
    if (target == NULL) {
        return false;
    }

    return target->role == LUMO_SCENE_OBJECT_LAYER_SURFACE ||
        target->role == LUMO_SCENE_OBJECT_POPUP;
}

bool lumo_input_hitbox_is_shell_reserved(
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

enum lumo_edge_zone lumo_input_system_edge_zone(
    struct lumo_compositor *compositor,
    const struct lumo_output *output,
    double lx,
    double ly
) {
    struct wlr_box box = {0};
    struct lumo_shell_surface_config shell_config = {0};
    double side_threshold;
    double top_threshold;
    double bottom_threshold;
    double top_dist;
    double left_dist;
    double right_dist;
    double bottom_dist;
    double best = DBL_MAX;
    enum lumo_edge_zone zone = LUMO_EDGE_NONE;

    if (compositor == NULL || output == NULL || output->wlr_output == NULL) {
        return LUMO_EDGE_NONE;
    }

    wlr_output_layout_get_box(compositor->output_layout, output->wlr_output,
        &box);
    if (wlr_box_empty(&box)) {
        return LUMO_EDGE_NONE;
    }

    side_threshold = compositor->gesture_threshold > 0.0
        ? compositor->gesture_threshold
        : 24.0;
    top_threshold = side_threshold;
    bottom_threshold = side_threshold;

    if (lumo_shell_surface_config_for_mode(LUMO_SHELL_MODE_STATUS,
            (uint32_t)box.width, (uint32_t)box.height, &shell_config) &&
            shell_config.height > 0) {
        top_threshold = shell_config.height;
    }
    if (lumo_shell_surface_config_for_mode(LUMO_SHELL_MODE_GESTURE,
            (uint32_t)box.width, (uint32_t)box.height, &shell_config) &&
            shell_config.height > 0) {
        bottom_threshold = shell_config.height;
    }

    if (lx < box.x || ly < box.y ||
            lx >= box.x + box.width ||
            ly >= box.y + box.height) {
        return LUMO_EDGE_NONE;
    }

    top_dist = ly - box.y;
    if (top_dist >= 0.0 && top_dist <= top_threshold && top_dist < best) {
        best = top_dist;
        zone = LUMO_EDGE_TOP;
    }

    left_dist = lx - box.x;
    if (left_dist >= 0.0 && left_dist <= side_threshold && left_dist < best) {
        best = left_dist;
        zone = LUMO_EDGE_LEFT;
    }

    right_dist = box.x + box.width - lx;
    if (right_dist >= 0.0 && right_dist <= side_threshold &&
            right_dist < best) {
        best = right_dist;
        zone = LUMO_EDGE_RIGHT;
    }

    if (!compositor->launcher_visible) {
        bottom_dist = box.y + box.height - ly;
        if (bottom_dist >= 0.0 && bottom_dist <= bottom_threshold &&
                bottom_dist < best) {
            zone = LUMO_EDGE_BOTTOM;
        }
    }

    return zone;
}

static void lumo_input_touch_down(struct wl_listener *listener, void *data);
static void lumo_input_touch_motion(struct wl_listener *listener, void *data);
static void lumo_input_touch_up(struct wl_listener *listener, void *data);
static void lumo_input_touch_cancel(struct wl_listener *listener, void *data);
static void lumo_input_touch_frame(struct wl_listener *listener, void *data);

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

    if (compositor->pinch_active &&
            (event->touch_id == compositor->pinch_touch_ids[0] ||
             event->touch_id == compositor->pinch_touch_ids[1])) {
        int32_t other_id = (event->touch_id == compositor->pinch_touch_ids[0])
            ? compositor->pinch_touch_ids[1] : compositor->pinch_touch_ids[0];
        struct lumo_touch_point *other_pt =
            lumo_input_touch_point_for_id(compositor, other_id);
        point->lx = lx;
        point->ly = ly;
        if (other_pt != NULL && compositor->pinch_initial_distance > 0.0) {
            static double last_notified_scale = 1.0;
            double dx = lx - other_pt->lx;
            double dy = ly - other_pt->ly;
            double dist = sqrt(dx * dx + dy * dy);
            double new_scale = dist / compositor->pinch_initial_distance;
            if (new_scale < 0.5) new_scale = 0.5;
            if (new_scale > 4.0) new_scale = 4.0;
            compositor->pinch_scale = new_scale;
            wlr_pointer_gestures_v1_send_pinch_update(
                compositor->pointer_gestures, compositor->seat,
                event->time_msec, 0.0, 0.0, new_scale, 0.0);
            if (fabs(new_scale - last_notified_scale) > 0.1) {
                uint32_t key = new_scale > last_notified_scale
                    ? KEY_ZOOMIN : KEY_ZOOMOUT;
                wlr_seat_keyboard_notify_key(compositor->seat,
                    event->time_msec, key,
                    WL_KEYBOARD_KEY_STATE_PRESSED);
                wlr_seat_keyboard_notify_key(compositor->seat,
                    event->time_msec, key,
                    WL_KEYBOARD_KEY_STATE_RELEASED);
                last_notified_scale = new_scale;
            }
        }
        return;
    }

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

    /* swipe-down anywhere to close the app drawer — check before
     * delivering motion to surfaces so it works even for touches
     * on the dark overlay outside the launcher panel hitbox */
    if (compositor->launcher_visible && !point->gesture_triggered) {
        double dy = ly - point->down_ly;
        if (dy > threshold) {
            lumo_protocol_set_launcher_visible(compositor, false);
            point->gesture_triggered = true;
            wlr_log(WLR_INFO,
                "input: touch %d swipe-down closed launcher (dy=%.0f)",
                point->touch_id, dy);
            lumo_input_remove_touch_point(compositor, point);
            return;
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

    /* pinch-to-zoom: second finger on app surface starts pinch */
    if (!compositor->pinch_active && !wl_list_empty(&compositor->toplevels)) {
        struct lumo_touch_point *other = NULL;
        int delivered_count = 0;
        struct lumo_touch_point *iter;
        wl_list_for_each(iter, &compositor->touch_points, link) {
            if (iter != point && iter->delivered && !iter->captured) {
                other = iter;
                delivered_count++;
            }
        }
        if (delivered_count == 1 && other != NULL) {
            double dx = point->lx - other->lx;
            double dy = point->ly - other->ly;
            double dist = sqrt(dx * dx + dy * dy);
            if (dist > 20.0) {
                compositor->pinch_active = true;
                compositor->pinch_touch_ids[0] = other->touch_id;
                compositor->pinch_touch_ids[1] = point->touch_id;
                compositor->pinch_initial_distance = dist;
                compositor->pinch_scale = 1.0;
                point->gesture_triggered = true;
                other->gesture_triggered = true;
                {
                    struct wlr_touch_point *seat_tp =
                        wlr_seat_touch_get_point(compositor->seat,
                            other->touch_id);
                    if (seat_tp != NULL) {
                        wlr_seat_touch_notify_cancel(compositor->seat,
                            seat_tp->client);
                    }
                }
                /* Send pointer focus + pinch begin for gesture protocol */
                if (other->surface != NULL) {
                    wlr_seat_pointer_notify_enter(compositor->seat, other->surface,
                        other->sx, other->sy);
                    wlr_pointer_gestures_v1_send_pinch_begin(
                        compositor->pointer_gestures, compositor->seat,
                        lumo_input_now_msec(), 2);
                }
                /* Hide keyboard if auto-shown during pinch */
                if (compositor->keyboard_auto_shown && compositor->keyboard_visible) {
                    lumo_protocol_set_keyboard_visible(compositor, false);
                }
                wlr_log(WLR_INFO, "input: pinch started (ids %d,%d dist=%.0f)",
                    other->touch_id, point->touch_id, dist);
                /* keep both points alive for motion tracking */
                return;
            }
        }
    }

    /* Ignore additional fingers during active pinch */
    if (compositor->pinch_active) {
        lumo_input_remove_touch_point(compositor, point);
        return;
    }

    if (compositor->quick_settings_visible || compositor->time_panel_visible ||
            compositor->notification_panel_visible) {
        bool in_panel = false;
        struct lumo_rect panel_rect = {0};
        struct lumo_output *o = lumo_input_first_output(compositor);

        if (o != NULL && o->wlr_output != NULL) {
            int ow = 0, oh = 0;

            wlr_output_effective_resolution(o->wlr_output, &ow, &oh);

            if (compositor->quick_settings_visible &&
                    lumo_shell_quick_settings_panel_rect((uint32_t)ow,
                        (uint32_t)oh, &panel_rect) &&
                    lumo_rect_contains(&panel_rect, point->lx, point->ly)) {
                in_panel = true;
            }
            if (!in_panel && compositor->time_panel_visible &&
                    lumo_shell_time_panel_rect((uint32_t)ow,
                        (uint32_t)oh, &panel_rect) &&
                    lumo_rect_contains(&panel_rect, point->lx, point->ly)) {
                in_panel = true;
            }
            if (!in_panel && compositor->notification_panel_visible &&
                    lumo_shell_notification_panel_rect((uint32_t)ow,
                        (uint32_t)oh, &panel_rect) &&
                    lumo_rect_contains(&panel_rect, point->lx, point->ly)) {
                in_panel = true;
            }
        }

        if (!in_panel) {
            if (compositor->quick_settings_visible) {
                lumo_protocol_set_quick_settings_visible(compositor, false);
            }
            if (compositor->time_panel_visible) {
                lumo_protocol_set_time_panel_visible(compositor, false);
            }
            if (compositor->notification_panel_visible) {
                lumo_protocol_set_notification_panel_visible(compositor, false);
            }
            wlr_log(WLR_INFO,
                "input: touch %d dismissed panel (outside tap)",
                point->touch_id);
            lumo_input_remove_touch_point(compositor, point);
            return;
        }
    }

    /* tap outside the launcher panel to dismiss the app drawer */
    if (compositor->launcher_visible) {
        bool in_launcher = false;
        struct lumo_rect launcher_rect = {0};
        struct lumo_output *o = lumo_input_first_output(compositor);

        if (o != NULL && o->wlr_output != NULL) {
            int ow = 0, oh = 0;

            wlr_output_effective_resolution(o->wlr_output, &ow, &oh);

            if (lumo_shell_launcher_panel_rect((uint32_t)ow,
                    (uint32_t)oh, &launcher_rect) &&
                    lumo_rect_contains(&launcher_rect,
                        point->lx, point->ly)) {
                in_launcher = true;
            }
        }

        if (!in_launcher) {
            lumo_protocol_set_launcher_visible(compositor, false);
            wlr_log(WLR_INFO,
                "input: touch %d dismissed launcher (outside tap)",
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

    /* --- edge gestures: bottom-edge takes priority over OSK/launcher
     * hitboxes but NOT over the gesture handle hitbox (which has its
     * own tap/swipe logic) --- */

    edge_zone = lumo_input_system_edge_zone(compositor, output, point->lx,
        point->ly);
    if (compositor->touch_audit_active && edge_zone != LUMO_EDGE_LEFT) {
        edge_zone = LUMO_EDGE_NONE;
    }
    if (edge_zone != LUMO_EDGE_NONE &&
            (point->hitbox == NULL ||
             point->hitbox->kind != LUMO_HITBOX_EDGE_GESTURE)) {
        /* When a toplevel app is focused and no panels are open, suppress
         * top-edge capture so app UI at the top (e.g. browser tab close
         * buttons) receives touch input.  The status bar hitbox still
         * provides panel access via LUMO_HITBOX_EDGE_GESTURE. */
        bool app_focused = !wl_list_empty(&compositor->toplevels) &&
            !compositor->launcher_visible &&
            !compositor->quick_settings_visible &&
            !compositor->time_panel_visible &&
            !compositor->notification_panel_visible;
        if (app_focused && (edge_zone == LUMO_EDGE_TOP ||
                edge_zone == LUMO_EDGE_BOTTOM)) {
            /* let the touch fall through to the app — the gesture
             * handle hitbox still provides swipe-to-close access */
        } else {
            point->capture_edge = edge_zone;
            lumo_input_touch_point_begin_capture(compositor, point, &target,
                event->time_msec);
            lumo_input_touch_debug_update(compositor, point,
                LUMO_TOUCH_SAMPLE_DOWN, true, point->lx, point->ly);
            return;
        }
    }

    /* --- hitbox checks (edges, gestures, OSK, launcher) --- */

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
        /* suppress top-edge gesture hitbox when an app is focused and
         * no panels are open, so app controls at the top of the screen
         * (e.g. browser tab close) receive touch input */
        bool suppress_top = false;
        enum lumo_edge_zone hb_edge = lumo_hitbox_edge_zone(point->hitbox);
        if (hb_edge == LUMO_EDGE_TOP &&
                !wl_list_empty(&compositor->toplevels) &&
                !compositor->launcher_visible &&
                !compositor->quick_settings_visible &&
                !compositor->time_panel_visible &&
                !compositor->notification_panel_visible) {
            suppress_top = true;
        }
        if (!suppress_top) {
            point->capture_edge = lumo_hitbox_edge_zone(point->hitbox);
            lumo_input_touch_point_begin_capture(compositor, point, &target,
                event->time_msec);
            lumo_input_touch_debug_update(compositor, point,
                LUMO_TOUCH_SAMPLE_DOWN, true, point->lx, point->ly);
            return;
        }
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
            compositor->notification_panel_visible ||
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

    if (compositor->pinch_active &&
            (event->touch_id == compositor->pinch_touch_ids[0] ||
             event->touch_id == compositor->pinch_touch_ids[1])) {
        wlr_log(WLR_INFO, "input: pinch ended (scale=%.2f)", compositor->pinch_scale);
        wlr_pointer_gestures_v1_send_pinch_end(
            compositor->pointer_gestures, compositor->seat,
            event->time_msec, false);
        int32_t other_id = (event->touch_id == compositor->pinch_touch_ids[0])
            ? compositor->pinch_touch_ids[1] : compositor->pinch_touch_ids[0];
        struct lumo_touch_point *other_pt =
            lumo_input_touch_point_for_id(compositor, other_id);
        compositor->pinch_active = false;
        compositor->pinch_touch_ids[0] = -1;
        compositor->pinch_touch_ids[1] = -1;
        lumo_input_remove_touch_point(compositor, point);
        if (other_pt != NULL)
            lumo_input_remove_touch_point(compositor, other_pt);
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
                /* very short movement = tap on the edge/handle.
                 * suppress top-edge taps from the thin system edge zone
                 * when a toplevel is focused to prevent accidental panel
                 * triggers, but allow taps on the status bar hitbox so
                 * panels remain accessible while apps are running. */
                bool suppress = false;
                if ((point->capture_edge == LUMO_EDGE_TOP ||
                        point->capture_edge == LUMO_EDGE_BOTTOM) &&
                        !wl_list_empty(&compositor->toplevels) &&
                        !compositor->launcher_visible &&
                        !compositor->time_panel_visible &&
                        !compositor->quick_settings_visible &&
                        !compositor->notification_panel_visible &&
                        (point->hitbox == NULL ||
                         point->hitbox->kind != LUMO_HITBOX_EDGE_GESTURE)) {
                    suppress = true;
                    wlr_log(WLR_INFO,
                        "input: touch %d edge tap suppressed "
                        "(app focused, system edge zone=%s)",
                        point->touch_id,
                        lumo_edge_zone_name(point->capture_edge));
                }
                if (!suppress) {
                    lumo_input_touch_point_trigger_edge_action(compositor,
                        point, event->time_msec);
                }
                if (lumo_hitbox_is_shell_gesture(point->hitbox)) {
                    wlr_log(WLR_INFO,
                        "input: touch %d tapped gesture handle",
                        point->touch_id);
                } else if (!suppress) {
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
            /* don't replay edge taps to the launcher when an app is
             * focused — that causes input bleed-through to tiles behind
             * the active app. Only replay when the launcher drawer is
             * actually open and visible. */
            if (!compositor->launcher_visible &&
                    !wl_list_empty(&compositor->toplevels)) {
                /* drop the touch — the app stays in foreground */
            } else {
                lumo_input_replay_touch_point(compositor, point);
            }
        }

        if (point->gesture_triggered) {
            wlr_log(WLR_INFO, "input: touch %d gesture completed", point->touch_id);
        } else if (point->hitbox != NULL &&
                lumo_input_hitbox_is_shell_reserved(point->hitbox)) {
            struct wlr_surface *shell_surface =
                lumo_input_shell_surface_for_hitbox(compositor, point->hitbox);
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

    compositor->pinch_active = false;
    compositor->pinch_touch_ids[0] = -1;
    compositor->pinch_touch_ids[1] = -1;
    compositor->pinch_initial_distance = 0.0;
    compositor->pinch_scale = 1.0;

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

    /* create a virtual keyboard for OSK key injection — on touchscreen-only
     * devices there is no physical keyboard, so wlr_seat_get_keyboard()
     * returns NULL and the virtual keyboard fallback in commit_osk_text
     * can never fire. This hidden keyboard stays set on the seat so OSK
     * keys always have a wlr_keyboard to route through. */
    {
        static const struct wlr_keyboard_impl osk_kbd_impl = {
            .name = "lumo-osk-virtual",
        };
        compositor->osk_keyboard = calloc(1, sizeof(struct wlr_keyboard));
        if (compositor->osk_keyboard != NULL) {
            wlr_keyboard_init(compositor->osk_keyboard, &osk_kbd_impl,
                "lumo-osk-virtual");
            struct xkb_keymap *osk_keymap = xkb_keymap_new_from_names(
                compositor->xkb_context,
                &(struct xkb_rule_names){ .layout = "us" },
                XKB_KEYMAP_COMPILE_NO_FLAGS);
            if (osk_keymap != NULL) {
                wlr_keyboard_set_keymap(compositor->osk_keyboard, osk_keymap);
                xkb_keymap_unref(osk_keymap);
            }
            wlr_seat_set_keyboard(compositor->seat, compositor->osk_keyboard);
            wlr_log(WLR_INFO, "input: created virtual OSK keyboard");
        }
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

    if (compositor->osk_keyboard != NULL) {
        wlr_keyboard_finish(compositor->osk_keyboard);
        free(compositor->osk_keyboard);
        compositor->osk_keyboard = NULL;
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

    wl_list_remove(&compositor->backend_new_input.link);

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
