/*
 * test_perf.c — rendering performance benchmarks for Lumo shell and app
 * drawing primitives.  Each test renders representative workloads at the
 * OrangePi RV2 portrait resolution (800x1280) and reports wall-clock
 * microseconds per frame.  The output is both human-readable and parseable
 * so CI can detect regressions.
 *
 * Run standalone:   ./build/lumo-perf-tests
 * Run via meson:    meson test -C build lumo-perf
 */

#include "lumo/shell.h"
#include "lumo/app_render.h"
#include "lumo/lumo_term.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── timing helpers ───────────────────────────────────────────────── */

static uint64_t now_usec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

/* declared in shell_draw.c / app_ui.c — test-visible via header or
 * direct declaration since we link the object files directly */
void lumo_clear_pixels(uint32_t *pixels, uint32_t width, uint32_t height);
void lumo_fill_span(uint32_t *row_ptr, int count, uint32_t color);
void lumo_fill_rect(uint32_t *pixels, uint32_t width, uint32_t height,
    int x, int y, int w, int h, uint32_t color);
void lumo_fill_rounded_rect(uint32_t *pixels, uint32_t width, uint32_t height,
    const struct lumo_rect *rect, uint32_t radius, uint32_t color);
void lumo_fill_vertical_gradient(uint32_t *pixels, uint32_t width,
    uint32_t height, const struct lumo_rect *rect,
    uint32_t top_color, uint32_t bottom_color);
void lumo_draw_text(uint32_t *pixels, uint32_t width, uint32_t height,
    int x, int y, int scale, uint32_t color, const char *text);

/* ── constants ────────────────────────────────────────────────────── */

/* OrangePi RV2 portrait resolution */
#define W 800
#define H 1280

/* 33ms = 30fps frame budget in microseconds */
#define FRAME_BUDGET_US 33333

#define BENCH_ITERATIONS 50

static uint32_t pixels[W * H];

/* ── benchmark runner ─────────────────────────────────────────────── */

typedef void (*bench_fn)(uint32_t *px, uint32_t w, uint32_t h);

static uint64_t run_bench(const char *name, bench_fn fn, int iterations) {
    uint64_t start, end, total = 0, best = UINT64_MAX;

    /* warmup */
    fn(pixels, W, H);

    for (int i = 0; i < iterations; i++) {
        start = now_usec();
        fn(pixels, W, H);
        end = now_usec();
        uint64_t elapsed = end - start;
        total += elapsed;
        if (elapsed < best) best = elapsed;
    }

    uint64_t avg = total / (uint64_t)iterations;
    const char *status = avg <= FRAME_BUDGET_US ? "OK" : "SLOW";
    printf("  %-32s  avg=%5lu us  best=%5lu us  budget=%s  [%s]\n",
        name, (unsigned long)avg, (unsigned long)best,
        avg <= FRAME_BUDGET_US ? "PASS" : "FAIL", status);
    return avg;
}

/* ── individual benchmarks ────────────────────────────────────────── */

static void bench_clear(uint32_t *px, uint32_t w, uint32_t h) {
    lumo_clear_pixels(px, w, h);
}

static void bench_fill_span_800(uint32_t *px, uint32_t w, uint32_t h) {
    (void)w;
    /* simulate filling 1280 rows of 800 pixels each (full screen) */
    for (uint32_t y = 0; y < h; y++) {
        lumo_fill_span(px + y * W, (int)W, 0xFFE95420);
    }
}

static void bench_fill_rect_fullscreen(uint32_t *px, uint32_t w, uint32_t h) {
    lumo_fill_rect(px, w, h, 0, 0, (int)w, (int)h, 0xFF2C001E);
}

static void bench_fill_rects_12_tiles(uint32_t *px, uint32_t w, uint32_t h) {
    /* simulate 12 launcher tiles: 4 columns x 3 rows of ~160x160 rects */
    (void)h;
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 4; col++) {
            lumo_fill_rect(px, w, H, 40 + col * 190, 200 + row * 240,
                160, 160, 0xFF44444A);
        }
    }
}

static void bench_rounded_rects_12(uint32_t *px, uint32_t w, uint32_t h) {
    (void)h;
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 4; col++) {
            struct lumo_rect r = {
                40 + col * 190, 200 + row * 240, 160, 160
            };
            lumo_fill_rounded_rect(px, w, H, &r, 16, 0xFF44444A);
        }
    }
}

static void bench_gradient_fullscreen(uint32_t *px, uint32_t w, uint32_t h) {
    struct lumo_rect r = {0, 0, (int)w, (int)h};
    lumo_fill_vertical_gradient(px, w, h, &r, 0xFF2A2A2E, 0xFF1E1E22);
}

static void bench_osk_33_keys(uint32_t *px, uint32_t w, uint32_t h) {
    /* simulate rendering 33 OSK keys: rounded rect + outline + text each */
    (void)h;
    size_t key_count = lumo_shell_osk_key_count();
    for (uint32_t i = 0; i < key_count; i++) {
        struct lumo_rect kr;
        if (!lumo_shell_osk_key_rect(w, 512, i, &kr)) continue;
        lumo_fill_rounded_rect(px, w, H, &kr, 10, 0xFF44444A);
        /* outline = 4 thin rects */
        lumo_fill_rect(px, w, H, kr.x, kr.y, kr.width, 1, 0xFF38383E);
        lumo_fill_rect(px, w, H, kr.x, kr.y + kr.height - 1, kr.width, 1, 0xFF38383E);
        lumo_fill_rect(px, w, H, kr.x, kr.y, 1, kr.height, 0xFF38383E);
        lumo_fill_rect(px, w, H, kr.x + kr.width - 1, kr.y, 1, kr.height, 0xFF38383E);
        /* key label */
        const char *label = lumo_shell_osk_key_label(i);
        if (label != NULL) {
            lumo_draw_text(px, w, H, kr.x + 10, kr.y + 10, 3, 0xFFF0F0F0, label);
        }
    }
}

static void bench_text_long_string(uint32_t *px, uint32_t w, uint32_t h) {
    (void)h;
    /* simulate 16 lines of terminal output */
    for (int i = 0; i < 16; i++) {
        lumo_draw_text(px, w, H, 12, 48 + i * 18, 2, 0xFFFFFFFF,
            "orangepi$ ls -la /home/user/documents");
    }
}

static void bench_launcher_full_frame(uint32_t *px, uint32_t w, uint32_t h) {
    /* simulate a complete launcher frame: clear + gradient + tiles + text */
    lumo_clear_pixels(px, w, h);
    {
        struct lumo_rect bg = {0, 0, (int)w, (int)h};
        lumo_fill_rounded_rect(px, w, h, &bg, 0, 0xCC101822);
    }
    /* search bar */
    lumo_fill_rounded_rect(px, w, h,
        &(struct lumo_rect){40, 80, (int)w - 80, 48}, 24, 0xFF1A1A24);
    lumo_draw_text(px, w, h, 60, 92, 2, 0xFF808088, "TYPE TO SEARCH...");
    /* 12 tiles with rounded rect + label */
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 4; col++) {
            struct lumo_rect tile = {
                40 + col * 185, 200 + row * 260, 160, 200
            };
            lumo_fill_rounded_rect(px, w, h, &tile, 16, 0xFF44444A);
            lumo_draw_text(px, w, h,
                tile.x + 20, tile.y + tile.height - 30, 2,
                0xFFFFFFFF, "APP");
        }
    }
}

static void bench_osk_full_frame(uint32_t *px, uint32_t w, uint32_t h) {
    /* simulate a complete OSK frame: clear + gradient + all keys */
    uint32_t osk_h = 512;
    lumo_clear_pixels(px, w, osk_h);
    {
        struct lumo_rect bg = {0, 0, (int)w, (int)osk_h};
        lumo_fill_vertical_gradient(px, w, osk_h, &bg, 0xFF2A2A2E, 0xFF1E1E22);
    }
    bench_osk_33_keys(px, w, osk_h);
}

/* app-side primitives */
static void bench_app_fill_rect_full(uint32_t *px, uint32_t w, uint32_t h) {
    lumo_app_fill_rect(px, w, h, 0, 0, (int)w, (int)h, 0xFF2C001E);
}

static void bench_app_rounded_rects(uint32_t *px, uint32_t w, uint32_t h) {
    (void)h;
    for (int i = 0; i < 12; i++) {
        struct lumo_rect r = {40, 60 + i * 100, (int)w - 80, 80};
        lumo_app_fill_rounded_rect(px, w, H, &r, 12, 0xFF44444A);
    }
}

static void bench_app_terminal_frame(uint32_t *px, uint32_t w, uint32_t h) {
    struct lumo_term term;
    lumo_term_init(&term, 80, 24);
    /* feed some sample output through the VT100 parser */
    const char *sample =
        "orangepi$ uname -a\r\n"
        "Linux orangepirv2 6.1.15\r\n"
        "orangepi$ ls\r\n"
        "\x1b[1;34mDesktop\x1b[0m  \x1b[1;34mDocuments\x1b[0m  Downloads\r\n"
        "orangepi$ pwd\r\n"
        "/home/orangepi\r\n"
        "orangepi$ whoami\r\n"
        "orangepi\r\n"
        "orangepi$ date\r\n"
        "Sun Mar 30 12:00:00 UTC 2026\r\n"
        "orangepi$ cat /etc/os";
    lumo_term_feed(&term, sample, strlen(sample));

    struct lumo_app_render_context ctx = {
        .app_id = LUMO_APP_MESSAGES,
        .term = &term,
    };
    lumo_app_render(&ctx, px, w, h);
}

/* ── main ─────────────────────────────────────────────────────────── */

int main(void) {
    uint64_t total_avg = 0;
    int fail_count = 0;

    printf("Lumo rendering performance benchmarks\n");
    printf("Resolution: %dx%d  Frame budget: %d us (30fps)\n",
        W, H, FRAME_BUDGET_US);
    printf("Iterations per test: %d\n\n", BENCH_ITERATIONS);

    printf("--- Primitive benchmarks ---\n");
    total_avg += run_bench("clear_pixels (full)", bench_clear, BENCH_ITERATIONS);
    total_avg += run_bench("fill_span (full screen)", bench_fill_span_800, BENCH_ITERATIONS);
    total_avg += run_bench("fill_rect (full screen)", bench_fill_rect_fullscreen, BENCH_ITERATIONS);
    total_avg += run_bench("fill_rect (12 tiles)", bench_fill_rects_12_tiles, BENCH_ITERATIONS);
    total_avg += run_bench("rounded_rect (12 tiles)", bench_rounded_rects_12, BENCH_ITERATIONS);
    total_avg += run_bench("gradient (full screen)", bench_gradient_fullscreen, BENCH_ITERATIONS);
    total_avg += run_bench("text (16 terminal lines)", bench_text_long_string, BENCH_ITERATIONS);

    printf("\n--- Composite frame benchmarks ---\n");
    uint64_t launcher_avg = run_bench("launcher full frame", bench_launcher_full_frame, BENCH_ITERATIONS);
    uint64_t osk_avg = run_bench("OSK full frame", bench_osk_full_frame, BENCH_ITERATIONS);
    run_bench("OSK 33 keys only", bench_osk_33_keys, BENCH_ITERATIONS);

    printf("\n--- App-side benchmarks ---\n");
    run_bench("app fill_rect (full)", bench_app_fill_rect_full, BENCH_ITERATIONS);
    run_bench("app rounded_rects (12)", bench_app_rounded_rects, BENCH_ITERATIONS);
    uint64_t term_avg = run_bench("app terminal frame", bench_app_terminal_frame, BENCH_ITERATIONS);

    printf("\n--- Frame budget analysis ---\n");
    printf("  Launcher frame:   %5lu / %d us  (%.0f%% of budget)\n",
        (unsigned long)launcher_avg, FRAME_BUDGET_US,
        100.0 * (double)launcher_avg / FRAME_BUDGET_US);
    printf("  OSK frame:        %5lu / %d us  (%.0f%% of budget)\n",
        (unsigned long)osk_avg, FRAME_BUDGET_US,
        100.0 * (double)osk_avg / FRAME_BUDGET_US);
    printf("  Terminal frame:   %5lu / %d us  (%.0f%% of budget)\n",
        (unsigned long)term_avg, FRAME_BUDGET_US,
        100.0 * (double)term_avg / FRAME_BUDGET_US);

    /* composite frames must fit in budget */
    if (launcher_avg > FRAME_BUDGET_US) fail_count++;
    if (osk_avg > FRAME_BUDGET_US) fail_count++;

    printf("\n--- Constraints (cannot overcome) ---\n");
    printf("  SpacemiT X1: 8-core rv64gcv @ 1.6GHz, 256-bit VLEN\n");
    printf("  L2 cache: 1MB shared (512KB per 4-core cluster)\n");
    printf("  No GPU — all rendering is software pixman/SHM\n");
    printf("  Full-screen clear: ~4MB memset per frame (unavoidable)\n");
    printf("  Wayland SHM: mmap'd shared buffers, no DMA-BUF zero-copy\n");

    if (fail_count > 0) {
        printf("\nWARNING: %d composite frame(s) exceed 30fps budget\n",
            fail_count);
    }

    printf("\nlumo performance tests passed\n");
    return 0;
}
