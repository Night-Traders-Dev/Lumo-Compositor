#include "lumo/app_render.h"
#include <stdio.h>
#include <time.h>

int lumo_app_clock_card_at(
    uint32_t width, uint32_t height, double x, double y
) {
    int card1_y = (int)height / 2 + 20;
    int card2_y = (int)height / 2 + 130;
    (void)width;
    if (x < 28.0 || x > (double)width - 28.0) return -1;
    if (y >= card1_y && y < card1_y + 90) return 1;
    if (y >= card2_y && y < card2_y + 90) return 2;
    return -1;
}

void lumo_app_render_clock(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
) {
    struct lumo_rect full = {0, 0, (int)width, (int)height};
    struct lumo_rect close_rect = {0};
    bool close_active = ctx != NULL ? ctx->close_active : false;
    uint32_t accent = lumo_app_accent_argb(LUMO_APP_CLOCK);
    uint32_t bg_top = lumo_app_argb(0xFF, 0x2C, 0x00, 0x1E);
    uint32_t bg_bottom = lumo_app_argb(0xFF, 0x1D, 0x11, 0x22);
    uint32_t text_primary = lumo_app_argb(0xFF, 0xFF, 0xFF, 0xFF);
    uint32_t text_secondary = lumo_app_argb(0xFF, 0xAE, 0xA7, 0x9F);
    uint32_t panel_fill = lumo_app_argb(0xFF, 0x2C, 0x16, 0x28);
    uint32_t panel_stroke = lumo_app_argb(0xFF, 0x5E, 0x2C, 0x56);
    time_t now = time(NULL);
    struct tm tm_now = {0};
    char time_buf[16] = {0};
    char date_buf[32] = {0};
    int cx = (int)width / 2;

    localtime_r(&now, &tm_now);
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &tm_now);
    strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &tm_now);

    memset(pixels, 0, (size_t)width * height * 4);
    lumo_app_fill_gradient(pixels, width, height, &full, bg_top, bg_bottom);

    {
        int tw = (int)strlen(time_buf) * 8 * 6 - 8;
        lumo_app_draw_text(pixels, width, height, cx - tw / 2,
            (int)height / 4 - 28, 8, accent, time_buf);
    }
    {
        int dw = (int)strlen(date_buf) * 3 * 6 - 3;
        lumo_app_draw_text(pixels, width, height, cx - dw / 2,
            (int)height / 4 + 40, 3, text_secondary, date_buf);
    }

    {
        struct lumo_rect alarm_card = {
            .x = 28, .y = (int)height / 2 + 20,
            .width = (int)width - 56, .height = 90
        };
        lumo_app_fill_rounded_rect(pixels, width, height, &alarm_card, 18,
            panel_fill);
        lumo_app_draw_outline(pixels, width, height, &alarm_card, 2,
            panel_stroke);
        lumo_app_draw_text(pixels, width, height, alarm_card.x + 20,
            alarm_card.y + 16, 2, text_secondary, "ALARM");
        lumo_app_draw_text(pixels, width, height, alarm_card.x + 20,
            alarm_card.y + 46, 3, text_primary, "06:30 TOMORROW");
        lumo_app_draw_text(pixels, width, height,
            alarm_card.x + alarm_card.width - 80,
            alarm_card.y + 50, 2, text_secondary, "06:30");
    }
    {
        struct lumo_rect timer_card = {
            .x = 28, .y = (int)height / 2 + 130,
            .width = (int)width - 56, .height = 90
        };
        uint64_t sw_ms = ctx != NULL ? ctx->stopwatch_elapsed_ms : 0;
        bool sw_run = ctx != NULL && ctx->stopwatch_running;
        uint32_t sw_secs = (uint32_t)(sw_ms / 1000);
        uint32_t sw_mins = sw_secs / 60;
        uint32_t sw_hrs = sw_mins / 60;
        char sw_buf[16];
        snprintf(sw_buf, sizeof(sw_buf), "%02u:%02u:%02u",
            sw_hrs, sw_mins % 60, sw_secs % 60);

        lumo_app_fill_rounded_rect(pixels, width, height, &timer_card, 18,
            panel_fill);
        lumo_app_draw_outline(pixels, width, height, &timer_card, 2,
            panel_stroke);
        lumo_app_draw_text(pixels, width, height, timer_card.x + 20,
            timer_card.y + 16, 2, text_secondary,
            sw_run ? "STOPWATCH  RUNNING" : "STOPWATCH  TAP TO START");
        lumo_app_draw_text(pixels, width, height, timer_card.x + 20,
            timer_card.y + 46, 3, sw_run ? accent : text_primary, sw_buf);
        lumo_app_draw_text(pixels, width, height,
            timer_card.x + timer_card.width - 100,
            timer_card.y + 50, 2, text_secondary, "TAP:RESET");
    }

    /* close button removed — use bottom-edge swipe */
}
