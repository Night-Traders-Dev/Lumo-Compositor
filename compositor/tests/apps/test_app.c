#include "lumo/app.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool lumo_test_any_nonzero(
    const uint32_t *pixels,
    size_t count
) {
    for (size_t i = 0; i < count; i++) {
        if (pixels[i] != 0) {
            return true;
        }
    }

    return false;
}

static void test_app_catalog(void) {
    enum lumo_app_id app_id = LUMO_APP_PHONE;

    assert(lumo_app_count() == 12);
    assert(strcmp(lumo_app_id_name(LUMO_APP_PHONE), "phone") == 0);
    assert(strcmp(lumo_app_title(LUMO_APP_MESSAGES), "Messages") == 0);
    assert(strcmp(lumo_app_subtitle(LUMO_APP_BROWSER),
        "Fast tabs and saved starts") == 0);
    assert(lumo_app_accent_argb(LUMO_APP_CAMERA) != 0);
    assert(lumo_app_id_parse("settings", &app_id));
    assert(app_id == LUMO_APP_SETTINGS);
    assert(!lumo_app_id_parse("unknown", &app_id));
}

static void test_app_launcher_mapping(void) {
    enum lumo_app_id app_id = LUMO_APP_PHONE;

    for (uint32_t i = 0; i < lumo_app_count(); i++) {
        assert(lumo_app_id_for_launcher_tile(i, &app_id));
        assert((uint32_t)app_id == i);
    }

    assert(!lumo_app_id_for_launcher_tile((uint32_t)lumo_app_count(),
        &app_id));
}

static void test_app_close_rect(void) {
    struct lumo_rect rect = {0};

    assert(lumo_app_close_rect(1280, 800, &rect));
    assert(rect.width >= 44);
    assert(rect.height == rect.width);
    assert(rect.x >= 0);
    assert(rect.y >= 0);
    assert(rect.x + rect.width <= 1280);
    assert(rect.y + rect.height <= 800);
}

static void test_app_osk_policy(void) {
    assert(lumo_app_wants_osk(LUMO_APP_MESSAGES, -1));
    assert(!lumo_app_wants_osk(LUMO_APP_NOTES, -1));
    assert(lumo_app_wants_osk(LUMO_APP_NOTES, 0));
    assert(!lumo_app_wants_osk(LUMO_APP_SETTINGS, -1));
}

static void test_photos_render_thumbnail(void) {
    const uint32_t width = 480;
    const uint32_t height = 320;
    uint32_t *pixels = calloc((size_t)width * (size_t)height, sizeof(*pixels));
    uint32_t thumbnail[4] = {
        0xFF102030u, 0xFF405060u,
        0xFF708090u, 0xFFA0B0C0u,
    };
    struct lumo_app_render_context ctx = {
        .app_id = LUMO_APP_PHOTOS,
        .media_file_count = 1,
        .media_selected = 0,
    };

    assert(pixels != NULL);
    strncpy(ctx.media_files[0], "sample.webp", sizeof(ctx.media_files[0]) - 1);
    ctx.media_files[0][sizeof(ctx.media_files[0]) - 1] = '\0';
    ctx.photo_thumbnails[0] = thumbnail;
    ctx.photo_thumbnail_widths[0] = 2;
    ctx.photo_thumbnail_heights[0] = 2;

    lumo_app_render(&ctx, pixels, width, height);
    assert(pixels[68 * width + 50] == 0xFF102030u);
    assert(pixels[68 * width + 110] == 0xFF405060u);

    free(pixels);
}

static void test_app_render(void) {
    const uint32_t width = 480;
    const uint32_t height = 320;
    size_t pixel_count = (size_t)width * (size_t)height;
    uint32_t *inactive = calloc(pixel_count, sizeof(*inactive));
    uint32_t *active = calloc(pixel_count, sizeof(*active));

    assert(inactive != NULL);
    assert(active != NULL);

    for (uint32_t i = 0; i < lumo_app_count(); i++) {
        memset(inactive, 0, pixel_count * sizeof(*inactive));
        memset(active, 0, pixel_count * sizeof(*active));
        {
            struct lumo_app_render_context ctx_inactive = {
                .app_id = (enum lumo_app_id)i,
                .close_active = false,
                .browse_path = "/tmp",
            };
            struct lumo_app_render_context ctx_active = {
                .app_id = (enum lumo_app_id)i,
                .close_active = true,
                .browse_path = "/tmp",
            };
            lumo_app_render(&ctx_inactive, inactive, width, height);
            lumo_app_render(&ctx_active, active, width, height);
        }
        assert(lumo_test_any_nonzero(inactive, pixel_count));
        assert(lumo_test_any_nonzero(active, pixel_count));
        /* close button removed — active/inactive may now be identical */
        (void)active;
    }

    free(active);
    free(inactive);
}

int main(void) {
    test_app_catalog();
    test_app_launcher_mapping();
    test_app_close_rect();
    test_app_osk_policy();
    test_photos_render_thumbnail();
    test_app_render();
    return 0;
}
