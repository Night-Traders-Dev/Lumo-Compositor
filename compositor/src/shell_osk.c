#include "lumo/shell.h"

#include <stdbool.h>
#include <stddef.h>

static const uint32_t lumo_shell_osk_rows = 4;
static const uint32_t lumo_shell_osk_row_columns[] = {
    10,
    9,
    7,
    5,
};
static const char *const lumo_shell_osk_key_texts[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p",
    "a", "s", "d", "f", "g", "h", "j", "k", "l",
    "z", "x", "c", "v", "b", "n", "m",
    ",", ".", " ", "?", "\n",
};
static const char *const lumo_shell_osk_key_labels[] = {
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P",
    "A", "S", "D", "F", "G", "H", "J", "K", "L",
    "Z", "X", "C", "V", "B", "N", "M",
    ",", ".", "SPACE", "?", "RETURN",
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

    gap = lumo_shell_clamp_u32(output_width / 96, 8, 14);
    top_bar = lumo_shell_clamp_u32(output_height / 8, 34, 54);
    bottom_padding = lumo_shell_clamp_u32(output_height / 18, 12, 28);
    side_padding = lumo_shell_clamp_u32(output_width / 20, 18, 44);

    if (output_height <= top_bar + bottom_padding + gap * (lumo_shell_osk_rows + 1) ||
            output_width <= side_padding * 2 + gap * 3) {
        return false;
    }

    row_height = (output_height - top_bar - bottom_padding -
        gap * (lumo_shell_osk_rows + 1)) / lumo_shell_osk_rows;
    row_height = lumo_shell_max_u32(row_height, 42);

    columns = lumo_shell_osk_row_columns[row];
    row_units = columns;
    if (row == 3) {
        row_units = 7;
        if (col == 2) {
            key_width_units = 3;
        }
    }

    if (output_width <= side_padding * 2 + gap * (columns - 1)) {
        return false;
    }

    unit_width = (output_width - side_padding * 2 - gap * (columns - 1)) / row_units;
    unit_width = lumo_shell_max_u32(unit_width, 26);
    total_row_width = row_units * unit_width + gap * (columns - 1);
    total_row_width = lumo_shell_min_u32(total_row_width, output_width);
    row_start_x = (output_width - total_row_width) / 2;

    if (row == 3 && col > 2) {
        preceding_units = col + 2;
    } else {
        preceding_units = col;
    }

    key_width = unit_width * key_width_units + gap * (key_width_units - 1);
    key_x = row_start_x + preceding_units * unit_width + col * gap;
    key_y = top_bar + gap + row * (row_height + gap);

    rect->x = (int)key_x;
    rect->y = (int)key_y;
    rect->width = (int)key_width;
    rect->height = (int)row_height;
    return true;
}
