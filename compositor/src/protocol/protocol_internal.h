/*
 * protocol_internal.h — shared declarations for protocol split modules.
 *
 * This header ties together protocol.c and protocol_setters.c so they
 * can call each other's non-static helpers.
 */
#ifndef LUMO_PROTOCOL_INTERNAL_H
#define LUMO_PROTOCOL_INTERNAL_H

#include "lumo/compositor.h"
#include "lumo/shell.h"

#include <wlr/util/box.h>

/* protocol.c — helpers used by protocol_setters.c */
void lumo_protocol_configure_layer_surface_for_output(
    struct lumo_layer_surface *layer_surface,
    struct lumo_output *output,
    const struct wlr_box *full_area,
    struct wlr_box *usable_area);
bool lumo_protocol_layer_surface_layout_unchanged(
    const struct lumo_layer_surface *layer_surface,
    const struct lumo_output *output,
    const struct wlr_box *full_area,
    const struct wlr_box *usable_area);

/* protocol_setters.c — setter/query functions */
int lumo_protocol_register_hitbox(
    struct lumo_compositor *compositor,
    const char *name,
    const struct lumo_rect *rect,
    enum lumo_hitbox_kind kind,
    bool accepts_touch,
    bool accepts_pointer);
const struct lumo_hitbox *lumo_protocol_hitbox_at(
    struct lumo_compositor *compositor,
    double lx,
    double ly);
void lumo_protocol_set_gesture_threshold(
    struct lumo_compositor *compositor,
    double threshold);
void lumo_protocol_set_launcher_visible(
    struct lumo_compositor *compositor,
    bool visible);
void lumo_protocol_set_quick_settings_visible(
    struct lumo_compositor *compositor,
    bool visible);
void lumo_protocol_set_time_panel_visible(
    struct lumo_compositor *compositor,
    bool visible);
void lumo_protocol_set_notification_panel_visible(
    struct lumo_compositor *compositor,
    bool visible);
void lumo_protocol_push_notification(
    struct lumo_compositor *compositor,
    const char *text);
void lumo_protocol_set_sidebar_visible(
    struct lumo_compositor *compositor, bool visible);
void lumo_protocol_set_scrim_state(
    struct lumo_compositor *compositor,
    enum lumo_scrim_state state);
void lumo_protocol_ack_keyboard_resize(
    struct lumo_compositor *compositor,
    uint32_t serial);
void lumo_protocol_set_keyboard_visible(
    struct lumo_compositor *compositor,
    bool visible);
void lumo_protocol_configure_layers(
    struct lumo_compositor *compositor,
    struct lumo_output *output);
void lumo_protocol_configure_all_layers(struct lumo_compositor *compositor);

#endif /* LUMO_PROTOCOL_INTERNAL_H */
