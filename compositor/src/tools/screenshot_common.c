#include "lumo/screenshot.h"

#include <stdio.h>
#include <string.h>

#include <wayland-client.h>

bool lumo_screenshot_runtime_dir(
    const char *env_runtime_dir,
    uid_t uid,
    char *buffer,
    size_t buffer_size
) {
    int written;

    if (buffer == NULL || buffer_size == 0) {
        return false;
    }

    if (env_runtime_dir != NULL && env_runtime_dir[0] != '\0') {
        written = snprintf(buffer, buffer_size, "%s", env_runtime_dir);
    } else {
        written = snprintf(buffer, buffer_size, "/run/user/%u",
            (unsigned int)uid);
    }

    return written > 0 && (size_t)written < buffer_size;
}

const char *lumo_screenshot_display_name(
    const char *env_display_name,
    const char *override_display_name
) {
    if (override_display_name != NULL && override_display_name[0] != '\0') {
        return override_display_name;
    }
    if (env_display_name != NULL && env_display_name[0] != '\0') {
        return env_display_name;
    }

    return "lumo-shell";
}

uint32_t lumo_screenshot_source_row(
    uint32_t row,
    uint32_t height,
    bool y_invert
) {
    if (!y_invert || row >= height) {
        return row;
    }

    return height - row - 1;
}

bool lumo_screenshot_format_supported(uint32_t format) {
    switch (format) {
    case WL_SHM_FORMAT_XRGB8888:
    case WL_SHM_FORMAT_ARGB8888:
    case WL_SHM_FORMAT_XBGR8888:
    case WL_SHM_FORMAT_ABGR8888:
    case WL_SHM_FORMAT_RGBX8888:
    case WL_SHM_FORMAT_RGBA8888:
    case WL_SHM_FORMAT_BGRX8888:
    case WL_SHM_FORMAT_BGRA8888:
        return true;
    default:
        return false;
    }
}

void lumo_screenshot_convert_shm_row(
    uint8_t *dst,
    size_t dst_size,
    const uint32_t *src,
    uint32_t width,
    uint32_t format
) {
    if (dst == NULL || src == NULL || dst_size < (size_t)width * 3u) {
        return;
    }

    for (uint32_t i = 0; i < width; i++) {
        uint32_t pixel = src[i];
        uint8_t red = 0;
        uint8_t green = 0;
        uint8_t blue = 0;

        switch (format) {
        case WL_SHM_FORMAT_XRGB8888:
        case WL_SHM_FORMAT_ARGB8888:
            red = (uint8_t)((pixel >> 16) & 0xFFu);
            green = (uint8_t)((pixel >> 8) & 0xFFu);
            blue = (uint8_t)(pixel & 0xFFu);
            break;
        case WL_SHM_FORMAT_XBGR8888:
        case WL_SHM_FORMAT_ABGR8888:
            red = (uint8_t)(pixel & 0xFFu);
            green = (uint8_t)((pixel >> 8) & 0xFFu);
            blue = (uint8_t)((pixel >> 16) & 0xFFu);
            break;
        case WL_SHM_FORMAT_RGBX8888:
        case WL_SHM_FORMAT_RGBA8888:
            red = (uint8_t)((pixel >> 24) & 0xFFu);
            green = (uint8_t)((pixel >> 16) & 0xFFu);
            blue = (uint8_t)((pixel >> 8) & 0xFFu);
            break;
        case WL_SHM_FORMAT_BGRX8888:
        case WL_SHM_FORMAT_BGRA8888:
            red = (uint8_t)((pixel >> 8) & 0xFFu);
            green = (uint8_t)((pixel >> 16) & 0xFFu);
            blue = (uint8_t)((pixel >> 24) & 0xFFu);
            break;
        default:
            return;
        }

        dst[i * 3u + 0] = red;
        dst[i * 3u + 1] = green;
        dst[i * 3u + 2] = blue;
    }
}
