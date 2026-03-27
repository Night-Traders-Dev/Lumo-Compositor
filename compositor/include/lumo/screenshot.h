#ifndef LUMO_SCREENSHOT_H
#define LUMO_SCREENSHOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

bool lumo_screenshot_runtime_dir(
    const char *env_runtime_dir,
    uid_t uid,
    char *buffer,
    size_t buffer_size
);
const char *lumo_screenshot_display_name(
    const char *env_display_name,
    const char *override_display_name
);
uint32_t lumo_screenshot_source_row(
    uint32_t row,
    uint32_t height,
    bool y_invert
);
bool lumo_screenshot_format_supported(uint32_t format);
void lumo_screenshot_convert_shm_row(
    uint8_t *dst,
    size_t dst_size,
    const uint32_t *src,
    uint32_t width,
    uint32_t format
);

#endif
