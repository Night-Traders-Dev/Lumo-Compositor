/*
 * shell_wave_gpu.c — GPU-accelerated PS4 Flow wave background.
 *
 * Uses EGL + GLES2 on the PowerVR renderD128 node to render the
 * animated wave background via a fragment shader. Outputs to an
 * offscreen FBO and reads pixels back to the SHM buffer.
 *
 * Falls back to CPU rendering if GPU init fails.
 */
#define _DEFAULT_SOURCE
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ── shader source ───────────────────────────────────────────────── */

static const char *wave_vert_src =
    "attribute vec2 pos;\n"
    "varying vec2 uv;\n"
    "void main() {\n"
    "    uv = pos * 0.5 + 0.5;\n"
    "    gl_Position = vec4(pos, 0.0, 1.0);\n"
    "}\n";

static const char *wave_frag_src =
    "precision mediump float;\n"
    "varying vec2 uv;\n"
    "uniform float u_time;\n"
    "uniform vec3 u_base_color;\n"
    "uniform vec3 u_wave_color;\n"
    "\n"
    "float smoothstep_(float e0, float e1, float x) {\n"
    "    float t = clamp((x - e0) / (e1 - e0), 0.0, 1.0);\n"
    "    return t * t * (3.0 - 2.0 * t);\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    float glow = 0.0;\n"
    "    \n"
    "    /* 7 wave layers matching the PS4 XMB flow animation */\n"
    "    /* wave params: speed, freq, amp, vert_off, line_w, sharp, invert */\n"
    "    \n"
    "    /* wave 0 */ {\n"
    "        float angle = u_time * 0.2 * 0.20 * -1.0 + uv.x * 2.0;\n"
    "        float wy = sin(angle) * 0.20 + 0.50;\n"
    "        float raw = wy - uv.y;\n"
    "        float d = abs(raw);\n"
    "        if (raw < 0.0) d *= 4.0;\n"
    "        float mxd = 0.10 * 1.5;\n"
    "        if (d < mxd) { float g = smoothstep_(mxd, 0.0, d); glow += pow(g, 15.0) * 0.50; }\n"
    "    }\n"
    "    /* wave 1 */ {\n"
    "        float angle = u_time * 0.4 * 0.40 * -1.0 + uv.x * 2.0;\n"
    "        float wy = sin(angle) * 0.15 + 0.50;\n"
    "        float raw = wy - uv.y;\n"
    "        float d = abs(raw);\n"
    "        if (raw < 0.0) d *= 4.0;\n"
    "        float mxd = 0.10 * 1.5;\n"
    "        if (d < mxd) { float g = smoothstep_(mxd, 0.0, d); glow += pow(g, 17.0) * 0.50; }\n"
    "    }\n"
    "    /* wave 2 */ {\n"
    "        float angle = u_time * 0.3 * 0.60 * -1.0 + uv.x * 2.0;\n"
    "        float wy = sin(angle) * 0.15 + 0.50;\n"
    "        float raw = wy - uv.y;\n"
    "        float d = abs(raw);\n"
    "        if (raw < 0.0) d *= 4.0;\n"
    "        float mxd = 0.05 * 1.5;\n"
    "        if (d < mxd) { float g = smoothstep_(mxd, 0.0, d); glow += pow(g, 23.0) * 0.50; }\n"
    "    }\n"
    "    /* wave 3 (inverted) */ {\n"
    "        float angle = u_time * 0.1 * 0.26 * -1.0 + uv.x * 2.0;\n"
    "        float wy = sin(angle) * 0.07 + 0.30;\n"
    "        float raw = wy - uv.y;\n"
    "        float d = abs(raw);\n"
    "        if (raw > 0.0) d *= 4.0;\n"
    "        float mxd = 0.10 * 1.5;\n"
    "        if (d < mxd) { float g = smoothstep_(mxd, 0.0, d); glow += pow(g, 17.0) * 0.50; }\n"
    "    }\n"
    "    /* wave 4 (inverted) */ {\n"
    "        float angle = u_time * 0.3 * 0.36 * -1.0 + uv.x * 2.0;\n"
    "        float wy = sin(angle) * 0.07 + 0.30;\n"
    "        float raw = wy - uv.y;\n"
    "        float d = abs(raw);\n"
    "        if (raw > 0.0) d *= 4.0;\n"
    "        float mxd = 0.10 * 1.5;\n"
    "        if (d < mxd) { float g = smoothstep_(mxd, 0.0, d); glow += pow(g, 17.0) * 0.50; }\n"
    "    }\n"
    "    /* wave 5 (inverted) */ {\n"
    "        float angle = u_time * 0.5 * 0.46 * -1.0 + uv.x * 2.0;\n"
    "        float wy = sin(angle) * 0.07 + 0.30;\n"
    "        float raw = wy - uv.y;\n"
    "        float d = abs(raw);\n"
    "        if (raw > 0.0) d *= 4.0;\n"
    "        float mxd = 0.05 * 1.5;\n"
    "        if (d < mxd) { float g = smoothstep_(mxd, 0.0, d); glow += pow(g, 23.0) * 0.50; }\n"
    "    }\n"
    "    /* wave 6 (inverted) */ {\n"
    "        float angle = u_time * 0.2 * 0.58 * -1.0 + uv.x * 2.0;\n"
    "        float wy = sin(angle) * 0.05 + 0.30;\n"
    "        float raw = wy - uv.y;\n"
    "        float d = abs(raw);\n"
    "        if (raw > 0.0) d *= 4.0;\n"
    "        float mxd = 0.20 * 1.5;\n"
    "        if (d < mxd) { float g = smoothstep_(mxd, 0.0, d); glow += pow(g, 15.0) * 0.50; }\n"
    "    }\n"
    "    \n"
    "    glow = clamp(glow, 0.0, 1.0);\n"
    "    \n"
    "    /* gradient background */\n"
    "    vec3 bg = mix(u_base_color, u_base_color * 0.6, uv.y);\n"
    "    vec3 color = mix(bg, u_wave_color, glow);\n"
    "    gl_FragColor = vec4(color, 1.0);\n"
    "}\n";

/* ── GPU state ───────────────────────────────────────────────────── */

static struct {
    EGLDisplay display;
    EGLContext context;
    EGLSurface surface;
    GLuint program;
    GLuint fbo;
    GLuint tex;
    GLuint vbo;
    GLint u_time, u_base_color, u_wave_color;
    uint32_t tex_w, tex_h;
    bool initialized;
    bool failed;
} gpu;

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "lumo-wave-gpu: shader error: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static bool wave_gpu_init(uint32_t width, uint32_t height) {
    if (gpu.failed) return false;
    if (gpu.initialized && gpu.tex_w == width && gpu.tex_h == height)
        return true;

    /* try render node first */
    const char *dev = getenv("WLR_RENDER_DRM_DEVICE");
    if (!dev) dev = "/dev/dri/renderD128";

    int fd = open(dev, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "lumo-wave-gpu: can't open %s\n", dev);
        gpu.failed = true;
        return false;
    }

    /* EGL setup with GBM platform */
    /* try surfaceless platform (doesn't need GBM) */
    gpu.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (gpu.display == EGL_NO_DISPLAY) {
        fprintf(stderr, "lumo-wave-gpu: no EGL display\n");
        close(fd);
        gpu.failed = true;
        return false;
    }

    EGLint major, minor;
    if (!eglInitialize(gpu.display, &major, &minor)) {
        fprintf(stderr, "lumo-wave-gpu: eglInitialize failed\n");
        close(fd);
        gpu.failed = true;
        return false;
    }

    eglBindAPI(EGL_OPENGL_ES_API);

    EGLint cfg_attrs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_NONE
    };
    EGLConfig config;
    EGLint num_configs;
    if (!eglChooseConfig(gpu.display, cfg_attrs, &config, 1, &num_configs) ||
            num_configs == 0) {
        /* try without surface type */
        EGLint cfg2[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_NONE
        };
        if (!eglChooseConfig(gpu.display, cfg2, &config, 1, &num_configs) ||
                num_configs == 0) {
            fprintf(stderr, "lumo-wave-gpu: no EGL config\n");
            eglTerminate(gpu.display);
            close(fd);
            gpu.failed = true;
            return false;
        }
    }

    EGLint ctx_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    gpu.context = eglCreateContext(gpu.display, config, EGL_NO_CONTEXT,
        ctx_attrs);
    if (gpu.context == EGL_NO_CONTEXT) {
        fprintf(stderr, "lumo-wave-gpu: can't create context\n");
        eglTerminate(gpu.display);
        close(fd);
        gpu.failed = true;
        return false;
    }

    /* use surfaceless (no pbuffer needed — we render to FBO) */
    gpu.surface = EGL_NO_SURFACE;
    if (!eglMakeCurrent(gpu.display, EGL_NO_SURFACE, EGL_NO_SURFACE,
            gpu.context)) {
        /* try pbuffer */
        EGLint pb_attrs[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
        gpu.surface = eglCreatePbufferSurface(gpu.display, config, pb_attrs);
        if (!eglMakeCurrent(gpu.display, gpu.surface, gpu.surface,
                gpu.context)) {
            fprintf(stderr, "lumo-wave-gpu: can't make current\n");
            eglDestroyContext(gpu.display, gpu.context);
            eglTerminate(gpu.display);
            close(fd);
            gpu.failed = true;
            return false;
        }
    }

    close(fd); /* don't need the fd after EGL init */

    /* compile shader */
    GLuint vs = compile_shader(GL_VERTEX_SHADER, wave_vert_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, wave_frag_src);
    if (!vs || !fs) {
        gpu.failed = true;
        return false;
    }

    gpu.program = glCreateProgram();
    glAttachShader(gpu.program, vs);
    glAttachShader(gpu.program, fs);
    glBindAttribLocation(gpu.program, 0, "pos");
    glLinkProgram(gpu.program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linked = 0;
    glGetProgramiv(gpu.program, GL_LINK_STATUS, &linked);
    if (!linked) {
        fprintf(stderr, "lumo-wave-gpu: link failed\n");
        gpu.failed = true;
        return false;
    }

    gpu.u_time = glGetUniformLocation(gpu.program, "u_time");
    gpu.u_base_color = glGetUniformLocation(gpu.program, "u_base_color");
    gpu.u_wave_color = glGetUniformLocation(gpu.program, "u_wave_color");

    /* fullscreen quad */
    static const float quad[] = {
        -1, -1, 1, -1, -1, 1,
        -1, 1, 1, -1, 1, 1,
    };
    glGenBuffers(1, &gpu.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, gpu.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

    /* FBO + texture */
    glGenTextures(1, &gpu.tex);
    glBindTexture(GL_TEXTURE_2D, gpu.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (int)width, (int)height, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenFramebuffers(1, &gpu.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, gpu.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D, gpu.tex, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "lumo-wave-gpu: FBO incomplete\n");
        gpu.failed = true;
        return false;
    }

    gpu.tex_w = width;
    gpu.tex_h = height;
    gpu.initialized = true;
    fprintf(stderr, "lumo-wave-gpu: initialized %ux%u on %s\n",
        width, height, dev);
    return true;
}

/* ── public API ──────────────────────────────────────────────────── */

bool lumo_wave_gpu_available(void) {
    return !gpu.failed;
}

bool lumo_wave_gpu_render(uint32_t *pixels, uint32_t width, uint32_t height,
    float time_sec, uint32_t base_argb, uint32_t wave_argb)
{
    if (!wave_gpu_init(width, height))
        return false;

    eglMakeCurrent(gpu.display,
        gpu.surface != EGL_NO_SURFACE ? gpu.surface : EGL_NO_SURFACE,
        gpu.surface != EGL_NO_SURFACE ? gpu.surface : EGL_NO_SURFACE,
        gpu.context);

    glBindFramebuffer(GL_FRAMEBUFFER, gpu.fbo);
    glViewport(0, 0, (int)width, (int)height);

    glUseProgram(gpu.program);
    glUniform1f(gpu.u_time, time_sec);
    glUniform3f(gpu.u_base_color,
        (float)((base_argb >> 16) & 0xFF) / 255.0f,
        (float)((base_argb >> 8) & 0xFF) / 255.0f,
        (float)(base_argb & 0xFF) / 255.0f);
    glUniform3f(gpu.u_wave_color,
        (float)((wave_argb >> 16) & 0xFF) / 255.0f,
        (float)((wave_argb >> 8) & 0xFF) / 255.0f,
        (float)(wave_argb & 0xFF) / 255.0f);

    glBindBuffer(GL_ARRAY_BUFFER, gpu.vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    /* read back to SHM buffer (RGBA → ARGB conversion) */
    glReadPixels(0, 0, (int)width, (int)height, GL_RGBA, GL_UNSIGNED_BYTE,
        pixels);

    /* GLES reads bottom-up; flip vertically and convert RGBA → ARGB */
    uint32_t *tmp = malloc(width * sizeof(uint32_t));
    if (tmp) {
        for (uint32_t y = 0; y < height / 2; y++) {
            uint32_t *top = pixels + y * width;
            uint32_t *bot = pixels + (height - 1 - y) * width;
            memcpy(tmp, top, width * 4);
            memcpy(top, bot, width * 4);
            memcpy(bot, tmp, width * 4);
        }
        free(tmp);
    }

    /* RGBA → ARGB (swap R and B) */
    for (uint32_t i = 0; i < width * height; i++) {
        uint32_t px = pixels[i];
        uint8_t r = (px >> 0) & 0xFF;
        uint8_t g = (px >> 8) & 0xFF;
        uint8_t b = (px >> 16) & 0xFF;
        pixels[i] = 0xFF000000 | ((uint32_t)r << 16) |
            ((uint32_t)g << 8) | b;
    }

    glDisableVertexAttribArray(0);
    return true;
}
