#include "lumo/app_render.h"
#include "lumo/version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <unistd.h>

/* ── layout constants ─────────────────────────────────────────────── */

static const int ROW_H      = 56;
static const int HEADER_Y   = 80;
static const int SECTION_H  = 28;
static const int PAD        = 24;

/* ── theme helpers ────────────────────────────────────────────────── */

static struct lumo_app_theme th;
static bool th_loaded;

static void ensure_theme(void) {
    if (!th_loaded) { lumo_app_theme_get(&th); th_loaded = true; }
}

/* ── drawing primitives ───────────────────────────────────────────── */

static void draw_section(
    uint32_t *px, uint32_t w, uint32_t h, int y, const char *label
) {
    ensure_theme();
    lumo_app_draw_text(px, w, h, PAD, y + 6, 2, th.accent, label);
    lumo_app_fill_rect(px, w, h, PAD, y + SECTION_H - 2,
        (int)w - PAD * 2, 1, th.separator);
}

static void draw_row(
    uint32_t *px, uint32_t w, uint32_t h,
    int y, const char *title, const char *subtitle
) {
    ensure_theme();
    struct lumo_rect r = { PAD, y, (int)w - PAD * 2, ROW_H - 4 };
    lumo_app_fill_rounded_rect(px, w, h, &r, 12, th.card_bg);
    lumo_app_draw_outline(px, w, h, &r, 1, th.card_stroke);
    lumo_app_draw_text(px, w, h, r.x + 16, r.y + 10, 2, th.text, title);
    if (subtitle != NULL) {
        lumo_app_draw_text(px, w, h, r.x + 16, r.y + 30, 2,
            th.text_dim, subtitle);
    }
    lumo_app_draw_text(px, w, h, r.x + r.width - 24, r.y + 16, 2,
        th.text_dim, ">");
}

static void draw_subheader(
    uint32_t *px, uint32_t w, uint32_t h, const char *title
) {
    ensure_theme();
    lumo_app_draw_text(px, w, h, PAD, 14, 2, th.accent, "< BACK");
    lumo_app_draw_text(px, w, h, PAD, 42, 3, th.text, title);
    lumo_app_fill_rect(px, w, h, PAD, 72, (int)w - PAD * 2, 1,
        th.separator);
}

static void draw_info(
    uint32_t *px, uint32_t w, uint32_t h,
    int y, const char *label, const char *value
) {
    ensure_theme();
    lumo_app_draw_text(px, w, h, PAD + 8, y, 2, th.text_dim, label);
    lumo_app_draw_text(px, w, h, (int)w / 3 + 20, y, 2, th.text, value);
}

static void draw_toggle(
    uint32_t *px, uint32_t w, uint32_t h,
    int y, const char *label, bool on
) {
    ensure_theme();
    lumo_app_draw_text(px, w, h, PAD + 8, y + 2, 2, th.text, label);

    int tx = (int)w - PAD - 56;
    struct lumo_rect track = { tx, y, 44, 22 };
    uint32_t track_c = on ? th.accent
        : lumo_app_argb(0xFF, 0x5E, 0x3A, 0x56);
    lumo_app_fill_rounded_rect(px, w, h, &track, 11, track_c);
    struct lumo_rect knob = {
        on ? tx + 24 : tx + 2,
        y + 3, 16, 16
    };
    lumo_app_fill_rounded_rect(px, w, h, &knob, 8,
        lumo_app_argb(0xFF, 0xFF, 0xFF, 0xFF));
}

static void draw_bar(
    uint32_t *px, uint32_t w, uint32_t h,
    int y, unsigned long used, unsigned long total
) {
    ensure_theme();
    int bw = (int)w - PAD * 2 - 16;
    struct lumo_rect bg = { PAD + 8, y, bw, 14 };
    lumo_app_fill_rounded_rect(px, w, h, &bg, 7, th.card_stroke);
    if (total > 0) {
        struct lumo_rect fg = { PAD + 8, y,
            (int)((long)bw * (long)used / (long)total), 14 };
        if (fg.width > 0)
            lumo_app_fill_rounded_rect(px, w, h, &fg, 7, th.accent);
    }
}

/* ── data helpers ─────────────────────────────────────────────────── */

static void get_wifi_info(char *iface, size_t isz,
                          char *status, size_t ssz,
                          char *signal_out, size_t sgsz,
                          char *ip, size_t ipsz)
{
    snprintf(iface, isz, "NONE");
    snprintf(status, ssz, "DISCONNECTED");
    snprintf(signal_out, sgsz, "--");
    snprintf(ip, ipsz, "--");

    /* find wireless interface from /proc/net/wireless */
    FILE *fp = fopen("/proc/net/wireless", "r");
    if (!fp) return;
    char line[256], ifn[32] = {0};
    float sig = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, " %31[^:]: %*d %*f %f", ifn, &sig) >= 1 &&
                ifn[0] && ifn[0] != '|') {
            snprintf(iface, isz, "%s", ifn);
            break;
        }
    }
    fclose(fp);
    if (!ifn[0]) return;

    /* check operstate */
    char path[128];
    snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", ifn);
    fp = fopen(path, "r");
    if (fp) {
        char st[32] = {0};
        if (fgets(st, sizeof(st), fp)) {
            char *nl = strchr(st, '\n'); if (nl) *nl = '\0';
            if (strcmp(st, "up") == 0 || strcmp(st, "unknown") == 0)
                snprintf(status, ssz, "CONNECTED");
            else
                snprintf(status, ssz, "%s", st);
        }
        fclose(fp);
    }

    snprintf(signal_out, sgsz, "%.0f DBM", sig);
    snprintf(ip, ipsz, "DHCP");
}

static void get_mem_info(unsigned long *total, unsigned long *avail,
                         unsigned long *buffers, unsigned long *cached)
{
    *total = *avail = *buffers = *cached = 0;
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return;
    char line[128];
    while (fgets(line, sizeof(line), fp)) {
        sscanf(line, "MemTotal: %lu", total);
        sscanf(line, "MemAvailable: %lu", avail);
        sscanf(line, "Buffers: %lu", buffers);
        sscanf(line, "Cached: %lu", cached);
    }
    fclose(fp);
}

/* ── row hit test ─────────────────────────────────────────────────── */

int lumo_app_settings_row_at(
    uint32_t width, uint32_t height, double x, double y
) {
    (void)height;
    if (x < PAD || x > (double)width - PAD) return -1;
    if (y < HEADER_Y) return -1;

    int cy = HEADER_Y;

    /* CONNECTIVITY section header */
    cy += SECTION_H;
    /* row 0: Wi-Fi */
    if (y >= cy && y < cy + ROW_H) return 0;
    cy += ROW_H;

    /* DEVICE section header */
    cy += SECTION_H;
    /* rows 1-4: Display, Sound, Storage, Memory */
    for (int i = 1; i <= 4; i++) {
        if (y >= cy && y < cy + ROW_H) return i;
        cy += ROW_H;
    }

    /* SYSTEM section header */
    cy += SECTION_H;
    /* rows 5-8: General, About, Lumo, Processor */
    for (int i = 5; i <= 8; i++) {
        if (y >= cy && y < cy + ROW_H) return i;
        cy += ROW_H;
    }

    return -1;
}

/* toggle hit test for subpages */
int lumo_app_settings_toggle_at(
    uint32_t width, uint32_t height,
    double x, double y, int subpage
) {
    (void)height;
    /* toggles occupy the right portion of the row */
    if (x < (double)width / 2) return -1;

    int base_y = HEADER_Y;
    int info_h = 28;
    int toggle_h = 34;

    switch (subpage) {
    case 0: /* Network: wifi_enabled after 3 info rows */
        if (y >= base_y + info_h * 3 && y < base_y + info_h * 3 + toggle_h)
            return 0;
        break;
    case 1: /* Display: auto_rotate after 3 info rows */
        if (y >= base_y + info_h * 3 && y < base_y + info_h * 3 + toggle_h)
            return 1;
        break;
    case 5: /* General: auto_updates after 3 info rows, persist_logs after */
        if (y >= base_y + info_h * 3 && y < base_y + info_h * 3 + toggle_h)
            return 2;
        if (y >= base_y + info_h * 3 + toggle_h &&
                y < base_y + info_h * 3 + toggle_h * 2)
            return 4;
        break;
    case 7: /* Lumo: debug_mode after 5 info rows */
        if (y >= base_y + info_h * 5 && y < base_y + info_h * 5 + toggle_h)
            return 3;
        break;
    }
    return -1;
}

/* ── subpage renderers ────────────────────────────────────────────── */

static void render_network(
    const struct lumo_app_render_context *ctx,
    uint32_t *px, uint32_t w, uint32_t h
) {
    draw_subheader(px, w, h, "Wi-Fi & Network");
    int y = HEADER_Y;
    char iface[64], status[64], sig[32], ip[32];
    get_wifi_info(iface, sizeof(iface), status, sizeof(status),
                  sig, sizeof(sig), ip, sizeof(ip));

    draw_info(px, w, h, y, "STATUS", status); y += 28;
    draw_info(px, w, h, y, "INTERFACE", iface); y += 28;
    draw_info(px, w, h, y, "SIGNAL", sig); y += 28;
    draw_toggle(px, w, h, y, "WI-FI ENABLED",
        ctx->settings.wifi_enabled); y += 34;
    draw_info(px, w, h, y, "IP ADDRESS", ip);
}

static void render_display(
    const struct lumo_app_render_context *ctx,
    uint32_t *px, uint32_t w, uint32_t h
) {
    draw_subheader(px, w, h, "Display");
    int y = HEADER_Y;
    char buf[64];
    snprintf(buf, sizeof(buf), "%ux%u", w, h);
    draw_info(px, w, h, y, "RESOLUTION", buf); y += 28;
    draw_info(px, w, h, y, "REFRESH", "60 HZ"); y += 28;
    draw_info(px, w, h, y, "RENDERER", "PIXMAN SOFTWARE"); y += 28;
    draw_toggle(px, w, h, y, "AUTO ROTATE",
        ctx->settings.auto_rotate); y += 34;
    draw_info(px, w, h, y, "ROTATION", "USE QUICK SETTINGS");
}

static void render_sound(
    const struct lumo_app_render_context *ctx,
    uint32_t *px, uint32_t w, uint32_t h
) {
    (void)ctx;
    draw_subheader(px, w, h, "Sound");
    int y = HEADER_Y;
    draw_info(px, w, h, y, "OUTPUT", "PIPEWIRE"); y += 28;
    draw_info(px, w, h, y, "VOLUME", "USE QUICK SETTINGS"); y += 28;
    draw_info(px, w, h, y, "DEVICE", "DEFAULT SINK");
}

static void render_storage(
    const struct lumo_app_render_context *ctx,
    uint32_t *px, uint32_t w, uint32_t h
) {
    (void)ctx;
    draw_subheader(px, w, h, "Storage");
    int y = HEADER_Y;
    char buf[64];
    struct statvfs st;
    if (statvfs("/", &st) == 0) {
        unsigned long total_m = (unsigned long)(st.f_blocks *
            (st.f_frsize / 1024)) / 1024;
        unsigned long free_m = (unsigned long)(st.f_bavail *
            (st.f_frsize / 1024)) / 1024;
        unsigned long used_m = total_m - free_m;

        snprintf(buf, sizeof(buf), "%lu MB", total_m);
        draw_info(px, w, h, y, "TOTAL", buf); y += 28;
        snprintf(buf, sizeof(buf), "%lu MB", used_m);
        draw_info(px, w, h, y, "USED", buf); y += 28;
        snprintf(buf, sizeof(buf), "%lu MB", free_m);
        draw_info(px, w, h, y, "FREE", buf); y += 28;
        snprintf(buf, sizeof(buf), "%lu%%",
            total_m > 0 ? (used_m * 100) / total_m : 0);
        draw_info(px, w, h, y, "USAGE", buf); y += 34;
        draw_bar(px, w, h, y, used_m, total_m);
    }
}

static void render_memory(
    const struct lumo_app_render_context *ctx,
    uint32_t *px, uint32_t w, uint32_t h
) {
    (void)ctx;
    draw_subheader(px, w, h, "Memory");
    int y = HEADER_Y;
    char buf[64];
    unsigned long total, avail, buffers, cached;
    get_mem_info(&total, &avail, &buffers, &cached);
    unsigned long used = total > avail ? total - avail : 0;

    snprintf(buf, sizeof(buf), "%lu MB", total / 1024);
    draw_info(px, w, h, y, "TOTAL", buf); y += 28;
    snprintf(buf, sizeof(buf), "%lu MB", used / 1024);
    draw_info(px, w, h, y, "USED", buf); y += 28;
    snprintf(buf, sizeof(buf), "%lu MB", avail / 1024);
    draw_info(px, w, h, y, "AVAILABLE", buf); y += 28;
    snprintf(buf, sizeof(buf), "%lu MB", (buffers + cached) / 1024);
    draw_info(px, w, h, y, "CACHE", buf); y += 34;
    if (total > 0)
        draw_bar(px, w, h, y, used, total);
}

static void render_general(
    const struct lumo_app_render_context *ctx,
    uint32_t *px, uint32_t w, uint32_t h
) {
    draw_subheader(px, w, h, "General");
    int y = HEADER_Y;
    char buf[64];

    FILE *fp = fopen("/proc/uptime", "r");
    if (fp) {
        double up = 0;
        if (fscanf(fp, "%lf", &up) == 1) {
            int d = (int)(up / 86400);
            int hr = (int)((up - d * 86400) / 3600);
            int mn = (int)((up - d * 86400 - hr * 3600) / 60);
            snprintf(buf, sizeof(buf), "%dD %dH %dM", d, hr, mn);
        }
        fclose(fp);
    } else {
        snprintf(buf, sizeof(buf), "UNKNOWN");
    }
    draw_info(px, w, h, y, "UPTIME", buf); y += 28;

    fp = fopen("/proc/loadavg", "r");
    if (fp) {
        char load[64] = "?";
        if (fgets(load, sizeof(load), fp)) {
            char *sp = strchr(load, ' ');
            if (sp) { sp = strchr(sp + 1, ' ');
                if (sp) { sp = strchr(sp + 1, ' ');
                    if (sp) *sp = '\0'; } }
        }
        fclose(fp);
        draw_info(px, w, h, y, "LOAD AVG", load);
    }
    y += 28;

    draw_info(px, w, h, y, "CPU", "4 CORES RISCV64"); y += 28;

    draw_toggle(px, w, h, y, "AUTO UPDATES",
        ctx->settings.auto_updates); y += 34;
    draw_toggle(px, w, h, y, "PERSIST LOGS",
        ctx->settings.persist_logs);
}

static void render_about(
    const struct lumo_app_render_context *ctx,
    uint32_t *px, uint32_t w, uint32_t h
) {
    (void)ctx;
    draw_subheader(px, w, h, "About Device");
    int y = HEADER_Y;
    char hostname[64] = "UNKNOWN";
    gethostname(hostname, sizeof(hostname) - 1);

    char kernel[128] = "UNKNOWN";
    FILE *fp = fopen("/proc/version", "r");
    if (fp) {
        if (fgets(kernel, sizeof(kernel), fp)) {
            char *s = strchr(kernel, ' ');
            if (s) s = strchr(s + 1, ' ');
            if (s) { char *e = strchr(s + 1, ' '); if (e) *e = '\0'; }
            if (s && s[0]) memmove(kernel, s + 1, strlen(s + 1) + 1);
        }
        fclose(fp);
    }

    draw_info(px, w, h, y, "DEVICE", "ORANGEPI RV2"); y += 28;
    draw_info(px, w, h, y, "HOSTNAME", hostname); y += 28;
    draw_info(px, w, h, y, "KERNEL", kernel); y += 28;
    draw_info(px, w, h, y, "ARCH", "RISCV64"); y += 28;
    draw_info(px, w, h, y, "OS", "UBUNTU 24.04");
}

static void render_lumo(
    const struct lumo_app_render_context *ctx,
    uint32_t *px, uint32_t w, uint32_t h
) {
    draw_subheader(px, w, h, "Lumo");
    int y = HEADER_Y;
    draw_info(px, w, h, y, "VERSION", LUMO_VERSION_STRING); y += 28;
    draw_info(px, w, h, y, "BUILD", "MESON + NINJA"); y += 28;
    draw_info(px, w, h, y, "RENDERER", "PIXMAN SOFTWARE"); y += 28;
    draw_info(px, w, h, y, "SHELL", "LAYER-SHELL V1"); y += 28;
    draw_info(px, w, h, y, "APPS", "NATIVE WAYLAND"); y += 28;
    draw_toggle(px, w, h, y, "DEBUG MODE",
        ctx->settings.debug_mode);
}

static void render_processor(
    const struct lumo_app_render_context *ctx,
    uint32_t *px, uint32_t w, uint32_t h
) {
    (void)ctx;
    draw_subheader(px, w, h, "Processor");
    int y = HEADER_Y;
    char cpu[64] = "RISCV64";
    int cores = 0;
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, "processor")) cores++;
            char *val;
            if ((val = strstr(line, "isa")) != NULL) {
                val = strchr(val, ':');
                if (val) { val += 2;
                    char *nl = strchr(val, '\n'); if (nl) *nl = '\0';
                    snprintf(cpu, sizeof(cpu), "%s", val); }
            }
        }
        fclose(fp);
    }
    char buf[32];
    draw_info(px, w, h, y, "ISA", cpu); y += 28;
    snprintf(buf, sizeof(buf), "%d", cores);
    draw_info(px, w, h, y, "CORES", buf); y += 28;
    draw_info(px, w, h, y, "GOVERNOR", "PERFORMANCE"); y += 28;
    draw_info(px, w, h, y, "SOC", "SPACEMIT K1");
}

/* ── main renderer ────────────────────────────────────────────────── */

void lumo_app_render_settings(
    const struct lumo_app_render_context *ctx,
    uint32_t *px, uint32_t w, uint32_t h
) {
    int selected = ctx != NULL ? ctx->selected_row : -1;
    th_loaded = false;

    lumo_app_draw_background(px, w, h);
    ensure_theme();

    /* ── subpage ── */
    if (selected >= 0 && selected <= 8) {
        switch (selected) {
        case 0: render_network(ctx, px, w, h); break;
        case 1: render_display(ctx, px, w, h); break;
        case 2: render_sound(ctx, px, w, h); break;
        case 3: render_storage(ctx, px, w, h); break;
        case 4: render_memory(ctx, px, w, h); break;
        case 5: render_general(ctx, px, w, h); break;
        case 6: render_about(ctx, px, w, h); break;
        case 7: render_lumo(ctx, px, w, h); break;
        case 8: render_processor(ctx, px, w, h); break;
        }
        return;
    }

    /* ── main page ── */
    lumo_app_draw_text(px, w, h, PAD, 16, 2, th.text_dim, "LUMO");
    lumo_app_draw_text(px, w, h, PAD, 42, 3, th.text, "Settings");

    int y = HEADER_Y;
    char buf[128];

    /* ── CONNECTIVITY ── */
    draw_section(px, w, h, y, "CONNECTIVITY");
    y += SECTION_H;
    {
        char iface[64], status[64], sig[32], ip[32];
        get_wifi_info(iface, sizeof(iface), status, sizeof(status),
                      sig, sizeof(sig), ip, sizeof(ip));
        snprintf(buf, sizeof(buf), "%s (%s)", status, iface);
        draw_row(px, w, h, y, "WI-FI", buf);
    }
    y += ROW_H;

    /* ── DEVICE ── */
    draw_section(px, w, h, y, "DEVICE");
    y += SECTION_H;
    snprintf(buf, sizeof(buf), "%ux%u PIXMAN", w, h);
    draw_row(px, w, h, y, "DISPLAY", buf); y += ROW_H;
    draw_row(px, w, h, y, "SOUND", "PIPEWIRE"); y += ROW_H;
    {
        struct statvfs st; char s[32] = "?";
        if (statvfs("/", &st) == 0) {
            unsigned long f = (unsigned long)(st.f_bavail *
                (st.f_frsize / 1024)) / 1024;
            snprintf(s, sizeof(s), "%lu MB FREE", f);
        }
        draw_row(px, w, h, y, "STORAGE", s);
    }
    y += ROW_H;
    {
        unsigned long total, avail, buffers, cached;
        get_mem_info(&total, &avail, &buffers, &cached);
        unsigned long used = total > avail ? total - avail : 0;
        snprintf(buf, sizeof(buf), "%lu / %lu MB", used / 1024, total / 1024);
        draw_row(px, w, h, y, "MEMORY", buf);
    }
    y += ROW_H;

    /* ── SYSTEM ── */
    draw_section(px, w, h, y, "SYSTEM");
    y += SECTION_H;
    {
        char up[32] = "?";
        FILE *fp = fopen("/proc/uptime", "r");
        if (fp) {
            double u = 0;
            if (fscanf(fp, "%lf", &u) == 1) {
                int d = (int)(u / 86400);
                int hr = (int)((u - d * 86400) / 3600);
                int mn = (int)((u - d * 86400 - hr * 3600) / 60);
                if (d > 0)
                    snprintf(up, sizeof(up), "%dD %dH %dM", d, hr, mn);
                else
                    snprintf(up, sizeof(up), "%dH %dM", hr, mn);
            }
            fclose(fp);
        }
        draw_row(px, w, h, y, "GENERAL", up);
    }
    y += ROW_H;
    {
        char hn[32] = "?";
        gethostname(hn, sizeof(hn) - 1);
        draw_row(px, w, h, y, "ABOUT", hn);
    }
    y += ROW_H;
    draw_row(px, w, h, y, "LUMO", LUMO_VERSION_STRING); y += ROW_H;
    {
        int cores = 0;
        FILE *fp = fopen("/proc/cpuinfo", "r");
        if (fp) {
            char line[256];
            while (fgets(line, sizeof(line), fp))
                if (strstr(line, "processor")) cores++;
            fclose(fp);
        }
        snprintf(buf, sizeof(buf), "RISCV64 %d CORES", cores);
        draw_row(px, w, h, y, "PROCESSOR", buf);
    }
}
