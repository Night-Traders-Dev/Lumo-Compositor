#include "lumo/app_render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <unistd.h>

static const int settings_row_height = 48;
static const int settings_header_y = 90;

int lumo_app_settings_row_at(
    uint32_t width, uint32_t height, double x, double y
) {
    (void)width; (void)height;
    if (x < 20.0 || x > (double)width - 20.0) return -1;
    if (y < settings_header_y) return -1;
    int idx = (int)(y - settings_header_y) / settings_row_height;
    if (idx < 0 || idx > 7) return -1;
    return idx;
}

static void settings_draw_row(
    uint32_t *pixels, uint32_t width, uint32_t height,
    int y, const char *icon, const char *label, const char *value,
    bool selected
) {
    struct lumo_rect row = {20, y, (int)width - 40, settings_row_height - 4};
    uint32_t fill = lumo_app_argb(0xFF, 0x2C, 0x16, 0x28);
    uint32_t stroke = selected
        ? lumo_app_argb(0xFF, 0xE9, 0x54, 0x20)
        : lumo_app_argb(0xFF, 0x3E, 0x20, 0x38);
    uint32_t label_c = lumo_app_argb(0xFF, 0xFF, 0xFF, 0xFF);
    uint32_t value_c = lumo_app_argb(0xFF, 0xAE, 0xA7, 0x9F);
    uint32_t icon_c = lumo_app_argb(0xFF, 0xE9, 0x54, 0x20);
    int val_x = (int)width / 2;

    lumo_app_fill_rounded_rect(pixels, width, height, &row, 10, fill);
    lumo_app_draw_outline(pixels, width, height, &row, 1, stroke);

    if (icon != NULL) {
        lumo_app_draw_text(pixels, width, height, row.x + 14, row.y + 10,
            2, icon_c, icon);
    }

    lumo_app_draw_text(pixels, width, height, row.x + 50, row.y + 10,
        2, label_c, label);

    if (value != NULL) {
        lumo_app_draw_text(pixels, width, height, val_x, row.y + 10,
            2, value_c, value);
    }

    if (selected && value != NULL) {
        lumo_app_draw_text(pixels, width, height, row.x + 50, row.y + 28,
            2, value_c, value);
    }
}

void lumo_app_render_settings(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
) {
    bool close_active = ctx != NULL ? ctx->close_active : false;
    int selected = ctx != NULL ? ctx->selected_row : -1;
    int row_y = settings_header_y;
    char buf[128];

    lumo_app_draw_background(pixels, width, height);

    lumo_app_draw_text(pixels, width, height, 20, 20, 2,
        lumo_app_argb(0xFF, 0xAE, 0xA7, 0x9F), "SETTINGS");
    lumo_app_draw_text(pixels, width, height, 20, 48, 3,
        lumo_app_argb(0xFF, 0xFF, 0xFF, 0xFF), "System");

    /* Row 0: Network */
    {
        char wifi[64] = "NOT CONNECTED";
        FILE *fp = fopen("/proc/net/wireless", "r");
        if (fp != NULL) {
            char line[256];
            while (fgets(line, sizeof(line), fp) != NULL) {
                char ifn[32] = {0}; float sig = 0;
                if (sscanf(line, " %31[^:]: %*d %*f %f", ifn, &sig) >= 1 &&
                        ifn[0] && ifn[0] != '|') {
                    snprintf(wifi, sizeof(wifi), "%s  %.0f DBM", ifn, sig);
                    break;
                }
            }
            fclose(fp);
        }
        settings_draw_row(pixels, width, height, row_y,
            "W", "NETWORK", wifi, selected == 0);
    }
    row_y += settings_row_height;

    /* Row 1: Display */
    snprintf(buf, sizeof(buf), "%ux%u", width, height);
    settings_draw_row(pixels, width, height, row_y,
        "D", "DISPLAY", buf, selected == 1);
    row_y += settings_row_height;

    /* Row 2: Storage */
    {
        struct statvfs st;
        char stor[64] = "UNKNOWN";
        if (statvfs("/", &st) == 0) {
            unsigned long free_mb = (unsigned long)(st.f_bavail *
                (st.f_frsize / 1024)) / 1024;
            unsigned long total_mb = (unsigned long)(st.f_blocks *
                (st.f_frsize / 1024)) / 1024;
            snprintf(stor, sizeof(stor), "%lu / %lu MB FREE",
                free_mb, total_mb);
        }
        settings_draw_row(pixels, width, height, row_y,
            "S", "STORAGE", stor, selected == 2);
    }
    row_y += settings_row_height;

    /* Row 3: Memory */
    {
        char mem[64] = "UNKNOWN";
        FILE *fp = fopen("/proc/meminfo", "r");
        if (fp != NULL) {
            unsigned long total = 0, avail = 0;
            char line[128];
            while (fgets(line, sizeof(line), fp) != NULL) {
                if (sscanf(line, "MemTotal: %lu", &total) == 1) continue;
                sscanf(line, "MemAvailable: %lu", &avail);
            }
            fclose(fp);
            if (total > 0)
                snprintf(mem, sizeof(mem), "%lu / %lu MB",
                    avail / 1024, total / 1024);
        }
        settings_draw_row(pixels, width, height, row_y,
            "M", "MEMORY", mem, selected == 3);
    }
    row_y += settings_row_height;

    /* Row 4: System */
    {
        char uptime[64] = "UNKNOWN";
        FILE *fp = fopen("/proc/uptime", "r");
        if (fp != NULL) {
            double up = 0;
            if (fscanf(fp, "%lf", &up) == 1) {
                int h = (int)(up / 3600), m = (int)((up - h * 3600) / 60);
                snprintf(uptime, sizeof(uptime), "%dH %dM UPTIME", h, m);
            }
            fclose(fp);
        }
        settings_draw_row(pixels, width, height, row_y,
            "U", "SYSTEM", uptime, selected == 4);
    }
    row_y += settings_row_height;

    /* Row 5: About */
    {
        char hostname[64] = "UNKNOWN";
        gethostname(hostname, sizeof(hostname) - 1);
        settings_draw_row(pixels, width, height, row_y,
            "I", "ABOUT", hostname, selected == 5);
    }
    row_y += settings_row_height;

    /* Row 6: Compositor */
    settings_draw_row(pixels, width, height, row_y,
        "L", "LUMO", "VERSION 0.0.50", selected == 6);
    row_y += settings_row_height;

    /* Row 7: CPU */
    {
        char cpu[64] = "RISCV64";
        FILE *fp = fopen("/proc/cpuinfo", "r");
        if (fp != NULL) {
            char line[256];
            while (fgets(line, sizeof(line), fp) != NULL) {
                char *val;
                if ((val = strstr(line, "isa")) != NULL) {
                    val = strchr(val, ':');
                    if (val) {
                        val += 2;
                        char *nl = strchr(val, '\n');
                        if (nl) *nl = '\0';
                        snprintf(cpu, sizeof(cpu), "%s", val);
                        break;
                    }
                }
            }
            fclose(fp);
        }
        settings_draw_row(pixels, width, height, row_y,
            "C", "CPU", cpu, selected == 7);
    }

    lumo_app_draw_close_button(pixels, width, height, close_active);
}
