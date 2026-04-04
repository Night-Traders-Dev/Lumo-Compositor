#ifndef LUMO_INPUT_INTERNAL_H
#define LUMO_INPUT_INTERNAL_H

#include "lumo/compositor.h"

#include <wlr/types/wlr_touch.h>

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

/* input.c — utilities and hit-testing */
struct lumo_input_state *lumo_input_state_from(
    struct lumo_compositor *compositor);
uint32_t lumo_input_now_msec(void);
struct lumo_output *lumo_input_first_output(
    struct lumo_compositor *compositor);
struct lumo_output *lumo_input_output_from_wlr(
    struct lumo_compositor *compositor,
    struct wlr_output *wlr_output);
struct lumo_output *lumo_input_output_for_layout_coords(
    struct lumo_compositor *compositor,
    double lx,
    double ly);
bool lumo_input_transform_touch_coords(
    struct lumo_compositor *compositor,
    struct wlr_input_device *device,
    double raw_x,
    double raw_y,
    double *lx,
    double *ly,
    struct lumo_output **output_out);
struct lumo_scene_object_head *lumo_input_scene_object_from_node(
    struct wlr_scene_node *node);
bool lumo_input_surface_target_at(
    struct lumo_compositor *compositor,
    double lx,
    double ly,
    struct lumo_surface_target *target);
bool lumo_input_target_is_shell(const struct lumo_surface_target *target);
bool lumo_input_hitbox_is_shell_reserved(const struct lumo_hitbox *hitbox);
enum lumo_edge_zone lumo_input_system_edge_zone(
    struct lumo_compositor *compositor,
    const struct lumo_output *output,
    double lx,
    double ly);

/* input_touch.c — touch point management, focus, gestures, edge actions */
void lumo_input_touch_audit_log(
    struct lumo_compositor *compositor,
    const struct lumo_touch_point *point,
    const struct lumo_output *output,
    const struct lumo_surface_target *target,
    double raw_x,
    double raw_y);
struct lumo_touch_point *lumo_input_touch_point_for_id(
    struct lumo_compositor *compositor,
    int32_t touch_id);
void lumo_input_touch_sample_append(
    struct lumo_touch_point *point,
    enum lumo_touch_sample_type type,
    uint32_t time_msec,
    double lx,
    double ly,
    double sx,
    double sy);
void lumo_input_touch_debug_update(
    struct lumo_compositor *compositor,
    const struct lumo_touch_point *point,
    enum lumo_touch_sample_type phase,
    bool active,
    double lx,
    double ly);
void lumo_input_touch_samples_clear(struct lumo_touch_point *point);
void lumo_input_touch_point_bind_surface(
    struct lumo_touch_point *point,
    struct wlr_surface *surface);
void lumo_input_touch_point_destroy(struct lumo_touch_point *point);
void lumo_input_focus_surface(
    struct lumo_compositor *compositor,
    struct wlr_surface *surface);
void lumo_input_refresh_capabilities(struct lumo_compositor *compositor);
void lumo_input_remove_touch_point(
    struct lumo_compositor *compositor,
    struct lumo_touch_point *point);
void lumo_input_maybe_start_gesture_timer(struct lumo_compositor *compositor);
void lumo_input_touch_point_begin_capture(
    struct lumo_compositor *compositor,
    struct lumo_touch_point *point,
    const struct lumo_surface_target *target,
    uint32_t time_msec);
void lumo_input_touch_point_deliver_now(
    struct lumo_compositor *compositor,
    struct lumo_touch_point *point,
    const struct lumo_surface_target *target,
    uint32_t time_msec);
double lumo_input_touch_point_edge_progress(
    const struct lumo_touch_point *point,
    double lx,
    double ly);
double lumo_input_touch_point_edge_velocity(
    const struct lumo_touch_point *point,
    double lx,
    double ly,
    uint32_t time_msec);
bool lumo_input_edge_angle_valid(
    enum lumo_edge_zone edge,
    double dx, double dy);
void lumo_input_touch_point_trigger_edge_action(
    struct lumo_compositor *compositor,
    struct lumo_touch_point *point,
    uint32_t time_msec);
struct wlr_surface *lumo_input_shell_surface_for_hitbox(
    struct lumo_compositor *compositor,
    const struct lumo_hitbox *hitbox);
void lumo_input_replay_touch_point(
    struct lumo_compositor *compositor,
    struct lumo_touch_point *point);

/* input_pointer.c — pointer/keyboard handlers, device management */
void lumo_input_pointer_motion(struct wl_listener *listener, void *data);
void lumo_input_pointer_motion_absolute(struct wl_listener *listener, void *data);
void lumo_input_pointer_button(struct wl_listener *listener, void *data);
void lumo_input_pointer_axis(struct wl_listener *listener, void *data);
void lumo_input_pointer_frame(struct wl_listener *listener, void *data);
void lumo_input_pointer_swipe_begin(struct wl_listener *listener, void *data);
void lumo_input_pointer_swipe_update(struct wl_listener *listener, void *data);
void lumo_input_pointer_swipe_end(struct wl_listener *listener, void *data);
void lumo_input_pointer_pinch_begin(struct wl_listener *listener, void *data);
void lumo_input_pointer_pinch_update(struct wl_listener *listener, void *data);
void lumo_input_pointer_pinch_end(struct wl_listener *listener, void *data);
void lumo_input_pointer_hold_begin(struct wl_listener *listener, void *data);
void lumo_input_pointer_hold_end(struct wl_listener *listener, void *data);
void lumo_input_request_set_cursor(struct wl_listener *listener, void *data);
void lumo_input_request_set_selection(struct wl_listener *listener, void *data);
void lumo_input_keyboard_key(struct wl_listener *listener, void *data);
void lumo_input_keyboard_modifiers(struct wl_listener *listener, void *data);
void lumo_input_keyboard_destroy(struct wl_listener *listener, void *data);
void lumo_input_device_destroy(struct wl_listener *listener, void *data);
void lumo_input_keyboard_attach(
    struct lumo_compositor *compositor,
    struct wlr_input_device *device);
void lumo_input_pointer_device_attach(
    struct lumo_compositor *compositor,
    struct wlr_input_device *device);

#endif
