#ifndef LUMO_COMPOSITOR_H
#define LUMO_COMPOSITOR_H

#include <stdbool.h>
#include <stddef.h>
#include <wayland-server-core.h>
#include <xkbcommon/xkbcommon.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>

enum lumo_rotation {
    LUMO_ROTATION_NORMAL = 0,
    LUMO_ROTATION_90 = 1,
    LUMO_ROTATION_180 = 2,
    LUMO_ROTATION_270 = 3,
};

enum lumo_hitbox_kind {
    LUMO_HITBOX_CUSTOM = 0,
    LUMO_HITBOX_LAUNCHER_TILE,
    LUMO_HITBOX_OSK_KEY,
    LUMO_HITBOX_EDGE_GESTURE,
    LUMO_HITBOX_SCRIM,
};

enum lumo_scrim_state {
    LUMO_SCRIM_HIDDEN = 0,
    LUMO_SCRIM_DIMMED,
    LUMO_SCRIM_MODAL,
};

struct lumo_rect {
    int x;
    int y;
    int width;
    int height;
};

struct lumo_compositor_config {
    const char *session_name;
    const char *socket_name;
    enum lumo_rotation initial_rotation;
    bool debug;
};

struct lumo_compositor;
struct lumo_output;
struct lumo_keyboard;
struct lumo_toplevel;
struct lumo_popup;
struct lumo_layer_surface;
struct lumo_hitbox;

enum lumo_scene_object_role {
    LUMO_SCENE_OBJECT_TOPLEVEL = 0,
    LUMO_SCENE_OBJECT_LAYER_SURFACE,
};

static inline enum wl_output_transform lumo_rotation_to_transform(
    enum lumo_rotation rotation
) {
    switch (rotation) {
    case LUMO_ROTATION_90:
        return WL_OUTPUT_TRANSFORM_90;
    case LUMO_ROTATION_180:
        return WL_OUTPUT_TRANSFORM_180;
    case LUMO_ROTATION_270:
        return WL_OUTPUT_TRANSFORM_270;
    case LUMO_ROTATION_NORMAL:
    default:
        return WL_OUTPUT_TRANSFORM_NORMAL;
    }
}

static inline enum lumo_rotation lumo_transform_to_rotation(
    enum wl_output_transform transform
) {
    switch (transform) {
    case WL_OUTPUT_TRANSFORM_90:
    case WL_OUTPUT_TRANSFORM_FLIPPED_90:
        return LUMO_ROTATION_90;
    case WL_OUTPUT_TRANSFORM_180:
    case WL_OUTPUT_TRANSFORM_FLIPPED_180:
        return LUMO_ROTATION_180;
    case WL_OUTPUT_TRANSFORM_270:
    case WL_OUTPUT_TRANSFORM_FLIPPED_270:
        return LUMO_ROTATION_270;
    case WL_OUTPUT_TRANSFORM_NORMAL:
    case WL_OUTPUT_TRANSFORM_FLIPPED:
    default:
        return LUMO_ROTATION_NORMAL;
    }
}

static inline const char *lumo_rotation_name(enum lumo_rotation rotation) {
    switch (rotation) {
    case LUMO_ROTATION_90:
        return "90";
    case LUMO_ROTATION_180:
        return "180";
    case LUMO_ROTATION_270:
        return "270";
    case LUMO_ROTATION_NORMAL:
    default:
        return "normal";
    }
}

static inline const char *lumo_hitbox_kind_name(enum lumo_hitbox_kind kind) {
    switch (kind) {
    case LUMO_HITBOX_LAUNCHER_TILE:
        return "launcher-tile";
    case LUMO_HITBOX_OSK_KEY:
        return "osk-key";
    case LUMO_HITBOX_EDGE_GESTURE:
        return "edge-gesture";
    case LUMO_HITBOX_SCRIM:
        return "scrim";
    case LUMO_HITBOX_CUSTOM:
    default:
        return "custom";
    }
}

static inline bool lumo_rect_contains(
    const struct lumo_rect *rect,
    double x,
    double y
) {
    return rect != NULL &&
        x >= rect->x &&
        y >= rect->y &&
        x < rect->x + rect->width &&
        y < rect->y + rect->height;
}

struct lumo_output {
    struct wl_list link;
    struct lumo_compositor *compositor;
    struct wlr_output *wlr_output;
    struct wlr_scene_output *scene_output;
    struct wlr_output_layout_output *layout_output;
    struct wl_listener frame;
    struct wl_listener request_state;
    struct wl_listener destroy;
};

struct lumo_keyboard {
    struct wl_list link;
    struct lumo_compositor *compositor;
    struct wlr_keyboard *wlr_keyboard;
    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;
};

struct lumo_toplevel {
    struct wl_list link;
    struct lumo_compositor *compositor;
    enum lumo_scene_object_role role;
    struct wlr_xdg_surface *xdg_surface;
    struct wlr_xdg_toplevel *xdg_toplevel;
    struct wlr_scene_tree *scene_tree;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
};

struct lumo_popup {
    struct wl_list link;
    struct lumo_compositor *compositor;
    struct wlr_xdg_popup *xdg_popup;
    struct wlr_scene_tree *scene_tree;
    struct wl_listener commit;
    struct wl_listener destroy;
};

struct lumo_layer_surface {
    struct wl_list link;
    struct lumo_compositor *compositor;
    enum lumo_scene_object_role role;
    struct wlr_layer_surface_v1 *layer_surface;
    struct wlr_scene_layer_surface_v1 *scene_surface;
    struct lumo_output *output;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
};

struct lumo_hitbox {
    struct wl_list link;
    char *name;
    struct lumo_rect rect;
    enum lumo_hitbox_kind kind;
    bool accepts_touch;
    bool accepts_pointer;
    void *userdata;
};

enum lumo_touch_target_kind {
    LUMO_TOUCH_TARGET_NONE = 0,
    LUMO_TOUCH_TARGET_HITBOX,
    LUMO_TOUCH_TARGET_SURFACE,
};

enum lumo_touch_sample_type {
    LUMO_TOUCH_SAMPLE_DOWN = 0,
    LUMO_TOUCH_SAMPLE_MOTION,
    LUMO_TOUCH_SAMPLE_UP,
    LUMO_TOUCH_SAMPLE_CANCEL,
};

struct lumo_touch_sample {
    struct wl_list link;
    enum lumo_touch_sample_type type;
    uint32_t time_msec;
    double lx;
    double ly;
    double sx;
    double sy;
};

struct lumo_touch_point {
    struct wl_list link;
    int32_t touch_id;
    enum lumo_touch_target_kind kind;
    const struct lumo_hitbox *hitbox;
    struct wlr_surface *surface;
    struct wlr_seat_client *seat_client;
    void *owner;
    struct wlr_scene_tree *scene_tree;
    double lx;
    double ly;
    double sx;
    double sy;
    uint32_t down_time_msec;
    bool delivered;
    bool captured;
    bool gesture_triggered;
    struct wl_list samples;
};

struct lumo_compositor {
    const struct lumo_compositor_config *config;
    struct wl_display *display;
    struct wl_event_loop *event_loop;
    struct wlr_session *session;
    struct wlr_backend *backend;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;
    struct wlr_compositor *compositor_protocol;
    struct wlr_subcompositor *subcompositor;
    struct wlr_data_device_manager *data_device_manager;
    struct wlr_output_layout *output_layout;
    struct wlr_scene *scene;
    struct wlr_scene_output_layout *scene_layout;
    struct wlr_xdg_shell *xdg_shell;
    struct wlr_layer_shell_v1 *layer_shell;
    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *cursor_mgr;
    struct wlr_seat *seat;
    struct xkb_context *xkb_context;
    struct wlr_text_input_manager_v3 *text_input_manager;
    struct wlr_input_method_manager_v2 *input_method_manager;
    struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard_manager;
    struct wlr_pointer_gestures_v1 *pointer_gestures;
    bool running;
    bool keyboard_visible;
    bool launcher_visible;
    enum lumo_rotation active_rotation;
    enum lumo_scrim_state scrim_state;
    double gesture_threshold;
    uint32_t gesture_timeout_ms;
    uint32_t keyboard_resize_serial;
    bool keyboard_resize_pending;
    bool keyboard_resize_acked;
    struct wl_list outputs;
    struct wl_list keyboards;
    struct wl_list toplevels;
    struct wl_list popups;
    struct wl_list layer_surfaces;
    struct wl_list hitboxes;
    struct wl_list input_devices;
    struct wl_list touch_points;
    void *input_state;
    void *protocol_state;
    size_t pointer_devices;
    size_t touch_devices;
    size_t keyboard_devices;
    struct wl_listener backend_new_output;
    struct wl_listener backend_new_input;
    struct wl_listener xdg_new_toplevel;
    struct wl_listener xdg_new_popup;
    struct wl_listener layer_new_surface;
    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;
    struct wl_listener cursor_swipe_begin;
    struct wl_listener cursor_swipe_update;
    struct wl_listener cursor_swipe_end;
    struct wl_listener cursor_pinch_begin;
    struct wl_listener cursor_pinch_update;
    struct wl_listener cursor_pinch_end;
    struct wl_listener cursor_hold_begin;
    struct wl_listener cursor_hold_end;
    struct wl_listener cursor_touch_down;
    struct wl_listener cursor_touch_motion;
    struct wl_listener cursor_touch_up;
    struct wl_listener cursor_touch_cancel;
    struct wl_listener cursor_touch_frame;
    struct wl_listener seat_request_cursor;
    struct wl_listener seat_request_set_selection;
    bool backend_started;
    bool output_started;
    bool protocol_started;
    bool input_started;
};

struct lumo_compositor *lumo_compositor_create(
    const struct lumo_compositor_config *config
);
void lumo_compositor_destroy(struct lumo_compositor *compositor);
int lumo_compositor_run(struct lumo_compositor *compositor);
void lumo_compositor_stop(struct lumo_compositor *compositor);

int lumo_backend_start(struct lumo_compositor *compositor);
void lumo_backend_stop(struct lumo_compositor *compositor);

int lumo_input_start(struct lumo_compositor *compositor);
void lumo_input_stop(struct lumo_compositor *compositor);
void lumo_input_set_rotation(
    struct lumo_compositor *compositor,
    enum lumo_rotation rotation
);

int lumo_output_start(struct lumo_compositor *compositor);
void lumo_output_stop(struct lumo_compositor *compositor);
void lumo_output_set_rotation(
    struct lumo_compositor *compositor,
    const char *output_name,
    enum lumo_rotation rotation
);
void lumo_protocol_configure_layers(
    struct lumo_compositor *compositor,
    struct lumo_output *output
);

int lumo_protocol_start(struct lumo_compositor *compositor);
void lumo_protocol_stop(struct lumo_compositor *compositor);
int lumo_protocol_register_hitbox(
    struct lumo_compositor *compositor,
    const char *name,
    const struct lumo_rect *rect,
    enum lumo_hitbox_kind kind,
    bool accepts_touch,
    bool accepts_pointer
);
const struct lumo_hitbox *lumo_protocol_hitbox_at(
    struct lumo_compositor *compositor,
    double lx,
    double ly
);
void lumo_protocol_set_gesture_threshold(
    struct lumo_compositor *compositor,
    double threshold
);
void lumo_protocol_set_launcher_visible(
    struct lumo_compositor *compositor,
    bool visible
);
void lumo_protocol_set_scrim_state(
    struct lumo_compositor *compositor,
    enum lumo_scrim_state state
);
void lumo_protocol_ack_keyboard_resize(
    struct lumo_compositor *compositor,
    uint32_t serial
);
void lumo_protocol_set_keyboard_visible(
    struct lumo_compositor *compositor,
    bool visible
);

#endif
