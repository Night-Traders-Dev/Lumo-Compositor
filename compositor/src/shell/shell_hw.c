/*
 * shell_hw.c — hardware/platform services for the Lumo compositor.
 * Split from shell_launch.c for maintainability.
 *
 * Contains brightness, volume, screenshot, boot sound, and weather.
 */

#define _DEFAULT_SOURCE
#include "shell_launch_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* --- volume / brightness helpers --- */

uint32_t lumo_read_brightness_pct(void) {
    FILE *fp;
    int cur = 0, max = 255;
    fp = fopen("/sys/class/backlight/soc:lcd_backlight/brightness", "r");
    if (fp) { if (fscanf(fp, "%d", &cur) < 1) cur = 0; fclose(fp); }
    fp = fopen("/sys/class/backlight/soc:lcd_backlight/max_brightness", "r");
    if (fp) { if (fscanf(fp, "%d", &max) < 1) max = 255; fclose(fp); }
    if (max <= 0) max = 255;
    return (uint32_t)(cur * 100 / max);
}

void lumo_write_brightness_pct(uint32_t pct) {
    /* non-blocking: fork to write sysfs so event loop isn't stalled */
    pid_t pid;
    if (pct > 100) pct = 100;
    pid = fork();
    if (pid == 0) {
        int max = 255;
        FILE *mfp = fopen("/sys/class/backlight/soc:lcd_backlight/max_brightness", "r");
        if (mfp) { if (fscanf(mfp, "%d", &max) < 1) max = 255; fclose(mfp); }
        int val = (int)(pct * (uint32_t)max / 100);
        if (val < 1) val = 1;
        FILE *fp = fopen("/sys/class/backlight/soc:lcd_backlight/brightness", "w");
        if (fp) { fprintf(fp, "%d", val); fclose(fp); }
        _exit(0);
    }
    /* parent returns immediately; the child is reaped by the compositor's
     * SIGCHLD handler which calls lumo_shell_reap_children() via
     * waitpid(-1, ..., WNOHANG) in a loop, covering all untracked forks. */
}

uint32_t lumo_read_volume_pct(void) {
    /* read volume from PipeWire/PulseAudio (same as GDM) */
    int pipefd[2];
    int pct = 50;
    if (pipe(pipefd) < 0) return (uint32_t)pct;
    pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        close(STDERR_FILENO);
        execlp("pactl", "pactl", "get-sink-volume", "@DEFAULT_SINK@",
            (char *)NULL);
        _exit(127);
    }
    close(pipefd[1]);
    /* 300 ms blocking poll is acceptable here: lumo_read_volume_pct() is
     * called exactly once at compositor startup (not on every frame), so
     * the brief wait does not affect steady-state event-loop latency. */
    struct pollfd pfd = {.fd = pipefd[0], .events = POLLIN};
    if (poll(&pfd, 1, 300) > 0) {
        char buf[512];
        ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            /* format: "Volume: front-left: 22937 /  35% / ..." */
            char *p = strstr(buf, "/ ");
            if (p) {
                p += 2;
                while (*p == ' ') p++;
                sscanf(p, "%d%%", &pct);
            }
        }
    }
    close(pipefd[0]);
    waitpid(pid, NULL, WNOHANG);
    return (uint32_t)pct;
}

void lumo_write_volume_pct(uint32_t pct) {
    /* non-blocking: fork pactl (PipeWire/PulseAudio) */
    pid_t pid;
    char pct_str[8];
    if (pct > 100) pct = 100;
    snprintf(pct_str, sizeof(pct_str), "%u%%", pct);
    pid = fork();
    if (pid == 0) {
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        execlp("pactl", "pactl", "set-sink-volume", "@DEFAULT_SINK@",
            pct_str, (char *)NULL);
        _exit(127);
    }
    /* child is reaped by the compositor's SIGCHLD handler which calls
     * lumo_shell_reap_children() via waitpid(-1, ..., WNOHANG) in a loop. */
}

bool lumo_shell_screenshot_output_path(
    char *buffer,
    size_t buffer_size
) {
    const char *home = getenv("HOME");
    char pictures_dir[PATH_MAX];
    char filename[64];
    time_t now;
    struct tm local_tm;

    if (buffer == NULL || buffer_size == 0) {
        return false;
    }

    if (home == NULL || home[0] == '\0') {
        home = "/tmp";
    }

    if (strcmp(home, "/tmp") == 0) {
        if (!lumo_shell_copy_path(pictures_dir, sizeof(pictures_dir), "/tmp")) {
            return false;
        }
    } else {
        if (!lumo_shell_join_path(pictures_dir, sizeof(pictures_dir), home,
                "Pictures")) {
            return false;
        }
        if (mkdir(pictures_dir, 0755) != 0 && errno != EEXIST) {
            return false;
        }
    }

    now = time(NULL);
    if (localtime_r(&now, &local_tm) == NULL) {
        return false;
    }
    if (strftime(filename, sizeof(filename), "lumo-screenshot-%Y%m%d-%H%M%S.png",
            &local_tm) == 0) {
        return false;
    }

    return lumo_shell_join_path(buffer, buffer_size, pictures_dir, filename);
}

void lumo_shell_capture_screenshot_async(
    struct lumo_compositor *compositor
) {
    struct lumo_shell_state *state;
    char output_path[PATH_MAX];
    char binary_path[PATH_MAX];
    char parent_directory[PATH_MAX];
    const char *binary = "lumo-screenshot";
    pid_t pid;

    if (compositor == NULL) {
        return;
    }

    if (!lumo_shell_screenshot_output_path(output_path, sizeof(output_path))) {
        wlr_log(WLR_ERROR, "shell: failed to build screenshot output path");
        return;
    }

    state = compositor->shell_state;
    if (state != NULL && state->binary_path[0] != '\0' &&
            lumo_shell_parent_directory(state->binary_path, parent_directory,
                sizeof(parent_directory)) &&
            lumo_shell_join_path(binary_path, sizeof(binary_path),
                parent_directory, "lumo-screenshot") &&
            access(binary_path, X_OK) == 0) {
        binary = binary_path;
    }

    pid = fork();
    if (pid < 0) {
        wlr_log_errno(WLR_ERROR, "shell: failed to fork screenshot helper");
        return;
    }

    if (pid == 0) {
        const struct timespec delay = {
            .tv_sec = 0,
            .tv_nsec = 150 * 1000 * 1000,
        };
        (void)nanosleep(&delay, NULL);
        setsid();
        if (strchr(binary, '/') != NULL) {
            execl(binary, binary, "--output", output_path, (char *)NULL);
        }
        execlp("lumo-screenshot", "lumo-screenshot", "--output", output_path,
            (char *)NULL);
        _exit(127);
    }

    wlr_log(WLR_INFO, "shell: scheduled screenshot capture to %s", output_path);
}

void lumo_shell_play_boot_sound(void) {
    /* generate a short two-tone chime WAV in /tmp and play it async */
    pid_t pid = fork();
    if (pid != 0) {
        /* parent returns immediately; child is reaped by the compositor's
         * SIGCHLD handler which calls lumo_shell_reap_children() via
         * waitpid(-1, ..., WNOHANG) in a loop. */
        return;
    }

    /* child: create a tiny WAV and play it */
    const char *path = "/tmp/lumo-boot.wav";
    const uint32_t sample_rate = 22050;
    const uint32_t duration_ms = 400;
    const uint32_t num_samples = sample_rate * duration_ms / 1000;
    const uint32_t data_size = num_samples * 2; /* 16-bit mono */
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) _exit(1);

    /* WAV header */
    uint32_t chunk_size = 36 + data_size;
    uint16_t audio_fmt = 1; /* PCM */
    uint16_t channels = 1;
    uint32_t byte_rate = sample_rate * 2;
    uint16_t block_align = 2;
    uint16_t bits = 16;
    fwrite("RIFF", 1, 4, fp);
    fwrite(&chunk_size, 4, 1, fp);
    fwrite("WAVE", 1, 4, fp);
    fwrite("fmt ", 1, 4, fp);
    uint32_t fmt_size = 16;
    fwrite(&fmt_size, 4, 1, fp);
    fwrite(&audio_fmt, 2, 1, fp);
    fwrite(&channels, 2, 1, fp);
    fwrite(&sample_rate, 4, 1, fp);
    fwrite(&byte_rate, 4, 1, fp);
    fwrite(&block_align, 2, 1, fp);
    fwrite(&bits, 2, 1, fp);
    fwrite("data", 1, 4, fp);
    fwrite(&data_size, 4, 1, fp);

    /* two-tone chime: C5 (523Hz) then E5 (659Hz) with fade */
    for (uint32_t i = 0; i < num_samples; i++) {
        double t = (double)i / sample_rate;
        double freq = (i < num_samples / 2) ? 523.0 : 659.0;
        double env = 1.0 - (double)i / num_samples; /* linear fade */
        env *= env; /* quadratic fade for smoother decay */
        double sample = env * 0.4 * __builtin_sin(2.0 * 3.14159265 * freq * t);
        int16_t pcm = (int16_t)(sample * 32000.0);
        fwrite(&pcm, 2, 1, fp);
    }
    fclose(fp);

    /* play with aplay (non-interactive, no blocking the compositor) */
    setsid();
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    execlp("aplay", "aplay", "-q", path, (char *)NULL);
    _exit(127);
}

/* --- weather fetcher --- */

void lumo_weather_parse(struct lumo_compositor *compositor,
    const char *data)
{
    /* expects format like "+5°C Sunny" or "-2°C Partly cloudy" from
     * curl 'https://wttr.in/41101?format=%t+%C' */
    int temp = 0;
    char condition[32] = "";

    if (data == NULL || compositor == NULL) return;

    /* parse temperature: skip leading whitespace, handle +/- */
    const char *p = data;
    while (*p == ' ') p++;
    if (sscanf(p, "%d", &temp) < 1) temp = 0;

    /* find condition after °C or °F */
    bool is_fahrenheit = false;
    const char *deg = strstr(p, "\xC2\xB0"); /* UTF-8 degree sign */
    if (deg != NULL) {
        deg += 2; /* skip ° */
        if (*deg == 'F') { is_fahrenheit = true; deg++; }
        else if (*deg == 'C') deg++;
        while (*deg == ' ') deg++;
        /* parse: "Sunny 45% →10mph\n" or similar */
        strncpy(condition, deg, sizeof(condition) - 1);
        condition[sizeof(condition) - 1] = '\0';
        char *nl = strchr(condition, '\n');
        if (nl) *nl = '\0';

        /* extract humidity (look for XX%) */
        char humidity[16] = "";
        char wind[24] = "";
        {
            const char *hp = strstr(deg, "%");
            if (hp != NULL) {
                /* scan backwards from % to find digits */
                const char *hs = hp;
                while (hs > deg && (*(hs - 1) >= '0' && *(hs - 1) <= '9'))
                    hs--;
                if (hs < hp) {
                    size_t hlen = (size_t)(hp - hs + 1);
                    if (hlen < sizeof(humidity)) {
                        memcpy(humidity, hs, hlen);
                        humidity[hlen] = '\0';
                    }
                }
            }
            /* extract wind (everything after % and space) */
            if (hp != NULL) {
                const char *wp = hp + 1;
                while (*wp == ' ') wp++;
                strncpy(wind, wp, sizeof(wind) - 1);
                wind[sizeof(wind) - 1] = '\0';
                nl = strchr(wind, '\n');
                if (nl) *nl = '\0';
            }
            /* truncate condition to just the weather name */
            if (hp != NULL) {
                /* find the space before humidity */
                char *sp = condition;
                char *pct = strstr(sp, "%");
                if (pct != NULL) {
                    char *cut = pct;
                    while (cut > sp && *(cut - 1) != ' ') cut--;
                    if (cut > sp) {
                        cut--;
                        while (cut > sp && *(cut - 1) == ' ') cut--;
                        *cut = '\0';
                    }
                }
            }
        }
        strncpy(compositor->weather_humidity, humidity,
            sizeof(compositor->weather_humidity) - 1);
        /* convert km/h wind to mph for US display */
        {
            int kmh = 0;
            char arrow[8] = "";
            if (sscanf(wind, "%4[^0-9]%d", arrow, &kmh) >= 2 ||
                    sscanf(wind, "%d", &kmh) >= 1) {
                int mph = kmh * 10 / 16; /* ×0.621 */
                snprintf(wind, sizeof(wind), "%s%dmph",
                    arrow[0] ? arrow : "", mph);
            }
        }
        strncpy(compositor->weather_wind, wind,
            sizeof(compositor->weather_wind) - 1);
    }

    /* map condition text to a weather code for the theme engine:
     * 0=clear 1=partly_cloudy 2=cloudy 3=rain 4=storm 5=snow 6=fog */
    int code = 0;
    char lower[32];
    for (int i = 0; i < (int)sizeof(lower) - 1 && condition[i]; i++) {
        lower[i] = (condition[i] >= 'A' && condition[i] <= 'Z')
            ? condition[i] + 32 : condition[i];
        lower[i + 1] = '\0';
    }
    if (strstr(lower, "snow") || strstr(lower, "sleet") ||
            strstr(lower, "ice") || strstr(lower, "blizzard"))
        code = 5;
    else if (strstr(lower, "thunder") || strstr(lower, "storm"))
        code = 4;
    else if (strstr(lower, "rain") || strstr(lower, "drizzle") ||
            strstr(lower, "shower"))
        code = 3;
    else if (strstr(lower, "fog") || strstr(lower, "mist") ||
            strstr(lower, "haze"))
        code = 6;
    else if (strstr(lower, "overcast") || strstr(lower, "cloudy"))
        code = 2;
    else if (strstr(lower, "partly"))
        code = 1;

    /* convert to Fahrenheit if the response is in Celsius.
     * wttr.in returns °F when &u is effective, °C otherwise. */
    int temp_f = is_fahrenheit ? temp : temp * 9 / 5 + 32;

    if (strcmp(compositor->weather_condition, condition) != 0 ||
            compositor->weather_temp_c != temp_f ||
            compositor->weather_code != code) {
        strncpy(compositor->weather_condition, condition,
            sizeof(compositor->weather_condition) - 1);
        compositor->weather_temp_c = temp_f;
        compositor->weather_code = code;
        wlr_log(WLR_INFO, "weather: %dC -> %dF %s (code=%d)", temp, temp_f,
            condition, code);
        lumo_shell_mark_state_dirty(compositor);
    }
}

void lumo_weather_fetch(struct lumo_compositor *compositor) {
    int pipefd[2];
    pid_t pid;

    if (compositor == NULL) return;
    if (pipe(pipefd) < 0) return;

    pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        close(STDERR_FILENO);
        execlp("curl", "curl", "-s", "--max-time", "8",
            "https://wttr.in/41101?format=%t+%C+%h+%w&u", (char *)NULL);
        _exit(127);
    }

    close(pipefd[1]);

    /* Use poll() with a 5 s timeout before reading so that a stalled curl
     * process cannot block the compositor event loop indefinitely.  curl is
     * also given --max-time 10 as a belt-and-suspenders guard, but poll()
     * here ensures we give up earlier if the kernel pipe never becomes
     * readable (e.g. curl hangs before writing anything). */
    char buf[128] = "";
    ssize_t n = 0;
    {
        struct pollfd pfd = { .fd = pipefd[0], .events = POLLIN };
        /* poll with enough time for wttr.in over WiFi on riscv64.
         * This blocks the compositor event loop, so we use 3000ms as
         * a compromise — long enough for most fetches, short enough
         * to avoid a noticeable UI freeze on the first fetch. */
        int pr = poll(&pfd, 1, 3000);
        if (pr > 0 && (pfd.revents & POLLIN)) {
            n = read(pipefd[0], buf, sizeof(buf) - 1);
        } else if (pr == 0) {
            wlr_log(WLR_INFO, "weather: curl timed out after 3000ms, skipping");
        }
    }
    close(pipefd[0]);
    waitpid(pid, NULL, WNOHANG); /* non-blocking reap; zombie cleaned by SIGCHLD */

    if (n > 0) {
        buf[n] = '\0';
        lumo_weather_parse(compositor, buf);
    }
}

int lumo_weather_timer_cb(void *data) {
    struct lumo_compositor *compositor = data;
    lumo_weather_fetch(compositor);
    /* re-arm for 5 minutes */
    if (compositor != NULL && compositor->weather_timer != NULL) {
        wl_event_source_timer_update(compositor->weather_timer, 300000);
    }
    return 0;
}
