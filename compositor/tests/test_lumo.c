#include "lumo/compositor.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

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
    assert(!compositor->launcher_visible);
    assert(compositor->xwayland == NULL);
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

    lumo_protocol_set_launcher_visible(compositor, true);
    assert(compositor->launcher_visible);
    assert(compositor->scrim_state == LUMO_SCRIM_MODAL);

    lumo_protocol_set_keyboard_visible(compositor, true);
    assert(compositor->keyboard_visible);
    assert(compositor->keyboard_resize_pending);

    lumo_protocol_ack_keyboard_resize(compositor,
        compositor->keyboard_resize_serial);
    assert(!compositor->keyboard_resize_pending);
    assert(compositor->keyboard_resize_acked);

    lumo_protocol_set_scrim_state(compositor, LUMO_SCRIM_DIMMED);
    assert(compositor->scrim_state == LUMO_SCRIM_DIMMED);

    lumo_input_set_rotation(compositor, LUMO_ROTATION_90);
    assert(compositor->active_rotation == LUMO_ROTATION_90);

    lumo_compositor_destroy(compositor);
}

int main(void) {
    test_rotation_helpers();
    test_compositor_defaults();
    test_hitbox_state();
    test_state_setters();
    puts("lumo compositor tests passed");
    return 0;
}
