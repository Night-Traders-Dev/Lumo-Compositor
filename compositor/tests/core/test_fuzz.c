/* Fuzz and stress tests for Lumo compositor.
 * Tests protocol parsing with malformed input, buffer boundary conditions,
 * touch coordinate transforms with degenerate values, and layout calculations
 * with extreme dimensions. */

#include "lumo/compositor.h"
#include "lumo/shell.h"
#include "lumo/shell_protocol.h"
#include "lumo/app.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- Protocol parser fuzz ---- */

static int fuzz_frame_count;
static void fuzz_frame_cb(const struct lumo_shell_protocol_frame *frame,
    void *user_data)
{
    (void)user_data;
    if (frame != NULL) fuzz_frame_count++;
}

static void test_protocol_empty_input(void) {
    struct lumo_shell_protocol_stream stream = {0};

    lumo_shell_protocol_stream_init(&stream);
    fuzz_frame_count = 0;
    /* feed empty string */
    assert(lumo_shell_protocol_stream_feed(&stream, "", 0,
        fuzz_frame_cb, NULL));
    assert(fuzz_frame_count == 0);
}

static void test_protocol_garbage_input(void) {
    struct lumo_shell_protocol_stream stream = {0};

    lumo_shell_protocol_stream_init(&stream);
    fuzz_frame_count = 0;

    /* feed random garbage bytes */
    unsigned char garbage[512];
    srand(42);
    for (size_t i = 0; i < sizeof(garbage); i++) {
        garbage[i] = (unsigned char)(rand() % 256);
    }
    /* should not crash */
    lumo_shell_protocol_stream_feed(&stream, (const char *)garbage,
        sizeof(garbage), fuzz_frame_cb, NULL);
    /* no valid frames from garbage */
    assert(fuzz_frame_count == 0);
}

static void test_protocol_oversized_line(void) {
    struct lumo_shell_protocol_stream stream = {0};

    lumo_shell_protocol_stream_init(&stream);
    fuzz_frame_count = 0;

    /* feed a line that exceeds buffer capacity */
    char huge[2048];
    memset(huge, 'A', sizeof(huge) - 1);
    huge[sizeof(huge) - 1] = '\n';
    lumo_shell_protocol_stream_feed(&stream, huge, sizeof(huge),
        fuzz_frame_cb, NULL);
    /* should reset, not crash */
    assert(fuzz_frame_count == 0);
}

static void test_protocol_many_fields(void) {
    struct lumo_shell_protocol_frame frame = {0};
    char buf[4096];

    assert(lumo_shell_protocol_frame_init(&frame,
        LUMO_SHELL_PROTOCOL_FRAME_EVENT, "stress", 1));

    /* add fields up to the max */
    for (uint32_t i = 0; i < LUMO_SHELL_PROTOCOL_MAX_FIELDS; i++) {
        char key[16];
        snprintf(key, sizeof(key), "f%u", i);
        bool ok = lumo_shell_protocol_frame_add_u32(&frame, key, i);
        if (!ok) break; /* expected to fail at capacity */
    }

    /* one more should fail */
    assert(!lumo_shell_protocol_frame_add_u32(&frame, "overflow", 999));

    /* format should still work */
    size_t len = lumo_shell_protocol_frame_format(&frame, buf, sizeof(buf));
    assert(len > 0);
}

static void test_protocol_roundtrip_stress(void) {
    /* build a frame, format it, parse it back, verify fields */
    struct lumo_shell_protocol_frame frame = {0};
    struct lumo_shell_protocol_stream stream = {0};
    char buf[2048];

    assert(lumo_shell_protocol_frame_init(&frame,
        LUMO_SHELL_PROTOCOL_FRAME_EVENT, "test_rt", 7));
    assert(lumo_shell_protocol_frame_add_string(&frame, "key1", "hello_world"));
    assert(lumo_shell_protocol_frame_add_u32(&frame, "num", 42));
    assert(lumo_shell_protocol_frame_add_bool(&frame, "flag", true));
    assert(lumo_shell_protocol_frame_add_double(&frame, "pi", 3.14));

    size_t len = lumo_shell_protocol_frame_format(&frame, buf, sizeof(buf));
    assert(len > 0);

    fuzz_frame_count = 0;
    lumo_shell_protocol_stream_init(&stream);
    lumo_shell_protocol_stream_feed(&stream, buf, len, fuzz_frame_cb, NULL);
    assert(fuzz_frame_count == 1);
}

/* ---- Touch coordinate fuzz ---- */

static void test_transform_degenerate_box(void) {
    struct wlr_box zero_box = {0, 0, 0, 0};
    struct wlr_box negative_box = {0, 0, -1, -1};
    double lx = 999.0, ly = 999.0;

    /* zero-size box should fail gracefully */
    assert(!lumo_transform_layout_coords_in_box(&zero_box,
        WL_OUTPUT_TRANSFORM_NORMAL, 50.0, 50.0, &lx, &ly));

    /* negative-size box */
    assert(!lumo_transform_layout_coords_in_box(&negative_box,
        WL_OUTPUT_TRANSFORM_NORMAL, 50.0, 50.0, &lx, &ly));

    /* NULL box */
    assert(!lumo_transform_layout_coords_in_box(NULL,
        WL_OUTPUT_TRANSFORM_NORMAL, 50.0, 50.0, &lx, &ly));

    /* NULL output pointers */
    struct wlr_box good_box = {0, 0, 100, 200};
    assert(!lumo_transform_layout_coords_in_box(&good_box,
        WL_OUTPUT_TRANSFORM_NORMAL, 50.0, 50.0, NULL, NULL));
}

static void test_transform_all_rotations(void) {
    struct wlr_box box = {0, 0, 1280, 800};
    double lx, ly;

    /* center point should always map to center region */
    int transforms[] = {
        WL_OUTPUT_TRANSFORM_NORMAL,
        WL_OUTPUT_TRANSFORM_90,
        WL_OUTPUT_TRANSFORM_180,
        WL_OUTPUT_TRANSFORM_270,
    };

    for (int i = 0; i < 4; i++) {
        assert(lumo_transform_layout_coords_in_box(&box,
            transforms[i], 640.0, 400.0, &lx, &ly));
        /* result should be within output bounds */
        assert(lx >= 0.0 && lx <= box.x + box.width);
        assert(ly >= 0.0 && ly <= box.y + box.height);
    }
}

/* ---- OSK layout stress ---- */

static void test_osk_layout_extreme_sizes(void) {
    struct lumo_rect rect = {0};

    /* very small display - should not crash, should return false */
    assert(!lumo_shell_osk_key_rect(10, 10, 0, &rect));
    assert(!lumo_shell_osk_key_rect(0, 0, 0, &rect));
    assert(!lumo_shell_osk_key_rect(1, 1, 0, &rect));
    assert(!lumo_shell_osk_key_rect(50, 50, 0, &rect));

    /* very large display */
    assert(lumo_shell_osk_key_rect(4096, 2048, 0, &rect));
    assert(rect.width > 0);
    assert(rect.height > 0);

    /* all keys should have valid rects at normal size */
    size_t count = lumo_shell_osk_key_count();
    for (uint32_t i = 0; i < (uint32_t)count; i++) {
        bool ok = lumo_shell_osk_key_rect(1280, 800, i, &rect);
        assert(ok);
        assert(rect.x >= 0);
        assert(rect.y >= 0);
        assert(rect.width > 0);
        assert(rect.height > 0);
    }

    /* out-of-range key index */
    assert(!lumo_shell_osk_key_rect(1280, 800, 999, &rect));

    /* NULL rect */
    assert(!lumo_shell_osk_key_rect(1280, 800, 0, NULL));
}

/* ---- Launcher layout stress ---- */

static void test_launcher_layout_extreme_sizes(void) {
    struct lumo_rect rect = {0};

    /* very small display — GNOME grid may produce valid tiny rects */
    (void)lumo_shell_launcher_tile_rect(10, 10, 0, &rect);
    assert(!lumo_shell_launcher_tile_rect(0, 0, 0, &rect));

    /* normal display - all 12 tiles */
    for (uint32_t i = 0; i < 12; i++) {
        assert(lumo_shell_launcher_tile_rect(1280, 800, i, &rect));
        assert(rect.width > 0);
        assert(rect.height > 0);
        assert(rect.x >= 0);
        assert(rect.y >= 0);
    }

    /* out-of-range tile */
    assert(!lumo_shell_launcher_tile_rect(1280, 800, 99, &rect));
}

/* ---- App render buffer safety ---- */

static void test_app_render_zero_size(void) {
    struct lumo_app_render_context ctx = {
        .app_id = LUMO_APP_CLOCK,
    };
    uint32_t pixel = 0;

    /* 1x1 should not crash */
    lumo_app_render(&ctx, &pixel, 1, 1);

    /* NULL ctx should not crash */
    lumo_app_render(NULL, &pixel, 100, 100);
}

static void test_app_render_all_apps(void) {
    /* render each app at a reasonable size and verify non-zero output */
    uint32_t width = 320, height = 240;
    size_t pixel_count = (size_t)width * height;
    uint32_t *pixels = calloc(pixel_count, sizeof(uint32_t));
    assert(pixels != NULL);

    for (int app = 0; app < (int)lumo_app_count(); app++) {
        struct lumo_app_render_context ctx = {
            .app_id = (enum lumo_app_id)app,
            .browse_path = "/tmp",
            .selected_row = -1,
            .note_editing = -1,
        };
        memset(pixels, 0, pixel_count * sizeof(uint32_t));
        lumo_app_render(&ctx, pixels, width, height);

        /* should have drawn something */
        bool any_nonzero = false;
        for (size_t i = 0; i < pixel_count; i++) {
            if (pixels[i] != 0) { any_nonzero = true; break; }
        }
        assert(any_nonzero);
    }
    free(pixels);
}

/* ---- Shell surface config edge cases ---- */

static void test_surface_config_extreme(void) {
    struct lumo_shell_surface_config config = {0};

    /* zero dimensions */
    assert(!lumo_shell_surface_config_for_mode(LUMO_SHELL_MODE_OSK, 0, 0,
        &config));

    /* very small — config may still succeed with clamped minimums */
    (void)lumo_shell_surface_config_for_mode(LUMO_SHELL_MODE_OSK, 1, 1,
        &config);

    /* huge */
    assert(lumo_shell_surface_config_for_mode(LUMO_SHELL_MODE_OSK, 8192, 4096,
        &config));
    assert(config.height > 0);

    /* NULL config */
    assert(!lumo_shell_surface_config_for_mode(LUMO_SHELL_MODE_OSK, 1280, 800,
        NULL));
}

/* ---- Close rect boundary ---- */

static void test_close_rect_boundaries(void) {
    struct lumo_rect rect = {0};

    assert(lumo_app_close_rect(1280, 800, &rect));
    assert(rect.x + rect.width <= 1280);
    assert(rect.y + rect.height <= 800);
    assert(rect.width >= 44);

    /* zero size */
    assert(!lumo_app_close_rect(0, 0, &rect));

    /* tiny size */
    assert(lumo_app_close_rect(50, 50, &rect));
    assert(rect.width >= 44);

    /* NULL */
    assert(!lumo_app_close_rect(1280, 800, NULL));
}

/* ---- Protocol field type safety ---- */

static void test_protocol_type_coercion(void) {
    struct lumo_shell_protocol_frame frame = {0};
    const char *str_val = NULL;
    bool bool_val = false;
    uint32_t u32_val = 0;
    double dbl_val = 0.0;

    assert(lumo_shell_protocol_frame_init(&frame,
        LUMO_SHELL_PROTOCOL_FRAME_EVENT, "types", 0));
    assert(lumo_shell_protocol_frame_add_string(&frame, "s", "test_val"));
    assert(lumo_shell_protocol_frame_add_bool(&frame, "b", true));
    assert(lumo_shell_protocol_frame_add_u32(&frame, "u", 12345));
    assert(lumo_shell_protocol_frame_add_double(&frame, "d", 1.5));

    /* correct type retrieval */
    assert(lumo_shell_protocol_frame_get(&frame, "s", &str_val));
    assert(strcmp(str_val, "test_val") == 0);
    assert(lumo_shell_protocol_frame_get_bool(&frame, "b", &bool_val));
    assert(bool_val == true);
    assert(lumo_shell_protocol_frame_get_u32(&frame, "u", &u32_val));
    assert(u32_val == 12345);
    assert(lumo_shell_protocol_frame_get_double(&frame, "d", &dbl_val));
    assert(dbl_val > 1.4 && dbl_val < 1.6);

    /* missing field */
    assert(!lumo_shell_protocol_frame_get(&frame, "missing", &str_val));
    assert(!lumo_shell_protocol_frame_get_u32(&frame, "missing", &u32_val));
}

/* ---- Hitbox name helpers ---- */

static void test_hitbox_helpers(void) {
    assert(strcmp(lumo_hitbox_kind_name(LUMO_HITBOX_EDGE_GESTURE),
        "edge-gesture") == 0);
    assert(strcmp(lumo_hitbox_kind_name(LUMO_HITBOX_SCRIM), "scrim") == 0);
    assert(strcmp(lumo_hitbox_kind_name(LUMO_HITBOX_OSK_KEY),
        "osk-key") == 0);
    assert(strcmp(lumo_hitbox_shell_namespace(
        &(struct lumo_hitbox){.kind = LUMO_HITBOX_OSK_KEY}), "osk") == 0);
    assert(strcmp(lumo_hitbox_shell_namespace(
        &(struct lumo_hitbox){.kind = LUMO_HITBOX_SCRIM}), "launcher") == 0);
    assert(lumo_hitbox_shell_namespace(
        &(struct lumo_hitbox){.kind = LUMO_HITBOX_EDGE_GESTURE}) == NULL);

    struct lumo_hitbox hb = {.name = "shell-edge-top",
        .kind = LUMO_HITBOX_EDGE_GESTURE};
    assert(lumo_hitbox_edge_zone(&hb) == LUMO_EDGE_TOP);

    hb.name = "shell-gesture";
    assert(lumo_hitbox_edge_zone(&hb) == LUMO_EDGE_BOTTOM);
    assert(lumo_hitbox_is_shell_gesture(&hb));
}

/* ---- main ---- */

int main(void) {
    test_protocol_empty_input();
    test_protocol_garbage_input();
    test_protocol_oversized_line();
    test_protocol_many_fields();
    test_protocol_roundtrip_stress();
    test_protocol_type_coercion();
    test_transform_degenerate_box();
    test_transform_all_rotations();
    test_osk_layout_extreme_sizes();
    test_launcher_layout_extreme_sizes();
    test_app_render_zero_size();
    test_app_render_all_apps();
    test_surface_config_extreme();
    test_close_rect_boundaries();
    test_hitbox_helpers();
    puts("lumo fuzz/stress tests passed");
    return 0;
}
