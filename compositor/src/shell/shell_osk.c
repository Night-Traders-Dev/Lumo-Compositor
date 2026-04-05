#include "lumo/shell.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LUMO_OSK_PAGE_COUNT 3

static const uint32_t lumo_shell_osk_rows = 4;
static const uint32_t lumo_shell_osk_row_columns[] = {
    10,
    10,
    9,
    4,
};

/* page 0: QWERTY letters */
static const char *const lumo_shell_osk_texts_alpha[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", "\b",
    "", "z", "x", "c", "v", "b", "n", "m", "\x01",
    ".", " ", "\n", "\x1b",
};
static const char *const lumo_shell_osk_labels_alpha[] = {
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", "<-",
    "^", "Z", "X", "C", "V", "B", "N", "M", "123>",
    ".", "SPACE", "ENTER", "v",
};

/* page 1: numbers and symbols */
static const char *const lumo_shell_osk_texts_sym[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0",
    "@", "#", "$", "%", "&", "-", "+", "(", ")", "\b",
    "", "!", "?", ",", ";", ":", "'", "/", "\x01",
    ".", " ", "\n", "\x1b",
};
static const char *const lumo_shell_osk_labels_sym[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0",
    "@", "#", "$", "%", "&", "-", "+", "(", ")", "<-",
    "^", "!", "?", ",", ";", ":", "'", "/", "TERM",
    ".", "SPACE", "ENTER", "v",
};

/* page 2: terminal control keys */
static const char *const lumo_shell_osk_texts_term[] = {
    "\x1b", "\t", "|", "~", "`", "_", "\\", "=", "[", "]",
    "\x03", "\x04", "\x1a", "\x0c", "{", "}", "\"", "<", ">", "\b",
    "", "\x1b[A", "\x1b[B", "\x1b[D", "\x1b[C", "\x1b[5~", "\x1b[6~", "\x1b[H", "\x01",
    ".", " ", "\n", "\x1b",
};
static const char *const lumo_shell_osk_labels_term[] = {
    "ESC", "TAB", "|", "~", "`", "_", "\\", "=", "[", "]",
    "C-C", "C-D", "C-Z", "C-L", "{", "}", "\"", "<", ">", "<-",
    "^", "UP", "DN", "LT", "RT", "PGU", "PGD", "HOM", "ABC",
    ".", "SPACE", "ENTER", "v",
};

static uint32_t lumo_shell_osk_page = 0;

static uint32_t lumo_shell_max_u32(uint32_t lhs, uint32_t rhs) {
    return lhs > rhs ? lhs : rhs;
}

static uint32_t lumo_shell_min_u32(uint32_t lhs, uint32_t rhs) {
    return lhs < rhs ? lhs : rhs;
}

static uint32_t lumo_shell_clamp_u32(
    uint32_t value,
    uint32_t minimum,
    uint32_t maximum
) {
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static bool lumo_shell_osk_row_col_for_index(
    uint32_t key_index,
    uint32_t *row_out,
    uint32_t *col_out
) {
    uint32_t offset = 0;

    if (row_out == NULL || col_out == NULL) {
        return false;
    }

    for (uint32_t row = 0; row < lumo_shell_osk_rows; row++) {
        uint32_t columns = lumo_shell_osk_row_columns[row];
        if (key_index < offset + columns) {
            *row_out = row;
            *col_out = key_index - offset;
            return true;
        }
        offset += columns;
    }

    return false;
}

static const char *const *osk_get_labels(void) {
    if (lumo_shell_osk_page == 2) return lumo_shell_osk_labels_term;
    if (lumo_shell_osk_page == 1) return lumo_shell_osk_labels_sym;
    return lumo_shell_osk_labels_alpha;
}

static const char *const *osk_get_texts(void) {
    if (lumo_shell_osk_page == 2) return lumo_shell_osk_texts_term;
    if (lumo_shell_osk_page == 1) return lumo_shell_osk_texts_sym;
    return lumo_shell_osk_texts_alpha;
}

static size_t osk_get_count(void) {
    if (lumo_shell_osk_page == 2)
        return sizeof(lumo_shell_osk_labels_term) / sizeof(lumo_shell_osk_labels_term[0]);
    if (lumo_shell_osk_page == 1)
        return sizeof(lumo_shell_osk_labels_sym) / sizeof(lumo_shell_osk_labels_sym[0]);
    return sizeof(lumo_shell_osk_labels_alpha) / sizeof(lumo_shell_osk_labels_alpha[0]);
}

const char *lumo_shell_osk_key_label(uint32_t key_index) {
    const char *const *labels = osk_get_labels();
    size_t count = osk_get_count();
    if (key_index >= count) return NULL;
    return labels[key_index];
}

const char *lumo_shell_osk_key_text(uint32_t key_index) {
    const char *const *texts = osk_get_texts();
    size_t count = osk_get_count();
    if (key_index >= count) return NULL;
    return texts[key_index];
}

void lumo_shell_osk_toggle_page(void) {
    lumo_shell_osk_page = (lumo_shell_osk_page + 1) % LUMO_OSK_PAGE_COUNT;
}

void lumo_shell_osk_set_page(uint32_t page) {
    if (page < LUMO_OSK_PAGE_COUNT) {
        lumo_shell_osk_page = page;
    }
}

uint32_t lumo_shell_osk_get_page(void) {
    return lumo_shell_osk_page;
}

size_t lumo_shell_osk_key_count(void) {
    size_t count = 0;

    for (uint32_t row = 0; row < lumo_shell_osk_rows; row++) {
        count += lumo_shell_osk_row_columns[row];
    }

    return count;
}

bool lumo_shell_osk_key_rect(
    uint32_t output_width,
    uint32_t output_height,
    uint32_t key_index,
    struct lumo_rect *rect
) {
    uint32_t row;
    uint32_t col;
    uint32_t gap;
    uint32_t top_bar;
    uint32_t bottom_padding;
    uint32_t row_height;
    uint32_t side_padding;
    uint32_t columns;
    uint32_t row_units;
    uint32_t total_row_width;
    uint32_t unit_width;
    uint32_t key_width_units = 1;
    uint32_t key_width;
    uint32_t row_start_x;
    uint32_t preceding_units = 0;
    uint32_t key_x;
    uint32_t key_y;

    if (rect == NULL || output_width == 0 || output_height == 0) {
        return false;
    }

    if (!lumo_shell_osk_row_col_for_index(key_index, &row, &col)) {
        return false;
    }

    gap = lumo_shell_clamp_u32(output_width / 96, 6, 10);
    top_bar = lumo_shell_clamp_u32(output_height / 10, 24, 40);
    bottom_padding = lumo_shell_clamp_u32(output_height / 20, 8, 20);
    side_padding = lumo_shell_clamp_u32(output_width / 24, 12, 32);

    if (output_height <= top_bar + bottom_padding + gap * (lumo_shell_osk_rows + 1) ||
            output_width <= side_padding * 2 + gap * 4) {
        return false;
    }

    row_height = (output_height - top_bar - bottom_padding -
        gap * (lumo_shell_osk_rows + 1)) / lumo_shell_osk_rows;
    row_height = lumo_shell_max_u32(row_height, 38);

    columns = lumo_shell_osk_row_columns[row];
    row_units = columns;

    /* row 3 special: . (1) SPACE (4) ENTER (2) ? (1) = 8 units */
    if (row == 3) {
        row_units = 8;
        if (col == 1) {
            key_width_units = 4; /* SPACE */
        } else if (col == 2) {
            key_width_units = 2; /* ENTER */
        }
    }

    if (output_width <= side_padding * 2 + gap * (columns - 1)) {
        return false;
    }
    /* Extra guard: ensure row_units worth of space remains after padding/gaps
     * to prevent unsigned wrap in the subtraction below on tiny displays. */
    if (output_width < side_padding * 2 + gap * (columns - 1) + row_units) {
        return false;
    }

    unit_width = (output_width - side_padding * 2 - gap * (columns - 1)) / row_units;
    unit_width = lumo_shell_max_u32(unit_width, 24);
    total_row_width = row_units * unit_width + gap * (columns - 1);
    if (total_row_width > output_width) {
        return false;
    }
    total_row_width = lumo_shell_min_u32(total_row_width, output_width);
    row_start_x = (output_width - total_row_width) / 2;

    /* compute preceding units for row 3 special layout */
    if (row == 3) {
        switch (col) {
        case 0: preceding_units = 0; break;
        case 1: preceding_units = 1; break; /* after . */
        case 2: preceding_units = 5; break; /* after . + SPACE(4) */
        case 3: preceding_units = 7; break; /* after . + SPACE(4) + ENTER(2) */
        default: preceding_units = col; break;
        }
    } else {
        preceding_units = col;
    }

    key_width = unit_width * key_width_units + gap * (key_width_units - 1);
    key_x = row_start_x + preceding_units * unit_width + col * gap;
    key_y = top_bar + gap + row * (row_height + gap);

    if (key_x > (uint32_t)INT_MAX || key_y > (uint32_t)INT_MAX ||
            key_width > (uint32_t)INT_MAX || row_height > (uint32_t)INT_MAX) {
        return false;
    }

    rect->x = (int)key_x;
    rect->y = (int)key_y;
    rect->width = (int)key_width;
    rect->height = (int)row_height;
    return true;
}
