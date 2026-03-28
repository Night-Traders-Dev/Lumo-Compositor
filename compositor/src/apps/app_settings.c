#include "lumo/app_render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <unistd.h>

static const int row_h = 48;
static const int header_y = 90;
static const int back_h = 40;

int lumo_app_settings_row_at(
    uint32_t width, uint32_t height, double x, double y
) {
    (void)width; (void)height;
    if (x < 20.0 || x > (double)width - 20.0) return -1;
    if (y < 20.0 && y < (double)back_h) return -99;
    if (y < header_y) return -1;
    int idx = (int)(y - header_y) / row_h;
    if (idx < 0 || idx > 7) return -1;
    return idx;
}

static void draw_row(
    uint32_t *px, uint32_t w, uint32_t h,
    int y, const char *label, const char *value, bool sel
) {
    struct lumo_rect r = {20, y, (int)w - 40, row_h - 4};
    uint32_t fill = lumo_app_argb(0xFF, 0x2C, 0x16, 0x28);
    uint32_t stroke = sel ? lumo_app_argb(0xFF, 0xE9, 0x54, 0x20)
        : lumo_app_argb(0xFF, 0x3E, 0x20, 0x38);
    uint32_t lc = lumo_app_argb(0xFF, 0xFF, 0xFF, 0xFF);
    uint32_t vc = lumo_app_argb(0xFF, 0xAE, 0xA7, 0x9F);

    lumo_app_fill_rounded_rect(px, w, h, &r, 10, fill);
    lumo_app_draw_outline(px, w, h, &r, 1, stroke);
    lumo_app_draw_text(px, w, h, r.x + 16, r.y + 14, 2, lc, label);
    if (value != NULL) {
        int vx = (int)w / 2;
        lumo_app_draw_text(px, w, h, vx, r.y + 14, 2, vc, value);
    }
    lumo_app_draw_text(px, w, h, r.x + r.width - 24, r.y + 14, 2, vc, ">");
}

static void draw_subpage_header(
    uint32_t *px, uint32_t w, uint32_t h,
    const char *title
) {
    uint32_t accent = lumo_app_argb(0xFF, 0xE9, 0x54, 0x20);
    uint32_t white = lumo_app_argb(0xFF, 0xFF, 0xFF, 0xFF);
    uint32_t dim = lumo_app_argb(0xFF, 0xAE, 0xA7, 0x9F);

    lumo_app_draw_text(px, w, h, 20, 16, 2, accent, "< BACK");
    lumo_app_draw_text(px, w, h, 20, 48, 3, white, title);
    lumo_app_fill_rect(px, w, h, 20, 78, (int)w - 40, 1, dim);
}

static void draw_info_row(
    uint32_t *px, uint32_t w, uint32_t h,
    int y, const char *label, const char *value
) {
    uint32_t lc = lumo_app_argb(0xFF, 0xAE, 0xA7, 0x9F);
    uint32_t vc = lumo_app_argb(0xFF, 0xFF, 0xFF, 0xFF);

    lumo_app_draw_text(px, w, h, 28, y, 2, lc, label);
    lumo_app_draw_text(px, w, h, (int)w / 3, y, 2, vc, value);
}

static void draw_toggle(
    uint32_t *px, uint32_t w, uint32_t h,
    int y, const char *label, bool on
) {
    uint32_t lc = lumo_app_argb(0xFF, 0xFF, 0xFF, 0xFF);
    uint32_t on_c = lumo_app_argb(0xFF, 0xE9, 0x54, 0x20);
    uint32_t off_c = lumo_app_argb(0xFF, 0x5E, 0x2C, 0x56);
    struct lumo_rect track = {(int)w - 80, y + 2, 40, 16};

    lumo_app_draw_text(px, w, h, 28, y, 2, lc, label);
    lumo_app_fill_rounded_rect(px, w, h, &track, 8, on ? on_c : off_c);
    {
        struct lumo_rect knob = {
            on ? track.x + 24 : track.x + 2,
            track.y + 2, 12, 12
        };
        lumo_app_fill_rounded_rect(px, w, h, &knob, 6,
            lumo_app_argb(0xFF, 0xFF, 0xFF, 0xFF));
    }
}

static void render_subpage(
    const struct lumo_app_render_context *ctx,
    uint32_t *px, uint32_t w, uint32_t h,
    int page
) {
    int y = header_y;
    char buf[128];

    switch (page) {
    case 0: /* Network */
        draw_subpage_header(px, w, h, "Network");
        {
            char wifi[64] = "NOT CONNECTED";
            float sig = 0;
            FILE *fp = fopen("/proc/net/wireless", "r");
            if (fp) {
                char line[256]; char ifn[32] = {0};
                while (fgets(line, sizeof(line), fp))
                    if (sscanf(line, " %31[^:]: %*d %*f %f", ifn, &sig) >= 1 &&
                            ifn[0] && ifn[0] != '|') {
                        snprintf(wifi, sizeof(wifi), "%s", ifn); break;
                    }
                fclose(fp);
            }
            draw_info_row(px, w, h, y, "INTERFACE", wifi); y += 24;
            snprintf(buf, sizeof(buf), "%.0f DBM", sig);
            draw_info_row(px, w, h, y, "SIGNAL", buf); y += 24;
            draw_toggle(px, w, h, y, "WI-FI ENABLED", true); y += 30;
            draw_info_row(px, w, h, y, "IP ADDRESS", "DHCP"); y += 24;
        }
        break;

    case 1: /* Display */
        draw_subpage_header(px, w, h, "Display");
        snprintf(buf, sizeof(buf), "%ux%u", w, h);
        draw_info_row(px, w, h, y, "RESOLUTION", buf); y += 24;
        draw_info_row(px, w, h, y, "REFRESH", "60 HZ"); y += 24;
        draw_info_row(px, w, h, y, "RENDERER", "PIXMAN"); y += 24;
        draw_toggle(px, w, h, y, "AUTO ROTATE", false); y += 30;
        draw_info_row(px, w, h, y, "ROTATION", "USE QUICK SETTINGS"); y += 24;
        break;

    case 2: /* Storage */
        draw_subpage_header(px, w, h, "Storage");
        {
            struct statvfs st;
            if (statvfs("/", &st) == 0) {
                unsigned long free_m = (unsigned long)(st.f_bavail *
                    (st.f_frsize / 1024)) / 1024;
                unsigned long total_m = (unsigned long)(st.f_blocks *
                    (st.f_frsize / 1024)) / 1024;
                unsigned long used_m = total_m - free_m;
                snprintf(buf, sizeof(buf), "%lu MB", total_m);
                draw_info_row(px, w, h, y, "TOTAL", buf); y += 24;
                snprintf(buf, sizeof(buf), "%lu MB", used_m);
                draw_info_row(px, w, h, y, "USED", buf); y += 24;
                snprintf(buf, sizeof(buf), "%lu MB", free_m);
                draw_info_row(px, w, h, y, "FREE", buf); y += 24;
                snprintf(buf, sizeof(buf), "%lu%%", (used_m * 100) / total_m);
                draw_info_row(px, w, h, y, "USAGE", buf); y += 30;

                {
                    struct lumo_rect bar = {28, y, (int)w - 56, 14};
                    struct lumo_rect used_bar = {28, y, (int)((w - 56) * used_m / total_m), 14};
                    lumo_app_fill_rounded_rect(px, w, h, &bar, 7,
                        lumo_app_argb(0xFF, 0x3E, 0x20, 0x38));
                    lumo_app_fill_rounded_rect(px, w, h, &used_bar, 7,
                        lumo_app_argb(0xFF, 0xE9, 0x54, 0x20));
                }
            }
        }
        break;

    case 3: /* Memory */
        draw_subpage_header(px, w, h, "Memory");
        {
            FILE *fp = fopen("/proc/meminfo", "r");
            unsigned long total = 0, avail = 0, buffers = 0, cached = 0;
            if (fp) {
                char line[128];
                while (fgets(line, sizeof(line), fp)) {
                    sscanf(line, "MemTotal: %lu", &total);
                    sscanf(line, "MemAvailable: %lu", &avail);
                    sscanf(line, "Buffers: %lu", &buffers);
                    sscanf(line, "Cached: %lu", &cached);
                }
                fclose(fp);
            }
            snprintf(buf, sizeof(buf), "%lu MB", total / 1024);
            draw_info_row(px, w, h, y, "TOTAL", buf); y += 24;
            snprintf(buf, sizeof(buf), "%lu MB", avail / 1024);
            draw_info_row(px, w, h, y, "AVAILABLE", buf); y += 24;
            snprintf(buf, sizeof(buf), "%lu MB", (total - avail) / 1024);
            draw_info_row(px, w, h, y, "USED", buf); y += 24;
            snprintf(buf, sizeof(buf), "%lu MB", (buffers + cached) / 1024);
            draw_info_row(px, w, h, y, "CACHE", buf); y += 30;

            if (total > 0) {
                struct lumo_rect bar = {28, y, (int)w - 56, 14};
                unsigned long used = total - avail;
                struct lumo_rect used_bar = {28, y,
                    (int)((w - 56) * used / total), 14};
                lumo_app_fill_rounded_rect(px, w, h, &bar, 7,
                    lumo_app_argb(0xFF, 0x3E, 0x20, 0x38));
                lumo_app_fill_rounded_rect(px, w, h, &used_bar, 7,
                    lumo_app_argb(0xFF, 0xE9, 0x54, 0x20));
            }
        }
        break;

    case 4: /* System */
        draw_subpage_header(px, w, h, "System");
        {
            char uptime[64] = "UNKNOWN";
            char load[64] = "UNKNOWN";
            FILE *fp = fopen("/proc/uptime", "r");
            if (fp) {
                double up = 0;
                if (fscanf(fp, "%lf", &up) == 1) {
                    int d = (int)(up / 86400);
                    int h2 = (int)((up - d * 86400) / 3600);
                    int m = (int)((up - d * 86400 - h2 * 3600) / 60);
                    snprintf(uptime, sizeof(uptime), "%dD %dH %dM", d, h2, m);
                }
                fclose(fp);
            }
            fp = fopen("/proc/loadavg", "r");
            if (fp) {
                if (fgets(load, sizeof(load), fp)) {
                    char *sp = strchr(load, ' ');
                    if (sp) { sp = strchr(sp + 1, ' ');
                        if (sp) { sp = strchr(sp + 1, ' ');
                            if (sp) *sp = '\0';
                        }
                    }
                }
                fclose(fp);
            }
            draw_info_row(px, w, h, y, "UPTIME", uptime); y += 24;
            draw_info_row(px, w, h, y, "LOAD AVG", load); y += 24;
            draw_toggle(px, w, h, y, "AUTO UPDATES", false); y += 30;
        }
        break;

    case 5: /* About */
        draw_subpage_header(px, w, h, "About");
        {
            char hostname[64] = "UNKNOWN";
            char kernel[128] = "UNKNOWN";
            gethostname(hostname, sizeof(hostname) - 1);
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
            draw_info_row(px, w, h, y, "HOSTNAME", hostname); y += 24;
            draw_info_row(px, w, h, y, "KERNEL", kernel); y += 24;
            draw_info_row(px, w, h, y, "DEVICE", "ORANGEPI RV2"); y += 24;
            draw_info_row(px, w, h, y, "ARCH", "RISCV64"); y += 24;
        }
        break;

    case 6: /* Lumo */
        draw_subpage_header(px, w, h, "Lumo");
        draw_info_row(px, w, h, y, "VERSION", "0.0.51"); y += 24;
        draw_info_row(px, w, h, y, "BUILD", "MESON + NINJA"); y += 24;
        draw_info_row(px, w, h, y, "RENDERER", "PIXMAN SOFTWARE"); y += 24;
        draw_info_row(px, w, h, y, "SHELL", "LAYER-SHELL V1"); y += 24;
        draw_info_row(px, w, h, y, "APPS", "NATIVE WAYLAND"); y += 24;
        draw_toggle(px, w, h, y, "DEBUG MODE", false); y += 30;
        break;

    case 7: /* CPU */
        draw_subpage_header(px, w, h, "Processor");
        {
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
            draw_info_row(px, w, h, y, "ISA", cpu); y += 24;
            snprintf(buf, sizeof(buf), "%d", cores);
            draw_info_row(px, w, h, y, "CORES", buf); y += 24;
            draw_info_row(px, w, h, y, "GOVERNOR", "PERFORMANCE"); y += 24;
        }
        break;
    }

    (void)ctx;
}

void lumo_app_render_settings(
    const struct lumo_app_render_context *ctx,
    uint32_t *px, uint32_t w, uint32_t h
) {
    bool close_active = ctx != NULL ? ctx->close_active : false;
    int selected = ctx != NULL ? ctx->selected_row : -1;
    char buf[128];

    lumo_app_draw_background(px, w, h);

    if (selected >= 0 && selected <= 7) {
        render_subpage(ctx, px, w, h, selected);
        lumo_app_draw_close_button(px, w, h, close_active);
        return;
    }

    lumo_app_draw_text(px, w, h, 20, 20, 2,
        lumo_app_argb(0xFF, 0xAE, 0xA7, 0x9F), "SETTINGS");
    lumo_app_draw_text(px, w, h, 20, 48, 3,
        lumo_app_argb(0xFF, 0xFF, 0xFF, 0xFF), "System");

    {
        int y = header_y;
        char wifi[32] = "OFF";
        FILE *fp = fopen("/proc/net/wireless", "r");
        if (fp) { char l[256]; char ifn[32] = {0};
            while (fgets(l, sizeof(l), fp))
                if (sscanf(l, " %31[^:]:", ifn) == 1 && ifn[0] && ifn[0] != '|')
                    { snprintf(wifi, sizeof(wifi), "%s", ifn); break; }
            fclose(fp); }
        draw_row(px, w, h, y, "NETWORK", wifi, false); y += row_h;

        snprintf(buf, sizeof(buf), "%ux%u", w, h);
        draw_row(px, w, h, y, "DISPLAY", buf, false); y += row_h;

        { struct statvfs st; char s[32] = "?";
            if (statvfs("/", &st) == 0) {
                unsigned long f = (unsigned long)(st.f_bavail*(st.f_frsize/1024))/1024;
                snprintf(s, sizeof(s), "%lu MB FREE", f); }
            draw_row(px, w, h, y, "STORAGE", s, false); } y += row_h;

        { unsigned long t=0,a=0; FILE *mf = fopen("/proc/meminfo","r");
            if (mf) { char ln[128];
                while (fgets(ln,sizeof(ln),mf)) {
                    sscanf(ln,"MemTotal: %lu",&t); sscanf(ln,"MemAvailable: %lu",&a); }
                fclose(mf); }
            snprintf(buf,sizeof(buf),"%lu/%lu MB",a/1024,t/1024);
            draw_row(px,w,h,y,"MEMORY",buf,false); } y += row_h;

        { char up[32]="?"; FILE *uf=fopen("/proc/uptime","r");
            if(uf){double u=0;if(fscanf(uf,"%lf",&u)==1){
                int hr=(int)(u/3600),mn=(int)((u-hr*3600)/60);
                snprintf(up,sizeof(up),"%dH %dM",hr,mn);}fclose(uf);}
            draw_row(px,w,h,y,"SYSTEM",up,false); } y += row_h;

        { char hn[32]="?"; gethostname(hn,sizeof(hn)-1);
            draw_row(px,w,h,y,"ABOUT",hn,false); } y += row_h;

        draw_row(px, w, h, y, "LUMO", "0.0.51", false); y += row_h;
        draw_row(px, w, h, y, "CPU", "RISCV64", false);
    }

    lumo_app_draw_close_button(px, w, h, close_active);
}
