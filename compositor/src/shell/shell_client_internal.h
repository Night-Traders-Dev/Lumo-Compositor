/*
 * shell_client_internal.h — shared definitions for shell_client modules.
 *
 * This header is private to the shell client implementation. Each
 * shell_client_*.c file includes it for access to the client struct,
 * buffer management, and cross-module function declarations.
 */
#ifndef LUMO_SHELL_CLIENT_INTERNAL_H
#define LUMO_SHELL_CLIENT_INTERNAL_H

#include "lumo/shell.h"
#include "lumo/shell_protocol.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

#include <wayland-client.h>
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

/* ── forward declarations ─────────────────────────────────────────── */

struct lumo_shell_client;

/* ── buffer ───────────────────────────────────────────────────────── */

struct lumo_shell_buffer {
    struct lumo_shell_client *client;
    struct wl_buffer *buffer;
    struct wl_shm_pool *pool;
    void *data;
    int fd;
    size_t size;
    uint32_t width;
    uint32_t height;
    bool busy;
    struct wl_buffer_listener release;
};

/* ── scrim / debug enums ──────────────────────────────────────────── */

enum lumo_shell_remote_scrim_state {
    LUMO_SHELL_REMOTE_SCRIM_HIDDEN = 0,
    LUMO_SHELL_REMOTE_SCRIM_DIMMED,
    LUMO_SHELL_REMOTE_SCRIM_MODAL,
};

/* ── per-surface state (one per shell mode in unified process) ────── */

#define LUMO_SHELL_MAX_SURFACES 6

struct lumo_shell_surface_slot {
    enum lumo_shell_mode mode;
    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct lumo_shell_buffer *buffer;
    struct lumo_shell_buffer *buffers[2];
    struct lumo_shell_surface_config config;
    uint32_t configured_width;
    uint32_t configured_height;
    bool configured;
    bool target_visible;
    bool surface_hidden;
    bool animation_active;
    double animation_from;
    double animation_to;
    uint64_t animation_started_msec;
    uint32_t animation_duration_msec;
    bool dirty;
};

/* ── client state ─────────────────────────────────────────────────── */

struct lumo_shell_client {
    enum lumo_shell_mode mode;       /* primary mode (legacy single-surface) */
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wl_touch *touch;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct lumo_shell_buffer *buffer;
    struct lumo_shell_buffer *buffers[2];
    struct lumo_shell_surface_config config;
    int state_fd;
    char state_socket_path[PATH_MAX];
    struct lumo_shell_protocol_stream protocol_stream;
    uint32_t configured_width;
    uint32_t configured_height;
    uint32_t output_width_hint;
    uint32_t output_height_hint;
    bool configured;
    bool running;

    /* unified mode: all surfaces in one process */
    bool unified;
    int surface_count;
    struct lumo_shell_surface_slot slots[LUMO_SHELL_MAX_SURFACES];
    bool pointer_pressed;
    bool touch_pressed;
    bool active_target_valid;
    bool pointer_position_valid;
    int32_t active_touch_id;
    uint32_t next_request_id;
    double pointer_x;
    double pointer_y;
    bool compositor_launcher_visible;
    bool compositor_keyboard_visible;
    bool compositor_quick_settings_visible;
    bool compositor_time_panel_visible;
    bool compositor_notification_panel_visible;
    char notifications[8][128];
    int notification_count;
    enum lumo_shell_remote_scrim_state compositor_scrim_state;
    uint32_t compositor_rotation_degrees;
    double compositor_gesture_threshold;
    uint32_t compositor_gesture_timeout_ms;
    bool compositor_osk_shift_active;
    bool compositor_keyboard_resize_pending;
    bool compositor_keyboard_resize_acked;
    uint32_t compositor_keyboard_resize_serial;
    bool compositor_touch_audit_active;
    bool compositor_touch_audit_saved;
    uint32_t compositor_touch_audit_step;
    uint32_t compositor_touch_audit_completed_mask;
    char compositor_touch_audit_profile[128];
    bool touch_debug_seen;
    bool touch_debug_active;
    double touch_debug_x;
    double touch_debug_y;
    uint32_t touch_debug_id;
    enum lumo_shell_touch_debug_phase touch_debug_phase;
    enum lumo_shell_touch_debug_target touch_debug_target;
    bool target_visible;
    bool surface_hidden;
    bool animation_active;
    double animation_from;
    double animation_to;
    uint64_t animation_started_msec;
    uint32_t animation_duration_msec;
    struct lumo_shell_target active_target;
    bool search_active;
    char search_query[32];
    int search_len;
    int launcher_page;           /* current page in multi-page drawer */
    double launcher_swipe_x;     /* touch-down x for page swipe */
    char weather_condition[32];
    char weather_humidity[16];
    char weather_wind[24];
    int weather_code;
    int weather_temp_c;
    uint32_t volume_pct;
    uint32_t brightness_pct;
    char toast_message[128];
    uint32_t toast_time_low;
    uint32_t toast_duration_ms;

    /* sidebar (running apps multitasking bar) */
    bool compositor_sidebar_visible;
    uint32_t running_app_count;
    char running_app_ids[16][64];
    char running_app_titles[16][64];
    bool sidebar_long_press_active;
    uint32_t sidebar_long_press_index;
    uint64_t sidebar_press_start_msec;
    bool sidebar_context_menu_visible;
    uint32_t sidebar_context_menu_index;

    /* touch ripple effect */
    double ripple_x, ripple_y;
    uint64_t ripple_start_msec;
    bool ripple_active;
};

/* ── theme (global, shared by all drawing functions) ──────────────── */

struct lumo_shell_theme {
    uint32_t base_r, base_g, base_b;
    uint32_t bar_top, bar_bottom;
    uint32_t panel_bg, panel_stroke;
    uint32_t tile_fill, tile_stroke;
    uint32_t accent, text_primary, text_secondary, dim;
    uint32_t hour;
    int weather_code;
};

extern struct lumo_shell_theme lumo_theme;

/* ── shell_draw.c — graphics primitives ───────────────────────────── */

uint32_t lumo_argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b);
uint32_t lumo_u32_min(uint32_t lhs, uint32_t rhs);
double lumo_clamp_unit(double value);
double lumo_ease_decelerate(double value);
double lumo_ease_standard(double value);
uint64_t lumo_now_msec(void);
void lumo_clear_pixels(uint32_t *pixels, uint32_t width, uint32_t height);
void lumo_fill_span(uint32_t *row_ptr, int count, uint32_t color);
void lumo_fill_rect(
    uint32_t *pixels, uint32_t width, uint32_t height,
    int x, int y, int rect_width, int rect_height, uint32_t color);
void lumo_fill_vertical_gradient(
    uint32_t *pixels, uint32_t width, uint32_t height,
    const struct lumo_rect *rect, uint32_t top_color, uint32_t bottom_color);
bool lumo_rounded_rect_contains(
    const struct lumo_rect *rect, int radius, int px, int py);
void lumo_fill_rounded_rect(
    uint32_t *pixels, uint32_t width, uint32_t height,
    const struct lumo_rect *rect, uint32_t radius, uint32_t color);
void lumo_draw_outline(
    uint32_t *pixels, uint32_t width, uint32_t height,
    const struct lumo_rect *rect, int thickness, uint32_t color);
int lumo_text_width(const char *text, int scale);
void lumo_draw_text(
    uint32_t *pixels, uint32_t width, uint32_t height,
    int x, int y, int scale, uint32_t color, const char *text);
void lumo_draw_text_centered(
    uint32_t *pixels, uint32_t width, uint32_t height,
    const struct lumo_rect *rect, int scale, uint32_t color,
    const char *text);

/* ── shell_render.c — theme + mode renderers ──────────────────────── */

void lumo_theme_update(int weather_code);
void lumo_render_surface(
    struct lumo_shell_client *client,
    uint32_t *pixels, uint32_t width, uint32_t height,
    const struct lumo_shell_target *active_target);
double lumo_shell_client_animation_value(
    const struct lumo_shell_client *client);

/* ── shell_input.c — touch/pointer handlers ───────────────────────── */

extern const struct wl_pointer_listener lumo_shell_pointer_listener;
extern const struct wl_touch_listener lumo_shell_touch_listener;

void lumo_shell_client_note_target(
    struct lumo_shell_client *client, double x, double y);
void lumo_shell_client_set_active_target(
    struct lumo_shell_client *client,
    const struct lumo_shell_target *target);
void lumo_shell_client_clear_active_target(struct lumo_shell_client *client);
void lumo_shell_client_activate_target(struct lumo_shell_client *client);
int lumo_shell_status_button_hit(
    const struct lumo_shell_client *client, double x, double y);
uint32_t lumo_shell_slider_pct_from_touch(
    const struct lumo_shell_client *client, double x);

/* ── shell_protocol_client.c — state socket + protocol ────────────── */

int lumo_shell_client_connect_state_socket(struct lumo_shell_client *client);
bool lumo_shell_client_pump_protocol(struct lumo_shell_client *client);
void lumo_shell_client_send_reload(struct lumo_shell_client *client);
void lumo_shell_client_send_cycle_rotation(struct lumo_shell_client *client);
void lumo_shell_client_send_capture_screenshot(
    struct lumo_shell_client *client);
bool lumo_shell_client_send_frame(
    struct lumo_shell_client *client,
    const struct lumo_shell_protocol_frame *frame);
void lumo_shell_send_set_u32(
    struct lumo_shell_client *client,
    const char *name, const char *field, uint32_t value);
void lumo_shell_client_send_focus_app(
    struct lumo_shell_client *client, uint32_t index);
void lumo_shell_client_send_close_app(
    struct lumo_shell_client *client, uint32_t index);
void lumo_shell_client_send_minimize_focused(
    struct lumo_shell_client *client);
void lumo_shell_client_send_open_drawer(
    struct lumo_shell_client *client);

/* ── shell_client.c — surface config, buffer mgmt, main ───────────── */

bool lumo_shell_client_redraw(struct lumo_shell_client *client);
void lumo_shell_client_redraw_unified(struct lumo_shell_client *client);
void lumo_shell_client_finish_hide_if_needed(
    struct lumo_shell_client *client);
int lumo_shell_client_animation_timeout(
    const struct lumo_shell_client *client);
void lumo_shell_client_tick_animation(struct lumo_shell_client *client);
void lumo_shell_client_sync_surface_state(struct lumo_shell_client *client,
    bool force_layout);

#endif /* LUMO_SHELL_CLIENT_INTERNAL_H */
