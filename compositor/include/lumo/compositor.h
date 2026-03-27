#ifndef LUMO_COMPOSITOR_H
#define LUMO_COMPOSITOR_H

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <wayland-server-core.h>
#include "lumo/shell.h"
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
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

#ifndef LUMO_ENABLE_XWAYLAND
#define LUMO_ENABLE_XWAYLAND 1
#endif

struct wlr_surface;
struct wlr_xwayland;
#if LUMO_ENABLE_XWAYLAND
#include <wlr/xwayland.h>
#endif

enum lumo_rotation {
    LUMO_ROTATION_NORMAL = 0,
    LUMO_ROTATION_90 = 1,
    LUMO_ROTATION_180 = 2,
    LUMO_ROTATION_270 = 3,
};

enum lumo_backend_mode {
    LUMO_BACKEND_AUTO = 0,
    LUMO_BACKEND_DRM,
    LUMO_BACKEND_WAYLAND,
    LUMO_BACKEND_HEADLESS,
    LUMO_BACKEND_X11,
};

enum lumo_edge_zone {
    LUMO_EDGE_NONE = 0,
    LUMO_EDGE_TOP,
    LUMO_EDGE_LEFT,
    LUMO_EDGE_RIGHT,
    LUMO_EDGE_BOTTOM,
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

struct lumo_compositor_config {
    const char *session_name;
    const char *socket_name;
    const char *executable_path;
    const char *shell_path;
    enum lumo_rotation initial_rotation;
    enum lumo_backend_mode backend_mode;
    bool debug;
};

struct lumo_compositor;
struct lumo_output;
struct lumo_keyboard;
struct lumo_toplevel;
struct lumo_popup;
struct lumo_layer_surface;
struct lumo_hitbox;
struct lumo_protocol_state;
struct lumo_text_input_binding;

enum lumo_scene_object_role {
    LUMO_SCENE_OBJECT_TOPLEVEL = 0,
    LUMO_SCENE_OBJECT_POPUP,
    LUMO_SCENE_OBJECT_LAYER_SURFACE,
};

enum lumo_protocol_listener_kind {
    LUMO_PROTOCOL_LISTENER_XDG_TOPLEVEL = 0,
    LUMO_PROTOCOL_LISTENER_XDG_POPUP,
    LUMO_PROTOCOL_LISTENER_LAYER_SURFACE,
};

struct lumo_protocol_state {
    struct lumo_compositor *compositor;
    struct wl_listener xdg_new_toplevel;
    struct wl_listener xdg_new_popup;
    struct wl_listener layer_new_surface;
    struct wl_listener text_input_new;
    struct wl_list text_input_bindings;
};

struct lumo_text_input_binding {
    struct wl_list link;
    struct lumo_compositor *compositor;
    struct wlr_text_input_v3 *text_input;
    struct wl_listener enable;
    struct wl_listener commit;
    struct wl_listener disable;
    struct wl_listener destroy;
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

static inline bool lumo_rotation_parse(
    const char *value,
    enum lumo_rotation *rotation
) {
    if (value == NULL || rotation == NULL) {
        return false;
    }

    if (strcmp(value, "normal") == 0 || strcmp(value, "0") == 0) {
        *rotation = LUMO_ROTATION_NORMAL;
        return true;
    }
    if (strcmp(value, "90") == 0) {
        *rotation = LUMO_ROTATION_90;
        return true;
    }
    if (strcmp(value, "180") == 0) {
        *rotation = LUMO_ROTATION_180;
        return true;
    }
    if (strcmp(value, "270") == 0) {
        *rotation = LUMO_ROTATION_270;
        return true;
    }

    return false;
}

static inline bool lumo_transform_layout_coords_in_box(
    const struct wlr_box *box,
    enum wl_output_transform transform,
    double raw_x,
    double raw_y,
    double *lx,
    double *ly
) {
    struct wlr_fbox point = {0};

    if (box == NULL || lx == NULL || ly == NULL || box->width <= 0 ||
            box->height <= 0) {
        return false;
    }

    point.x = (raw_x - box->x) / box->width;
    point.y = (raw_y - box->y) / box->height;
    point.width = 0.0;
    point.height = 0.0;

    wlr_fbox_transform(&point, &point, transform, 1.0, 1.0);

    *lx = box->x + point.x * box->width;
    *ly = box->y + point.y * box->height;
    return true;
}

static inline const char *lumo_backend_mode_name(
    enum lumo_backend_mode mode
) {
    switch (mode) {
    case LUMO_BACKEND_DRM:
        return "drm";
    case LUMO_BACKEND_WAYLAND:
        return "wayland";
    case LUMO_BACKEND_HEADLESS:
        return "headless";
    case LUMO_BACKEND_X11:
        return "x11";
    case LUMO_BACKEND_AUTO:
    default:
        return "auto";
    }
}

static inline const char *lumo_edge_zone_name(enum lumo_edge_zone zone) {
    switch (zone) {
    case LUMO_EDGE_TOP:
        return "top";
    case LUMO_EDGE_LEFT:
        return "left";
    case LUMO_EDGE_RIGHT:
        return "right";
    case LUMO_EDGE_BOTTOM:
        return "bottom";
    case LUMO_EDGE_NONE:
    default:
        return "none";
    }
}

static inline enum lumo_edge_zone lumo_edge_zone_in_box(
    const struct wlr_box *box,
    double lx,
    double ly,
    double threshold
) {
    double top_dist;
    double left_dist;
    double right_dist;
    double bottom_dist;
    double best;
    enum lumo_edge_zone zone = LUMO_EDGE_NONE;

    if (box == NULL || box->width <= 0 || box->height <= 0) {
        return LUMO_EDGE_NONE;
    }
    if (lx < box->x || ly < box->y ||
            lx >= box->x + box->width ||
            ly >= box->y + box->height) {
        return LUMO_EDGE_NONE;
    }

    best = threshold > 0.0 ? threshold : 24.0;
    top_dist = ly - box->y;
    if (top_dist >= 0.0 && top_dist <= best) {
        best = top_dist;
        zone = LUMO_EDGE_TOP;
    }

    left_dist = lx - box->x;
    if (left_dist >= 0.0 && left_dist < best) {
        best = left_dist;
        zone = LUMO_EDGE_LEFT;
    }

    right_dist = box->x + box->width - lx;
    if (right_dist >= 0.0 && right_dist < best) {
        best = right_dist;
        zone = LUMO_EDGE_RIGHT;
    }

    bottom_dist = box->y + box->height - ly;
    if (bottom_dist >= 0.0 && bottom_dist < best) {
        zone = LUMO_EDGE_BOTTOM;
    }

    return zone;
}

static inline bool lumo_backend_mode_parse(
    const char *value,
    enum lumo_backend_mode *mode
) {
    if (value == NULL || mode == NULL) {
        return false;
    }

    if (strcmp(value, "auto") == 0) {
        *mode = LUMO_BACKEND_AUTO;
        return true;
    }
    if (strcmp(value, "drm") == 0) {
        *mode = LUMO_BACKEND_DRM;
        return true;
    }
    if (strcmp(value, "wayland") == 0) {
        *mode = LUMO_BACKEND_WAYLAND;
        return true;
    }
    if (strcmp(value, "headless") == 0) {
        *mode = LUMO_BACKEND_HEADLESS;
        return true;
    }
    if (strcmp(value, "x11") == 0) {
        *mode = LUMO_BACKEND_X11;
        return true;
    }

    return false;
}

static inline const char *lumo_backend_env_value(
    enum lumo_backend_mode mode
) {
    switch (mode) {
    case LUMO_BACKEND_DRM:
        return "libinput,drm";
    case LUMO_BACKEND_WAYLAND:
        return "wayland";
    case LUMO_BACKEND_HEADLESS:
        return "headless";
    case LUMO_BACKEND_X11:
        return "x11";
    case LUMO_BACKEND_AUTO:
    default:
        return NULL;
    }
}

static inline bool lumo_tty_name_looks_like_vt(const char *tty_name) {
    const char *suffix = NULL;

    if (tty_name == NULL) {
        return false;
    }
    if (strncmp(tty_name, "/dev/tty", 8) != 0) {
        return false;
    }

    suffix = tty_name + 8;
    if (*suffix == '\0') {
        return false;
    }

    for (; *suffix != '\0'; ++suffix) {
        if (!isdigit((unsigned char)*suffix)) {
            return false;
        }
    }

    return true;
}

static inline enum lumo_backend_mode lumo_backend_auto_mode_for_session(
    const char *tty_name,
    const char *ssh_connection,
    const char *ssh_tty,
    const char *wayland_display,
    const char *display
) {
    if (lumo_tty_name_looks_like_vt(tty_name)) {
        return LUMO_BACKEND_AUTO;
    }

    if ((ssh_connection != NULL && ssh_connection[0] != '\0') ||
            (ssh_tty != NULL && ssh_tty[0] != '\0')) {
        return LUMO_BACKEND_HEADLESS;
    }
    if (wayland_display != NULL && wayland_display[0] != '\0') {
        return LUMO_BACKEND_WAYLAND;
    }
    if (display != NULL && display[0] != '\0') {
        return LUMO_BACKEND_X11;
    }

    return LUMO_BACKEND_HEADLESS;
}

static inline const char *lumo_scrim_state_name(enum lumo_scrim_state state) {
    switch (state) {
    case LUMO_SCRIM_DIMMED:
        return "dimmed";
    case LUMO_SCRIM_MODAL:
        return "modal";
    case LUMO_SCRIM_HIDDEN:
    default:
        return "hidden";
    }
}

static inline bool lumo_scrim_state_parse(
    const char *value,
    enum lumo_scrim_state *state
) {
    if (value == NULL || state == NULL) {
        return false;
    }

    if (strcmp(value, "hidden") == 0) {
        *state = LUMO_SCRIM_HIDDEN;
        return true;
    }
    if (strcmp(value, "dimmed") == 0) {
        *state = LUMO_SCRIM_DIMMED;
        return true;
    }
    if (strcmp(value, "modal") == 0) {
        *state = LUMO_SCRIM_MODAL;
        return true;
    }

    return false;
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

struct lumo_output {
    struct wl_list link;
    struct lumo_compositor *compositor;
    struct wlr_output *wlr_output;
    struct wlr_scene_output *scene_output;
    struct wlr_output_layout_output *layout_output;
    struct wlr_box usable_area;
    bool usable_area_valid;
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
    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;
    struct wl_listener request_minimize;
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_show_window_menu;
    struct wl_listener set_parent;
    struct wl_listener set_title;
    struct wl_listener set_app_id;
    struct wl_listener destroy;
};

struct lumo_popup {
    struct wl_list link;
    struct lumo_compositor *compositor;
    enum lumo_scene_object_role role;
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
    struct lumo_output *last_configured_output;
    bool layout_snapshot_valid;
    struct wlr_box last_full_area;
    struct wlr_box last_usable_area;
    struct wlr_layer_surface_v1_state last_current_state;
    struct wlr_layer_surface_v1_state last_pending_state;
    bool commit_snapshot_valid;
    struct wlr_layer_surface_v1_state last_committed_state;
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

static inline bool lumo_hitbox_is_shell_gesture(
    const struct lumo_hitbox *hitbox
) {
    return hitbox != NULL &&
        hitbox->kind == LUMO_HITBOX_EDGE_GESTURE &&
        hitbox->name != NULL &&
        strcmp(hitbox->name, "shell-gesture") == 0;
}

static inline bool lumo_touch_hitbox_uses_immediate_launcher_toggle(
    const struct lumo_hitbox *hitbox
) {
    return lumo_hitbox_is_shell_gesture(hitbox);
}

static inline enum lumo_edge_zone lumo_hitbox_edge_zone(
    const struct lumo_hitbox *hitbox
) {
    if (hitbox == NULL || hitbox->kind != LUMO_HITBOX_EDGE_GESTURE ||
            hitbox->name == NULL) {
        return LUMO_EDGE_NONE;
    }

    if (strcmp(hitbox->name, "shell-gesture") == 0 ||
            strcmp(hitbox->name, "shell-edge-bottom") == 0) {
        return LUMO_EDGE_BOTTOM;
    }
    if (strcmp(hitbox->name, "shell-edge-top") == 0) {
        return LUMO_EDGE_TOP;
    }
    if (strcmp(hitbox->name, "shell-edge-left") == 0) {
        return LUMO_EDGE_LEFT;
    }
    if (strcmp(hitbox->name, "shell-edge-right") == 0) {
        return LUMO_EDGE_RIGHT;
    }

    return LUMO_EDGE_NONE;
}

enum lumo_touch_target_kind {
    LUMO_TOUCH_TARGET_NONE = 0,
    LUMO_TOUCH_TARGET_HITBOX,
    LUMO_TOUCH_TARGET_SURFACE,
};

static inline const char *lumo_touch_target_kind_name(
    enum lumo_touch_target_kind kind
) {
    switch (kind) {
    case LUMO_TOUCH_TARGET_HITBOX:
        return "hitbox";
    case LUMO_TOUCH_TARGET_SURFACE:
        return "surface";
    case LUMO_TOUCH_TARGET_NONE:
    default:
        return "none";
    }
}

static inline const char *lumo_touch_region_name_in_box(
    const struct wlr_box *box,
    double lx,
    double ly,
    double threshold
) {
    bool left;
    bool right;
    bool top;
    bool bottom;
    double edge = threshold;

    if (box == NULL || box->width <= 0 || box->height <= 0) {
        return "unknown";
    }

    if (lx < box->x || ly < box->y ||
            lx >= box->x + box->width ||
            ly >= box->y + box->height) {
        return "outside";
    }

    if (edge <= 0.0) {
        edge = 24.0;
    }
    if (edge * 2.0 > box->width) {
        edge = box->width / 2.0;
    }
    if (edge * 2.0 > box->height) {
        edge = box->height / 2.0;
    }

    left = lx < box->x + edge;
    right = lx >= box->x + box->width - edge;
    top = ly < box->y + edge;
    bottom = ly >= box->y + box->height - edge;

    if (top && left) {
        return "top-left";
    }
    if (top && right) {
        return "top-right";
    }
    if (bottom && left) {
        return "bottom-left";
    }
    if (bottom && right) {
        return "bottom-right";
    }
    if (top) {
        return "top-center";
    }
    if (bottom) {
        return "bottom-center";
    }
    if (left) {
        return "left-center";
    }
    if (right) {
        return "right-center";
    }

    return "center";
}

enum lumo_touch_sample_type {
    LUMO_TOUCH_SAMPLE_DOWN = 0,
    LUMO_TOUCH_SAMPLE_MOTION,
    LUMO_TOUCH_SAMPLE_UP,
    LUMO_TOUCH_SAMPLE_CANCEL,
};

static inline const char *lumo_touch_sample_type_name(
    enum lumo_touch_sample_type type
) {
    switch (type) {
    case LUMO_TOUCH_SAMPLE_DOWN:
        return "down";
    case LUMO_TOUCH_SAMPLE_MOTION:
        return "motion";
    case LUMO_TOUCH_SAMPLE_UP:
        return "up";
    case LUMO_TOUCH_SAMPLE_CANCEL:
        return "cancel";
    default:
        return "unknown";
    }
}

static inline struct wlr_scene_surface *lumo_scene_surface_from_node(
    struct wlr_scene_node *node,
    struct wlr_scene_buffer **buffer_out
) {
    struct wlr_scene_buffer *scene_buffer = NULL;

    if (buffer_out != NULL) {
        *buffer_out = NULL;
    }

    if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) {
        return NULL;
    }

    scene_buffer = wlr_scene_buffer_from_node(node);
    if (buffer_out != NULL) {
        *buffer_out = scene_buffer;
    }

    return scene_buffer != NULL
        ? wlr_scene_surface_try_from_buffer(scene_buffer)
        : NULL;
}

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
    struct wl_listener surface_destroy;
    bool surface_destroy_active;
    struct wlr_seat_client *seat_client;
    void *owner;
    struct wlr_scene_tree *scene_tree;
    double lx;
    double ly;
    double down_lx;
    double down_ly;
    double sx;
    double sy;
    uint32_t down_time_msec;
    bool delivered;
    bool captured;
    bool gesture_triggered;
    enum lumo_edge_zone capture_edge;
    struct wl_list samples;
};

static inline bool lumo_touch_point_is_launcher_capture(
    const struct lumo_touch_point *point
) {
    return point != NULL &&
        point->captured &&
        !point->delivered &&
        (point->capture_edge == LUMO_EDGE_BOTTOM ||
            point->capture_edge == LUMO_EDGE_RIGHT);
}

struct lumo_touch_audit_sample {
    bool captured;
    double raw_x_pct;
    double raw_y_pct;
    double logical_x_pct;
    double logical_y_pct;
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
    struct wlr_screencopy_manager_v1 *screencopy_manager;
    struct wlr_xwayland *xwayland;
    bool xwayland_ready;
    struct wlr_box xwayland_workarea;
    bool xwayland_workarea_valid;
    void *shell_state;
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
    bool touch_audit_active;
    bool touch_audit_saved;
    uint32_t touch_audit_step;
    uint32_t touch_audit_completed_mask;
    char touch_audit_profile_name[128];
    char touch_audit_device_name[128];
    uint32_t touch_audit_device_vendor;
    uint32_t touch_audit_device_product;
    struct lumo_touch_audit_sample touch_audit_samples[8];
    bool touch_debug_active;
    int32_t touch_debug_id;
    double touch_debug_lx;
    double touch_debug_ly;
    enum lumo_touch_target_kind touch_debug_target;
    enum lumo_touch_sample_type touch_debug_phase;
    enum lumo_hitbox_kind touch_debug_hitbox_kind;
    bool layer_config_dirty;
    struct wl_list outputs;
    struct wl_list keyboards;
    struct wl_list toplevels;
    struct wl_list popups;
    struct wl_list layer_surfaces;
    struct wl_list hitboxes;
    struct wl_list input_devices;
    struct wl_list touch_points;
    void *input_state;
    struct lumo_protocol_state *protocol_state;
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

static inline bool lumo_touch_audit_debug_gesture_enabled(
    const struct lumo_compositor *compositor
) {
    return compositor != NULL &&
        compositor->config != NULL &&
        compositor->config->debug;
}

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
void lumo_touch_audit_set_active(
    struct lumo_compositor *compositor,
    bool active
);
void lumo_touch_audit_note_touch(
    struct lumo_compositor *compositor,
    const struct lumo_output *output,
    const struct wlr_input_device *device,
    const struct lumo_touch_point *point,
    double raw_x,
    double raw_y
);

int lumo_output_start(struct lumo_compositor *compositor);
void lumo_output_stop(struct lumo_compositor *compositor);
void lumo_output_set_rotation(
    struct lumo_compositor *compositor,
    const char *output_name,
    enum lumo_rotation rotation
);
bool lumo_xwayland_collect_workarea(
    struct lumo_compositor *compositor,
    struct wlr_box *workarea
);
void lumo_xwayland_sync_workareas(struct lumo_compositor *compositor);
void lumo_xwayland_focus_surface(
    struct lumo_compositor *compositor,
    struct wlr_surface *surface
);
void lumo_protocol_configure_layers(
    struct lumo_compositor *compositor,
    struct lumo_output *output
);
void lumo_protocol_configure_all_layers(
    struct lumo_compositor *compositor
);

int lumo_protocol_start(struct lumo_compositor *compositor);
void lumo_protocol_stop(struct lumo_compositor *compositor);
struct lumo_compositor *lumo_protocol_listener_compositor(
    struct wl_listener *listener,
    enum lumo_protocol_listener_kind kind
);
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
void lumo_protocol_refresh_shell_hitboxes(struct lumo_compositor *compositor);
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
void lumo_protocol_refresh_keyboard_visibility(
    struct lumo_compositor *compositor
);
void lumo_protocol_mark_layers_dirty(struct lumo_compositor *compositor);
bool lumo_protocol_layer_surface_commit_needs_reconfigure(
    const struct wlr_layer_surface_v1_state *previous,
    bool previous_valid,
    const struct wlr_layer_surface_v1_state *current,
    bool initialized
);

bool lumo_shell_resolve_binary_path(
    const struct lumo_compositor_config *config,
    char *buffer,
    size_t buffer_size
);
bool lumo_shell_state_socket_path(
    const char *runtime_dir,
    char *buffer,
    size_t buffer_size
);
size_t lumo_shell_state_format_line(
    char *buffer,
    size_t buffer_size,
    const char *key,
    const char *value
);
size_t lumo_shell_state_format_bool(
    char *buffer,
    size_t buffer_size,
    const char *key,
    bool value
);
size_t lumo_shell_state_format_double(
    char *buffer,
    size_t buffer_size,
    const char *key,
    double value
);
size_t lumo_shell_build_argv(
    enum lumo_shell_mode mode,
    const char *binary,
    const char **argv,
    size_t capacity
);
int lumo_shell_autostart_start(struct lumo_compositor *compositor);
void lumo_shell_autostart_poll(struct lumo_compositor *compositor);
void lumo_shell_autostart_stop(struct lumo_compositor *compositor);
void lumo_shell_state_broadcast_launcher_visible(
    struct lumo_compositor *compositor,
    bool visible
);
void lumo_shell_state_broadcast_keyboard_visible(
    struct lumo_compositor *compositor,
    bool visible
);
void lumo_shell_state_broadcast_scrim_state(
    struct lumo_compositor *compositor,
    enum lumo_scrim_state state
);
void lumo_shell_state_broadcast_gesture_threshold(
    struct lumo_compositor *compositor,
    double threshold,
    uint32_t timeout_ms
);
void lumo_shell_state_broadcast_rotation(
    struct lumo_compositor *compositor,
    enum lumo_rotation rotation
);
void lumo_shell_state_broadcast_touch_debug(struct lumo_compositor *compositor);
void lumo_shell_state_broadcast_touch_audit(struct lumo_compositor *compositor);

int lumo_xwayland_start(struct lumo_compositor *compositor);
void lumo_xwayland_stop(struct lumo_compositor *compositor);

#endif
