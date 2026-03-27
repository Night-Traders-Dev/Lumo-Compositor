#include "lumo/shell.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_mode_names(void) {
    assert(strcmp(lumo_shell_mode_name(LUMO_SHELL_MODE_LAUNCHER), "launcher") == 0);
    assert(strcmp(lumo_shell_mode_name(LUMO_SHELL_MODE_OSK), "osk") == 0);
    assert(strcmp(lumo_shell_mode_name(LUMO_SHELL_MODE_GESTURE), "gesture") == 0);
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

int main(void) {
    test_mode_names();
    test_launcher_config();
    test_osk_config();
    test_gesture_config();
    test_invalid_config();
    puts("lumo shell tests passed");
    return 0;
}
