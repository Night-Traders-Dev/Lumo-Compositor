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
        lumo_app_render((enum lumo_app_id)i, inactive, width, height, false);
        lumo_app_render((enum lumo_app_id)i, active, width, height, true);
        assert(lumo_test_any_nonzero(inactive, pixel_count));
        assert(lumo_test_any_nonzero(active, pixel_count));
        assert(memcmp(inactive, active, pixel_count * sizeof(*inactive)) != 0);
    }

    free(active);
    free(inactive);
}

int main(void) {
    test_app_catalog();
    test_app_launcher_mapping();
    test_app_close_rect();
    test_app_render();
    return 0;
}
