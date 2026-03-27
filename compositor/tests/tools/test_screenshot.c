#include "lumo/screenshot.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <wayland-client.h>

static void test_runtime_dir_resolution(void) {
    char buffer[128];

    assert(lumo_screenshot_runtime_dir("/tmp/runtime-test", 1001, buffer,
        sizeof(buffer)));
    assert(strcmp(buffer, "/tmp/runtime-test") == 0);

    assert(lumo_screenshot_runtime_dir(NULL, 1001, buffer, sizeof(buffer)));
    assert(strcmp(buffer, "/run/user/1001") == 0);
}

static void test_display_name_resolution(void) {
    assert(strcmp(lumo_screenshot_display_name("wayland-1", NULL),
        "wayland-1") == 0);
    assert(strcmp(lumo_screenshot_display_name("wayland-1", "lumo-dev"),
        "lumo-dev") == 0);
    assert(strcmp(lumo_screenshot_display_name(NULL, NULL), "lumo-shell") == 0);
}

static void test_row_mapping(void) {
    assert(lumo_screenshot_source_row(0, 4, false) == 0);
    assert(lumo_screenshot_source_row(3, 4, false) == 3);
    assert(lumo_screenshot_source_row(0, 4, true) == 3);
    assert(lumo_screenshot_source_row(2, 4, true) == 1);
}

static void test_format_support(void) {
    assert(lumo_screenshot_format_supported(WL_SHM_FORMAT_XRGB8888));
    assert(lumo_screenshot_format_supported(WL_SHM_FORMAT_XBGR8888));
    assert(lumo_screenshot_format_supported(WL_SHM_FORMAT_BGR888));
    assert(lumo_screenshot_format_supported(WL_SHM_FORMAT_RGBA8888));
    assert(!lumo_screenshot_format_supported(0xDEADBEEFu));
}

static void test_xrgb_conversion(void) {
    const uint8_t src[8] = {
        0x33, 0x22, 0x11, 0xFF,
        0x66, 0x55, 0x44, 0xFF,
    };
    uint8_t dst[6] = {0};

    lumo_screenshot_convert_shm_row(dst, sizeof(dst), src, 2,
        WL_SHM_FORMAT_XRGB8888);
    assert(dst[0] == 0x11);
    assert(dst[1] == 0x22);
    assert(dst[2] == 0x33);
    assert(dst[3] == 0x44);
    assert(dst[4] == 0x55);
    assert(dst[5] == 0x66);
}

static void test_xbgr_conversion(void) {
    const uint8_t src[8] = {
        0x11, 0x22, 0x33, 0xFF,
        0x44, 0x55, 0x66, 0xFF,
    };
    uint8_t dst[6] = {0};

    lumo_screenshot_convert_shm_row(dst, sizeof(dst), src, 2,
        WL_SHM_FORMAT_XBGR8888);
    assert(dst[0] == 0x11);
    assert(dst[1] == 0x22);
    assert(dst[2] == 0x33);
    assert(dst[3] == 0x44);
    assert(dst[4] == 0x55);
    assert(dst[5] == 0x66);
}

static void test_bgr888_conversion(void) {
    const uint8_t src[6] = {
        0x33, 0x22, 0x11,
        0x66, 0x55, 0x44,
    };
    uint8_t dst[6] = {0};

    lumo_screenshot_convert_shm_row(dst, sizeof(dst), src, 2,
        WL_SHM_FORMAT_BGR888);
    assert(dst[0] == 0x11);
    assert(dst[1] == 0x22);
    assert(dst[2] == 0x33);
    assert(dst[3] == 0x44);
    assert(dst[4] == 0x55);
    assert(dst[5] == 0x66);
}

int main(void) {
    test_runtime_dir_resolution();
    test_display_name_resolution();
    test_row_mapping();
    test_format_support();
    test_xrgb_conversion();
    test_xbgr_conversion();
    test_bgr888_conversion();
    puts("lumo screenshot tests passed");
    return 0;
}
