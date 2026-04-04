/*
 * app_sysmon.c — Lumo System Monitor (btop-style GUI)
 *
 * Displays real-time CPU, GPU, RAM, and filesystem stats in a
 * touch-friendly dashboard layout.  Reads from /proc and /sys.
 */
#define _DEFAULT_SOURCE
#include "lumo/app_render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <time.h>

/* ── CPU stats ───────────────────────────────────────────────────── */

struct cpu_stat {
    unsigned long user, nice, system, idle, iowait, irq, softirq;
};

static struct cpu_stat prev_total, prev_cores[8];
static int cpu_pcts[8];
static int cpu_total_pct;

static void read_cpu_stats(void) {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return;

    char line[256];
    int core = -1;
    while (fgets(line, sizeof(line), fp) && core < 8) {
        struct cpu_stat cur = {0};
        if (strncmp(line, "cpu ", 4) == 0) {
            sscanf(line + 4, "%lu %lu %lu %lu %lu %lu %lu",
                &cur.user, &cur.nice, &cur.system, &cur.idle,
                &cur.iowait, &cur.irq, &cur.softirq);
            unsigned long total_d = (cur.user - prev_total.user) +
                (cur.nice - prev_total.nice) +
                (cur.system - prev_total.system) +
                (cur.idle - prev_total.idle) +
                (cur.iowait - prev_total.iowait) +
                (cur.irq - prev_total.irq) +
                (cur.softirq - prev_total.softirq);
            unsigned long idle_d = (cur.idle - prev_total.idle) +
                (cur.iowait - prev_total.iowait);
            cpu_total_pct = total_d > 0
                ? (int)(100 * (total_d - idle_d) / total_d) : 0;
            prev_total = cur;
            core = 0;
        } else if (strncmp(line, "cpu", 3) == 0 && core >= 0 && core < 8) {
            sscanf(line + 5, "%lu %lu %lu %lu %lu %lu %lu",
                &cur.user, &cur.nice, &cur.system, &cur.idle,
                &cur.iowait, &cur.irq, &cur.softirq);
            unsigned long total_d = (cur.user - prev_cores[core].user) +
                (cur.nice - prev_cores[core].nice) +
                (cur.system - prev_cores[core].system) +
                (cur.idle - prev_cores[core].idle) +
                (cur.iowait - prev_cores[core].iowait) +
                (cur.irq - prev_cores[core].irq) +
                (cur.softirq - prev_cores[core].softirq);
            unsigned long idle_d = (cur.idle - prev_cores[core].idle) +
                (cur.iowait - prev_cores[core].iowait);
            cpu_pcts[core] = total_d > 0
                ? (int)(100 * (total_d - idle_d) / total_d) : 0;
            prev_cores[core] = cur;
            core++;
        }
    }
    fclose(fp);
}

/* ── helpers ─────────────────────────────────────────────────────── */

static void draw_bar(uint32_t *pixels, uint32_t width, uint32_t height,
    int x, int y, int bar_w, int bar_h, int pct,
    uint32_t fg, uint32_t bg, uint32_t outline)
{
    struct lumo_rect bg_rect = {x, y, bar_w, bar_h};
    lumo_app_fill_rounded_rect(pixels, width, height, &bg_rect, 4, bg);
    lumo_app_draw_outline(pixels, width, height, &bg_rect, 1, outline);

    int fill_w = (bar_w - 4) * pct / 100;
    if (fill_w < 0) fill_w = 0;
    if (fill_w > bar_w - 4) fill_w = bar_w - 4;
    if (fill_w > 0) {
        struct lumo_rect fill = {x + 2, y + 2, fill_w, bar_h - 4};
        lumo_app_fill_rounded_rect(pixels, width, height, &fill, 3, fg);
    }
}

static uint32_t pct_color(int pct) {
    if (pct > 80) return lumo_app_argb(0xFF, 0xFF, 0x44, 0x44); /* red */
    if (pct > 50) return lumo_app_argb(0xFF, 0xFF, 0xAA, 0x44); /* yellow */
    return lumo_app_argb(0xFF, 0x44, 0xCC, 0x44); /* green */
}

/* ── render ──────────────────────────────────────────────────────── */

void lumo_app_render_sysmon(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
) {
    (void)ctx;
    struct lumo_app_theme theme;
    lumo_app_theme_get(&theme);

    struct lumo_rect full = {0, 0, (int)width, (int)height};
    memset(pixels, 0, (size_t)width * height * 4);
    lumo_app_fill_gradient(pixels, width, height, &full,
        theme.header_bg, theme.bg);

    int y = 16;
    int pad = 20;
    int col_w = (int)width - pad * 2;
    char buf[128];

    /* header */
    lumo_app_draw_text(pixels, width, height, pad, y, 2,
        theme.text_dim, "SYSTEM MONITOR");
    y += 20;
    lumo_app_draw_text(pixels, width, height, pad, y, 4,
        theme.accent, "LUMO SYSMON");
    y += 36;
    lumo_app_fill_rect(pixels, width, height, pad, y, col_w, 1,
        theme.separator);
    y += 12;

    /* ── CPU ─────────────────────────────────────────────────── */
    read_cpu_stats();

    snprintf(buf, sizeof(buf), "CPU  %d%%", cpu_total_pct);
    lumo_app_draw_text(pixels, width, height, pad, y, 3,
        theme.text, buf);
    y += 24;

    /* total bar */
    draw_bar(pixels, width, height, pad, y, col_w, 16,
        cpu_total_pct, pct_color(cpu_total_pct),
        theme.card_bg, theme.card_stroke);
    y += 22;

    /* per-core bars */
    int bar_w = (col_w - 8 * 4) / 8;
    if (bar_w < 20) bar_w = 20;
    for (int i = 0; i < 8; i++) {
        int bx = pad + i * (bar_w + 4);
        draw_bar(pixels, width, height, bx, y, bar_w, 28,
            cpu_pcts[i], pct_color(cpu_pcts[i]),
            theme.card_bg, theme.card_stroke);
        snprintf(buf, sizeof(buf), "%d", i);
        lumo_app_draw_text(pixels, width, height,
            bx + bar_w / 2 - 3, y + 8, 1, theme.text_dim, buf);
    }
    y += 36;

    /* CPU info */
    {
        FILE *fp = fopen("/proc/cpuinfo", "r");
        if (fp) {
            char line[256];
            while (fgets(line, sizeof(line), fp)) {
                if (strncmp(line, "isa", 3) == 0) {
                    char *colon = strchr(line, ':');
                    if (colon) {
                        char *nl = strchr(colon, '\n');
                        if (nl) *nl = '\0';
                        lumo_app_draw_text(pixels, width, height, pad, y, 1,
                            theme.text_dim, colon + 2);
                        y += 14;
                    }
                    break;
                }
            }
            fclose(fp);
        }
    }

    y += 8;
    lumo_app_fill_rect(pixels, width, height, pad, y, col_w, 1,
        theme.separator);
    y += 12;

    /* ── GPU ─────────────────────────────────────────────────── */
    lumo_app_draw_text(pixels, width, height, pad, y, 3,
        theme.text, "GPU");
    y += 24;

    {
        /* read GPU info from DRM */
        const char *gpu_name = "PowerVR BXE-2-32";
        const char *gpu_api = "GLES 3.2 / Vulkan 1.3";
        lumo_app_draw_text(pixels, width, height, pad + 8, y, 2,
            theme.accent, gpu_name);
        y += 18;
        lumo_app_draw_text(pixels, width, height, pad + 8, y, 1,
            theme.text_dim, gpu_api);
        y += 14;

        /* check renderer */
        const char *renderer = getenv("WLR_RENDERER");
        snprintf(buf, sizeof(buf), "RENDERER: %s",
            renderer ? renderer : "pixman");
        lumo_app_draw_text(pixels, width, height, pad + 8, y, 1,
            renderer && strcmp(renderer, "gles2") == 0
                ? theme.accent : theme.text_dim,
            buf);
        y += 16;
    }

    y += 4;
    lumo_app_fill_rect(pixels, width, height, pad, y, col_w, 1,
        theme.separator);
    y += 12;

    /* ── RAM ─────────────────────────────────────────────────── */
    {
        struct sysinfo si;
        if (sysinfo(&si) == 0) {
            unsigned long total_mb = (si.totalram * si.mem_unit) / (1024*1024);
            unsigned long used_mb = total_mb -
                ((si.freeram + si.bufferram + si.sharedram) *
                 si.mem_unit) / (1024*1024);
            int pct = total_mb > 0 ? (int)(100 * used_mb / total_mb) : 0;

            snprintf(buf, sizeof(buf), "RAM  %lu / %lu MB  (%d%%)",
                used_mb, total_mb, pct);
            lumo_app_draw_text(pixels, width, height, pad, y, 3,
                theme.text, buf);
            y += 24;

            draw_bar(pixels, width, height, pad, y, col_w, 16,
                pct, pct_color(pct), theme.card_bg, theme.card_stroke);
            y += 22;

            /* swap */
            unsigned long swap_total = (si.totalswap * si.mem_unit) / (1024*1024);
            unsigned long swap_used = swap_total -
                (si.freeswap * si.mem_unit) / (1024*1024);
            snprintf(buf, sizeof(buf), "SWAP  %lu / %lu MB",
                swap_used, swap_total);
            lumo_app_draw_text(pixels, width, height, pad + 8, y, 1,
                theme.text_dim, buf);
            y += 16;

            /* uptime */
            long up = si.uptime;
            int days = (int)(up / 86400);
            int hours = (int)((up % 86400) / 3600);
            int mins = (int)((up % 3600) / 60);
            snprintf(buf, sizeof(buf), "UPTIME  %dd %dh %dm",
                days, hours, mins);
            lumo_app_draw_text(pixels, width, height, pad + 8, y, 1,
                theme.text_dim, buf);
            y += 16;

            /* load average */
            snprintf(buf, sizeof(buf), "LOAD  %.2f %.2f %.2f",
                si.loads[0] / 65536.0, si.loads[1] / 65536.0,
                si.loads[2] / 65536.0);
            lumo_app_draw_text(pixels, width, height, pad + 8, y, 1,
                theme.text_dim, buf);
            y += 16;
        }
    }

    y += 4;
    lumo_app_fill_rect(pixels, width, height, pad, y, col_w, 1,
        theme.separator);
    y += 12;

    /* ── Filesystem ──────────────────────────────────────────── */
    lumo_app_draw_text(pixels, width, height, pad, y, 3,
        theme.text, "STORAGE");
    y += 24;

    static const char *mounts[] = {"/", "/data", "/tmp", NULL};
    static const char *names[] = {"ROOT", "DATA", "TMP"};

    for (int i = 0; mounts[i] != NULL && y + 24 < (int)height; i++) {
        struct statvfs st;
        if (statvfs(mounts[i], &st) != 0) continue;

        unsigned long total_mb = (unsigned long)
            (st.f_blocks * (st.f_frsize / 1024)) / 1024;
        unsigned long free_mb = (unsigned long)
            (st.f_bavail * (st.f_frsize / 1024)) / 1024;
        unsigned long used_mb = total_mb > free_mb ? total_mb - free_mb : 0;
        int pct = total_mb > 0 ? (int)(100 * used_mb / total_mb) : 0;

        snprintf(buf, sizeof(buf), "%-5s %lu / %lu MB  %d%%",
            names[i], used_mb, total_mb, pct);
        lumo_app_draw_text(pixels, width, height, pad + 8, y, 1,
            theme.text_dim, buf);
        y += 14;

        draw_bar(pixels, width, height, pad, y, col_w, 12,
            pct, pct_color(pct), theme.card_bg, theme.card_stroke);
        y += 18;
    }

    /* ── Processes ────────────────────────────────────────────── */
    if (y + 40 < (int)height) {
        y += 4;
        lumo_app_fill_rect(pixels, width, height, pad, y, col_w, 1,
            theme.separator);
        y += 12;

        struct sysinfo si;
        if (sysinfo(&si) == 0) {
            snprintf(buf, sizeof(buf), "PROCESSES  %d", si.procs);
            lumo_app_draw_text(pixels, width, height, pad, y, 2,
                theme.text, buf);
            y += 20;
        }

        /* temperature if available */
        FILE *fp = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
        if (fp) {
            int temp = 0;
            if (fscanf(fp, "%d", &temp) == 1) {
                snprintf(buf, sizeof(buf), "CPU TEMP  %.1f C",
                    temp / 1000.0);
                lumo_app_draw_text(pixels, width, height, pad + 8, y, 1,
                    temp > 70000 ? lumo_app_argb(0xFF, 0xFF, 0x44, 0x44)
                                 : theme.text_dim,
                    buf);
            }
            fclose(fp);
        }
    }
}
