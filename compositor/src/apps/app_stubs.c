/*
 * app_stubs.c — stub implementations for app modules not yet implemented
 * (browser, phone, camera, maps).
 */

#include "lumo/app.h"
#include "lumo/app_render.h"

#include <stdint.h>

void lumo_app_render_browser(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height)
{
    (void)ctx; (void)pixels; (void)width; (void)height;
}

void lumo_app_render_phone(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height)
{
    (void)ctx; (void)pixels; (void)width; (void)height;
}

void lumo_app_render_camera(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height)
{
    (void)ctx; (void)pixels; (void)width; (void)height;
}

void lumo_app_render_maps(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height)
{
    (void)ctx; (void)pixels; (void)width; (void)height;
}

int lumo_app_browser_button_at(
    uint32_t width, uint32_t height,
    double x, double y)
{
    (void)width; (void)height; (void)x; (void)y;
    return -1;
}

int lumo_app_phone_button_at(
    uint32_t width, uint32_t height,
    double x, double y, int tab)
{
    (void)width; (void)height; (void)x; (void)y; (void)tab;
    return -1;
}

int lumo_app_camera_button_at(
    uint32_t width, uint32_t height,
    double x, double y, bool gallery_mode)
{
    (void)width; (void)height; (void)x; (void)y; (void)gallery_mode;
    return -1;
}

int lumo_app_maps_button_at(
    uint32_t width, uint32_t height,
    double x, double y, int tab)
{
    (void)width; (void)height; (void)x; (void)y; (void)tab;
    return -1;
}
