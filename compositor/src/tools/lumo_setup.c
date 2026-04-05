/*
 * lumo_setup.c — First-boot setup wizard for Lumo OS.
 *
 * Standalone Wayland client that runs on first boot to configure:
 *   1. Welcome + language
 *   2. Username + password
 *   3. WiFi connection
 *   4. Timezone
 *   5. Complete + reboot
 *
 * Renders directly to an xdg_toplevel surface with SHM buffers.
 * Designed for the OrangePi RV2 7" touchscreen (800x1280).
 */
#define _DEFAULT_SOURCE
#include "lumo/app_render.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

/* ── setup state ─────────────────────────────────────────────────── */

#define SETUP_PAGES 5

enum setup_page {
    SETUP_WELCOME = 0,
    SETUP_USER,
    SETUP_WIFI,
    SETUP_TIMEZONE,
    SETUP_COMPLETE,
};

static struct {
    enum setup_page page;
    char username[32];
    int username_len;
    char password[32];
    int password_len;
    int editing_field;  /* 0=username, 1=password */
    /* wifi */
    char wifi_ssids[16][64];
    char wifi_signals[16][8];
    char wifi_security[16][16];
    int wifi_count;
    int wifi_selected;
    char wifi_password[64];
    int wifi_password_len;
    bool wifi_connecting;
    bool wifi_editing_pw;
    /* timezone */
    int tz_selected;
    /* state */
    bool running;
    uint32_t width, height;
} setup = {
    .page = SETUP_WELCOME,
    .editing_field = 0,
    .wifi_selected = -1,
    .tz_selected = 8,  /* default: America/New_York = index 8 */
    .running = true,
    .width = 1280,
    .height = 800,
};

/* common timezones */
static const char *timezones[] = {
    "Pacific/Honolulu",    /* UTC-10 */
    "America/Anchorage",   /* UTC-9  */
    "America/Los_Angeles", /* UTC-8  */
    "America/Denver",      /* UTC-7  */
    "America/Chicago",     /* UTC-6  */
    "America/New_York",    /* UTC-5  */
    "America/Halifax",     /* UTC-4  */
    "America/Sao_Paulo",   /* UTC-3  */
    "Europe/London",       /* UTC+0  */
    "Europe/Paris",        /* UTC+1  */
    "Europe/Helsinki",     /* UTC+2  */
    "Asia/Kolkata",        /* UTC+5:30 */
    "Asia/Shanghai",       /* UTC+8  */
    "Asia/Tokyo",          /* UTC+9  */
    "Australia/Sydney",    /* UTC+11 */
};
#define TZ_COUNT 15

static const char *tz_labels[] = {
    "HAWAII (UTC-10)", "ALASKA (UTC-9)", "PACIFIC (UTC-8)",
    "MOUNTAIN (UTC-7)", "CENTRAL (UTC-6)", "EASTERN (UTC-5)",
    "ATLANTIC (UTC-4)", "BRAZIL (UTC-3)", "LONDON (UTC+0)",
    "PARIS (UTC+1)", "HELSINKI (UTC+2)", "INDIA (UTC+5:30)",
    "CHINA (UTC+8)", "JAPAN (UTC+9)", "SYDNEY (UTC+11)",
};

/* ── WiFi scanning ───────────────────────────────────────────────── */

static void wifi_scan(void) {
    setup.wifi_count = 0;
    FILE *fp = popen(
        "nmcli -t -f SSID,SIGNAL,SECURITY dev wifi list 2>/dev/null"
        " | head -16", "r");
    if (!fp) return;
    char line[256];
    while (fgets(line, sizeof(line), fp) && setup.wifi_count < 16) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *p = line;
        /* SSID */
        char *sep = strchr(p, ':');
        if (!sep) continue;
        *sep = '\0';
        if (p[0] == '\0') { p = sep + 1; continue; }
        snprintf(setup.wifi_ssids[setup.wifi_count], 64, "%s", p);
        p = sep + 1;
        /* SIGNAL */
        sep = strchr(p, ':');
        if (!sep) continue;
        *sep = '\0';
        snprintf(setup.wifi_signals[setup.wifi_count], 8, "%s", p);
        p = sep + 1;
        /* SECURITY */
        snprintf(setup.wifi_security[setup.wifi_count], 16, "%s", p);
        setup.wifi_count++;
    }
    pclose(fp);
}

/* ── render ──────────────────────────────────────────────────────── */

static void render_setup(uint32_t *pixels) {
    struct lumo_app_theme theme;
    lumo_app_theme_get(&theme);
    uint32_t w = setup.width, h = setup.height;
    int iw = (int)w, ih = (int)h;

    /* background */
    struct lumo_rect full = {0, 0, iw, ih};
    lumo_app_fill_gradient(pixels, w, h, &full, theme.header_bg, theme.bg);

    /* page indicator dots at top */
    int dot_y = 16;
    int dot_gap = 24;
    int dots_w = SETUP_PAGES * dot_gap;
    for (int i = 0; i < SETUP_PAGES; i++) {
        struct lumo_rect dot = {iw / 2 - dots_w / 2 + i * dot_gap, dot_y,
            8, 8};
        lumo_app_fill_rounded_rect(pixels, w, h, &dot, 4,
            i == (int)setup.page ? theme.accent
                : lumo_app_argb(0x40, 0xFF, 0xFF, 0xFF));
    }

    int y = 48;
    int pad = 32;
    int center_x = iw / 2;
    char buf[128];

    switch (setup.page) {
    case SETUP_WELCOME:
        /* Lumo OS logo */
        lumo_app_draw_text(pixels, w, h, center_x - 72, y + 40, 6,
            theme.accent, "LUMO");
        lumo_app_draw_text(pixels, w, h, center_x - 24, y + 90, 3,
            theme.text_dim, "OS");
        lumo_app_draw_text(pixels, w, h, center_x - 108, y + 140, 2,
            theme.text, "WELCOME TO LUMO OS");
        lumo_app_draw_text(pixels, w, h, center_x - 150, y + 170, 2,
            theme.text_dim, "TOUCH-FIRST WAYLAND COMPOSITOR");
        lumo_app_draw_text(pixels, w, h, center_x - 120, y + 200, 2,
            theme.text_dim, "FOR ORANGEPI RV2 (RISC-V)");

        /* next button */
        {
            struct lumo_rect btn = {center_x - 80, ih - 80, 160, 44};
            lumo_app_fill_rounded_rect(pixels, w, h, &btn, 22, theme.accent);
            lumo_app_draw_text_centered(pixels, w, h, &btn, 3,
                theme.text, "GET STARTED");
        }
        break;

    case SETUP_USER:
        lumo_app_draw_text(pixels, w, h, pad, y, 3, theme.accent,
            "CREATE YOUR ACCOUNT");
        y += 36;
        lumo_app_draw_text(pixels, w, h, pad, y, 2, theme.text_dim,
            "CHOOSE A USERNAME AND PASSWORD");
        y += 32;

        /* username field */
        {
            struct lumo_rect field = {pad, y, iw - pad * 2, 44};
            lumo_app_fill_rounded_rect(pixels, w, h, &field, 10,
                setup.editing_field == 0 ? theme.card_bg : theme.bg);
            lumo_app_draw_outline(pixels, w, h, &field, 2,
                setup.editing_field == 0 ? theme.accent : theme.card_stroke);
            lumo_app_draw_text(pixels, w, h, pad + 8, y - 14, 2,
                theme.text_dim, "USERNAME");
            const char *utext = setup.username_len > 0
                ? setup.username : "TYPE A USERNAME...";
            lumo_app_draw_text(pixels, w, h, pad + 16, y + 14, 2,
                setup.username_len > 0 ? theme.text : theme.text_dim, utext);
            /* cursor */
            if (setup.editing_field == 0) {
                struct timespec ts;
                clock_gettime(CLOCK_MONOTONIC, &ts);
                if ((ts.tv_nsec / 500000000) == 0) {
                    int cx = pad + 16 + setup.username_len * 12;
                    lumo_app_fill_rect(pixels, w, h, cx, y + 12, 2, 20,
                        theme.accent);
                }
            }
        }
        y += 60;

        /* password field */
        {
            struct lumo_rect field = {pad, y, iw - pad * 2, 44};
            lumo_app_fill_rounded_rect(pixels, w, h, &field, 10,
                setup.editing_field == 1 ? theme.card_bg : theme.bg);
            lumo_app_draw_outline(pixels, w, h, &field, 2,
                setup.editing_field == 1 ? theme.accent : theme.card_stroke);
            lumo_app_draw_text(pixels, w, h, pad + 8, y - 14, 2,
                theme.text_dim, "PASSWORD");
            /* show dots for password */
            if (setup.password_len > 0) {
                char dots[33];
                int dl = setup.password_len > 32 ? 32 : setup.password_len;
                for (int i = 0; i < dl; i++) dots[i] = '*';
                dots[dl] = '\0';
                lumo_app_draw_text(pixels, w, h, pad + 16, y + 14, 2,
                    theme.text, dots);
            } else {
                lumo_app_draw_text(pixels, w, h, pad + 16, y + 14, 2,
                    theme.text_dim, "TYPE A PASSWORD...");
            }
            if (setup.editing_field == 1) {
                struct timespec ts;
                clock_gettime(CLOCK_MONOTONIC, &ts);
                if ((ts.tv_nsec / 500000000) == 0) {
                    int cx = pad + 16 + setup.password_len * 12;
                    lumo_app_fill_rect(pixels, w, h, cx, y + 12, 2, 20,
                        theme.accent);
                }
            }
        }
        y += 60;

        /* tap field to switch */
        lumo_app_draw_text(pixels, w, h, pad, y, 2, theme.text_dim,
            "TAP FIELD TO SELECT, USE OSK TO TYPE");

        /* next button */
        {
            bool valid = setup.username_len >= 2 && setup.password_len >= 2;
            struct lumo_rect btn = {center_x - 60, ih - 80, 120, 44};
            lumo_app_fill_rounded_rect(pixels, w, h, &btn, 22,
                valid ? theme.accent : theme.card_bg);
            lumo_app_draw_text_centered(pixels, w, h, &btn, 3,
                valid ? theme.text : theme.text_dim, "NEXT");
        }
        break;

    case SETUP_WIFI:
        lumo_app_draw_text(pixels, w, h, pad, y, 3, theme.accent,
            "CONNECT TO WI-FI");
        y += 36;

        if (setup.wifi_count == 0) {
            lumo_app_draw_text(pixels, w, h, pad, y, 2, theme.text_dim,
                "SCANNING FOR NETWORKS...");
        }

        for (int i = 0; i < setup.wifi_count && y + 44 < ih - 100; i++) {
            struct lumo_rect row = {pad, y, iw - pad * 2, 38};
            bool selected = (i == setup.wifi_selected);
            lumo_app_fill_rounded_rect(pixels, w, h, &row, 8,
                selected ? theme.accent : theme.card_bg);
            lumo_app_draw_outline(pixels, w, h, &row, 1, theme.card_stroke);

            lumo_app_draw_text(pixels, w, h, pad + 12, y + 4, 2,
                selected ? theme.bg : theme.text,
                setup.wifi_ssids[i]);
            snprintf(buf, sizeof(buf), "%s%%  %s",
                setup.wifi_signals[i], setup.wifi_security[i]);
            lumo_app_draw_text(pixels, w, h, pad + 12, y + 22, 2,
                selected ? theme.bg : theme.text_dim, buf);
            y += 44;
        }

        /* WiFi password field (shown when a secured network is selected) */
        if (setup.wifi_selected >= 0 && setup.wifi_editing_pw) {
            y += 8;
            struct lumo_rect field = {pad, y, iw - pad * 2, 44};
            lumo_app_fill_rounded_rect(pixels, w, h, &field, 10,
                theme.card_bg);
            lumo_app_draw_outline(pixels, w, h, &field, 2, theme.accent);
            lumo_app_draw_text(pixels, w, h, pad + 8, y - 14, 2,
                theme.text_dim, "WI-FI PASSWORD");
            if (setup.wifi_password_len > 0) {
                char dots[65];
                int dl = setup.wifi_password_len > 64
                    ? 64 : setup.wifi_password_len;
                for (int i = 0; i < dl; i++) dots[i] = '*';
                dots[dl] = '\0';
                lumo_app_draw_text(pixels, w, h, pad + 16, y + 14, 2,
                    theme.text, dots);
            } else {
                lumo_app_draw_text(pixels, w, h, pad + 16, y + 14, 2,
                    theme.text_dim, "ENTER PASSWORD...");
            }
        }

        /* buttons */
        {
            struct lumo_rect skip = {pad, ih - 80, 100, 44};
            lumo_app_fill_rounded_rect(pixels, w, h, &skip, 22,
                theme.card_bg);
            lumo_app_draw_text_centered(pixels, w, h, &skip, 3,
                theme.text_dim, "SKIP");
        }
        if (setup.wifi_selected >= 0) {
            struct lumo_rect conn = {center_x - 60, ih - 80, 120, 44};
            lumo_app_fill_rounded_rect(pixels, w, h, &conn, 22,
                theme.accent);
            lumo_app_draw_text_centered(pixels, w, h, &conn, 3,
                theme.text,
                setup.wifi_connecting ? "..." : "CONNECT");
        }
        break;

    case SETUP_TIMEZONE:
        lumo_app_draw_text(pixels, w, h, pad, y, 3, theme.accent,
            "SELECT TIMEZONE");
        y += 36;

        for (int i = 0; i < TZ_COUNT && y + 36 < ih - 100; i++) {
            struct lumo_rect row = {pad, y, iw - pad * 2, 30};
            bool selected = (i == setup.tz_selected);
            lumo_app_fill_rounded_rect(pixels, w, h, &row, 6,
                selected ? theme.accent : theme.card_bg);
            lumo_app_draw_text_centered(pixels, w, h, &row, 2,
                selected ? theme.bg : theme.text, tz_labels[i]);
            y += 34;
        }

        {
            struct lumo_rect btn = {center_x - 60, ih - 80, 120, 44};
            lumo_app_fill_rounded_rect(pixels, w, h, &btn, 22,
                theme.accent);
            lumo_app_draw_text_centered(pixels, w, h, &btn, 3,
                theme.text, "NEXT");
        }
        break;

    case SETUP_COMPLETE:
        lumo_app_draw_text(pixels, w, h, center_x - 96, y + 40, 6,
            theme.accent, "READY");
        lumo_app_draw_text(pixels, w, h, center_x - 132, y + 110, 2,
            theme.text, "LUMO OS IS CONFIGURED");
        y = ih / 2 - 20;
        snprintf(buf, sizeof(buf), "USER: %s", setup.username);
        lumo_app_draw_text(pixels, w, h, center_x - 90, y, 2,
            theme.text_dim, buf);
        y += 24;
        if (setup.tz_selected >= 0 && setup.tz_selected < TZ_COUNT) {
            snprintf(buf, sizeof(buf), "TZ: %s", timezones[setup.tz_selected]);
            lumo_app_draw_text(pixels, w, h, center_x - 90, y, 2,
                theme.text_dim, buf);
        }
        y += 24;
        if (setup.wifi_selected >= 0 && setup.wifi_selected < setup.wifi_count) {
            snprintf(buf, sizeof(buf), "WIFI: %s",
                setup.wifi_ssids[setup.wifi_selected]);
            lumo_app_draw_text(pixels, w, h, center_x - 90, y, 2,
                theme.text_dim, buf);
        }

        {
            struct lumo_rect btn = {center_x - 80, ih - 80, 160, 44};
            lumo_app_fill_rounded_rect(pixels, w, h, &btn, 22,
                theme.accent);
            lumo_app_draw_text_centered(pixels, w, h, &btn, 3,
                theme.text, "FINISH SETUP");
        }
        break;
    }
}

/* ── apply configuration ─────────────────────────────────────────── */

static void apply_setup(void) {
    char cmd[512];

    /* create user */
    if (setup.username_len > 0) {
        snprintf(cmd, sizeof(cmd),
            "useradd -m -s /bin/bash -G sudo,video,audio,input,render '%s' "
            "2>/dev/null || true", setup.username);
        system(cmd);
        snprintf(cmd, sizeof(cmd), "echo '%s:%s' | chpasswd",
            setup.username, setup.password);
        system(cmd);
        snprintf(cmd, sizeof(cmd),
            "echo '%s ALL=(ALL) NOPASSWD: ALL' > /etc/sudoers.d/%s",
            setup.username, setup.username);
        system(cmd);
    }

    /* set timezone */
    if (setup.tz_selected >= 0 && setup.tz_selected < TZ_COUNT) {
        snprintf(cmd, sizeof(cmd), "timedatectl set-timezone '%s' 2>/dev/null",
            timezones[setup.tz_selected]);
        system(cmd);
    }

    /* configure GDM auto-login */
    if (setup.username_len > 0) {
        FILE *fp = fopen("/etc/gdm3/custom.conf", "w");
        if (fp) {
            fprintf(fp,
                "[daemon]\n"
                "AutomaticLoginEnable = true\n"
                "AutomaticLogin = %s\n"
                "WaylandEnable = true\n"
                "DefaultSession = lumo.desktop\n\n"
                "[security]\n\n[xdmcp]\n\n[chooser]\n\n[debug]\n",
                setup.username);
            fclose(fp);
        }

        /* set home directory ownership */
        snprintf(cmd, sizeof(cmd), "chown -R '%s:%s' '/home/%s'",
            setup.username, setup.username, setup.username);
        system(cmd);

        /* copy Lumo source if it exists */
        snprintf(cmd, sizeof(cmd),
            "test -d /home/lumo/Lumo-Compositor && "
            "cp -r /home/lumo/Lumo-Compositor '/home/%s/' && "
            "chown -R '%s:%s' '/home/%s/Lumo-Compositor' 2>/dev/null || true",
            setup.username, setup.username, setup.username, setup.username);
        system(cmd);
    }

    /* mark setup complete */
    FILE *fp = fopen("/etc/lumo-setup-complete", "w");
    if (fp) {
        time_t now = time(NULL);
        fprintf(fp, "setup_complete=%ld\nuser=%s\n", now, setup.username);
        fclose(fp);
    }
}

/* ── touch handling ──────────────────────────────────────────────── */

static void handle_tap(double x, double y) {
    int iw = (int)setup.width, ih = (int)setup.height;
    int pad = 32;
    int center_x = iw / 2;

    switch (setup.page) {
    case SETUP_WELCOME:
        /* "GET STARTED" button */
        if (y >= ih - 80 && y < ih - 36 &&
                x >= center_x - 80 && x < center_x + 80) {
            setup.page = SETUP_USER;
        }
        break;

    case SETUP_USER:
        /* username field tap */
        if (y >= 116 && y < 160) setup.editing_field = 0;
        /* password field tap */
        else if (y >= 176 && y < 220) setup.editing_field = 1;
        /* next button */
        else if (y >= ih - 80 && y < ih - 36 &&
                setup.username_len >= 2 && setup.password_len >= 2) {
            setup.page = SETUP_WIFI;
            wifi_scan();
        }
        break;

    case SETUP_WIFI:
        /* skip button */
        if (y >= ih - 80 && y < ih - 36 && x < pad + 100) {
            setup.page = SETUP_TIMEZONE;
            break;
        }
        /* connect button */
        if (y >= ih - 80 && y < ih - 36 &&
                x >= center_x - 60 && setup.wifi_selected >= 0) {
            /* try connecting */
            char cmd[512];
            if (setup.wifi_password_len > 0) {
                snprintf(cmd, sizeof(cmd),
                    "nmcli dev wifi connect '%s' password '%s' 2>&1",
                    setup.wifi_ssids[setup.wifi_selected],
                    setup.wifi_password);
            } else {
                snprintf(cmd, sizeof(cmd),
                    "nmcli dev wifi connect '%s' 2>&1",
                    setup.wifi_ssids[setup.wifi_selected]);
            }
            system(cmd);
            setup.page = SETUP_TIMEZONE;
            break;
        }
        /* network row tap */
        {
            int row_y = 84;
            for (int i = 0; i < setup.wifi_count; i++) {
                if (y >= row_y && y < row_y + 38) {
                    setup.wifi_selected = i;
                    /* check if network needs password */
                    if (setup.wifi_security[i][0] != '\0' &&
                            strcmp(setup.wifi_security[i], "--") != 0) {
                        setup.wifi_editing_pw = true;
                    } else {
                        setup.wifi_editing_pw = false;
                    }
                    break;
                }
                row_y += 44;
            }
        }
        break;

    case SETUP_TIMEZONE:
        /* timezone row tap */
        {
            int row_y = 84;
            for (int i = 0; i < TZ_COUNT; i++) {
                if (y >= row_y && y < row_y + 30) {
                    setup.tz_selected = i;
                    break;
                }
                row_y += 34;
            }
        }
        /* next button */
        if (y >= ih - 80 && y < ih - 36) {
            setup.page = SETUP_COMPLETE;
        }
        break;

    case SETUP_COMPLETE:
        /* finish button */
        if (y >= ih - 80 && y < ih - 36) {
            apply_setup();
            setup.running = false;
            /* reboot */
            system("systemctl reboot");
        }
        break;
    }
    (void)x;
}

/* ── keyboard input (for username/password) ──────────────────────── */

static void handle_key(char ch) {
    if (setup.page == SETUP_USER) {
        char *field;
        int *len;
        int max;
        if (setup.editing_field == 0) {
            field = setup.username; len = &setup.username_len; max = 30;
        } else {
            field = setup.password; len = &setup.password_len; max = 30;
        }
        if (ch == '\b' || ch == 0x7f) {
            if (*len > 0) field[--(*len)] = '\0';
        } else if (ch >= 0x20 && ch < 0x7f && *len < max) {
            /* lowercase only for username */
            if (setup.editing_field == 0 && ch >= 'A' && ch <= 'Z')
                ch += 32;
            field[(*len)++] = ch;
            field[*len] = '\0';
        }
    } else if (setup.page == SETUP_WIFI && setup.wifi_editing_pw) {
        if (ch == '\b' || ch == 0x7f) {
            if (setup.wifi_password_len > 0)
                setup.wifi_password[--setup.wifi_password_len] = '\0';
        } else if (ch >= 0x20 && ch < 0x7f &&
                setup.wifi_password_len < 62) {
            setup.wifi_password[setup.wifi_password_len++] = ch;
            setup.wifi_password[setup.wifi_password_len] = '\0';
        }
    }
}

/* ── Wayland client (minimal xdg-shell) ──────────────────────────── */
/* NOTE: This is a simplified client. In the actual Lumo OS image,
 * lumo-setup runs as a fullscreen Wayland client under the compositor.
 * For now, this file provides the setup logic and rendering.
 * The actual Wayland plumbing reuses the app_client infrastructure
 * by building lumo-setup as a mode of lumo-app. */

/* app render entry point (matches lumo_app_render_* signature) */
void lumo_app_render_setup(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
) {
    (void)ctx;
    setup.width = width;
    setup.height = height;
    render_setup(pixels);
}

/* export the render and tap functions for app_client integration */
void lumo_setup_render(uint32_t *pixels, uint32_t width, uint32_t height) {
    setup.width = width;
    setup.height = height;
    render_setup(pixels);
}

void lumo_setup_handle_tap(double x, double y) {
    handle_tap(x, y);
}

void lumo_setup_handle_key(char ch) {
    handle_key(ch);
}

bool lumo_setup_is_complete(void) {
    return !setup.running;
}

bool lumo_setup_needs_run(void) {
    return access("/etc/lumo-setup-complete", F_OK) != 0;
}

int lumo_setup_page(void) {
    return (int)setup.page;
}

void lumo_setup_wifi_scan(void) {
    wifi_scan();
}
