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

void lumo_input_keyboard_attach(
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

void lumo_input_pointer_device_attach(
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

void lumo_input_device_destroy(
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

void lumo_input_keyboard_destroy(
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

void lumo_input_keyboard_modifiers(
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

void lumo_input_keyboard_key(
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

void lumo_input_request_set_cursor(
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

void lumo_input_request_set_selection(
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

void lumo_input_pointer_motion(
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

void lumo_input_pointer_motion_absolute(
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

void lumo_input_pointer_button(
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

void lumo_input_pointer_axis(
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

void lumo_input_pointer_frame(
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

void lumo_input_pointer_swipe_begin(
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

void lumo_input_pointer_swipe_update(
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

void lumo_input_pointer_swipe_end(
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

void lumo_input_pointer_pinch_begin(
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

void lumo_input_pointer_pinch_update(
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

void lumo_input_pointer_pinch_end(
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

void lumo_input_pointer_hold_begin(
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

void lumo_input_pointer_hold_end(
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

