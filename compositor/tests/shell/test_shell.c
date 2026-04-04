#include "lumo/shell.h"
#include "lumo/shell_protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool protocol_callback_seen = false;
static struct lumo_shell_protocol_frame protocol_callback_frame = {0};

static void test_shell_protocol_capture(
    const struct lumo_shell_protocol_frame *frame,
    void *user_data
) {
    (void)user_data;
    protocol_callback_seen = true;
    protocol_callback_frame = *frame;
}

static void test_mode_names(void) {
    assert(strcmp(lumo_shell_mode_name(LUMO_SHELL_MODE_LAUNCHER), "launcher") == 0);
    assert(strcmp(lumo_shell_mode_name(LUMO_SHELL_MODE_OSK), "osk") == 0);
    assert(strcmp(lumo_shell_mode_name(LUMO_SHELL_MODE_GESTURE), "gesture") == 0);
    assert(strcmp(lumo_shell_target_kind_name(LUMO_SHELL_TARGET_NONE), "none") == 0);
    assert(strcmp(lumo_shell_target_kind_name(LUMO_SHELL_TARGET_LAUNCHER_TILE),
        "launcher-tile") == 0);
    assert(strcmp(lumo_shell_target_kind_name(LUMO_SHELL_TARGET_LAUNCHER_CLOSE),
        "launcher-close") == 0);
    assert(strcmp(lumo_shell_target_kind_name(LUMO_SHELL_TARGET_OSK_KEY),
        "osk-key") == 0);
    assert(strcmp(lumo_shell_target_kind_name(LUMO_SHELL_TARGET_GESTURE_HANDLE),
        "gesture-handle") == 0);
}

static void test_target_kind_parse(void) {
    enum lumo_shell_target_kind kind = LUMO_SHELL_TARGET_NONE;

    assert(lumo_shell_target_kind_parse("launcher-tile", &kind));
    assert(kind == LUMO_SHELL_TARGET_LAUNCHER_TILE);
    assert(lumo_shell_target_kind_parse("tile", &kind));
    assert(kind == LUMO_SHELL_TARGET_LAUNCHER_TILE);
    assert(lumo_shell_target_kind_parse("launcher_close", &kind));
    assert(kind == LUMO_SHELL_TARGET_LAUNCHER_CLOSE);
    assert(lumo_shell_target_kind_parse("osk_key", &kind));
    assert(kind == LUMO_SHELL_TARGET_OSK_KEY);
    assert(lumo_shell_target_kind_parse("gesture-handle", &kind));
    assert(kind == LUMO_SHELL_TARGET_GESTURE_HANDLE);
    assert(lumo_shell_target_kind_parse("none", &kind));
    assert(kind == LUMO_SHELL_TARGET_NONE);
}

static void test_osk_key_text(void) {
    /* page 0: alpha */
    lumo_shell_osk_set_page(0);
    assert(strcmp(lumo_shell_osk_key_text(0), "q") == 0);
    assert(strcmp(lumo_shell_osk_key_text(10), "a") == 0);
    assert(strcmp(lumo_shell_osk_key_text(19), "\b") == 0);  /* backspace */
    assert(strcmp(lumo_shell_osk_key_text(28), "\x01") == 0); /* page toggle */
    assert(strcmp(lumo_shell_osk_key_text(30), " ") == 0);   /* space */
    assert(strcmp(lumo_shell_osk_key_text(31), "\n") == 0);  /* enter */
    assert(strcmp(lumo_shell_osk_key_text(32), "\x1b") == 0); /* close */
    assert(lumo_shell_osk_key_text(33) == NULL);
    /* page 1: symbols */
    lumo_shell_osk_set_page(1);
    assert(strcmp(lumo_shell_osk_key_text(0), "1") == 0);
    assert(strcmp(lumo_shell_osk_key_text(10), "@") == 0);
    assert(strcmp(lumo_shell_osk_key_text(22), "?") == 0);
    assert(strcmp(lumo_shell_osk_key_text(23), ",") == 0);
    lumo_shell_osk_set_page(0);
}

static void test_shell_labels(void) {
    assert(strcmp(lumo_shell_launcher_tile_label(0), "PHONE") == 0);
    assert(strcmp(lumo_shell_launcher_tile_label(11), "SETTINGS") == 0);
    assert(strcmp(lumo_shell_launcher_tile_label(12), "SYSMON") == 0);
    assert(strcmp(lumo_shell_launcher_tile_label(13), "GITHUB") == 0);
    assert(lumo_shell_launcher_tile_label(14) == NULL);

    lumo_shell_osk_set_page(0);
    assert(strcmp(lumo_shell_osk_key_label(0), "Q") == 0);
    assert(strcmp(lumo_shell_osk_key_label(19), "<-") == 0);
    assert(strcmp(lumo_shell_osk_key_label(28), "123") == 0);
    assert(strcmp(lumo_shell_osk_key_label(30), "SPACE") == 0);
    assert(strcmp(lumo_shell_osk_key_label(31), "ENTER") == 0);
    assert(strcmp(lumo_shell_osk_key_label(32), "v") == 0);
    assert(lumo_shell_osk_key_label(33) == NULL);
    lumo_shell_osk_set_page(1);
    assert(strcmp(lumo_shell_osk_key_label(0), "1") == 0);
    assert(strcmp(lumo_shell_osk_key_label(28), "ABC") == 0);
    lumo_shell_osk_set_page(0);

    assert(strcmp(lumo_shell_touch_audit_point_name(0), "top-left") == 0);
    assert(strcmp(lumo_shell_touch_audit_point_name(7), "bottom-right") == 0);
    assert(lumo_shell_touch_audit_point_name(8) == NULL);
    assert(strcmp(lumo_shell_touch_audit_point_label(1), "TOP") == 0);
    assert(strcmp(lumo_shell_touch_audit_point_label(6), "BOTTOM") == 0);
    assert(lumo_shell_touch_audit_point_label(8) == NULL);
}

static void test_layout_counts(void) {
    assert(lumo_shell_launcher_tile_count() == 16);
    assert(lumo_shell_osk_key_count() == 33);
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
    assert(config.height == 260);
    assert(config.anchor == (LUMO_SHELL_ANCHOR_BOTTOM |
        LUMO_SHELL_ANCHOR_LEFT |
        LUMO_SHELL_ANCHOR_RIGHT));
    assert(config.exclusive_zone == 260);
    assert(config.keyboard_interactive);
    assert(config.background_rgba == 0x002A2A2E);
}

static void test_gesture_config(void) {
    struct lumo_shell_surface_config config = {0};

    assert(lumo_shell_surface_config_for_mode(LUMO_SHELL_MODE_GESTURE,
        1024, 600, &config));
    assert(config.mode == LUMO_SHELL_MODE_GESTURE);
    assert(config.width == 1024);
    assert(config.height == 48);
    assert(config.anchor == (LUMO_SHELL_ANCHOR_BOTTOM |
        LUMO_SHELL_ANCHOR_LEFT |
        LUMO_SHELL_ANCHOR_RIGHT));
    assert(config.exclusive_zone == 48);
    assert(!config.keyboard_interactive);
    assert(config.background_rgba == 0x00000000);
}

static void test_bootstrap_config(void) {
    struct lumo_shell_surface_config config = {0};

    assert(lumo_shell_surface_bootstrap_config(LUMO_SHELL_MODE_LAUNCHER, &config));
    assert(config.width == 0);
    assert(config.height == 0);
    assert(config.anchor == (LUMO_SHELL_ANCHOR_TOP |
        LUMO_SHELL_ANCHOR_BOTTOM |
        LUMO_SHELL_ANCHOR_LEFT |
        LUMO_SHELL_ANCHOR_RIGHT));
    assert(config.exclusive_zone == 0);
    assert(!config.keyboard_interactive);

    assert(lumo_shell_surface_bootstrap_config(LUMO_SHELL_MODE_OSK, &config));
    assert(config.width == 0);
    assert(config.height == 1);
    assert(config.exclusive_zone == 0);
    assert(!config.keyboard_interactive);

    assert(lumo_shell_surface_bootstrap_config(LUMO_SHELL_MODE_GESTURE, &config));
    assert(config.width == 0);
    assert(config.height == 60);
    assert(config.exclusive_zone == 60);
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
    struct lumo_rect close_rect = {0};
    struct lumo_shell_target target = {0};

    assert(lumo_shell_launcher_panel_rect(1024, 600, &panel));
    assert(panel.x > 0);
    assert(panel.y > 0);
    assert(panel.width < 1024);
    assert(panel.height < 600);

    assert(lumo_shell_launcher_close_rect(1024, 600, &close_rect));
    assert(close_rect.x > panel.x);
    assert(close_rect.y >= panel.y);
    assert(close_rect.x + close_rect.width <= panel.x + panel.width);

    assert(lumo_shell_target_for_mode(LUMO_SHELL_MODE_LAUNCHER,
        1024, 600, close_rect.x + close_rect.width / 2.0,
        close_rect.y + close_rect.height / 2.0, &target));
    assert(target.kind == LUMO_SHELL_TARGET_LAUNCHER_CLOSE);
    assert(target.index == 0);

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

    assert(lumo_shell_osk_key_rect(1024, 320, 30, &rect));
    assert(rect.width > 0);
    assert(lumo_shell_target_for_mode(LUMO_SHELL_MODE_OSK,
        1024, 320, rect.x + rect.width / 2.0, rect.y + rect.height / 2.0,
        &target));
    assert(target.kind == LUMO_SHELL_TARGET_OSK_KEY);
    assert(target.index == 30);
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

static void test_quick_settings_button_rects(void) {
    struct lumo_rect reload = {0};
    struct lumo_rect rotate = {0};
    struct lumo_rect screenshot = {0};
    struct lumo_rect settings = {0};
    struct lumo_rect panel = {0};

    assert(lumo_shell_quick_settings_panel_rect(1024, 600, &panel));
    assert(lumo_shell_quick_settings_button_rect(1024, 600, 0, &reload));
    assert(lumo_shell_quick_settings_button_rect(1024, 600, 1, &rotate));
    assert(lumo_shell_quick_settings_button_rect(1024, 600, 2, &screenshot));
    assert(lumo_shell_quick_settings_button_rect(1024, 600, 3, &settings));
    assert(!lumo_shell_quick_settings_button_rect(1024, 600, 4, &settings));

    assert(panel.x == 504);
    assert(panel.y == 52);
    assert(panel.width == 512);
    assert(panel.height == 544);
    assert(reload.width > 0);
    assert(reload.height == 28);
    assert(rotate.width == reload.width);
    assert(rotate.y == reload.y);
    assert(rotate.x > reload.x);
    assert(screenshot.width == reload.width);
    assert(screenshot.x == reload.x);
    assert(screenshot.y > reload.y);
    assert(settings.x > screenshot.x);
    assert(settings.y == screenshot.y);
}

static void test_launcher_search_and_filtered_tiles(void) {
    struct lumo_rect panel = {0};
    struct lumo_rect search = {0};
    struct lumo_rect filtered = {0};
    struct lumo_rect time_panel = {0};
    struct lumo_shell_target target = {0};
    uint32_t tile_index = UINT32_MAX;

    assert(lumo_shell_launcher_panel_rect(1024, 600, &panel));
    assert(lumo_shell_launcher_search_bar_rect(1024, 600, &search));
    assert(search.x == 324);
    assert(search.y == 54);
    assert(search.width == 376);
    assert(search.height == 40);
    assert(lumo_rect_contains(&panel, search.x + search.width / 2.0,
        search.y + search.height / 2.0));

    assert(lumo_shell_launcher_filtered_tile_count(NULL) == 14);
    assert(lumo_shell_launcher_filtered_tile_count("SET") == 1);
    assert(lumo_shell_launcher_filtered_tile_rect(1024, 600, "SET", 0,
        &tile_index, &filtered));
    assert(tile_index == 11);
    assert(filtered.x >= panel.x);
    assert(filtered.x + filtered.width <= panel.x + panel.width);
    assert(filtered.y >= search.y + search.height);
    assert(!lumo_shell_launcher_filtered_tile_rect(1024, 600, "SET", 1,
        &tile_index, &filtered));

    assert(lumo_shell_target_for_mode_with_query(LUMO_SHELL_MODE_LAUNCHER,
        1024, 600, "SET", filtered.x + filtered.width / 2.0,
        filtered.y + filtered.height / 2.0, &target));
    assert(target.kind == LUMO_SHELL_TARGET_LAUNCHER_TILE);
    assert(target.index == 11);

    assert(lumo_shell_time_panel_rect(1024, 600, &time_panel));
    assert(time_panel.x == 256);
    assert(time_panel.y == 52);
    assert(time_panel.width == 512);
    assert(time_panel.height == 220);
}

static void test_tall_launcher_layout(void) {
    struct lumo_rect panel = {0};
    struct lumo_rect first = {0};
    struct lumo_rect last = {0};

    assert(lumo_shell_launcher_panel_rect(800, 1280, &panel));
    assert(lumo_shell_launcher_tile_rect(800, 1280, 0, &first));
    assert(lumo_shell_launcher_tile_rect(800, 1280, 13, &last));
    assert(first.y > panel.y + 80);
    assert(last.y > first.y);
    assert(last.y + last.height > panel.y + panel.height / 2);
    assert(last.y + last.height <= panel.y + panel.height);
}

static void test_transition_durations(void) {
    assert(lumo_shell_transition_duration_ms(LUMO_SHELL_MODE_LAUNCHER, true) == 200);
    assert(lumo_shell_transition_duration_ms(LUMO_SHELL_MODE_LAUNCHER, false) == 150);
    assert(lumo_shell_transition_duration_ms(LUMO_SHELL_MODE_OSK, true) == 300);
    assert(lumo_shell_transition_duration_ms(LUMO_SHELL_MODE_OSK, false) == 200);
    assert(lumo_shell_transition_duration_ms(LUMO_SHELL_MODE_GESTURE, true) == 0);
    assert(lumo_shell_transition_duration_ms(LUMO_SHELL_MODE_STATUS, false) == 0);
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

static void test_protocol_string_values(void) {
    struct lumo_shell_protocol_frame frame = {0};
    struct lumo_shell_protocol_stream stream;
    char buffer[1024] = {0};
    const char *value = NULL;

    assert(lumo_shell_protocol_frame_init(&frame,
        LUMO_SHELL_PROTOCOL_FRAME_EVENT, "state", 0));
    assert(lumo_shell_protocol_frame_add_string(&frame, "weather_condition",
        "Partly cloudy"));
    assert(lumo_shell_protocol_frame_add_string(&frame, "weather_wind",
        "5 mph"));
    assert(lumo_shell_protocol_frame_format(&frame, buffer,
        sizeof(buffer)) > 0);

    protocol_callback_seen = false;
    memset(&protocol_callback_frame, 0, sizeof(protocol_callback_frame));
    lumo_shell_protocol_stream_init(&stream);
    assert(lumo_shell_protocol_stream_feed(&stream, buffer, strlen(buffer),
        test_shell_protocol_capture, NULL));
    assert(protocol_callback_seen);
    assert(strcmp(protocol_callback_frame.name, "state") == 0);
    assert(lumo_shell_protocol_frame_get(&protocol_callback_frame,
        "weather_condition", &value));
    assert(strcmp(value, "Partly cloudy") == 0);
    assert(lumo_shell_protocol_frame_get(&protocol_callback_frame,
        "weather_wind", &value));
    assert(strcmp(value, "5 mph") == 0);
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
    test_protocol_string_values();
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
    test_quick_settings_button_rects();
    test_launcher_search_and_filtered_tiles();
    test_tall_launcher_layout();
    test_transition_durations();
    test_touch_audit_layout();
    test_surface_local_coords();
    puts("lumo shell tests passed");
    return 0;
}
