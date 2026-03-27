#include "lumo/compositor.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <wlr/backend/session.h>

static const char *lumo_backend_controlling_tty(void) {
    const char *tty_name = ttyname(STDIN_FILENO);

    if (tty_name != NULL) {
        return tty_name;
    }

    tty_name = ttyname(STDERR_FILENO);
    if (tty_name != NULL) {
        return tty_name;
    }

    tty_name = ttyname(STDOUT_FILENO);
    if (tty_name != NULL) {
        return tty_name;
    }

    return NULL;
}

static enum lumo_backend_mode lumo_backend_auto_selection(void) {
    const char *tty_name = lumo_backend_controlling_tty();

    return lumo_backend_auto_mode_for_session(
        tty_name,
        getenv("SSH_CONNECTION"),
        getenv("SSH_TTY"),
        getenv("WAYLAND_DISPLAY"),
        getenv("DISPLAY")
    );
}

static bool lumo_backend_session_looks_remote(void) {
    const char *ssh_connection = getenv("SSH_CONNECTION");
    const char *ssh_tty = getenv("SSH_TTY");

    return (ssh_connection != NULL && ssh_connection[0] != '\0') ||
        (ssh_tty != NULL && ssh_tty[0] != '\0');
}

int lumo_backend_start(struct lumo_compositor *compositor) {
    enum lumo_backend_mode selected_mode = LUMO_BACKEND_AUTO;

    if (compositor == NULL || compositor->display == NULL) {
        return -1;
    }

    if (compositor->config != NULL) {
        if (compositor->config->backend_mode != LUMO_BACKEND_AUTO) {
            const char *backend_name =
                lumo_backend_env_value(compositor->config->backend_mode);
            const char *tty_name = lumo_backend_controlling_tty();

            if (compositor->config->backend_mode == LUMO_BACKEND_DRM &&
                    lumo_backend_session_looks_remote()) {
                wlr_log(WLR_ERROR,
                    "backend: DRM mode was requested from a remote shell (current tty=%s); use --backend headless, --backend wayland, or --backend x11 for remote debugging",
                    tty_name != NULL ? tty_name : "none");
                return -1;
            }

            if (compositor->config->backend_mode == LUMO_BACKEND_DRM &&
                    !lumo_tty_name_looks_like_vt(tty_name)) {
                wlr_log(WLR_INFO,
                    "backend: DRM mode requested without a local VT (current tty=%s); continuing so logind or GDM can provide seat access",
                    tty_name != NULL ? tty_name : "none");
            }

            if (backend_name == NULL) {
                backend_name = lumo_backend_mode_name(
                    compositor->config->backend_mode);
            }
            if (setenv("WLR_BACKENDS", backend_name, 1) != 0) {
                wlr_log_errno(WLR_ERROR,
                    "backend: failed to set WLR_BACKENDS=%s", backend_name);
                return -1;
            }
            wlr_log(WLR_INFO, "backend: forcing WLR_BACKENDS=%s", backend_name);
        } else {
            const char *override = getenv("WLR_BACKENDS");

            if (override != NULL && override[0] != '\0') {
                wlr_log(WLR_INFO, "backend: honoring WLR_BACKENDS=%s", override);
            } else {
                selected_mode = lumo_backend_auto_selection();
                if (selected_mode != LUMO_BACKEND_AUTO) {
                    const char *backend_name =
                        lumo_backend_env_value(selected_mode);

                    if (backend_name == NULL) {
                        backend_name = lumo_backend_mode_name(selected_mode);
                    }

                    if (setenv("WLR_BACKENDS", backend_name, 1) != 0) {
                        wlr_log_errno(WLR_ERROR,
                            "backend: failed to set auto-selected WLR_BACKENDS=%s",
                            backend_name);
                        return -1;
                    }
                    wlr_log(WLR_INFO,
                        "backend: auto-selected WLR_BACKENDS=%s for this session",
                        backend_name);
                } else {
                    wlr_log(WLR_INFO, "backend: using wlroots backend autocreate");
                }
            }
        }
    }

    compositor->backend = wlr_backend_autocreate(
        compositor->event_loop,
        &compositor->session
    );
    if (compositor->backend == NULL) {
        wlr_log(WLR_ERROR, "backend: failed to autocreate wlroots backend");
        if (compositor->config != NULL &&
                compositor->config->backend_mode == LUMO_BACKEND_DRM) {
            wlr_log(WLR_ERROR,
                "backend: DRM mode failed; verify wlroots DRM support, /dev/dri access, and that you are running from a local VT or seat-managed session");
        } else if (compositor->config != NULL &&
                compositor->config->backend_mode == LUMO_BACKEND_AUTO) {
            wlr_log(WLR_ERROR,
                "backend: try --backend drm from a local VT, or --backend wayland, --backend x11, or --backend headless for nested or remote debugging");
        }
        return -1;
    }

    compositor->renderer = wlr_renderer_autocreate(compositor->backend);
    if (compositor->renderer == NULL) {
        wlr_log(WLR_ERROR, "backend: failed to create renderer");
        goto fail;
    }

    if (!wlr_renderer_init_wl_display(compositor->renderer, compositor->display)) {
        wlr_log(WLR_ERROR, "backend: failed to initialize wl_display renderer support");
        goto fail;
    }

    compositor->allocator = wlr_allocator_autocreate(
        compositor->backend,
        compositor->renderer
    );
    if (compositor->allocator == NULL) {
        wlr_log(WLR_ERROR, "backend: failed to create allocator");
        goto fail;
    }

    compositor->compositor_protocol = wlr_compositor_create(
        compositor->display,
        5,
        compositor->renderer
    );
    if (compositor->compositor_protocol == NULL) {
        wlr_log(WLR_ERROR, "backend: failed to create compositor global");
        goto fail;
    }

    compositor->subcompositor = wlr_subcompositor_create(compositor->display);
    if (compositor->subcompositor == NULL) {
        wlr_log(WLR_ERROR, "backend: failed to create subcompositor global");
        goto fail;
    }

    compositor->data_device_manager = wlr_data_device_manager_create(
        compositor->display
    );
    if (compositor->data_device_manager == NULL) {
        wlr_log(WLR_ERROR, "backend: failed to create data-device manager");
        goto fail;
    }

    return 0;

fail:
    lumo_backend_stop(compositor);
    return -1;
}

void lumo_backend_stop(struct lumo_compositor *compositor) {
    if (compositor == NULL) {
        return;
    }

    compositor->data_device_manager = NULL;
    compositor->subcompositor = NULL;
    compositor->compositor_protocol = NULL;

    if (compositor->allocator != NULL) {
        wlr_allocator_destroy(compositor->allocator);
        compositor->allocator = NULL;
    }

    if (compositor->renderer != NULL) {
        wlr_renderer_destroy(compositor->renderer);
        compositor->renderer = NULL;
    }

    if (compositor->backend != NULL) {
        wlr_backend_destroy(compositor->backend);
        compositor->backend = NULL;
    }

    if (compositor->session != NULL) {
        wlr_session_destroy(compositor->session);
        compositor->session = NULL;
    }
}
