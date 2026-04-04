/*
 * shell_theme.c — continuous theme engine for the Lumo shell client.
 * Split from shell_render.c for maintainability.
 *
 * Contains the global theme definition, time-of-day color interpolation,
 * weather tinting, and UI color derivation.
 */

#include "shell_client_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/* ── global theme definition ──────────────────────────────────────── */

struct lumo_shell_theme lumo_theme;

/* ── continuous theme engine ──────────────────────────────────────── */

/* key-color stops at specific hours — the engine interpolates smoothly
 * between adjacent stops using the fractional time of day.  This gives
 * a gradual sunrise/sunset/night shift rather than an abrupt change. */
struct lumo_color_stop { float hour; float r, g, b; };
static const struct lumo_color_stop lumo_day_stops[] = {
    {  0.0f, 0x12, 0x08, 0x1A }, /* midnight — deep aubergine         */
    {  4.0f, 0x10, 0x0C, 0x20 }, /* pre-dawn — slight blue lift       */
    {  5.5f, 0x14, 0x28, 0x38 }, /* dawn — cool teal                  */
    {  7.0f, 0x30, 0x10, 0x28 }, /* early morning — warm rose          */
    { 10.0f, 0x2C, 0x00, 0x1E }, /* mid-morning — pure aubergine       */
    { 13.0f, 0x2C, 0x00, 0x1E }, /* midday — hold aubergine            */
    { 15.0f, 0x28, 0x14, 0x18 }, /* afternoon — dusty warm             */
    { 17.0f, 0x42, 0x0C, 0x16 }, /* late afternoon — sunset orange     */
    { 19.0f, 0x30, 0x0A, 0x22 }, /* dusk — purple                      */
    { 20.5f, 0x10, 0x18, 0x30 }, /* twilight — deep blue               */
    { 22.0f, 0x12, 0x08, 0x1A }, /* night — back to aubergine          */
    { 24.0f, 0x12, 0x08, 0x1A }, /* wrap to midnight                   */
};
#define LUMO_DAY_STOP_COUNT \
    (sizeof(lumo_day_stops) / sizeof(lumo_day_stops[0]))

/* smooth interpolation state — current color approaches target */
static float lumo_smooth_r, lumo_smooth_g, lumo_smooth_b;
static bool lumo_smooth_initialized;

static void lumo_theme_interpolate_time(float fractional_hour,
    float *out_r, float *out_g, float *out_b)
{
    /* find the two stops surrounding the current time */
    size_t i;
    for (i = 1; i < LUMO_DAY_STOP_COUNT; i++) {
        if (fractional_hour < lumo_day_stops[i].hour)
            break;
    }
    if (i >= LUMO_DAY_STOP_COUNT) i = LUMO_DAY_STOP_COUNT - 1;

    const struct lumo_color_stop *a = &lumo_day_stops[i - 1];
    const struct lumo_color_stop *b = &lumo_day_stops[i];
    float span = b->hour - a->hour;
    float t = (span > 0.0f) ? (fractional_hour - a->hour) / span : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    /* smoothstep for a more natural curve */
    t = t * t * (3.0f - 2.0f * t);

    *out_r = a->r + (b->r - a->r) * t;
    *out_g = a->g + (b->g - a->g) * t;
    *out_b = a->b + (b->b - a->b) * t;
}

static void lumo_theme_apply_weather(int weather_code,
    float *r, float *g, float *b)
{
    switch (weather_code) {
    case 1: /* partly cloudy */
        *g += 6.0f; *b += 10.0f; break;
    case 2: /* overcast */
        *r = (*r * 3.0f + 0x30) / 4.0f;
        *g = (*g * 3.0f + 0x28) / 4.0f;
        *b = (*b * 3.0f + 0x28) / 4.0f;
        break;
    case 3: /* rain */
        *r = *r * 2.0f / 3.0f; *g += 8.0f; *b += 26.0f; break;
    case 4: /* storm */
        *r = *r / 2.0f + 8.0f; *g = *g / 2.0f; *b += 36.0f; break;
    case 5: /* snow */
        *r += 20.0f; *g += 26.0f; *b += 36.0f; break;
    case 6: /* fog */
        *r = (*r + 0x28) / 2.0f;
        *g = (*g + 0x24) / 2.0f;
        *b = (*b + 0x22) / 2.0f;
        break;
    default: break;
    }
    if (*r > 255.0f) *r = 255.0f;
    if (*g > 255.0f) *g = 255.0f;
    if (*b > 255.0f) *b = 255.0f;
}

static void lumo_theme_derive_ui(uint32_t r, uint32_t g, uint32_t b) {
    lumo_theme.base_r = r;
    lumo_theme.base_g = g;
    lumo_theme.base_b = b;

    lumo_theme.bar_top = lumo_argb(0xE0, (uint8_t)(r + 0x10 > 0xFF ? 0xFF : r + 0x10),
        (uint8_t)g, (uint8_t)(b + 0x06 > 0xFF ? 0xFF : b + 0x06));
    lumo_theme.bar_bottom = lumo_argb(0xE0, (uint8_t)r, (uint8_t)g, (uint8_t)b);
    lumo_theme.panel_bg = lumo_argb(0xF0, (uint8_t)(r + 0x08 > 0xFF ? 0xFF : r + 0x08),
        (uint8_t)(g + 0x04 > 0xFF ? 0xFF : g + 0x04),
        (uint8_t)(b + 0x06 > 0xFF ? 0xFF : b + 0x06));
    lumo_theme.panel_stroke = lumo_argb(0x60,
        (uint8_t)(r + 0x30 > 0xFF ? 0xFF : r + 0x30),
        (uint8_t)(g + 0x18 > 0xFF ? 0xFF : g + 0x18),
        (uint8_t)(b + 0x28 > 0xFF ? 0xFF : b + 0x28));
    lumo_theme.tile_fill = lumo_argb(0xFF, (uint8_t)(r + 0x0A > 0xFF ? 0xFF : r + 0x0A),
        (uint8_t)(g + 0x08 > 0xFF ? 0xFF : g + 0x08),
        (uint8_t)(b + 0x0A > 0xFF ? 0xFF : b + 0x0A));
    lumo_theme.tile_stroke = lumo_argb(0xFF,
        (uint8_t)(r + 0x20 > 0xFF ? 0xFF : r + 0x20),
        (uint8_t)(g + 0x14 > 0xFF ? 0xFF : g + 0x14),
        (uint8_t)(b + 0x1C > 0xFF ? 0xFF : b + 0x1C));
    lumo_theme.accent = lumo_argb(0xFF, 0xE9, 0x54, 0x20);
    lumo_theme.text_primary = lumo_argb(0xFF, 0xFF, 0xFF, 0xFF);
    lumo_theme.text_secondary = lumo_argb(0xFF, 0xAE, 0xA7, 0x9F);
    lumo_theme.dim = lumo_argb(0x40,
        (uint8_t)(r + 0x30 > 0xFF ? 0xFF : r + 0x30),
        (uint8_t)(g + 0x18 > 0xFF ? 0xFF : g + 0x18),
        (uint8_t)(b + 0x28 > 0xFF ? 0xFF : b + 0x28));
}

void lumo_theme_update(int weather_code) {
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    float fractional_hour = (float)tm_now.tm_hour +
        (float)tm_now.tm_min / 60.0f;

    /* compute target color from time-of-day interpolation */
    float target_r, target_g, target_b;
    lumo_theme_interpolate_time(fractional_hour,
        &target_r, &target_g, &target_b);

    /* apply weather tint to target */
    lumo_theme_apply_weather(weather_code,
        &target_r, &target_g, &target_b);

    /* initialize smooth state on first call */
    if (!lumo_smooth_initialized) {
        lumo_smooth_r = target_r;
        lumo_smooth_g = target_g;
        lumo_smooth_b = target_b;
        lumo_smooth_initialized = true;
    }

    /* exponential approach: move 8% of the way toward target each call.
     * at 5fps background refresh, this gives a ~3 second visual blend
     * for time-of-day shifts and ~1 second for weather changes. */
    float blend = 0.08f;
    lumo_smooth_r += (target_r - lumo_smooth_r) * blend;
    lumo_smooth_g += (target_g - lumo_smooth_g) * blend;
    lumo_smooth_b += (target_b - lumo_smooth_b) * blend;

    uint32_t r = (uint32_t)(lumo_smooth_r + 0.5f);
    uint32_t g = (uint32_t)(lumo_smooth_g + 0.5f);
    uint32_t b = (uint32_t)(lumo_smooth_b + 0.5f);
    if (r > 0xFF) r = 0xFF;
    if (g > 0xFF) g = 0xFF;
    if (b > 0xFF) b = 0xFF;

    lumo_theme.hour = (uint32_t)tm_now.tm_hour;
    lumo_theme.weather_code = weather_code;

    lumo_theme_derive_ui(r, g, b);
}
