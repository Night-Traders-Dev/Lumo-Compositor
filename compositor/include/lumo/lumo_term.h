/*
 * lumo_term.h — VT100/xterm-256color terminal emulator for Lumo OS.
 *
 * Provides a cell-based grid with ANSI escape sequence parsing,
 * 256-color support, cursor positioning, scroll regions, and
 * alternate screen buffer for curses apps (btop, vim, nano, etc.).
 */
#ifndef LUMO_TERM_H
#define LUMO_TERM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ── cell attributes (bitmask) ───────────────────────────────────── */

#define LUMO_TERM_ATTR_BOLD       0x01
#define LUMO_TERM_ATTR_DIM        0x02
#define LUMO_TERM_ATTR_UNDERLINE  0x04
#define LUMO_TERM_ATTR_INVERSE    0x08
#define LUMO_TERM_ATTR_HIDDEN     0x10
#define LUMO_TERM_ATTR_DEFAULT_FG 0x20
#define LUMO_TERM_ATTR_DEFAULT_BG 0x40

/* ── maximum grid size ───────────────────────────────────────────── */

#define LUMO_TERM_MAX_COLS 132
#define LUMO_TERM_MAX_ROWS  60

/* ── cell ─────────────────────────────────────────���──────────────── */

struct lumo_term_cell {
    char ch;       /* ASCII 0x20-0x7E, 0 = empty (treated as space) */
    uint8_t fg;    /* 0-255 palette index (valid when !ATTR_DEFAULT_FG) */
    uint8_t bg;    /* 0-255 palette index (valid when !ATTR_DEFAULT_BG) */
    uint8_t attr;  /* LUMO_TERM_ATTR_* bitmask */
};

/* ── parser state ────────��───────────────────────────────────────── */

enum lumo_term_parse_state {
    LUMO_TERM_STATE_GROUND = 0,
    LUMO_TERM_STATE_ESC,
    LUMO_TERM_STATE_CSI,
    LUMO_TERM_STATE_OSC,
};

/* ── terminal ──��──────────────────────────────���──────────────────── */

struct lumo_term {
    int cols, rows;

    /* primary and alternate screen buffers */
    struct lumo_term_cell cells[LUMO_TERM_MAX_ROWS * LUMO_TERM_MAX_COLS];
    struct lumo_term_cell alt_cells[LUMO_TERM_MAX_ROWS * LUMO_TERM_MAX_COLS];
    bool alt_active;

    /* cursor */
    int cursor_row, cursor_col;
    bool cursor_visible;

    /* saved cursor (DECSC/DECRC) */
    int saved_row, saved_col;
    uint8_t saved_fg, saved_bg, saved_attr;

    /* current SGR state for new characters */
    uint8_t current_fg, current_bg, current_attr;

    /* scroll region (inclusive) */
    int scroll_top, scroll_bottom;

    /* modes */
    bool autowrap;        /* DECAWM (default true) */
    bool origin_mode;     /* DECOM */
    bool app_cursor_keys; /* DECCKM */
    bool bracketed_paste; /* mode 2004 */

    /* CSI parser */
    enum lumo_term_parse_state parse_state;
    int csi_params[16];
    int csi_param_count;
    bool csi_private;     /* '?' prefix */

    /* tab stops */
    bool tab_stops[LUMO_TERM_MAX_COLS];

    /* response buffer (DA, DSR replies to write back to PTY) */
    char response[64];
    int response_len;
};

/* ── API ─────��──────────────────────────────���────────────────────── */

/* allocate and initialize (static storage, no malloc) */
void lumo_term_init(struct lumo_term *t, int cols, int rows);

/* process raw PTY output through the ANSI state machine */
void lumo_term_feed(struct lumo_term *t, const char *data, size_t len);

/* resize the grid (truncate/pad, notify caller to TIOCSWINSZ) */
void lumo_term_resize(struct lumo_term *t, int cols, int rows);

/* full reset (clear grid, home cursor, default attrs) */
void lumo_term_reset(struct lumo_term *t);

/* return pointer to the active screen buffer (primary or alt) */
const struct lumo_term_cell *lumo_term_screen(const struct lumo_term *t);

/* drain pending response bytes; returns count written to buf */
int lumo_term_drain_response(struct lumo_term *t, char *buf, size_t buf_size);

/* 256-color palette: convert palette index to ARGB */
uint32_t lumo_term_color_argb(uint8_t index);

#endif /* LUMO_TERM_H */
