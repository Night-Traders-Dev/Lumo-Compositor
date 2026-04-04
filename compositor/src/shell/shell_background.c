/*
 * shell_background.c — animated background renderer for the Lumo shell.
 * Split from shell_render.c for maintainability.
 *
 * Contains the bokeh ball system, PS4 Flow wave recreation, multi-core
 * thread pool, wave loop pre-rendering, and the boot splash.
 */

#include "shell_client_internal.h"
#include "lumo/lumo_icon.h"
#include "lumo/version.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ── bokeh ball system ────────────────────────────────────────────── */

#define BOKEH_COUNT 10

struct bokeh_ball {
    /* base position as fraction of screen (0.0-1.0) */
    float base_x;
    float base_y;
    /* drift speed in fractions-per-frame */
    float drift_x;
    float drift_y;
    /* radius as fraction of screen height */
    float radius_frac;
    /* peak alpha (0-255) */
    uint8_t alpha;
};

/* deterministic particle set — hand-tuned for visual balance
 * drift values scaled for 15fps frame counter */
static const struct bokeh_ball bokeh_particles[BOKEH_COUNT] = {
    { 0.12f, 0.18f,  0.00027f,  0.00010f, 0.08f, 32 },
    { 0.85f, 0.25f, -0.00017f,  0.00013f, 0.11f, 24 },
    { 0.42f, 0.72f,  0.00020f, -0.00007f, 0.06f, 40 },
    { 0.68f, 0.10f, -0.00010f,  0.00020f, 0.14f, 18 },
    { 0.25f, 0.55f,  0.00013f,  0.00017f, 0.05f, 48 },
    { 0.90f, 0.80f, -0.00023f, -0.00010f, 0.09f, 28 },
    { 0.55f, 0.40f,  0.00007f, -0.00013f, 0.07f, 36 },
    { 0.08f, 0.88f,  0.00020f, -0.00017f, 0.10f, 22 },
    { 0.72f, 0.60f, -0.00013f,  0.00007f, 0.13f, 16 },
    { 0.35f, 0.15f,  0.00017f,  0.00020f, 0.04f, 52 },
    { 0.50f, 0.90f, -0.00010f, -0.00020f, 0.08f, 30 },
    { 0.18f, 0.42f,  0.00023f,  0.00003f, 0.06f, 44 },
    { 0.78f, 0.35f, -0.00020f,  0.00017f, 0.12f, 20 },
    { 0.60f, 0.75f,  0.00010f, -0.00010f, 0.05f, 50 },
    { 0.30f, 0.85f, -0.00007f,  0.00013f, 0.09f, 26 },
    { 0.95f, 0.50f, -0.00017f, -0.00007f, 0.07f, 38 },
    { 0.15f, 0.65f,  0.00013f,  0.00010f, 0.11f, 20 },
    { 0.48f, 0.22f, -0.00010f,  0.00017f, 0.06f, 42 },
};

/* alpha-blend a single pixel: src over dst.
 * Uses (x * 257 + 256) >> 16 ~ x / 255 to avoid per-pixel division. */
static inline uint32_t lumo_blend_pixel(uint32_t dst, uint32_t src_rgb,
                                        uint8_t alpha)
{
    if (alpha == 0) return dst;
    uint32_t a  = alpha;
    uint32_t ia = 255 - a;
    /* blend RB channels together, then G separately — avoids 3 multiplies */
    uint32_t dst_rb = dst & 0x00FF00FF;
    uint32_t dst_g  = dst & 0x0000FF00;
    uint32_t src_rb = src_rgb & 0x00FF00FF;
    uint32_t src_g  = src_rgb & 0x0000FF00;
    uint32_t rb = (src_rb * a + dst_rb * ia + 0x00800080) >> 8;
    uint32_t g  = (src_g  * a + dst_g  * ia + 0x00008000) >> 8;
    return 0xFF000000 | (rb & 0x00FF00FF) | (g & 0x0000FF00);
}

/* draw one soft bokeh ball with radial alpha falloff */
static void lumo_draw_bokeh_ball(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    int cx,
    int cy,
    int radius,
    uint32_t tint_rgb,
    uint8_t peak_alpha
) {
    int x0 = cx - radius;
    int y0 = cy - radius;
    int x1 = cx + radius;
    int y1 = cy + radius;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= (int)width)  x1 = (int)width  - 1;
    if (y1 >= (int)height) y1 = (int)height - 1;

    int r2 = radius * radius;
    if (r2 == 0) return;

    /* pre-compute reciprocal to avoid per-pixel division */
    uint32_t inv_r2 = r2 > 0 ? (255u * 65536u) / (uint32_t)r2 : 0;

    for (int y = y0; y <= y1; y++) {
        int dy = y - cy;
        int dy2 = dy * dy;
        uint32_t *row = pixels + y * width;
        for (int x = x0; x <= x1; x++) {
            int dx = x - cx;
            int dist2 = dx * dx + dy2;
            if (dist2 >= r2) continue;

            /* smooth radial falloff: alpha = peak * (1 - (d/r)^2)^2
             * Use pre-computed reciprocal: frac = dist2 * 255 / r2
             * approximated as (dist2 * inv_r2) >> 16 */
            uint32_t frac = ((uint32_t)dist2 * inv_r2) >> 16;
            if (frac > 255) frac = 255;
            uint32_t inv  = 255 - frac;
            uint32_t a    = ((uint32_t)peak_alpha * inv * inv) >> 16;
            if (a == 0) continue;
            if (a > 255) a = 255;

            row[x] = lumo_blend_pixel(row[x], tint_rgb, (uint8_t)a);
        }
    }
}

/* pre-computed sine LUT for fast per-column wave position */
#define SINE_LUT_SIZE 4096
static float sine_lut[SINE_LUT_SIZE];
static bool sine_lut_ready = false;

static void init_sine_lut(void) {
    if (sine_lut_ready) return;
    for (int i = 0; i < SINE_LUT_SIZE; i++) {
        float a = (float)i / (float)SINE_LUT_SIZE * 6.2832f;
        /* Bhaskara sine approximation — good enough, avoids libm */
        float p = a;
        if (p > 3.14159f) {
            p -= 3.14159f;
            sine_lut[i] = -(16.0f * p * (3.14159f - p)) /
                (49.348f - 4.0f * p * (3.14159f - p));
        } else {
            sine_lut[i] = (16.0f * p * (3.14159f - p)) /
                (49.348f - 4.0f * p * (3.14159f - p));
        }
    }
    sine_lut_ready = true;
}

static inline float fast_sin(float angle) {
    float norm = angle * (1.0f / 6.2832f);
    norm = norm - (float)(int)norm;
    if (norm < 0.0f) norm += 1.0f;
    return sine_lut[(int)(norm * (float)SINE_LUT_SIZE) % SINE_LUT_SIZE];
}

/* smoothstep: 3t^2 - 2t^3, clamped to [0,1] */
static inline float smoothstep(float edge0, float edge1, float x) {
    float t = (x - edge0) / (edge1 - edge0);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

/* fast pow for wave sharpness — only needs 15, 17, or 23.
 * Uses repeated squaring: pow15 = x^8 * x^4 * x^2 * x */
static inline float fast_pow(float x, int e) {
    if (x <= 0.0f) return 0.0f;
    float x2 = x * x;
    float x4 = x2 * x2;
    float x8 = x4 * x4;
    if (e <= 15) return x8 * x4 * x2 * x;      /* x^15 */
    float x16 = x8 * x8;
    if (e <= 17) return x16 * x;                 /* x^17 */
    return x16 * x4 * x2 * x;                   /* x^23 */
}

/* PS4 "Flow" theme wave recreation — 7 sine wave ribbons with
 * asymmetric glow falloff, per-column sine calc, additive blend.
 * Based on the exact parameters from fchavonet's reverse engineering
 * of the PS4 XMB wave background. */

static void lumo_draw_wave_layer(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    uint32_t frame,
    uint32_t base_r,
    uint32_t base_g,
    uint32_t base_b
) {
    init_sine_lut();
    float t = (float)frame * 0.003f;
    float h_inv = 1.0f / (float)height;

    /* wave tint: lighter version of base for the glow color */
    uint32_t tr = base_r + 0x40 > 0xFF ? 0xFF : base_r + 0x40;
    uint32_t tg = base_g + 0x30 > 0xFF ? 0xFF : base_g + 0x30;
    uint32_t tb = base_b + 0x48 > 0xFF ? 0xFF : base_b + 0x48;

    /* PS4 exact wave parameters (from fchavonet recreation) */
    static const struct {
        float speed;
        float freq;
        float amp;
        float vert_off;  /* 0-1, vertical center */
        float line_w;    /* glow width */
        float sharp;     /* pow exponent */
        bool invert;     /* asymmetric falloff direction */
    } waves[7] = {
        /* upper group */
        { 0.2f, 0.20f, 0.20f, 0.50f, 0.10f, 15.0f, false },
        { 0.4f, 0.40f, 0.15f, 0.50f, 0.10f, 17.0f, false },
        { 0.3f, 0.60f, 0.15f, 0.50f, 0.05f, 23.0f, false },
        /* lower group */
        { 0.1f, 0.26f, 0.07f, 0.30f, 0.10f, 17.0f, true },
        { 0.3f, 0.36f, 0.07f, 0.30f, 0.10f, 17.0f, true },
        { 0.5f, 0.46f, 0.07f, 0.30f, 0.05f, 23.0f, true },
        { 0.2f, 0.58f, 0.05f, 0.30f, 0.20f, 15.0f, true },
    };

    /* row-major rendering: for each row, compute glow from all 7 waves.
     * This is cache-friendly and avoids vertical tearing artifacts. */
    for (uint32_t y = 0; y < height; y++) {
        float uv_y = (float)y * h_inv;
        uint32_t *row = pixels + y * width;

        for (uint32_t x = 0; x < width; x++) {
            float uv_x = (float)x / (float)width;
            float total_glow = 0.0f;

            for (int w = 0; w < 7; w++) {
                float angle = t * waves[w].speed * waves[w].freq * -1.0f
                    + uv_x * 2.0f;
                float wy = fast_sin(angle) * waves[w].amp
                    + waves[w].vert_off;

                float raw_dist = wy - uv_y;
                float dist = raw_dist < 0.0f ? -raw_dist : raw_dist;

                /* asymmetric falloff */
                if (waves[w].invert) {
                    if (raw_dist > 0.0f) dist *= 4.0f;
                } else {
                    if (raw_dist < 0.0f) dist *= 4.0f;
                }

                float max_d = waves[w].line_w * 1.5f;
                if (dist >= max_d) continue;

                float glow = smoothstep(max_d, 0.0f, dist);
                glow = fast_pow(glow, (int)waves[w].sharp);
                total_glow += glow * 0.35f;
            }

            if (total_glow < 0.004f) continue;
            if (total_glow > 1.0f) total_glow = 1.0f;

            uint32_t a = (uint32_t)(total_glow * 255.0f);
            if (a > 255) a = 255;

            uint32_t dst = row[x];
            uint32_t or_ = ((dst >> 16) & 0xFF) + ((tr * a) >> 8);
            uint32_t og = ((dst >> 8) & 0xFF) + ((tg * a) >> 8);
            uint32_t ob = (dst & 0xFF) + ((tb * a) >> 8);
            if (or_ > 255) or_ = 255;
            if (og > 255) og = 255;
            if (ob > 255) ob = 255;
            row[x] = 0xFF000000 | (or_ << 16) | (og << 8) | ob;
        }
    }
}

/* ── multi-core thread pool for background rendering ──────────────── */

#define LUMO_BG_THREADS 8

/* Half-resolution wave glow buffer.  Waves are computed at half the
 * output resolution (half_w x half_h) as uint8 intensity values, then
 * upscaled 2x and composited onto the full-res gradient.  This cuts
 * wave math by 75% while the soft glow look stays virtually identical. */
#define LUMO_WAVE_MAX_W 640
#define LUMO_WAVE_MAX_H 1024
static uint8_t wave_glow_buf[LUMO_WAVE_MAX_W * LUMO_WAVE_MAX_H];

/* Pre-rendered wave loop: 5 minutes at 5fps = 1500 frames of half-res
 * glow stored in RAM (~366 MB).  A 3-second crossfade at the boundary
 * makes the loop seamless regardless of wave frequencies.
 * 5fps is sufficient — waves move slowly and the glow is soft.
 * Pre-computed once at startup, then playback is just memcpy. */
#define WAVE_LOOP_FRAMES 1500
#define WAVE_LOOP_FPS    5
#define WAVE_LOOP_BLEND  15   /* frames to crossfade (3s at 5fps) */
static uint8_t *wave_loop_buf;   /* 600 x half_w x half_h bytes */
static uint32_t wave_loop_half_w;
static uint32_t wave_loop_half_h;
static bool wave_loop_ready;
static bool wave_loop_started;   /* prerender thread launched */

struct lumo_bg_stripe {
    /* full-res output */
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
    const uint32_t *row_cache;
    /* half-res wave overlay */
    uint8_t *glow;
    uint32_t half_w;
    uint32_t half_h;
    /* stripe bounds (in half-res coords for wave pass,
     * full-res coords for gradient+composite pass) */
    uint32_t y_start;
    uint32_t y_end;
    /* wave parameters */
    uint32_t wave_tr, wave_tg, wave_tb;
    float wave_t;
    /* which pass: 0 = wave glow (half-res), 1 = gradient + composite */
    int pass;
};

static struct {
    pthread_t threads[LUMO_BG_THREADS];
    struct lumo_bg_stripe tasks[LUMO_BG_THREADS];
    pthread_barrier_t barrier;
    pthread_mutex_t start_mutex;
    pthread_cond_t start_cond;
    uint64_t generation;
    bool shutdown;
    bool initialized;
} bg_pool;

/* PS4 wave parameters — shared across all workers */
static const struct {
    float speed, freq, amp, vert_off, line_w, sharp;
    bool invert;
} wv[7] = {
    { 0.2f, 0.20f, 0.20f, 0.50f, 0.10f, 15.0f, false },
    { 0.4f, 0.40f, 0.15f, 0.50f, 0.10f, 17.0f, false },
    { 0.3f, 0.60f, 0.15f, 0.50f, 0.05f, 23.0f, false },
    { 0.1f, 0.26f, 0.07f, 0.30f, 0.10f, 17.0f, true },
    { 0.3f, 0.36f, 0.07f, 0.30f, 0.10f, 17.0f, true },
    { 0.5f, 0.46f, 0.07f, 0.30f, 0.05f, 23.0f, true },
    { 0.2f, 0.58f, 0.05f, 0.30f, 0.20f, 15.0f, true },
};

/* Pass 0: compute wave glow at half resolution into uint8 buffer */
static void bg_worker_wave_pass(struct lumo_bg_stripe *t) {
    float h_inv = 1.0f / (float)t->half_h;
    float w_inv = 1.0f / (float)t->half_w;

    for (uint32_t hy = t->y_start; hy < t->y_end; hy++) {
        float uv_y = (float)hy * h_inv;
        uint8_t *glow_row = t->glow + hy * t->half_w;

        /* pre-check which waves affect this row */
        uint8_t active_waves = 0;
        for (int w = 0; w < 7; w++) {
            float margin = wv[w].line_w * 1.5f;
            float y_min = wv[w].vert_off - wv[w].amp - margin;
            float y_max = wv[w].vert_off + wv[w].amp + margin;
            if (wv[w].invert) y_min -= margin * 3.0f;
            else y_max += margin * 3.0f;
            if (uv_y >= y_min && uv_y <= y_max)
                active_waves |= (1 << w);
        }

        if (active_waves == 0) {
            memset(glow_row, 0, t->half_w);
            continue;
        }

        for (uint32_t hx = 0; hx < t->half_w; hx++) {
            float uv_x = (float)hx * w_inv;
            float total_glow = 0.0f;

            for (int w = 0; w < 7; w++) {
                if (!(active_waves & (1 << w))) continue;

                float angle = t->wave_t * wv[w].speed * wv[w].freq
                    * -1.0f + uv_x * 2.0f;
                float wy = fast_sin(angle) * wv[w].amp + wv[w].vert_off;
                float raw_dist = wy - uv_y;
                float dist = raw_dist < 0.0f ? -raw_dist : raw_dist;

                if (wv[w].invert) {
                    if (raw_dist > 0.0f) dist *= 4.0f;
                } else {
                    if (raw_dist < 0.0f) dist *= 4.0f;
                }

                float max_d = wv[w].line_w * 1.5f;
                if (dist >= max_d) continue;

                float gl = smoothstep(max_d, 0.0f, dist);
                gl = fast_pow(gl, (int)wv[w].sharp);
                total_glow += gl * 0.35f;
            }

            if (total_glow > 1.0f) total_glow = 1.0f;
            glow_row[hx] = (uint8_t)(total_glow * 255.0f);
        }
    }
}

/* Pass 1: fill gradient + upscale-composite wave glow onto full-res output.
 * Skips the per-pixel blend loop for rows where glow is all zero — this
 * eliminates ~40-60% of rows since waves only cover part of the screen. */
static void bg_worker_composite_pass(struct lumo_bg_stripe *t) {
    for (uint32_t y = t->y_start; y < t->y_end; y++) {
        uint32_t row_color = y < 2048 ? t->row_cache[y] : t->row_cache[2047];
        uint32_t *row_ptr = t->pixels + y * t->width;

        /* fill gradient */
        lumo_fill_span(row_ptr, (int)t->width, row_color);

        /* upscale wave glow: each half-res pixel maps to a 2x2 block */
        uint32_t hy = y / 2;
        if (hy >= t->half_h) hy = t->half_h - 1;
        const uint8_t *glow_row = t->glow + hy * t->half_w;

        /* quick check: skip row if glow is all zero (test 8 bytes at a time) */
        {
            bool has_glow = false;
            const uint64_t *qw = (const uint64_t *)glow_row;
            uint32_t qcount = t->half_w / 8;
            for (uint32_t q = 0; q < qcount; q++) {
                if (qw[q] != 0) { has_glow = true; break; }
            }
            /* check remaining bytes */
            if (!has_glow) {
                for (uint32_t r = qcount * 8; r < t->half_w; r++) {
                    if (glow_row[r] != 0) { has_glow = true; break; }
                }
            }
            if (!has_glow) continue;
        }

        for (uint32_t x = 0; x < t->width; x++) {
            uint32_t hx = x / 2;
            if (hx >= t->half_w) hx = t->half_w - 1;
            uint32_t a = glow_row[hx];
            if (a == 0) continue;

            uint32_t dst = row_ptr[x];
            uint32_t or_ = ((dst >> 16) & 0xFF) + ((t->wave_tr * a) >> 8);
            uint32_t og = ((dst >> 8) & 0xFF) + ((t->wave_tg * a) >> 8);
            uint32_t ob = (dst & 0xFF) + ((t->wave_tb * a) >> 8);
            if (or_ > 255) or_ = 255;
            if (og > 255) og = 255;
            if (ob > 255) ob = 255;
            row_ptr[x] = 0xFF000000 | (or_ << 16) | (og << 8) | ob;
        }
    }
}

static void *bg_worker(void *arg) {
    int id = (int)(intptr_t)arg;
    uint64_t generation = 0;

    for (;;) {
        pthread_mutex_lock(&bg_pool.start_mutex);
        while (bg_pool.generation == generation && !bg_pool.shutdown)
            pthread_cond_wait(&bg_pool.start_cond, &bg_pool.start_mutex);
        if (bg_pool.shutdown) {
            pthread_mutex_unlock(&bg_pool.start_mutex);
            break;
        }
        generation = bg_pool.generation;
        pthread_mutex_unlock(&bg_pool.start_mutex);

        struct lumo_bg_stripe *t = &bg_pool.tasks[id];
        if (t->pass == 0)
            bg_worker_wave_pass(t);
        else
            bg_worker_composite_pass(t);

        pthread_barrier_wait(&bg_pool.barrier);
    }
    return NULL;
}

static void bg_pool_init(void) {
    if (bg_pool.initialized) return;
    pthread_barrier_init(&bg_pool.barrier, NULL, LUMO_BG_THREADS + 1);
    pthread_mutex_init(&bg_pool.start_mutex, NULL);
    pthread_cond_init(&bg_pool.start_cond, NULL);
    bg_pool.generation = 0;
    bg_pool.shutdown = false;
    for (int i = 0; i < LUMO_BG_THREADS; i++) {
        pthread_create(&bg_pool.threads[i], NULL, bg_worker,
            (void *)(intptr_t)i);
    }
    bg_pool.initialized = true;
    fprintf(stderr, "lumo-shell: background thread pool started "
        "(%d workers, half-res waves)\n", LUMO_BG_THREADS);
}

/* Pre-render the entire wave loop into RAM.  Called once at startup.
 * Uses the thread pool to parallelize across 8 cores.
 *
 * For seamless looping: we render WAVE_LOOP_FRAMES + WAVE_LOOP_BLEND
 * raw frames.  Then we crossfade the tail BLEND frames with the
 * corresponding head frames, so frame[N-1] blends smoothly into
 * frame[0] when the loop wraps.  The result is stored in exactly
 * WAVE_LOOP_FRAMES frames — playback with modular indexing gives
 * an infinite, seamless animation. */
static void wave_loop_prerender(uint32_t half_w, uint32_t half_h) {
    size_t frame_size = (size_t)half_w * half_h;
    int raw_count = WAVE_LOOP_FRAMES + WAVE_LOOP_BLEND;
    size_t raw_total = frame_size * (size_t)raw_count;
    size_t final_total = frame_size * WAVE_LOOP_FRAMES;

    if (wave_loop_buf != NULL) {
        free(wave_loop_buf);
        wave_loop_buf = NULL;
    }

    /* allocate space for all raw frames (including overshoot) */
    uint8_t *raw = malloc(raw_total);
    if (raw == NULL) {
        fprintf(stderr, "lumo-shell: failed to allocate wave loop "
            "(%zu MB)\n", raw_total / (1024 * 1024));
        return;
    }

    fprintf(stderr, "lumo-shell: pre-rendering %d+%d wave frames "
        "(%u×%u, %.1f MB)...\n",
        WAVE_LOOP_FRAMES, WAVE_LOOP_BLEND, half_w, half_h,
        (float)final_total / (1024.0f * 1024.0f));

    wave_loop_half_w = half_w;
    wave_loop_half_h = half_h;

    float wt_per_frame = 60.0f * 0.003f / (float)WAVE_LOOP_FPS;

    /* pass 1: render all raw frames (main + overshoot) */
    for (int f = 0; f < raw_count; f++) {
        float wt = (float)f * wt_per_frame;
        uint8_t *dest = raw + (size_t)f * frame_size;

        uint32_t stripe_h = half_h / LUMO_BG_THREADS;
        pthread_mutex_lock(&bg_pool.start_mutex);
        for (int i = 0; i < LUMO_BG_THREADS; i++) {
            bg_pool.tasks[i].glow = dest;
            bg_pool.tasks[i].half_w = half_w;
            bg_pool.tasks[i].half_h = half_h;
            bg_pool.tasks[i].y_start = (uint32_t)i * stripe_h;
            bg_pool.tasks[i].y_end = (i == LUMO_BG_THREADS - 1)
                ? half_h : (uint32_t)(i + 1) * stripe_h;
            bg_pool.tasks[i].wave_t = wt;
            bg_pool.tasks[i].pass = 0;
        }
        bg_pool.generation++;
        pthread_cond_broadcast(&bg_pool.start_cond);
        pthread_mutex_unlock(&bg_pool.start_mutex);
        pthread_barrier_wait(&bg_pool.barrier);
    }

    /* pass 2: crossfade the boundary region.
     * For the last BLEND frames of the main loop, blend between the
     * original frame and the overshoot frame that wraps past the end.
     *
     * frame[N-B+j] = lerp(raw[N-B+j], raw[N+j], j/(B-1))
     *
     * At j=0: fully original (t=0)
     * At j=B-1: fully overshoot (t=1) which equals what frame[0]
     * would look like if the animation continued past the loop point */
    for (int j = 0; j < WAVE_LOOP_BLEND; j++) {
        float t = (float)j / (float)(WAVE_LOOP_BLEND - 1);
        uint8_t *tail = raw + (size_t)(WAVE_LOOP_FRAMES - WAVE_LOOP_BLEND + j) * frame_size;
        const uint8_t *overshoot = raw + (size_t)(WAVE_LOOP_FRAMES + j) * frame_size;
        for (size_t p = 0; p < frame_size; p++) {
            float blended = (float)tail[p] * (1.0f - t) +
                            (float)overshoot[p] * t;
            tail[p] = (uint8_t)(blended + 0.5f);
        }
    }

    /* keep only the first WAVE_LOOP_FRAMES — realloc to trim overshoot */
    wave_loop_buf = realloc(raw, final_total);
    if (wave_loop_buf == NULL)
        wave_loop_buf = raw; /* realloc failed, keep full buffer */

    wave_loop_ready = true;
    fprintf(stderr, "lumo-shell: wave loop ready "
        "(%d frames, %ds, seamless %d-frame crossfade)\n",
        WAVE_LOOP_FRAMES, WAVE_LOOP_FRAMES / WAVE_LOOP_FPS,
        WAVE_LOOP_BLEND);
}

/* Background thread for wave loop prerendering so the shell can
 * show a boot splash while it computes. */
struct wave_prerender_args {
    uint32_t half_w;
    uint32_t half_h;
};
static struct wave_prerender_args prerender_args;

static void *wave_prerender_thread(void *arg) {
    struct wave_prerender_args *a = arg;
    wave_loop_prerender(a->half_w, a->half_h);
    return NULL;
}

static void bg_parallel_fill(uint32_t *pixels, uint32_t width,
    uint32_t height, uint32_t frame, const uint32_t *row_cache,
    uint32_t wave_tr, uint32_t wave_tg, uint32_t wave_tb, float wave_t)
{
    uint32_t half_w = width / 2;
    uint32_t half_h = height / 2;
    if (half_w > LUMO_WAVE_MAX_W) half_w = LUMO_WAVE_MAX_W;
    if (half_h > LUMO_WAVE_MAX_H) half_h = LUMO_WAVE_MAX_H;

    /* while prerendering, caller shows boot splash — never reaches here */
    if (!bg_pool.initialized) bg_pool_init();
    init_sine_lut();

    /* look up pre-rendered glow frame from the loop */
    if (wave_loop_ready &&
            half_w == wave_loop_half_w && half_h == wave_loop_half_h) {
        uint32_t loop_frame = frame % WAVE_LOOP_FRAMES;
        size_t frame_size = (size_t)half_w * half_h;
        memcpy(wave_glow_buf, wave_loop_buf + loop_frame * frame_size,
            frame_size);
    } else {
        /* fallback: real-time wave computation */
        uint32_t stripe_h = half_h / LUMO_BG_THREADS;
        pthread_mutex_lock(&bg_pool.start_mutex);
        for (int i = 0; i < LUMO_BG_THREADS; i++) {
            bg_pool.tasks[i].glow = wave_glow_buf;
            bg_pool.tasks[i].half_w = half_w;
            bg_pool.tasks[i].half_h = half_h;
            bg_pool.tasks[i].y_start = (uint32_t)i * stripe_h;
            bg_pool.tasks[i].y_end = (i == LUMO_BG_THREADS - 1)
                ? half_h : (uint32_t)(i + 1) * stripe_h;
            bg_pool.tasks[i].wave_t = wave_t;
            bg_pool.tasks[i].pass = 0;
        }
        bg_pool.generation++;
        pthread_cond_broadcast(&bg_pool.start_cond);
        pthread_mutex_unlock(&bg_pool.start_mutex);
        pthread_barrier_wait(&bg_pool.barrier);
    }

    /* ── gradient fill + composite (always runs, very cheap) ───────── */
    uint32_t stripe_h = height / LUMO_BG_THREADS;
    pthread_mutex_lock(&bg_pool.start_mutex);
    for (int i = 0; i < LUMO_BG_THREADS; i++) {
        bg_pool.tasks[i].pixels = pixels;
        bg_pool.tasks[i].width = width;
        bg_pool.tasks[i].height = height;
        bg_pool.tasks[i].row_cache = row_cache;
        bg_pool.tasks[i].glow = wave_glow_buf;
        bg_pool.tasks[i].half_w = half_w;
        bg_pool.tasks[i].half_h = half_h;
        bg_pool.tasks[i].wave_tr = wave_tr;
        bg_pool.tasks[i].wave_tg = wave_tg;
        bg_pool.tasks[i].wave_tb = wave_tb;
        bg_pool.tasks[i].y_start = (uint32_t)i * stripe_h;
        bg_pool.tasks[i].y_end = (i == LUMO_BG_THREADS - 1)
            ? height : (uint32_t)(i + 1) * stripe_h;
        bg_pool.tasks[i].pass = 1;
    }
    bg_pool.generation++;
    pthread_cond_broadcast(&bg_pool.start_cond);
    pthread_mutex_unlock(&bg_pool.start_mutex);
    pthread_barrier_wait(&bg_pool.barrier);
}

/* ── background row cache + animated background ───────────────────── */

static uint32_t bg_row_cache[2048];
static uint32_t bg_cache_height;
static uint32_t bg_cache_minute = 0xFFFF;
static int bg_cache_weather_code = -1;

/* ── hour palette lookup ─────────────────────────────────────────── */

/* Time-of-day palettes.  Each entry covers a range of hours;
 * get_hour_palette() returns the base RGB for a given hour.
 *
 * Ubuntu (aubergine/orange), Sailfish (teal/petrol blue),
 * webOS (warm charcoal/slate) blended by time period. */
static void get_hour_palette(uint32_t hour,
    uint32_t *r, uint32_t *g, uint32_t *b)
{
    if (hour >= 5 && hour < 7) {
        /* dawn — Sailfish teal with warm hint */
        *r = 0x14; *g = 0x28; *b = 0x38;
    } else if (hour >= 7 && hour < 10) {
        /* morning — Ubuntu warm purple + Sailfish blue */
        *r = 0x30; *g = 0x10; *b = 0x28;
    } else if (hour >= 10 && hour < 14) {
        /* midday — Ubuntu aubergine core */
        *r = 0x2C; *g = 0x00; *b = 0x1E;
    } else if (hour >= 14 && hour < 17) {
        /* afternoon — webOS warm charcoal */
        *r = 0x28; *g = 0x14; *b = 0x18;
    } else if (hour >= 17 && hour < 19) {
        /* sunset — Ubuntu orange-red warmth */
        *r = 0x42; *g = 0x0C; *b = 0x16;
    } else if (hour >= 19 && hour < 21) {
        /* evening — Sailfish deep petrol */
        *r = 0x10; *g = 0x18; *b = 0x30;
    } else {
        /* night — deep blend of all three */
        *r = 0x12; *g = 0x08; *b = 0x1A;
    }
}

/* Apply weather hue shift to a base palette */
static void apply_weather_shift(int weather_code,
    uint32_t *r, uint32_t *g, uint32_t *b)
{
    switch (weather_code) {
    case 1: /* partly cloudy — Sailfish cool blue push */
        *g += 0x06; *b += 0x0A;
        break;
    case 2: /* cloudy — webOS grey-slate overlay */
        *r = (*r * 3 + 0x30) / 4;
        *g = (*g * 3 + 0x28) / 4;
        *b = (*b * 3 + 0x28) / 4;
        break;
    case 3: /* rain — Sailfish deep teal-blue */
        *r = *r * 2 / 3;
        *g += 0x08; *b += 0x1A;
        break;
    case 4: /* storm — deep purple-indigo */
        *r = *r / 2 + 0x08;
        *g = *g / 2;
        *b += 0x24;
        break;
    case 5: /* snow — Sailfish ice blue-white */
        *r += 0x14; *g += 0x1A; *b += 0x24;
        break;
    case 6: /* fog — webOS warm grey wash */
        *r = (*r + 0x28) / 2;
        *g = (*g + 0x24) / 2;
        *b = (*b + 0x22) / 2;
        break;
    default: break;
    }
    if (*r > 0xFF) *r = 0xFF;
    if (*g > 0xFF) *g = 0xFF;
    if (*b > 0xFF) *b = 0xFF;
}

void lumo_draw_animated_bg(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    int weather_code
) {
    struct timespec mono_ts;
    time_t wall_now;
    struct tm tm_now;
    uint32_t frame;

    clock_gettime(CLOCK_MONOTONIC, &mono_ts);
    /* frame counter at loop rate — indexes into pre-rendered wave buffer */
    frame = (uint32_t)(mono_ts.tv_sec * WAVE_LOOP_FPS +
        mono_ts.tv_nsec / (1000000000 / WAVE_LOOP_FPS));

    wall_now = time(NULL);
    localtime_r(&wall_now, &tm_now);
    uint32_t hour = (uint32_t)tm_now.tm_hour;
    uint32_t minute = (uint32_t)tm_now.tm_min;
    /* unique key: changes every minute so gradient transitions smoothly */
    uint32_t time_key = hour * 60 + minute;

    /* ── smooth gradient interpolation ───────────────────────────────
     * Lerp between current hour's palette and next hour's palette
     * based on how far into the current period we are.  The transition
     * happens gradually over each time-of-day period so there is never
     * a hard color jump. */
    uint32_t cur_r, cur_g, cur_b;
    uint32_t nxt_r, nxt_g, nxt_b;
    get_hour_palette(hour, &cur_r, &cur_g, &cur_b);
    get_hour_palette((hour + 1) % 24, &nxt_r, &nxt_g, &nxt_b);

    apply_weather_shift(weather_code, &cur_r, &cur_g, &cur_b);
    apply_weather_shift(weather_code, &nxt_r, &nxt_g, &nxt_b);

    /* t = 0.0 at minute 0, 1.0 at minute 59 */
    float t = (float)minute / 60.0f;

    uint32_t base_r = (uint32_t)((float)cur_r * (1.0f - t) + (float)nxt_r * t);
    uint32_t base_g = (uint32_t)((float)cur_g * (1.0f - t) + (float)nxt_g * t);
    uint32_t base_b = (uint32_t)((float)cur_b * (1.0f - t) + (float)nxt_b * t);

    /* rebuild row gradient cache when palette or size changes (once/minute) */
    if (time_key != bg_cache_minute || height != bg_cache_height ||
            weather_code != bg_cache_weather_code) {
        uint32_t max_h = height < 2048 ? height : 2048;
        for (uint32_t y = 0; y < max_h; y++) {
            uint32_t r = base_r + (y * 0x20) / height;
            uint32_t g = base_g + (y * 0x08) / height;
            uint32_t b = base_b + (y * 0x06) / height;
            if (r > 0xFF) r = 0xFF;
            bg_row_cache[y] = lumo_argb(0xFF, (uint8_t)r, (uint8_t)g, (uint8_t)b);
        }
        bg_cache_height = height;
        bg_cache_minute = time_key;
        bg_cache_weather_code = weather_code;
    }

    /* wave tint color — also smoothly interpolated */
    uint32_t wave_tr = base_r + 0x40 > 0xFF ? 0xFF : base_r + 0x40;
    uint32_t wave_tg = base_g + 0x30 > 0xFF ? 0xFF : base_g + 0x30;
    uint32_t wave_tb = base_b + 0x48 > 0xFF ? 0xFF : base_b + 0x48;
    float wave_t = (float)frame * 0.003f;

    /* launch prerender thread on first call (non-blocking) */
    if (!wave_loop_ready && !wave_loop_started) {
        if (!bg_pool.initialized) bg_pool_init();
        init_sine_lut();
        uint32_t hw = width / 2, hh = height / 2;
        if (hw > LUMO_WAVE_MAX_W) hw = LUMO_WAVE_MAX_W;
        if (hh > LUMO_WAVE_MAX_H) hh = LUMO_WAVE_MAX_H;
        prerender_args.half_w = hw;
        prerender_args.half_h = hh;
        wave_loop_started = true;
        pthread_t prerender_tid;
        pthread_create(&prerender_tid, NULL, wave_prerender_thread,
            &prerender_args);
        pthread_detach(prerender_tid);
    }

    /* show boot splash while wave loop is pre-rendering */
    if (!wave_loop_ready) {
        /* signal other shell processes to hide during boot */
        static bool boot_flag_set = false;
        if (!boot_flag_set) {
            FILE *bf = fopen("/tmp/lumo-boot-active", "w");
            if (bf) { fputs("1", bf); fclose(bf); }
            boot_flag_set = true;
        }
        /* fill gradient background */
        for (uint32_t y = 0; y < height; y++) {
            uint32_t rc = y < 2048 ? bg_row_cache[y] : bg_row_cache[2047];
            lumo_fill_span(pixels + y * width, (int)width, rc);
        }

        /* center the Lumo icon */
        int ix = ((int)width - LUMO_ICON_W) / 2;
        int iy = ((int)height / 2) - LUMO_ICON_H - 10;
        for (int sy = 0; sy < LUMO_ICON_H; sy++) {
            int dy = iy + sy;
            if (dy < 0 || dy >= (int)height) continue;
            for (int sx = 0; sx < LUMO_ICON_W; sx++) {
                int dx = ix + sx;
                if (dx < 0 || dx >= (int)width) continue;
                uint32_t src = lumo_icon_48x48[sy * LUMO_ICON_W + sx];
                uint32_t sa = (src >> 24) & 0xFF;
                if (sa == 0) continue;
                pixels[dy * width + dx] = src;
            }
        }

        /* "LUMO" text below icon */
        {
            const char *label = "LUMO";
            int tw = lumo_text_width(label, 5);
            lumo_draw_text(pixels, width, height,
                ((int)width - tw) / 2, iy + LUMO_ICON_H + 16,
                5, lumo_argb(0xFF, 0xE8, 0x76, 0x20), label);
        }

        /* Ubuntu-style three-dot loading indicator:
         * three circles that light up orange one at a time */
        {
            int dot_r = 8;
            int dot_gap = 28;
            int dot_cy = (int)height * 3 / 4;
            int dot_cx = (int)width / 2;
            int active = (frame / 3) % 3;
            uint32_t dim = lumo_argb(0x60, 0x50, 0x20, 0x40);
            uint32_t lit = lumo_argb(0xFF, 0xE8, 0x76, 0x20);

            for (int d = 0; d < 3; d++) {
                int cx = dot_cx + (d - 1) * dot_gap;
                struct lumo_rect dot = {
                    cx - dot_r, dot_cy - dot_r,
                    dot_r * 2, dot_r * 2
                };
                lumo_fill_rounded_rect(pixels, width, height, &dot,
                    dot_r, d == active ? lit : dim);
            }
        }

        /* version */
        {
            const char *ver = "v" LUMO_VERSION_STRING;
            int tw = lumo_text_width(ver, 2);
            lumo_draw_text(pixels, width, height,
                ((int)width - tw) / 2, (int)height - 40,
                2, lumo_argb(0x60, 0xC0, 0xC0, 0xC0), ver);
        }
        return;
    }

    /* remove boot flag once waves are ready */
    {
        static bool boot_flag_cleared = false;
        if (!boot_flag_cleared) {
            unlink("/tmp/lumo-boot-active");
            boot_flag_cleared = true;
        }
    }

    /* multi-core parallel gradient fill + PS4 Flow waves */
    bg_parallel_fill(pixels, width, height, frame, bg_row_cache,
                     wave_tr, wave_tg, wave_tb, wave_t);
}
