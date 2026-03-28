#define _DEFAULT_SOURCE
#include "lumo/app_render.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

static const int files_row_height = 52;
static const int files_header_height = 140;

int lumo_app_files_entry_at(
    uint32_t width, uint32_t height, double x, double y
) {
    int row_y = files_header_height;
    int max_rows = ((int)height - row_y - 60) / files_row_height;
    (void)width;
    if (x < 28.0 || x > (double)width - 28.0) return -1;
    if (y < (double)row_y) return -1;
    int index = (int)(y - row_y) / files_row_height;
    if (index < 0 || index >= max_rows) return -1;
    return index;
}

void lumo_app_render_files(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
) {
    struct lumo_rect full = {0, 0, (int)width, (int)height};
    bool close_active = ctx != NULL ? ctx->close_active : false;
    const char *browse_path_override = ctx != NULL ? ctx->browse_path : NULL;
    int scroll_off = ctx != NULL ? ctx->scroll_offset : 0;
    int selected = ctx != NULL ? ctx->selected_row : -1;
    uint32_t accent = lumo_app_accent_argb(LUMO_APP_FILES);
    uint32_t bg_top = lumo_app_argb(0xFF, 0x2C, 0x00, 0x1E);
    uint32_t bg_bottom = lumo_app_argb(0xFF, 0x1D, 0x11, 0x22);
    uint32_t text_primary = lumo_app_argb(0xFF, 0xFF, 0xFF, 0xFF);
    uint32_t text_secondary = lumo_app_argb(0xFF, 0xAE, 0xA7, 0x9F);
    uint32_t panel_fill = lumo_app_argb(0xFF, 0x2C, 0x16, 0x28);
    uint32_t panel_stroke = lumo_app_argb(0xFF, 0x5E, 0x2C, 0x56);
    uint32_t folder_color = lumo_app_argb(0xFF, 0xE9, 0x54, 0x20);
    uint32_t file_color = lumo_app_argb(0xFF, 0x77, 0x21, 0x6F);
    int row_y;
    DIR *dir;
    const char *browse_path;

    memset(pixels, 0, (size_t)width * height * 4);
    lumo_app_fill_gradient(pixels, width, height, &full, bg_top, bg_bottom);

    lumo_app_draw_text(pixels, width, height, 28, 28, 2,
        text_secondary, "FILE MANAGER");
    lumo_app_draw_text(pixels, width, height, 28, 60, 4, text_primary,
        "Files");

    if (browse_path_override != NULL && browse_path_override[0] != '\0') {
        browse_path = browse_path_override;
    } else {
        browse_path = getenv("HOME");
        if (browse_path == NULL) browse_path = "/home";
    }

    lumo_app_draw_text(pixels, width, height, 28, 108, 2, accent, browse_path);

    lumo_app_fill_rect(pixels, width, height, 28, 130, (int)width - 56, 1,
        panel_stroke);

    row_y = files_header_height;
    dir = opendir(browse_path);
    if (dir != NULL) {
        struct dirent *entry;
        int count = 0, skipped = 0, total_visible = 0;
        int max_rows = ((int)height - row_y - 60) / files_row_height;
        if (max_rows < 1) max_rows = 1;

        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') continue;
            if (skipped < scroll_off) { skipped++; total_visible++; continue; }
            if (count >= max_rows) { total_visible++; continue; }

            bool is_dir = entry->d_type == DT_DIR;
            struct lumo_rect row_rect = {28, row_y, (int)width - 56, files_row_height - 4};

            if (selected == total_visible) {
                lumo_app_fill_rounded_rect(pixels, width, height, &row_rect,
                    10, lumo_app_argb(0xFF, 0x3B, 0x1F, 0x34));
                lumo_app_draw_outline(pixels, width, height, &row_rect, 1,
                    lumo_app_argb(0xFF, 0xE9, 0x54, 0x20));
            } else {
                lumo_app_fill_rounded_rect(pixels, width, height, &row_rect,
                    10, panel_fill);
            }

            {
                struct lumo_rect icon = {row_rect.x + 10, row_rect.y + 10, 20, 20};
                lumo_app_fill_rounded_rect(pixels, width, height, &icon,
                    4, is_dir ? folder_color : file_color);
            }

            lumo_app_draw_text(pixels, width, height, row_rect.x + 42,
                row_rect.y + 12, 2, text_primary, entry->d_name);
            if (!is_dir) {
                char path_buf[1100];
                struct stat st;
                snprintf(path_buf, sizeof(path_buf), "%s/%s",
                    browse_path, entry->d_name);
                if (stat(path_buf, &st) == 0) {
                    char size_buf[16];
                    if (st.st_size < 1024) {
                        snprintf(size_buf, sizeof(size_buf), "%ldB",
                            (long)st.st_size);
                    } else if (st.st_size < 1024 * 1024) {
                        snprintf(size_buf, sizeof(size_buf), "%ldK",
                            (long)(st.st_size / 1024));
                    } else {
                        snprintf(size_buf, sizeof(size_buf), "%ldM",
                            (long)(st.st_size / (1024 * 1024)));
                    }
                    lumo_app_draw_text(pixels, width, height,
                        row_rect.x + row_rect.width - 80, row_rect.y + 12,
                        2, text_secondary, size_buf);
                } else {
                    lumo_app_draw_text(pixels, width, height,
                        row_rect.x + row_rect.width - 60, row_rect.y + 12,
                        2, text_secondary, "FILE");
                }
            } else {
                lumo_app_draw_text(pixels, width, height,
                    row_rect.x + row_rect.width - 60, row_rect.y + 12,
                    2, text_secondary, "DIR");
            }

            row_y += files_row_height;
            count++;
            total_visible++;
        }
        closedir(dir);

        if (total_visible > max_rows) {
            char scroll_buf[32];
            snprintf(scroll_buf, sizeof(scroll_buf), "%d-%d / %d",
                scroll_off + 1, scroll_off + count, total_visible);
            lumo_app_draw_text(pixels, width, height,
                (int)width - 160, (int)height - 40, 2,
                text_secondary, scroll_buf);
        }
        if (count == 0) {
            lumo_app_draw_text(pixels, width, height, 28, row_y, 2,
                text_secondary, "EMPTY DIRECTORY");
        }
    } else {
        lumo_app_draw_text(pixels, width, height, 28, files_header_height, 2,
            text_secondary, "CANNOT OPEN DIRECTORY");
    }

    {
        struct statvfs st;
        if (statvfs("/", &st) == 0) {
            char storage_buf[64];
            unsigned long free_mb = (unsigned long)(st.f_bavail *
                (st.f_frsize / 1024)) / 1024;
            unsigned long total_mb = (unsigned long)(st.f_blocks *
                (st.f_frsize / 1024)) / 1024;
            snprintf(storage_buf, sizeof(storage_buf),
                "%lu / %lu MB FREE", free_mb, total_mb);
            lumo_app_draw_text(pixels, width, height, 28,
                (int)height - 40, 2, text_secondary, storage_buf);
        }
    }

    /* close button removed — use bottom-edge swipe */
}
