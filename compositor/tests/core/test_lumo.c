#include "lumo/compositor.h"
#include "lumo/shell_protocol.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_rotation_helpers(void) {
    assert(lumo_rotation_to_transform(LUMO_ROTATION_NORMAL) ==
        WL_OUTPUT_TRANSFORM_NORMAL);
    assert(lumo_rotation_to_transform(LUMO_ROTATION_90) ==
        WL_OUTPUT_TRANSFORM_90);
    assert(lumo_rotation_to_transform(LUMO_ROTATION_180) ==
        WL_OUTPUT_TRANSFORM_180);
    assert(lumo_rotation_to_transform(LUMO_ROTATION_270) ==
        WL_OUTPUT_TRANSFORM_270);
    assert(lumo_transform_to_rotation(WL_OUTPUT_TRANSFORM_NORMAL) ==
        LUMO_ROTATION_NORMAL);
    assert(lumo_transform_to_rotation(WL_OUTPUT_TRANSFORM_90) ==
        LUMO_ROTATION_90);
    assert(lumo_transform_to_rotation(WL_OUTPUT_TRANSFORM_180) ==
        LUMO_ROTATION_180);
    assert(lumo_transform_to_rotation(WL_OUTPUT_TRANSFORM_270) ==
        LUMO_ROTATION_270);
}

static void test_touch_rotation_mapping(void) {
    struct wlr_box box = {
        .x = 10,
        .y = 20,
        .width = 100,
        .height = 200,
    };
    double lx = 0.0;
    double ly = 0.0;

    assert(lumo_transform_layout_coords_in_box(&box,
        WL_OUTPUT_TRANSFORM_NORMAL, 20.0, 40.0, &lx, &ly));
    assert(lx == 20.0);
    assert(ly == 40.0);

    assert(lumo_transform_layout_coords_in_box(&box,
        WL_OUTPUT_TRANSFORM_180, 20.0, 40.0, &lx, &ly));
    assert(lx == 100.0);
    assert(ly == 200.0);

    assert(lumo_transform_layout_coords_in_box(&box,
        WL_OUTPUT_TRANSFORM_90, 20.0, 40.0, &lx, &ly));
    assert(lx == 100.0);
    assert(ly == 40.0);

    assert(lumo_transform_layout_coords_in_box(&box,
        WL_OUTPUT_TRANSFORM_270, 20.0, 40.0, &lx, &ly));
    assert(lx == 20.0);
    assert(ly == 200.0);

    assert(!lumo_transform_layout_coords_in_box(NULL,
        WL_OUTPUT_TRANSFORM_NORMAL, 0.0, 0.0, &lx, &ly));
}

static void test_touch_region_helpers(void) {
    struct wlr_box box = {
        .x = 10,
        .y = 20,
        .width = 100,
        .height = 200,
    };

    assert(strcmp(lumo_touch_region_name_in_box(&box, 15.0, 25.0, 20.0),
        "top-left") == 0);
    assert(strcmp(lumo_touch_region_name_in_box(&box, 60.0, 25.0, 20.0),
        "top-center") == 0);
    assert(strcmp(lumo_touch_region_name_in_box(&box, 105.0, 25.0, 20.0),
        "top-right") == 0);
    assert(strcmp(lumo_touch_region_name_in_box(&box, 15.0, 120.0, 20.0),
        "left-center") == 0);
    assert(strcmp(lumo_touch_region_name_in_box(&box, 60.0, 120.0, 20.0),
        "center") == 0);
    assert(strcmp(lumo_touch_region_name_in_box(&box, 105.0, 120.0, 20.0),
        "right-center") == 0);
    assert(strcmp(lumo_touch_region_name_in_box(&box, 15.0, 215.0, 20.0),
        "bottom-left") == 0);
    assert(strcmp(lumo_touch_region_name_in_box(&box, 60.0, 215.0, 20.0),
        "bottom-center") == 0);
    assert(strcmp(lumo_touch_region_name_in_box(&box, 105.0, 215.0, 20.0),
        "bottom-right") == 0);
    assert(strcmp(lumo_touch_region_name_in_box(&box, 0.0, 0.0, 20.0),
        "outside") == 0);
}

static void test_edge_zone_helpers(void) {
    struct wlr_box box = {
        .x = 10,
        .y = 20,
        .width = 100,
        .height = 200,
    };
    struct lumo_hitbox top_hitbox = {
        .name = "shell-edge-top",
        .kind = LUMO_HITBOX_EDGE_GESTURE,
    };
    struct lumo_hitbox bottom_hitbox = {
        .name = "shell-gesture",
        .kind = LUMO_HITBOX_EDGE_GESTURE,
    };

    assert(strcmp(lumo_edge_zone_name(LUMO_EDGE_TOP), "top") == 0);
    assert(strcmp(lumo_edge_zone_name(LUMO_EDGE_LEFT), "left") == 0);
    assert(strcmp(lumo_edge_zone_name(LUMO_EDGE_RIGHT), "right") == 0);
    assert(strcmp(lumo_edge_zone_name(LUMO_EDGE_BOTTOM), "bottom") == 0);
    assert(lumo_edge_zone_in_box(&box, 60.0, 22.0, 20.0) == LUMO_EDGE_TOP);
    assert(lumo_edge_zone_in_box(&box, 12.0, 120.0, 20.0) == LUMO_EDGE_LEFT);
    assert(lumo_edge_zone_in_box(&box, 108.0, 120.0, 20.0) == LUMO_EDGE_RIGHT);
    assert(lumo_edge_zone_in_box(&box, 60.0, 218.0, 20.0) == LUMO_EDGE_BOTTOM);
    assert(lumo_edge_zone_in_box(&box, 60.0, 120.0, 20.0) == LUMO_EDGE_NONE);
    assert(lumo_hitbox_edge_zone(&top_hitbox) == LUMO_EDGE_TOP);
    assert(lumo_hitbox_edge_zone(&bottom_hitbox) == LUMO_EDGE_BOTTOM);
}

static void test_backend_helpers(void) {
    enum lumo_backend_mode mode = LUMO_BACKEND_AUTO;

    assert(strcmp(lumo_backend_mode_name(LUMO_BACKEND_AUTO), "auto") == 0);
    assert(strcmp(lumo_backend_mode_name(LUMO_BACKEND_DRM), "drm") == 0);
    assert(strcmp(lumo_backend_mode_name(LUMO_BACKEND_WAYLAND), "wayland") == 0);
    assert(strcmp(lumo_backend_mode_name(LUMO_BACKEND_HEADLESS), "headless") == 0);
    assert(strcmp(lumo_backend_mode_name(LUMO_BACKEND_X11), "x11") == 0);
    assert(lumo_backend_env_value(LUMO_BACKEND_AUTO) == NULL);
    assert(strcmp(lumo_backend_env_value(LUMO_BACKEND_DRM), "libinput,drm") == 0);
    assert(strcmp(lumo_backend_env_value(LUMO_BACKEND_WAYLAND), "wayland") == 0);
    assert(strcmp(lumo_backend_env_value(LUMO_BACKEND_HEADLESS), "headless") == 0);
    assert(strcmp(lumo_backend_env_value(LUMO_BACKEND_X11), "x11") == 0);
    assert(lumo_backend_mode_parse("auto", &mode));
    assert(mode == LUMO_BACKEND_AUTO);
    assert(lumo_backend_mode_parse("drm", &mode));
    assert(mode == LUMO_BACKEND_DRM);
    assert(lumo_backend_mode_parse("wayland", &mode));
    assert(mode == LUMO_BACKEND_WAYLAND);
    assert(lumo_backend_mode_parse("headless", &mode));
    assert(mode == LUMO_BACKEND_HEADLESS);
    assert(lumo_backend_mode_parse("x11", &mode));
    assert(mode == LUMO_BACKEND_X11);
    assert(!lumo_backend_mode_parse("bogus", &mode));
    assert(lumo_tty_name_looks_like_vt("/dev/tty1"));
    assert(lumo_tty_name_looks_like_vt("/dev/tty12"));
    assert(!lumo_tty_name_looks_like_vt("/dev/pts/0"));
    assert(!lumo_tty_name_looks_like_vt("/dev/ttyS0"));
    assert(lumo_backend_auto_mode_for_session(
        "/dev/tty1",
        NULL,
        NULL,
        NULL,
        NULL) == LUMO_BACKEND_AUTO);
    assert(lumo_backend_auto_mode_for_session(
        "/dev/pts/0",
        "localhost",
        "/dev/pts/0",
        "wayland-1",
        NULL) == LUMO_BACKEND_HEADLESS);
    assert(lumo_backend_auto_mode_for_session(
        "/dev/pts/0",
        NULL,
        NULL,
        "wayland-1",
        NULL) == LUMO_BACKEND_WAYLAND);
    assert(lumo_backend_auto_mode_for_session(
        "/dev/pts/0",
        NULL,
        NULL,
        NULL,
        NULL) == LUMO_BACKEND_HEADLESS);
    assert(lumo_backend_auto_mode_for_session(
        "/dev/pts/0",
        NULL,
        NULL,
        NULL,
        ":1") == LUMO_BACKEND_X11);
}

struct lumo_shell_protocol_capture {
    bool called;
    struct lumo_shell_protocol_frame frame;
};

static void lumo_shell_protocol_capture_frame(
    const struct lumo_shell_protocol_frame *frame,
    void *user_data
) {
    struct lumo_shell_protocol_capture *capture = user_data;

    if (capture == NULL || frame == NULL) {
        return;
    }

    capture->called = true;
    capture->frame = *frame;
}

static void test_shell_protocol_roundtrip(void) {
    struct lumo_shell_protocol_frame frame = {0};
    struct lumo_shell_protocol_stream stream = {0};
    struct lumo_shell_protocol_capture capture = {0};
    char buffer[512];
    size_t length;
    size_t split;
    const char *value = NULL;
    bool bool_value = false;
    uint32_t u32_value = 0;
    double double_value = 0.0;
    enum lumo_shell_protocol_frame_kind frame_kind;

    assert(strcmp(lumo_shell_protocol_frame_kind_name(
        LUMO_SHELL_PROTOCOL_FRAME_EVENT), "event") == 0);
    assert(strcmp(lumo_shell_protocol_frame_kind_name(
        LUMO_SHELL_PROTOCOL_FRAME_REQUEST), "request") == 0);
    assert(lumo_shell_protocol_frame_kind_parse("response", &frame_kind));
    assert(frame_kind == LUMO_SHELL_PROTOCOL_FRAME_RESPONSE);
    assert(lumo_shell_protocol_frame_init(&frame,
        LUMO_SHELL_PROTOCOL_FRAME_EVENT, "state", 12));
    assert(lumo_shell_protocol_frame_add_bool(&frame, "launcher_visible", true));
    assert(lumo_shell_protocol_frame_add_bool(&frame, "keyboard_visible", false));
    assert(lumo_shell_protocol_frame_add_string(&frame, "scrim_state", "modal"));
    assert(lumo_shell_protocol_frame_add_string(&frame, "rotation", "180"));
    assert(lumo_shell_protocol_frame_add_double(&frame, "gesture_threshold", 32.5));
    assert(lumo_shell_protocol_frame_add_u32(&frame, "gesture_timeout_ms", 180));

    length = lumo_shell_protocol_frame_format(&frame, buffer, sizeof(buffer));
    assert(length > 0);
    assert(strncmp(buffer, "LUMO/1 event state id=12\n",
        strlen("LUMO/1 event state id=12\n")) == 0);

    split = length / 2;
    lumo_shell_protocol_stream_init(&stream);
    assert(lumo_shell_protocol_stream_feed(&stream, buffer, split,
        lumo_shell_protocol_capture_frame, &capture));
    assert(!capture.called);
    assert(lumo_shell_protocol_stream_feed(&stream, buffer + split,
        length - split, lumo_shell_protocol_capture_frame, &capture));
    assert(capture.called);
    assert(capture.frame.kind == LUMO_SHELL_PROTOCOL_FRAME_EVENT);
    assert(strcmp(capture.frame.name, "state") == 0);
    assert(capture.frame.id == 12);

    assert(lumo_shell_protocol_frame_get_bool(&capture.frame, "launcher_visible",
        &bool_value));
    assert(bool_value);
    assert(lumo_shell_protocol_frame_get_bool(&capture.frame, "keyboard_visible",
        &bool_value));
    assert(!bool_value);
    assert(lumo_shell_protocol_frame_get(&capture.frame, "scrim_state",
        &value));
    assert(strcmp(value, "modal") == 0);
    assert(lumo_shell_protocol_frame_get_double(&capture.frame,
        "gesture_threshold", &double_value));
    assert(double_value == 32.5);
    assert(lumo_shell_protocol_frame_get_u32(&capture.frame,
        "gesture_timeout_ms", &u32_value));
    assert(u32_value == 180);

    capture.called = false;
    memset(&capture.frame, 0, sizeof(capture.frame));
    assert(lumo_shell_protocol_frame_init(&frame,
        LUMO_SHELL_PROTOCOL_FRAME_REQUEST, "activate_target", 99));
    assert(lumo_shell_protocol_frame_add_string(&frame, "kind",
        "launcher-tile"));
    assert(lumo_shell_protocol_frame_add_u32(&frame, "index", 3));
    length = lumo_shell_protocol_frame_format(&frame, buffer, sizeof(buffer));
    assert(length > 0);

    lumo_shell_protocol_stream_init(&stream);
    assert(lumo_shell_protocol_stream_feed(&stream, buffer, length,
        lumo_shell_protocol_capture_frame, &capture));
    assert(capture.called);
    assert(capture.frame.kind == LUMO_SHELL_PROTOCOL_FRAME_REQUEST);
    assert(strcmp(capture.frame.name, "activate_target") == 0);
    assert(capture.frame.id == 99);
    assert(lumo_shell_protocol_frame_get_u32(&capture.frame, "index",
        &u32_value));
    assert(u32_value == 3);
    assert(lumo_shell_protocol_frame_get(&capture.frame, "kind", &value));
    assert(strcmp(value, "launcher-tile") == 0);
}

static void test_protocol_listener_owner(void) {
    struct lumo_compositor compositor = {0};
    struct lumo_protocol_state state = {
        .compositor = &compositor,
    };

    assert(lumo_protocol_listener_compositor(&state.xdg_new_toplevel,
        LUMO_PROTOCOL_LISTENER_XDG_TOPLEVEL) == &compositor);
    assert(lumo_protocol_listener_compositor(&state.xdg_new_popup,
        LUMO_PROTOCOL_LISTENER_XDG_POPUP) == &compositor);
    assert(lumo_protocol_listener_compositor(&state.layer_new_surface,
        LUMO_PROTOCOL_LISTENER_LAYER_SURFACE) == &compositor);
    assert(lumo_protocol_listener_compositor(NULL,
        LUMO_PROTOCOL_LISTENER_LAYER_SURFACE) == NULL);
}

static void test_compositor_defaults(void) {
    const struct lumo_compositor_config config = {
        .session_name = "lumo-test",
        .socket_name = "lumo-test-socket",
        .initial_rotation = LUMO_ROTATION_180,
        .debug = false,
    };
    struct lumo_compositor *compositor = lumo_compositor_create(&config);

    assert(compositor != NULL);
    assert(compositor->active_rotation == LUMO_ROTATION_180);
    assert(compositor->gesture_threshold == 32.0);
    assert(compositor->gesture_timeout_ms == 180);
    assert(compositor->scrim_state == LUMO_SCRIM_HIDDEN);
    assert(!compositor->touch_audit_active);
    assert(!compositor->touch_audit_saved);
    assert(compositor->touch_audit_step == 0);
    assert(compositor->touch_audit_completed_mask == 0);
    assert(compositor->layer_config_dirty);
    assert(!compositor->launcher_visible);
    assert(compositor->xwayland == NULL);
    assert(!compositor->xwayland_ready);
    assert(compositor->input_devices.next == &compositor->input_devices);
    assert(compositor->input_devices.prev == &compositor->input_devices);
    lumo_compositor_destroy(compositor);
}

static void test_layer_configuration_dirty_without_outputs(void) {
    const struct lumo_compositor_config config = {
        .session_name = "lumo-test",
        .socket_name = "lumo-test-socket",
        .initial_rotation = LUMO_ROTATION_NORMAL,
        .debug = false,
    };
    struct lumo_compositor *compositor = lumo_compositor_create(&config);

    assert(compositor != NULL);
    compositor->layer_config_dirty = false;
    lumo_protocol_configure_all_layers(compositor);
    assert(compositor->layer_config_dirty);
    lumo_compositor_destroy(compositor);
}

static void test_hitbox_state(void) {
    const struct lumo_compositor_config config = {
        .session_name = "lumo-test",
        .socket_name = "lumo-test-socket",
        .initial_rotation = LUMO_ROTATION_NORMAL,
        .debug = false,
    };
    struct lumo_compositor *compositor = lumo_compositor_create(&config);
    const struct lumo_hitbox *hitbox;
    struct lumo_rect primary = {
        .x = 0,
        .y = 0,
        .width = 100,
        .height = 100,
    };
    struct lumo_rect secondary = {
        .x = 25,
        .y = 25,
        .width = 50,
        .height = 50,
    };

    assert(compositor != NULL);
    assert(lumo_protocol_register_hitbox(compositor, "primary", &primary,
        LUMO_HITBOX_CUSTOM, true, true) == 0);
    assert(lumo_protocol_register_hitbox(compositor, "secondary", &secondary,
        LUMO_HITBOX_EDGE_GESTURE, true, false) == 0);

    hitbox = lumo_protocol_hitbox_at(compositor, 30, 30);
    assert(hitbox != NULL);
    assert(hitbox->kind == LUMO_HITBOX_EDGE_GESTURE);

    hitbox = lumo_protocol_hitbox_at(compositor, 5, 5);
    assert(hitbox != NULL);
    assert(hitbox->kind == LUMO_HITBOX_CUSTOM);

    lumo_compositor_destroy(compositor);
}

static void test_touch_debug_helpers(void) {
    struct lumo_hitbox gesture_hitbox = {
        .name = "shell-gesture",
        .kind = LUMO_HITBOX_EDGE_GESTURE,
    };
    struct lumo_hitbox other_hitbox = {
        .name = "launcher-scrim",
        .kind = LUMO_HITBOX_SCRIM,
    };
    struct lumo_touch_point launcher_capture = {
        .captured = true,
        .delivered = false,
        .hitbox = &gesture_hitbox,
        .capture_edge = LUMO_EDGE_BOTTOM,
    };
    struct lumo_touch_point generic_edge_capture = {
        .captured = true,
        .delivered = false,
        .hitbox = NULL,
        .capture_edge = LUMO_EDGE_BOTTOM,
    };
    struct lumo_touch_point scrim_capture = {
        .captured = true,
        .delivered = false,
        .hitbox = &other_hitbox,
    };

    assert(strcmp(lumo_touch_target_kind_name(LUMO_TOUCH_TARGET_NONE),
        "none") == 0);
    assert(strcmp(lumo_touch_target_kind_name(LUMO_TOUCH_TARGET_HITBOX),
        "hitbox") == 0);
    assert(strcmp(lumo_touch_target_kind_name(LUMO_TOUCH_TARGET_SURFACE),
        "surface") == 0);

    assert(strcmp(lumo_touch_sample_type_name(LUMO_TOUCH_SAMPLE_DOWN),
        "down") == 0);
    assert(strcmp(lumo_touch_sample_type_name(LUMO_TOUCH_SAMPLE_MOTION),
        "motion") == 0);
    assert(strcmp(lumo_touch_sample_type_name(LUMO_TOUCH_SAMPLE_UP),
        "up") == 0);
    assert(strcmp(lumo_touch_sample_type_name(LUMO_TOUCH_SAMPLE_CANCEL),
        "cancel") == 0);

    assert(lumo_hitbox_is_shell_gesture(&gesture_hitbox));
    assert(!lumo_hitbox_is_shell_gesture(&other_hitbox));
    gesture_hitbox.name = NULL;
    assert(!lumo_hitbox_is_shell_gesture(&gesture_hitbox));

    assert(lumo_touch_point_is_launcher_capture(&launcher_capture));
    assert(lumo_touch_point_is_launcher_capture(&generic_edge_capture));
    assert(!lumo_touch_point_is_launcher_capture(&scrim_capture));
    generic_edge_capture.delivered = true;
    assert(!lumo_touch_point_is_launcher_capture(&generic_edge_capture));
}

static void test_scene_surface_helper_ignores_rect_buffers(void) {
    struct wlr_scene *scene;
    struct wlr_scene_rect *rect;
    struct wlr_scene_node *node = NULL;
    struct wlr_scene_buffer *buffer = NULL;
    struct wlr_scene_surface *surface = NULL;
    const float color[4] = {0.02f, 0.04f, 0.08f, 1.0f};
    double sx = 0.0;
    double sy = 0.0;

    scene = wlr_scene_create();
    assert(scene != NULL);

    rect = wlr_scene_rect_create(&scene->tree, 128, 64, color);
    assert(rect != NULL);

    node = wlr_scene_node_at(&scene->tree.node, 32.0, 16.0, &sx, &sy);
    assert(node != NULL);

    surface = lumo_scene_surface_from_node(node, &buffer);
    assert(surface == NULL);

    wlr_scene_node_destroy(&scene->tree.node);
}

static void test_shell_hitbox_refresh(void) {
    const struct lumo_compositor_config config = {
        .session_name = "lumo-test",
        .socket_name = "lumo-test-socket",
        .initial_rotation = LUMO_ROTATION_NORMAL,
        .debug = false,
    };
    struct lumo_compositor *compositor = lumo_compositor_create(&config);
    struct lumo_output output = {0};
    struct lumo_rect workarea = {
        .x = 0,
        .y = 0,
        .width = 1024,
        .height = 600,
    };
    const struct lumo_hitbox *hitbox;

    assert(compositor != NULL);
    wl_list_init(&output.link);
    output.usable_area.x = workarea.x;
    output.usable_area.y = workarea.y;
    output.usable_area.width = workarea.width;
    output.usable_area.height = workarea.height;
    output.usable_area_valid = true;
    wl_list_insert(&compositor->outputs, &output.link);

    lumo_protocol_set_keyboard_visible(compositor, true);
    hitbox = lumo_protocol_hitbox_at(compositor, 64, 588);
    assert(hitbox != NULL);
    assert(hitbox->kind == LUMO_HITBOX_OSK_KEY);

    wl_list_remove(&output.link);
    lumo_compositor_destroy(compositor);

    compositor = lumo_compositor_create(&config);
    wl_list_init(&output.link);
    output.usable_area.x = workarea.x;
    output.usable_area.y = workarea.y;
    output.usable_area.width = workarea.width;
    output.usable_area.height = workarea.height;
    output.usable_area_valid = true;
    wl_list_insert(&compositor->outputs, &output.link);

    lumo_protocol_set_launcher_visible(compositor, true);
    hitbox = lumo_protocol_hitbox_at(compositor, 128, 128);
    assert(hitbox != NULL);
    assert(hitbox->kind == LUMO_HITBOX_SCRIM);
    assert(hitbox->rect.width < workarea.width);
    assert(hitbox->rect.height < workarea.height);

    wl_list_remove(&output.link);
    lumo_compositor_destroy(compositor);
}

static void test_xwayland_workarea_collection(void) {
    const struct lumo_compositor_config config = {
        .session_name = "lumo-test",
        .socket_name = "lumo-test-socket",
        .initial_rotation = LUMO_ROTATION_NORMAL,
        .debug = false,
    };
    struct lumo_compositor *compositor = lumo_compositor_create(&config);
    struct lumo_output output = {0};
    struct wlr_box workarea = {0};
    struct wlr_box expected = {
        .x = 12,
        .y = 24,
        .width = 800,
        .height = 600,
    };

    assert(compositor != NULL);
    assert(!lumo_xwayland_collect_workarea(compositor, &workarea));
    assert(workarea.x == 0);
    assert(workarea.y == 0);
    assert(workarea.width == 0);
    assert(workarea.height == 0);

    wl_list_init(&output.link);
    output.usable_area = expected;
    output.usable_area_valid = true;
    wl_list_insert(&compositor->outputs, &output.link);

    assert(lumo_xwayland_collect_workarea(compositor, &workarea));
    assert(workarea.x == expected.x);
    assert(workarea.y == expected.y);
    assert(workarea.width == expected.width);
    assert(workarea.height == expected.height);

    wl_list_remove(&output.link);
    lumo_compositor_destroy(compositor);
}

static void test_xwayland_toggle(void) {
    const struct lumo_compositor_config config = {
        .session_name = "lumo-test",
        .socket_name = "lumo-test-socket",
        .initial_rotation = LUMO_ROTATION_NORMAL,
        .debug = false,
    };
    struct lumo_compositor *compositor = lumo_compositor_create(&config);

    assert(compositor != NULL);
#if LUMO_ENABLE_XWAYLAND
    assert(lumo_xwayland_start(compositor) == -1);
#else
    assert(lumo_xwayland_start(compositor) == 0);
#endif
    assert(compositor->xwayland == NULL);
    lumo_xwayland_sync_workareas(compositor);
    lumo_xwayland_focus_surface(compositor, NULL);
    lumo_xwayland_stop(compositor);
    lumo_compositor_destroy(compositor);
}

static void test_xwayland_ready_gate(void) {
    const struct lumo_compositor_config config = {
        .session_name = "lumo-test",
        .socket_name = "lumo-test-socket",
        .initial_rotation = LUMO_ROTATION_NORMAL,
        .debug = false,
    };
    struct lumo_compositor *compositor = lumo_compositor_create(&config);
    struct lumo_output output = {0};
    struct wlr_box expected = {
        .x = 12,
        .y = 24,
        .width = 800,
        .height = 600,
    };

    assert(compositor != NULL);
    wl_list_init(&output.link);
    output.usable_area = expected;
    output.usable_area_valid = true;
    wl_list_insert(&compositor->outputs, &output.link);

#if LUMO_ENABLE_XWAYLAND
    compositor->xwayland = (struct wlr_xwayland *)0x1;
    compositor->xwayland_ready = false;
    lumo_xwayland_sync_workareas(compositor);
    assert(!compositor->xwayland_workarea_valid);
    lumo_xwayland_focus_surface(compositor, NULL);
#endif

    wl_list_remove(&output.link);
    lumo_compositor_destroy(compositor);
}

static void test_state_setters(void) {
    const struct lumo_compositor_config config = {
        .session_name = "lumo-test",
        .socket_name = "lumo-test-socket",
        .initial_rotation = LUMO_ROTATION_NORMAL,
        .debug = false,
    };
    struct lumo_compositor *compositor = lumo_compositor_create(&config);

    assert(compositor != NULL);
    lumo_protocol_set_gesture_threshold(compositor, 44.0);
    assert(compositor->gesture_threshold == 44.0);

    compositor->layer_config_dirty = false;
    lumo_protocol_set_launcher_visible(compositor, true);
    assert(compositor->launcher_visible);
    assert(compositor->scrim_state == LUMO_SCRIM_MODAL);
    assert(compositor->layer_config_dirty);

    compositor->layer_config_dirty = false;
    lumo_protocol_set_keyboard_visible(compositor, true);
    assert(compositor->keyboard_visible);
    assert(compositor->keyboard_resize_pending);
    assert(compositor->layer_config_dirty);

    lumo_protocol_ack_keyboard_resize(compositor,
        compositor->keyboard_resize_serial);
    assert(!compositor->keyboard_resize_pending);
    assert(compositor->keyboard_resize_acked);

    lumo_protocol_set_scrim_state(compositor, LUMO_SCRIM_DIMMED);
    assert(compositor->scrim_state == LUMO_SCRIM_DIMMED);

    assert(lumo_rotation_parse("270", &compositor->active_rotation));
    assert(compositor->active_rotation == LUMO_ROTATION_270);
    assert(lumo_scrim_state_parse("hidden", &compositor->scrim_state));
    assert(compositor->scrim_state == LUMO_SCRIM_HIDDEN);

    lumo_input_set_rotation(compositor, LUMO_ROTATION_90);
    assert(compositor->active_rotation == LUMO_ROTATION_90);

    compositor->layer_config_dirty = false;
    lumo_touch_audit_set_active(compositor, true);
    assert(compositor->touch_audit_active);
    assert(compositor->scrim_state == LUMO_SCRIM_DIMMED);
    assert(compositor->layer_config_dirty);
    lumo_touch_audit_set_active(compositor, false);
    assert(!compositor->touch_audit_active);

    lumo_compositor_destroy(compositor);
}

static void test_touch_audit_debug_gesture_policy(void) {
    const struct lumo_compositor_config release_config = {
        .session_name = "lumo-release",
        .socket_name = "lumo-release-socket",
        .initial_rotation = LUMO_ROTATION_NORMAL,
        .debug = false,
    };
    const struct lumo_compositor_config debug_config = {
        .session_name = "lumo-debug",
        .socket_name = "lumo-debug-socket",
        .initial_rotation = LUMO_ROTATION_NORMAL,
        .debug = true,
    };
    struct lumo_compositor *release_compositor =
        lumo_compositor_create(&release_config);
    struct lumo_compositor *debug_compositor =
        lumo_compositor_create(&debug_config);

    assert(!lumo_touch_audit_debug_gesture_enabled(NULL));
    assert(release_compositor != NULL);
    assert(debug_compositor != NULL);
    assert(!lumo_touch_audit_debug_gesture_enabled(release_compositor));
    assert(lumo_touch_audit_debug_gesture_enabled(debug_compositor));

    lumo_compositor_destroy(release_compositor);
    lumo_compositor_destroy(debug_compositor);
}

static void test_shell_binary_resolution(void) {
    struct lumo_compositor_config config = {
        .session_name = "lumo-test",
        .socket_name = "lumo-test-socket",
        .executable_path = "/opt/lumo/bin/lumo-compositor",
        .shell_path = NULL,
        .initial_rotation = LUMO_ROTATION_NORMAL,
        .debug = false,
    };
    char buffer[PATH_MAX];

    assert(lumo_shell_resolve_binary_path(&config, buffer, sizeof(buffer)));
    assert(strcmp(buffer, "/opt/lumo/bin/lumo-shell") == 0);

    config.shell_path = "/custom/bin/lumo-shell-debug";
    assert(lumo_shell_resolve_binary_path(&config, buffer, sizeof(buffer)));
    assert(strcmp(buffer, "/custom/bin/lumo-shell-debug") == 0);

    config.shell_path = "lumo-shell-debug";
    assert(lumo_shell_resolve_binary_path(&config, buffer, sizeof(buffer)));
    assert(strcmp(buffer, "/opt/lumo/bin/lumo-shell-debug") == 0);

    config.executable_path = "lumo-compositor";
    config.shell_path = NULL;
    assert(lumo_shell_resolve_binary_path(&config, buffer, sizeof(buffer)));
    assert(strcmp(buffer, "lumo-shell") == 0);
}

static void test_shell_mode_helpers(void) {
    size_t index = SIZE_MAX;

    assert(lumo_shell_mode_count() == 4);
    assert(lumo_shell_mode_index(LUMO_SHELL_MODE_LAUNCHER, &index));
    assert(index == 0);
    assert(lumo_shell_mode_index(LUMO_SHELL_MODE_OSK, &index));
    assert(index == 1);
    assert(lumo_shell_mode_index(LUMO_SHELL_MODE_GESTURE, &index));
    assert(index == 2);
    assert(lumo_shell_mode_index(LUMO_SHELL_MODE_STATUS, &index));
    assert(index == 3);
    assert(!lumo_shell_mode_index((enum lumo_shell_mode)99, &index));
}

static void test_shell_argv_builder(void) {
    const char *argv[4] = {0};
    size_t argc;

    argc = lumo_shell_build_argv(LUMO_SHELL_MODE_LAUNCHER,
        "/opt/lumo/bin/lumo-shell", argv, 4);
    assert(argc == 3);
    assert(strcmp(argv[0], "/opt/lumo/bin/lumo-shell") == 0);
    assert(strcmp(argv[1], "--mode") == 0);
    assert(strcmp(argv[2], "launcher") == 0);
    assert(argv[3] == NULL);

    assert(lumo_shell_build_argv((enum lumo_shell_mode)99,
        "/opt/lumo/bin/lumo-shell", argv, 4) == 0);
    assert(lumo_shell_build_argv(LUMO_SHELL_MODE_LAUNCHER,
        "/opt/lumo/bin/lumo-shell", argv, 3) == 0);
}

static void test_shell_state_helpers(void) {
    char buffer[128];

    assert(lumo_shell_state_socket_path("/run/user/1000", buffer,
        sizeof(buffer)));
    assert(strcmp(buffer, "/run/user/1000/lumo-shell-state.sock") == 0);

    assert(lumo_shell_state_format_line(buffer, sizeof(buffer),
        "rotation", "90") == strlen("rotation=90\n"));
    assert(strcmp(buffer, "rotation=90\n") == 0);

    assert(lumo_shell_state_format_bool(buffer, sizeof(buffer),
        "launcher visible", true) == strlen("launcher visible=1\n"));
    assert(strcmp(buffer, "launcher visible=1\n") == 0);

    assert(lumo_shell_state_format_double(buffer, sizeof(buffer),
        "gesture threshold", 32.0) == strlen("gesture threshold=32.00\n"));
    assert(strcmp(buffer, "gesture threshold=32.00\n") == 0);
}

static void test_layer_surface_commit_reconfigure_policy(void) {
    struct wlr_layer_surface_v1_state previous = {0};
    struct wlr_layer_surface_v1_state current = {0};

    assert(lumo_protocol_layer_surface_commit_needs_reconfigure(
        &previous, false, &current, false));
    assert(lumo_protocol_layer_surface_commit_needs_reconfigure(
        &previous, false, &current, true));
    assert(!lumo_protocol_layer_surface_commit_needs_reconfigure(
        &previous, true, &current, true));

    current.margin.top = 12;
    assert(lumo_protocol_layer_surface_commit_needs_reconfigure(
        &previous, true, &current, true));

    previous = current;
    assert(!lumo_protocol_layer_surface_commit_needs_reconfigure(
        &previous, true, &current, true));

    current.exclusive_zone = 48;
    assert(lumo_protocol_layer_surface_commit_needs_reconfigure(
        &previous, true, &current, true));
}

int main(void) {
    test_backend_helpers();
    test_rotation_helpers();
    test_touch_rotation_mapping();
    test_touch_region_helpers();
    test_edge_zone_helpers();
    test_compositor_defaults();
    test_layer_configuration_dirty_without_outputs();
    test_hitbox_state();
    test_touch_debug_helpers();
    test_scene_surface_helper_ignores_rect_buffers();
    test_shell_hitbox_refresh();
    test_xwayland_workarea_collection();
    test_xwayland_toggle();
    test_xwayland_ready_gate();
    test_state_setters();
    test_touch_audit_debug_gesture_policy();
    test_shell_protocol_roundtrip();
    test_protocol_listener_owner();
    test_shell_binary_resolution();
    test_shell_mode_helpers();
    test_shell_argv_builder();
    test_shell_state_helpers();
    test_layer_surface_commit_reconfigure_policy();
    puts("lumo compositor tests passed");
    return 0;
}
