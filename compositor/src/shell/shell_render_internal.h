/*
 * shell_render_internal.h — shared declarations for shell_render split modules.
 *
 * This header ties together shell_render.c, shell_theme.c, and
 * shell_background.c so they can call each other's entry points.
 */
#ifndef LUMO_SHELL_RENDER_INTERNAL_H
#define LUMO_SHELL_RENDER_INTERNAL_H

#include "shell_client_internal.h"

void lumo_theme_update(int weather_code);
void lumo_draw_animated_bg(uint32_t *pixels, uint32_t width, uint32_t height,
    int weather_code);

#endif /* LUMO_SHELL_RENDER_INTERNAL_H */
