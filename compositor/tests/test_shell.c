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

static void test_layout_counts(void) {
    assert(lumo_shell_launcher_tile_count() == 12);
    assert(lumo_shell_osk_key_count() == 26);
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
    assert(config.background_rgba == 0xFF172033);
}

static void test_osk_config(void) {
    struct lumo_shell_surface_config config = {0};

    assert(lumo_shell_surface_config_for_mode(LUMO_SHELL_MODE_OSK,
        1024, 600, &config));
    assert(config.mode == LUMO_SHELL_MODE_OSK);
    assert(config.width == 1024);
    assert(config.height == 240);
    assert(config.anchor == (LUMO_SHELL_ANCHOR_BOTTOM |
        LUMO_SHELL_ANCHOR_LEFT |
        LUMO_SHELL_ANCHOR_RIGHT));
    assert(config.exclusive_zone == 240);
    assert(config.keyboard_interactive);
    assert(config.background_rgba == 0xFF222222);
}

static void test_gesture_config(void) {
    struct lumo_shell_surface_config config = {0};

    assert(lumo_shell_surface_config_for_mode(LUMO_SHELL_MODE_GESTURE,
        1024, 600, &config));
    assert(config.mode == LUMO_SHELL_MODE_GESTURE);
    assert(config.width == 1024);
    assert(config.height == 25);
    assert(config.anchor == (LUMO_SHELL_ANCHOR_BOTTOM |
        LUMO_SHELL_ANCHOR_LEFT |
        LUMO_SHELL_ANCHOR_RIGHT));
    assert(config.exclusive_zone == 25);
    assert(!config.keyboard_interactive);
    assert(config.background_rgba == 0xCC0F1115);
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
    struct lumo_shell_target target = {0};

    assert(lumo_shell_launcher_tile_rect(1024, 600, 0, &rect));
    assert(rect.x >= 0);
    assert(rect.y >= 0);
    assert(rect.width > 0);
    assert(rect.height > 0);

    assert(lumo_shell_target_for_mode(LUMO_SHELL_MODE_LAUNCHER,
        1024, 600, 40, 160, &target));
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

    assert(lumo_shell_osk_key_rect(1024, 600, 0, &rect));
    assert(rect.width > 0);
    assert(rect.height > 0);

    assert(lumo_shell_target_for_mode(LUMO_SHELL_MODE_OSK,
        1024, 600, 20, 60, &target));
    assert(target.kind == LUMO_SHELL_TARGET_OSK_KEY);
    assert(target.index == 0);

    assert(lumo_shell_osk_key_rect(1024, 600, 22, &rect));
    assert(rect.width > 0);
    assert(lumo_shell_target_for_mode(LUMO_SHELL_MODE_OSK,
        1024, 600, rect.x + rect.width / 2.0, rect.y + rect.height / 2.0,
        &target));
    assert(target.kind == LUMO_SHELL_TARGET_OSK_KEY);
    assert(target.index == 22);
}

static void test_gesture_hitbox(void) {
    struct lumo_rect rect = {0};
    struct lumo_shell_target target = {0};

    assert(lumo_shell_gesture_handle_rect(1024, 80, &rect));
    assert(rect.x == 0);
    assert(rect.y == 0);
    assert(rect.width == 1024);
    assert(rect.height == 80);

    assert(lumo_shell_target_for_mode(LUMO_SHELL_MODE_GESTURE,
        1024, 80, 100, 40, &target));
    assert(target.kind == LUMO_SHELL_TARGET_GESTURE_HANDLE);
    assert(target.index == 0);
}

int main(void) {
    test_mode_names();
    test_layout_counts();
    test_launcher_config();
    test_osk_config();
    test_gesture_config();
    test_invalid_config();
    test_launcher_hitboxes();
    test_osk_hitboxes();
    test_gesture_hitbox();
    puts("lumo shell tests passed");
    return 0;
}
