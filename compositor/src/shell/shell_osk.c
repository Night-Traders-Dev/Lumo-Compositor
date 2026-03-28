#include "lumo/shell.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static const uint32_t lumo_shell_osk_rows = 4;
static const uint32_t lumo_shell_osk_row_columns[] = {
    10,
    10,
    9,
    4,
};
static const char *const lumo_shell_osk_key_texts[] = {
    /* row 0: numbers/top row */
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p",
    /* row 1: middle row - with backspace at end */
    "a", "s", "d", "f", "g", "h", "j", "k", "l", "\b",
    /* row 2: bottom letters - with shift at start */
    "", "z", "x", "c", "v", "b", "n", "m", ",",
    /* row 3: space row */
    ".", " ", "\n", "?",
};
static const char *const lumo_shell_osk_key_labels[] = {
    /* row 0 */
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P",
    /* row 1 */
    "A", "S", "D", "F", "G", "H", "J", "K", "L", "<-",
    /* row 2 */
    "^", "Z", "X", "C", "V", "B", "N", "M", ",",
    /* row 3 */
    ".", "SPACE", "ENTER", "?",
};

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

const char *lumo_shell_osk_key_label(uint32_t key_index) {
    if (key_index >= sizeof(lumo_shell_osk_key_labels) /
            sizeof(lumo_shell_osk_key_labels[0])) {
        return NULL;
    }

    return lumo_shell_osk_key_labels[key_index];
}

const char *lumo_shell_osk_key_text(uint32_t key_index) {
    if (key_index >= sizeof(lumo_shell_osk_key_texts) /
            sizeof(lumo_shell_osk_key_texts[0])) {
        return NULL;
    }

    return lumo_shell_osk_key_texts[key_index];
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
