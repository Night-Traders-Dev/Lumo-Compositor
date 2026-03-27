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
    assert(lumo_screenshot_format_supported(WL_SHM_FORMAT_RGBA8888));
    assert(!lumo_screenshot_format_supported(0xDEADBEEFu));
}

static void test_xrgb_conversion(void) {
    const uint32_t src[2] = {
        0xFF112233u,
        0xFF445566u,
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
    const uint32_t src[2] = {
        0xFF332211u,
        0xFF665544u,
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

int main(void) {
    test_runtime_dir_resolution();
    test_display_name_resolution();
    test_row_mapping();
    test_format_support();
    test_xrgb_conversion();
    test_xbgr_conversion();
    puts("lumo screenshot tests passed");
    return 0;
}
