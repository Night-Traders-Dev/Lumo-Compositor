#include "lumo/shell.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_mode_names(void) {
    assert(strcmp(lumo_shell_mode_name(LUMO_SHELL_MODE_LAUNCHER), "launcher") == 0);
    assert(strcmp(lumo_shell_mode_name(LUMO_SHELL_MODE_OSK), "osk") == 0);
    assert(strcmp(lumo_shell_mode_name(LUMO_SHELL_MODE_GESTURE), "gesture") == 0);
    assert(strcmp(lumo_shell_target_kind_name(LUMO_SHELL_TARGET_NONE), "none") == 0);
    assert(strcmp(lumo_shell_target_kind_name(LUMO_SHELL_TARGET_LAUNCHER_TILE),
        "launcher-tile") == 0);
    assert(strcmp(lumo_shell_target_kind_name(LUMO_SHELL_TARGET_OSK_KEY),
        "osk-key") == 0);
    assert(strcmp(lumo_shell_target_kind_name(LUMO_SHELL_TARGET_GESTURE_HANDLE),
        "gesture-handle") == 0);
}

static void test_target_kind_parse(void) {
    enum lumo_shell_target_kind kind = LUMO_SHELL_TARGET_NONE;

    assert(lumo_shell_target_kind_parse("launcher-tile", &kind));
    assert(kind == LUMO_SHELL_TARGET_LAUNCHER_TILE);
    assert(lumo_shell_target_kind_parse("osk_key", &kind));
    assert(kind == LUMO_SHELL_TARGET_OSK_KEY);
    assert(lumo_shell_target_kind_parse("gesture-handle", &kind));
    assert(kind == LUMO_SHELL_TARGET_GESTURE_HANDLE);
    assert(lumo_shell_target_kind_parse("none", &kind));
    assert(kind == LUMO_SHELL_TARGET_NONE);
}

static void test_osk_key_text(void) {
    assert(strcmp(lumo_shell_osk_key_text(0), "q") == 0);
    assert(strcmp(lumo_shell_osk_key_text(10), "a") == 0);
    assert(strcmp(lumo_shell_osk_key_text(26), ",") == 0);
    assert(strcmp(lumo_shell_osk_key_text(28), " ") == 0);
    assert(strcmp(lumo_shell_osk_key_text(30), "\n") == 0);
    assert(lumo_shell_osk_key_text(31) == NULL);
}

static void test_shell_labels(void) {
    assert(strcmp(lumo_shell_launcher_tile_label(0), "PHONE") == 0);
    assert(strcmp(lumo_shell_launcher_tile_label(11), "SETTINGS") == 0);
    assert(lumo_shell_launcher_tile_label(12) == NULL);

    assert(strcmp(lumo_shell_osk_key_label(0), "Q") == 0);
    assert(strcmp(lumo_shell_osk_key_label(28), "SPACE") == 0);
    assert(strcmp(lumo_shell_osk_key_label(30), "RETURN") == 0);
    assert(lumo_shell_osk_key_label(31) == NULL);

    assert(strcmp(lumo_shell_touch_audit_point_name(0), "top-left") == 0);
    assert(strcmp(lumo_shell_touch_audit_point_name(7), "bottom-right") == 0);
    assert(lumo_shell_touch_audit_point_name(8) == NULL);
    assert(strcmp(lumo_shell_touch_audit_point_label(1), "TOP") == 0);
    assert(strcmp(lumo_shell_touch_audit_point_label(6), "BOTTOM") == 0);
    assert(lumo_shell_touch_audit_point_label(8) == NULL);
}

static void test_layout_counts(void) {
    assert(lumo_shell_launcher_tile_count() == 12);
    assert(lumo_shell_osk_key_count() == 31);
    assert(lumo_shell_touch_audit_point_count() == 8);
}

static void test_launcher_config(void) {
    struct lumo_shell_surface_config config = {0};

    assert(lumo_shell_surface_config_for_mode(LUMO_SHELL_MODE_LAUNCHER,
        1024, 600, &config));
    assert(config.mode == LUMO_SHELL_MODE_LAUNCHER);
    assert(config.width == 1024);
    assert(config.height == 600);
    assert(config.anchor == (LUMO_SHELL_ANCHOR_TOP |
        LUMO_SHELL_ANCHOR_BOTTOM |
        LUMO_SHELL_ANCHOR_LEFT |
        LUMO_SHELL_ANCHOR_RIGHT));
    assert(config.exclusive_zone == 0);
    assert(config.keyboard_interactive);
    assert(config.background_rgba == 0x00101822);
}

static void test_osk_config(void) {
    struct lumo_shell_surface_config config = {0};

    assert(lumo_shell_surface_config_for_mode(LUMO_SHELL_MODE_OSK,
        1024, 600, &config));
    assert(config.mode == LUMO_SHELL_MODE_OSK);
    assert(config.width == 1024);
    assert(config.height == 280);
    assert(config.anchor == (LUMO_SHELL_ANCHOR_BOTTOM |
        LUMO_SHELL_ANCHOR_LEFT |
        LUMO_SHELL_ANCHOR_RIGHT));
    assert(config.exclusive_zone == 280);
    assert(config.keyboard_interactive);
    assert(config.background_rgba == 0x0012161C);
}

static void test_gesture_config(void) {
    struct lumo_shell_surface_config config = {0};

    assert(lumo_shell_surface_config_for_mode(LUMO_SHELL_MODE_GESTURE,
        1024, 600, &config));
    assert(config.mode == LUMO_SHELL_MODE_GESTURE);
    assert(config.width == 1024);
    assert(config.height == 28);
    assert(config.anchor == (LUMO_SHELL_ANCHOR_BOTTOM |
        LUMO_SHELL_ANCHOR_LEFT |
        LUMO_SHELL_ANCHOR_RIGHT));
    assert(config.exclusive_zone == 28);
    assert(!config.keyboard_interactive);
    assert(config.background_rgba == 0x00000000);
}

static void test_bootstrap_config(void) {
    struct lumo_shell_surface_config config = {0};

    assert(lumo_shell_surface_bootstrap_config(LUMO_SHELL_MODE_LAUNCHER, &config));
    assert(config.width == 1);
    assert(config.height == 1);
    assert(!config.keyboard_interactive);

    assert(lumo_shell_surface_bootstrap_config(LUMO_SHELL_MODE_OSK, &config));
    assert(config.width == 0);
    assert(config.height == 1);
    assert(config.exclusive_zone == 0);
    assert(!config.keyboard_interactive);

    assert(lumo_shell_surface_bootstrap_config(LUMO_SHELL_MODE_GESTURE, &config));
    assert(config.width == 0);
    assert(config.height == 40);
    assert(config.exclusive_zone == 40);
    assert(!config.keyboard_interactive);
}

static void test_invalid_config(void) {
    struct lumo_shell_surface_config config = {0};

    assert(!lumo_shell_surface_config_for_mode(LUMO_SHELL_MODE_LAUNCHER,
        0, 600, &config));
    assert(!lumo_shell_surface_config_for_mode(LUMO_SHELL_MODE_LAUNCHER,
        1024, 0, &config));
    assert(!lumo_shell_surface_config_for_mode((enum lumo_shell_mode)99,
        1024, 600, &config));
}

static void test_launcher_hitboxes(void) {
    struct lumo_rect rect = {0};
    struct lumo_rect panel = {0};
    struct lumo_shell_target target = {0};

    assert(lumo_shell_launcher_panel_rect(1024, 600, &panel));
    assert(panel.x > 0);
    assert(panel.y > 0);
    assert(panel.width < 1024);
    assert(panel.height < 600);

    assert(lumo_shell_launcher_tile_rect(1024, 600, 0, &rect));
    assert(rect.x >= 0);
    assert(rect.y >= 0);
    assert(rect.width > 0);
    assert(rect.height > 0);

    assert(lumo_shell_target_for_mode(LUMO_SHELL_MODE_LAUNCHER,
        1024, 600, rect.x + rect.width / 2.0, rect.y + rect.height / 2.0,
        &target));
    assert(target.kind == LUMO_SHELL_TARGET_LAUNCHER_TILE);
    assert(target.index == 0);
    assert(target.rect.x == rect.x);
    assert(target.rect.y == rect.y);

    assert(lumo_shell_launcher_tile_rect(1024, 600, 11, &rect));
    assert(lumo_shell_target_for_mode(LUMO_SHELL_MODE_LAUNCHER,
        1024, 600, rect.x + rect.width / 2.0, rect.y + rect.height / 2.0,
        &target));
    assert(target.kind == LUMO_SHELL_TARGET_LAUNCHER_TILE);
    assert(target.index == 11);
}

static void test_osk_hitboxes(void) {
    struct lumo_rect rect = {0};
    struct lumo_shell_target target = {0};

    assert(lumo_shell_osk_key_rect(1024, 320, 0, &rect));
    assert(rect.width > 0);
    assert(rect.height > 0);

    assert(lumo_shell_target_for_mode(LUMO_SHELL_MODE_OSK,
        1024, 320, rect.x + rect.width / 2.0, rect.y + rect.height / 2.0,
        &target));
    assert(target.kind == LUMO_SHELL_TARGET_OSK_KEY);
    assert(target.index == 0);

    assert(lumo_shell_osk_key_rect(1024, 320, 28, &rect));
    assert(rect.width > 0);
    assert(lumo_shell_target_for_mode(LUMO_SHELL_MODE_OSK,
        1024, 320, rect.x + rect.width / 2.0, rect.y + rect.height / 2.0,
        &target));
    assert(target.kind == LUMO_SHELL_TARGET_OSK_KEY);
    assert(target.index == 28);
}

static void test_gesture_hitbox(void) {
    struct lumo_rect rect = {0};
    struct lumo_shell_target target = {0};

    assert(lumo_shell_gesture_handle_rect(1024, 80, &rect));
    assert(rect.x > 0);
    assert(rect.y > 0);
    assert(rect.width < 1024);
    assert(rect.height < 80);

    assert(lumo_shell_target_for_mode(LUMO_SHELL_MODE_GESTURE,
        1024, 80, rect.x + rect.width / 2.0, rect.y + rect.height / 2.0,
        &target));
    assert(target.kind == LUMO_SHELL_TARGET_GESTURE_HANDLE);
    assert(target.index == 0);
}

static void test_touch_audit_layout(void) {
    struct lumo_rect rect = {0};
    uint32_t point_index = UINT32_MAX;

    assert(lumo_shell_touch_audit_point_for_region("top-left", &point_index));
    assert(point_index == 0);
    assert(lumo_shell_touch_audit_point_for_region("right-center", &point_index));
    assert(point_index == 4);
    assert(!lumo_shell_touch_audit_point_for_region("center", &point_index));

    assert(lumo_shell_touch_audit_point_rect(1280, 800, 0, &rect));
    assert(rect.x >= 0);
    assert(rect.y >= 0);
    assert(rect.width > 0);
    assert(rect.height > 0);

    assert(lumo_shell_touch_audit_point_rect(1280, 800, 7, &rect));
    assert(rect.x + rect.width <= 1280);
    assert(rect.y + rect.height <= 800);
}

static void test_touch_debug_names(void) {
    enum lumo_shell_touch_debug_phase phase = LUMO_SHELL_TOUCH_DEBUG_NONE;
    enum lumo_shell_touch_debug_target target =
        LUMO_SHELL_TOUCH_DEBUG_TARGET_NONE;

    assert(strcmp(lumo_shell_touch_debug_phase_name(
        LUMO_SHELL_TOUCH_DEBUG_DOWN), "down") == 0);
    assert(strcmp(lumo_shell_touch_debug_phase_name(
        LUMO_SHELL_TOUCH_DEBUG_MOTION), "motion") == 0);
    assert(strcmp(lumo_shell_touch_debug_phase_name(
        LUMO_SHELL_TOUCH_DEBUG_UP), "up") == 0);
    assert(strcmp(lumo_shell_touch_debug_phase_name(
        LUMO_SHELL_TOUCH_DEBUG_CANCEL), "cancel") == 0);
    assert(strcmp(lumo_shell_touch_debug_phase_name(
        LUMO_SHELL_TOUCH_DEBUG_NONE), "none") == 0);
    assert(lumo_shell_touch_debug_phase_parse("motion", &phase));
    assert(phase == LUMO_SHELL_TOUCH_DEBUG_MOTION);
    assert(!lumo_shell_touch_debug_phase_parse("drag", &phase));

    assert(strcmp(lumo_shell_touch_debug_target_name(
        LUMO_SHELL_TOUCH_DEBUG_TARGET_HITBOX), "hitbox") == 0);
    assert(strcmp(lumo_shell_touch_debug_target_name(
        LUMO_SHELL_TOUCH_DEBUG_TARGET_SURFACE), "surface") == 0);
    assert(strcmp(lumo_shell_touch_debug_target_name(
        LUMO_SHELL_TOUCH_DEBUG_TARGET_NONE), "none") == 0);
    assert(lumo_shell_touch_debug_target_parse("surface", &target));
    assert(target == LUMO_SHELL_TOUCH_DEBUG_TARGET_SURFACE);
    assert(!lumo_shell_touch_debug_target_parse("edge", &target));
}

static void test_surface_local_coords(void) {
    double local_x = -1.0;
    double local_y = -1.0;

    assert(lumo_shell_surface_local_coords(LUMO_SHELL_MODE_LAUNCHER,
        1280, 800, 1280, 800, 512.0, 240.0, &local_x, &local_y));
    assert(local_x == 512.0);
    assert(local_y == 240.0);

    assert(lumo_shell_surface_local_coords(LUMO_SHELL_MODE_GESTURE,
        1280, 800, 1280, 28, 640.0, 790.0, &local_x, &local_y));
    assert(local_x == 640.0);
    assert(local_y == 18.0);

    assert(!lumo_shell_surface_local_coords(LUMO_SHELL_MODE_GESTURE,
        1280, 800, 1280, 28, 640.0, 700.0, &local_x, &local_y));
}

int main(void) {
    test_mode_names();
    test_target_kind_parse();
    test_touch_debug_names();
    test_osk_key_text();
    test_shell_labels();
    test_layout_counts();
    test_launcher_config();
    test_osk_config();
    test_gesture_config();
    test_bootstrap_config();
    test_invalid_config();
    test_launcher_hitboxes();
    test_osk_hitboxes();
    test_gesture_hitbox();
    test_touch_audit_layout();
    test_surface_local_coords();
    puts("lumo shell tests passed");
    return 0;
}
