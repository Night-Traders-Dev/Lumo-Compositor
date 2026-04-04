/*
 * hal.h — Hardware Abstraction Layer for Lumo compositor.
 *
 * Provides a configurable interface for platform-specific operations:
 * backlight control, audio volume, weather/location data, and device
 * quirks.  The default implementation targets OrangePi RV2 (riscv64)
 * but the function pointer interface allows porting to other platforms
 * by providing alternative implementations.
 */
#ifndef LUMO_HAL_H
#define LUMO_HAL_H

#include <stdbool.h>
#include <stdint.h>

struct lumo_compositor;

struct lumo_hal {
    /* display backlight (0-100%) */
    uint32_t (*read_brightness)(void);
    void (*write_brightness)(uint32_t pct);

    /* audio volume (0-100%) */
    uint32_t (*read_volume)(void);
    void (*write_volume)(uint32_t pct);

    /* sound playback */
    void (*play_sound)(const char *path);

    /* weather data fetch (async, updates compositor fields) */
    void (*fetch_weather)(struct lumo_compositor *compositor);

    /* platform-specific configuration */
    const char *backlight_sysfs_path;
    const char *audio_mixer_control;
    const char *weather_location;
    const char *weather_api_format;

    /* device quirks */
    bool has_gpu;
    bool has_nvme_cache;
    bool touch_needs_calibration;
    const char *touch_device_name;
};

/* default HAL for OrangePi RV2 (SpacemiT K1 / riscv64) */
void lumo_hal_init_default(struct lumo_hal *hal);

/* get the active HAL instance */
const struct lumo_hal *lumo_hal_get(void);

#endif /* LUMO_HAL_H */
