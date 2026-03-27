#include "lumo/app_render.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int lumo_app_settings_row_at(
    uint32_t width, uint32_t height, double x, double y
) {
    int row0_y = 120;
    (void)width; (void)height;
    if (x < 28.0 || x > (double)width - 28.0) return -1;
    for (int i = 0; i < 3; i++) {
        int ry = row0_y + i * 86;
        if (y >= ry && y < ry + 70) return i;
    }
    return -1;
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
    int row_y;
    char hostname_buf[64] = "orangepi";
    char kernel_buf[128] = "unknown";

    memset(pixels, 0, (size_t)width * height * 4);
    lumo_app_fill_gradient(pixels, width, height, &full, bg_top, bg_bottom);

    lumo_app_draw_text(pixels, width, height, 28, 28, 2,
        text_secondary, "SYSTEM SETTINGS");
    lumo_app_draw_text(pixels, width, height, 28, 60, 4, text_primary,
        "Settings");

    gethostname(hostname_buf, sizeof(hostname_buf) - 1);
    {
        FILE *fp = fopen("/proc/version", "r");
        if (fp != NULL) {
            if (fgets(kernel_buf, sizeof(kernel_buf), fp) != NULL) {
                char *space = strchr(kernel_buf, ' ');
                if (space) space = strchr(space + 1, ' ');
                if (space) {
                    char *end = strchr(space + 1, ' ');
                    if (end) *end = '\0';
                }
                if (space && space[0] != '\0')
                    memmove(kernel_buf, space + 1, strlen(space + 1) + 1);
            }
            fclose(fp);
        }
    }

    row_y = 120;

    {
        struct lumo_rect card = {28, row_y, (int)width - 56, 70};
        int val_x = card.x + card.width / 3;
        if (selected == 0) {
            lumo_app_draw_outline(pixels, width, height, &card, 2, accent);
        }
        lumo_app_fill_rounded_rect(pixels, width, height, &card, 14, panel_fill);
        lumo_app_draw_outline(pixels, width, height, &card, 1,
            selected == 0 ? accent : panel_stroke);
        lumo_app_draw_text(pixels, width, height, card.x + 20,
            card.y + 14, 2, text_secondary, "HOSTNAME");
        lumo_app_draw_text(pixels, width, height, val_x,
            card.y + 14, 2, text_primary, hostname_buf);
        lumo_app_draw_text(pixels, width, height, card.x + 20,
            card.y + 42, 2, text_secondary, "KERNEL");
        lumo_app_draw_text(pixels, width, height, val_x,
            card.y + 42, 2, text_primary, kernel_buf);
    }
    row_y += 86;

    {
        struct lumo_rect card = {28, row_y, (int)width - 56, 70};
        int val_x = card.x + card.width / 3;
        char uptime_buf[64] = "unknown";
        char mem_buf[64] = "unknown";
        FILE *fp;

        fp = fopen("/proc/uptime", "r");
        if (fp != NULL) {
            double up_secs = 0.0;
            if (fscanf(fp, "%lf", &up_secs) == 1) {
                int hours = (int)(up_secs / 3600);
                int mins = (int)((up_secs - hours * 3600) / 60);
                snprintf(uptime_buf, sizeof(uptime_buf), "%dH %dM", hours, mins);
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

        lumo_app_fill_rounded_rect(pixels, width, height, &card, 14, panel_fill);
        lumo_app_draw_outline(pixels, width, height, &card, 1,
            selected == 1 ? accent : panel_stroke);
        lumo_app_draw_text(pixels, width, height, card.x + 20,
            card.y + 14, 2, text_secondary, "UPTIME");
        lumo_app_draw_text(pixels, width, height, val_x,
            card.y + 14, 2, text_primary, uptime_buf);
        lumo_app_draw_text(pixels, width, height, card.x + 20,
            card.y + 42, 2, text_secondary, "MEMORY");
        lumo_app_draw_text(pixels, width, height, val_x,
            card.y + 42, 2, text_primary, mem_buf);
    }
    row_y += 86;

    {
        struct lumo_rect card = {28, row_y, (int)width - 56, 70};
        int val_x = card.x + card.width / 3;
        char wifi_buf[64] = "NOT CONNECTED";

        {
            FILE *wfp = fopen("/proc/net/wireless", "r");
            if (wfp != NULL) {
                char wline[256];
                while (fgets(wline, sizeof(wline), wfp) != NULL) {
                    char ifname[32] = {0};
                    float level = 0;
                    if (sscanf(wline, " %31[^:]: %*d %*f %f",
                            ifname, &level) >= 1 &&
                            ifname[0] != '\0' && ifname[0] != '|') {
                        snprintf(wifi_buf, sizeof(wifi_buf),
                            "%s  SIGNAL %.0f", ifname, level);
                        break;
                    }
                }
                fclose(wfp);
            }
            if (strcmp(wifi_buf, "NOT CONNECTED") == 0) {
                FILE *ofp = fopen("/sys/class/net/wlan0/operstate", "r");
                if (ofp != NULL) {
                    char state[32] = {0};
                    if (fgets(state, sizeof(state), ofp) != NULL) {
                        char *nl = strchr(state, '\n');
                        if (nl) *nl = '\0';
                        if (strcmp(state, "up") == 0)
                            snprintf(wifi_buf, sizeof(wifi_buf),
                                "WLAN0 CONNECTED");
                    }
                    fclose(ofp);
                }
            }
        }

        lumo_app_fill_rounded_rect(pixels, width, height, &card, 14, panel_fill);
        lumo_app_draw_outline(pixels, width, height, &card, 1,
            selected == 2 ? accent : panel_stroke);
        lumo_app_draw_text(pixels, width, height, card.x + 20,
            card.y + 14, 2, text_secondary, "WI-FI");
        lumo_app_draw_text(pixels, width, height, val_x,
            card.y + 14, 2, text_primary, wifi_buf);
        lumo_app_draw_text(pixels, width, height, card.x + 20,
            card.y + 42, 2, text_secondary, "COMPOSITOR");
        lumo_app_draw_text(pixels, width, height, val_x,
            card.y + 42, 2, accent, "LUMO 0.0.49");
    }

    lumo_app_draw_close_button(pixels, width, height, close_active);
}
