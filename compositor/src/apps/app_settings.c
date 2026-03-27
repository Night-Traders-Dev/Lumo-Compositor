#include "lumo/app_render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <unistd.h>

static const int settings_row_height = 56;
static const int settings_header_y = 100;

int lumo_app_settings_row_at(
    uint32_t width, uint32_t height, double x, double y
) {
    (void)width; (void)height;
    if (x < 28.0 || x > (double)width - 28.0) return -1;
    if (y < settings_header_y) return -1;
    int idx = (int)(y - settings_header_y) / settings_row_height;
    if (idx < 0 || idx > 5) return -1;
    return idx;
}

void lumo_app_render_settings(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
) {
    struct lumo_rect full = {0, 0, (int)width, (int)height};
    bool close_active = ctx != NULL ? ctx->close_active : false;
    int selected = ctx != NULL ? ctx->selected_row : -1;
    uint32_t accent = lumo_app_argb(0xFF, 0xE9, 0x54, 0x20);
    uint32_t bg_top = lumo_app_argb(0xFF, 0x2C, 0x00, 0x1E);
    uint32_t bg_bottom = lumo_app_argb(0xFF, 0x1D, 0x11, 0x22);
    uint32_t text_primary = lumo_app_argb(0xFF, 0xFF, 0xFF, 0xFF);
    uint32_t text_secondary = lumo_app_argb(0xFF, 0xAE, 0xA7, 0x9F);
    uint32_t panel_fill = lumo_app_argb(0xFF, 0x2C, 0x16, 0x28);
    uint32_t panel_stroke = lumo_app_argb(0xFF, 0x5E, 0x2C, 0x56);
    int row_y = settings_header_y;
    int val_x;
    char buf[128];

    memset(pixels, 0, (size_t)width * height * 4);
    lumo_app_fill_gradient(pixels, width, height, &full, bg_top, bg_bottom);

    lumo_app_draw_text(pixels, width, height, 28, 28, 2,
        text_secondary, "SYSTEM SETTINGS");
    lumo_app_draw_text(pixels, width, height, 28, 56, 3, text_primary,
        "Settings");

    val_x = 28 + (int)width / 3;

    /* Row 0: Hostname & Kernel */
    {
        struct lumo_rect row = {28, row_y, (int)width - 56, settings_row_height - 4};
        char hostname[64] = "unknown";
        char kernel[128] = "unknown";

        gethostname(hostname, sizeof(hostname) - 1);
        {
            FILE *fp = fopen("/proc/version", "r");
            if (fp != NULL) {
                if (fgets(kernel, sizeof(kernel), fp) != NULL) {
                    char *s = strchr(kernel, ' ');
                    if (s) s = strchr(s + 1, ' ');
                    if (s) { char *e = strchr(s + 1, ' '); if (e) *e = '\0'; }
                    if (s && s[0]) memmove(kernel, s + 1, strlen(s + 1) + 1);
                }
                fclose(fp);
            }
        }

        lumo_app_fill_rounded_rect(pixels, width, height, &row, 12, panel_fill);
        lumo_app_draw_outline(pixels, width, height, &row, 1,
            selected == 0 ? accent : panel_stroke);
        lumo_app_draw_text(pixels, width, height, row.x + 16, row.y + 10, 2,
            text_secondary, "HOSTNAME");
        lumo_app_draw_text(pixels, width, height, val_x, row.y + 10, 2,
            text_primary, hostname);
        lumo_app_draw_text(pixels, width, height, row.x + 16, row.y + 30, 2,
            text_secondary, "KERNEL");
        lumo_app_draw_text(pixels, width, height, val_x, row.y + 30, 2,
            text_primary, kernel);
    }
    row_y += settings_row_height;

    /* Row 1: Uptime & Memory */
    {
        struct lumo_rect row = {28, row_y, (int)width - 56, settings_row_height - 4};
        char uptime_buf[64] = "unknown";
        char mem_buf[64] = "unknown";
        FILE *fp;

        fp = fopen("/proc/uptime", "r");
        if (fp != NULL) {
            double up = 0;
            if (fscanf(fp, "%lf", &up) == 1) {
                int h = (int)(up / 3600), m = (int)((up - h * 3600) / 60);
                snprintf(uptime_buf, sizeof(uptime_buf), "%dH %dM", h, m);
            }
            fclose(fp);
        }

        fp = fopen("/proc/meminfo", "r");
        if (fp != NULL) {
            unsigned long total = 0, avail = 0;
            char line[128];
            while (fgets(line, sizeof(line), fp) != NULL) {
                if (sscanf(line, "MemTotal: %lu", &total) == 1) continue;
                sscanf(line, "MemAvailable: %lu", &avail);
            }
            fclose(fp);
            if (total > 0)
                snprintf(mem_buf, sizeof(mem_buf), "%lu / %lu MB",
                    avail / 1024, total / 1024);
        }

        lumo_app_fill_rounded_rect(pixels, width, height, &row, 12, panel_fill);
        lumo_app_draw_outline(pixels, width, height, &row, 1,
            selected == 1 ? accent : panel_stroke);
        lumo_app_draw_text(pixels, width, height, row.x + 16, row.y + 10, 2,
            text_secondary, "UPTIME");
        lumo_app_draw_text(pixels, width, height, val_x, row.y + 10, 2,
            text_primary, uptime_buf);
        lumo_app_draw_text(pixels, width, height, row.x + 16, row.y + 30, 2,
            text_secondary, "MEMORY");
        lumo_app_draw_text(pixels, width, height, val_x, row.y + 30, 2,
            text_primary, mem_buf);
    }
    row_y += settings_row_height;

    /* Row 2: WiFi */
    {
        struct lumo_rect row = {28, row_y, (int)width - 56, settings_row_height - 4};
        char wifi_buf[64] = "NOT CONNECTED";
        float signal = 0;

        {
            FILE *wfp = fopen("/proc/net/wireless", "r");
            if (wfp != NULL) {
                char wline[256];
                while (fgets(wline, sizeof(wline), wfp) != NULL) {
                    char ifn[32] = {0};
                    if (sscanf(wline, " %31[^:]: %*d %*f %f", ifn, &signal) >= 1 &&
                            ifn[0] != '\0' && ifn[0] != '|') {
                        snprintf(wifi_buf, sizeof(wifi_buf),
                            "%s  %.0f DBM", ifn, signal);
                        break;
                    }
                }
                fclose(wfp);
            }
        }

        lumo_app_fill_rounded_rect(pixels, width, height, &row, 12, panel_fill);
        lumo_app_draw_outline(pixels, width, height, &row, 1,
            selected == 2 ? accent : panel_stroke);
        lumo_app_draw_text(pixels, width, height, row.x + 16, row.y + 10, 2,
            text_secondary, "WI-FI");
        lumo_app_draw_text(pixels, width, height, val_x, row.y + 10, 2,
            text_primary, wifi_buf);
        lumo_app_draw_text(pixels, width, height, row.x + 16, row.y + 30, 2,
            text_secondary, "STATUS");
        lumo_app_draw_text(pixels, width, height, val_x, row.y + 30, 2,
            signal < -80 ? accent : text_primary,
            signal < -80 ? "WEAK" : signal < -60 ? "GOOD" : "STRONG");
    }
    row_y += settings_row_height;

    /* Row 3: Storage */
    {
        struct lumo_rect row = {28, row_y, (int)width - 56, settings_row_height - 4};
        char root_buf[64] = "unknown";
        char home_buf[64] = "unknown";

        {
            struct statvfs st;
            if (statvfs("/", &st) == 0) {
                unsigned long free_mb = (unsigned long)(st.f_bavail *
                    (st.f_frsize / 1024)) / 1024;
                unsigned long total_mb = (unsigned long)(st.f_blocks *
                    (st.f_frsize / 1024)) / 1024;
                snprintf(root_buf, sizeof(root_buf), "%lu / %lu MB",
                    free_mb, total_mb);
            }
        }
        {
            const char *home = getenv("HOME");
            struct statvfs st;
            if (home != NULL && statvfs(home, &st) == 0) {
                unsigned long free_mb = (unsigned long)(st.f_bavail *
                    (st.f_frsize / 1024)) / 1024;
                unsigned long total_mb = (unsigned long)(st.f_blocks *
                    (st.f_frsize / 1024)) / 1024;
                snprintf(home_buf, sizeof(home_buf), "%lu / %lu MB",
                    free_mb, total_mb);
            }
        }

        lumo_app_fill_rounded_rect(pixels, width, height, &row, 12, panel_fill);
        lumo_app_draw_outline(pixels, width, height, &row, 1,
            selected == 3 ? accent : panel_stroke);
        lumo_app_draw_text(pixels, width, height, row.x + 16, row.y + 10, 2,
            text_secondary, "ROOT");
        lumo_app_draw_text(pixels, width, height, val_x, row.y + 10, 2,
            text_primary, root_buf);
        lumo_app_draw_text(pixels, width, height, row.x + 16, row.y + 30, 2,
            text_secondary, "HOME");
        lumo_app_draw_text(pixels, width, height, val_x, row.y + 30, 2,
            text_primary, home_buf);
    }
    row_y += settings_row_height;

    /* Row 4: Display */
    {
        struct lumo_rect row = {28, row_y, (int)width - 56, settings_row_height - 4};

        lumo_app_fill_rounded_rect(pixels, width, height, &row, 12, panel_fill);
        lumo_app_draw_outline(pixels, width, height, &row, 1,
            selected == 4 ? accent : panel_stroke);
        lumo_app_draw_text(pixels, width, height, row.x + 16, row.y + 10, 2,
            text_secondary, "DISPLAY");
        snprintf(buf, sizeof(buf), "%ux%u", width, height);
        lumo_app_draw_text(pixels, width, height, val_x, row.y + 10, 2,
            text_primary, buf);
        lumo_app_draw_text(pixels, width, height, row.x + 16, row.y + 30, 2,
            text_secondary, "COMPOSITOR");
        lumo_app_draw_text(pixels, width, height, val_x, row.y + 30, 2,
            accent, "LUMO 0.0.50");
    }
    row_y += settings_row_height;

    /* Row 5: About */
    {
        struct lumo_rect row = {28, row_y, (int)width - 56, settings_row_height - 4};
        char cpu_buf[64] = "RISCV64";

        {
            FILE *fp = fopen("/proc/cpuinfo", "r");
            if (fp != NULL) {
                char line[256];
                while (fgets(line, sizeof(line), fp) != NULL) {
                    char *val;
                    if ((val = strstr(line, "model name")) != NULL ||
                            (val = strstr(line, "isa")) != NULL) {
                        val = strchr(val, ':');
                        if (val != NULL) {
                            val += 2;
                            char *nl = strchr(val, '\n');
                            if (nl) *nl = '\0';
                            snprintf(cpu_buf, sizeof(cpu_buf), "%s", val);
                            break;
                        }
                    }
                }
                fclose(fp);
            }
        }

        lumo_app_fill_rounded_rect(pixels, width, height, &row, 12, panel_fill);
        lumo_app_draw_outline(pixels, width, height, &row, 1,
            selected == 5 ? accent : panel_stroke);
        lumo_app_draw_text(pixels, width, height, row.x + 16, row.y + 10, 2,
            text_secondary, "CPU");
        lumo_app_draw_text(pixels, width, height, val_x, row.y + 10, 2,
            text_primary, cpu_buf);
        lumo_app_draw_text(pixels, width, height, row.x + 16, row.y + 30, 2,
            text_secondary, "DEVICE");
        lumo_app_draw_text(pixels, width, height, val_x, row.y + 30, 2,
            text_primary, "ORANGEPI RV2");
    }

    lumo_app_draw_close_button(pixels, width, height, close_active);
}
