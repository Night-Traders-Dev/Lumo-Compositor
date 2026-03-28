#include "lumo/compositor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void lumo_output_apply_rotation(
    struct lumo_output *output,
    enum lumo_rotation rotation
) {
    if (output == NULL || output->wlr_output == NULL) {
        return;
    }

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_transform(&state, lumo_rotation_to_transform(rotation));

    if (!wlr_output_commit_state(output->wlr_output, &state)) {
        wlr_log(WLR_ERROR,
            "output %s: failed to apply rotation %s",
            output->wlr_output->name,
            lumo_rotation_name(rotation));
    }

    wlr_output_state_finish(&state);
}

static void lumo_output_configure_scene(struct lumo_output *output) {
    if (output == NULL || output->compositor == NULL) {
        return;
    }

    if (output->compositor->layer_config_dirty) {
        lumo_protocol_configure_all_layers(output->compositor);
    }
    lumo_xwayland_sync_workareas(output->compositor);
}

static void lumo_output_frame(struct wl_listener *listener, void *data) {
    struct lumo_output *output = wl_container_of(listener, output, frame);

    lumo_shell_autostart_poll(output != NULL ? output->compositor : NULL);
    lumo_output_configure_scene(output);

    if (output->scene_output == NULL) {
        return;
    }

    /* Passing NULL for the options struct is correct: in wlroots 0.18 the
     * scene graph maintains its own damage tracking internally, so there is
     * no need to supply explicit damage rectangles here.  The scene layer
     * will only repaint regions that have actually changed. */
    wlr_scene_output_commit(output->scene_output, NULL);

    {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        wlr_scene_output_send_frame_done(output->scene_output, &now);
    }
    (void)data;
}

static void lumo_output_request_state(struct wl_listener *listener, void *data) {
    struct lumo_output *output = wl_container_of(listener, output, request_state);
    const struct wlr_output_event_request_state *event = data;

    if (!wlr_output_commit_state(output->wlr_output, event->state)) {
        wlr_log(WLR_ERROR,
            "output %s: failed to commit backend-requested state",
            output->wlr_output->name);
    }
}

static void lumo_output_destroy(struct wl_listener *listener, void *data) {
    struct lumo_output *output = wl_container_of(listener, output, destroy);

    if (output->compositor != NULL && output->compositor->output_layout != NULL) {
        wlr_output_layout_remove(output->compositor->output_layout,
            output->wlr_output);
    }

    if (output->scene_output != NULL) {
        wlr_scene_output_destroy(output->scene_output);
        output->scene_output = NULL;
    }

    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->request_state.link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->link);
    lumo_xwayland_sync_workareas(output->compositor);
    free(output);
    (void)data;
}

static void lumo_output_add(
    struct lumo_compositor *compositor,
    struct wlr_output *wlr_output
) {
    if (!wlr_output_init_render(wlr_output, compositor->allocator, compositor->renderer)) {
        wlr_log(WLR_ERROR,
            "output %s: failed to initialize renderer bindings",
            wlr_output->name);
        return;
    }

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);

    struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
    if (mode != NULL) {
        wlr_output_state_set_mode(&state, mode);
    }
    wlr_output_state_set_transform(
        &state,
        lumo_rotation_to_transform(compositor->active_rotation)
    );

    if (!wlr_output_commit_state(wlr_output, &state)) {
        wlr_log(WLR_ERROR,
            "output %s: failed to enable output",
            wlr_output->name);
        wlr_output_state_finish(&state);
        return;
    }
    wlr_output_state_finish(&state);

    struct lumo_output *output = calloc(1, sizeof(*output));
    if (output == NULL) {
        wlr_log_errno(WLR_ERROR, "output %s: allocation failed", wlr_output->name);
        return;
    }

    output->compositor = compositor;
    output->wlr_output = wlr_output;
    output->usable_area_valid = false;
    output->frame.notify = lumo_output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);
    output->request_state.notify = lumo_output_request_state;
    wl_signal_add(&wlr_output->events.request_state, &output->request_state);
    output->destroy.notify = lumo_output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);

    output->layout_output = wlr_output_layout_add_auto(
        compositor->output_layout,
        wlr_output
    );
    if (output->layout_output == NULL) {
        wlr_log(WLR_ERROR, "output %s: failed to add output to layout",
            wlr_output->name);
        wl_list_remove(&output->frame.link);
        wl_list_remove(&output->request_state.link);
        wl_list_remove(&output->destroy.link);
        free(output);
        return;
    }

    output->scene_output = wlr_scene_output_create(compositor->scene, wlr_output);
    if (output->scene_output == NULL) {
        wlr_log(WLR_ERROR, "output %s: failed to create scene viewport",
            wlr_output->name);
        wlr_output_layout_remove(compositor->output_layout, wlr_output);
        wl_list_remove(&output->frame.link);
        wl_list_remove(&output->request_state.link);
        wl_list_remove(&output->destroy.link);
        free(output);
        return;
    }

    wlr_scene_output_layout_add_output(
        compositor->scene_layout,
        output->layout_output,
        output->scene_output
    );

    wl_list_insert(&compositor->outputs, &output->link);
    compositor->layer_config_dirty = true;
    lumo_output_configure_scene(output);
    wlr_log(WLR_INFO, "output %s: ready", wlr_output->name);
}

static void lumo_backend_new_output(struct wl_listener *listener, void *data) {
    struct lumo_compositor *compositor =
        wl_container_of(listener, compositor, backend_new_output);
    struct wlr_output *wlr_output = data;

    lumo_output_add(compositor, wlr_output);
}

int lumo_output_start(struct lumo_compositor *compositor) {
    if (compositor == NULL || compositor->display == NULL) {
        return -1;
    }

    wl_list_init(&compositor->outputs);

    compositor->output_layout = wlr_output_layout_create(compositor->display);
    if (compositor->output_layout == NULL) {
        wlr_log(WLR_ERROR, "output: failed to create output layout");
        return -1;
    }

    compositor->scene = wlr_scene_create();
    if (compositor->scene == NULL) {
        wlr_log(WLR_ERROR, "output: failed to create scene graph");
        wlr_output_layout_destroy(compositor->output_layout);
        compositor->output_layout = NULL;
        return -1;
    }

    {
        float bg_color[4] = {0.10f, 0.0f, 0.07f, 1.0f};
        struct wlr_scene_rect *bg = wlr_scene_rect_create(
            &compositor->scene->tree, 8192, 8192, bg_color);
        if (bg != NULL) {
            wlr_scene_node_set_position(&bg->node, -4096, -4096);
            wlr_scene_node_lower_to_bottom(&bg->node);
            bg->node.data = NULL;
            wlr_scene_node_set_enabled(&bg->node, true);
        }
    }

    compositor->scene_layout = wlr_scene_attach_output_layout(
        compositor->scene,
        compositor->output_layout
    );
    if (compositor->scene_layout == NULL) {
        wlr_log(WLR_ERROR, "output: failed to attach output layout to scene");
        wlr_scene_node_destroy(&compositor->scene->tree.node);
        compositor->scene = NULL;
        wlr_output_layout_destroy(compositor->output_layout);
        compositor->output_layout = NULL;
        return -1;
    }

    /* touch indicator disabled */
    compositor->touch_indicator = NULL;

    compositor->backend_new_output.notify = lumo_backend_new_output;
    wl_signal_add(&compositor->backend->events.new_output,
        &compositor->backend_new_output);

    compositor->output_started = true;
    return 0;
}

void lumo_output_stop(struct lumo_compositor *compositor) {
    if (compositor == NULL || !compositor->output_started) {
        return;
    }

    wl_list_remove(&compositor->backend_new_output.link);

    struct lumo_output *output, *tmp;
    wl_list_for_each_safe(output, tmp, &compositor->outputs, link) {
        if (output->scene_output != NULL) {
            wlr_scene_output_destroy(output->scene_output);
            output->scene_output = NULL;
        }
        wl_list_remove(&output->link);
        free(output);
    }

    if (compositor->scene != NULL) {
        wlr_scene_node_destroy(&compositor->scene->tree.node);
        compositor->scene = NULL;
        compositor->scene_layout = NULL;
    }

    if (compositor->output_layout != NULL) {
        wlr_output_layout_destroy(compositor->output_layout);
        compositor->output_layout = NULL;
    }

    compositor->output_started = false;
}

void lumo_output_set_rotation(
    struct lumo_compositor *compositor,
    const char *output_name,
    enum lumo_rotation rotation
) {
    if (compositor == NULL) {
        return;
    }

    compositor->active_rotation = rotation;

    bool matched = false;
    struct lumo_output *output;
    wl_list_for_each(output, &compositor->outputs, link) {
        if (output_name != NULL && strcmp(output->wlr_output->name, output_name) != 0) {
            continue;
        }

        matched = true;
        lumo_output_apply_rotation(output, rotation);
        compositor->layer_config_dirty = true;
        lumo_output_configure_scene(output);

    }

    lumo_protocol_refresh_shell_hitboxes(compositor);
    lumo_protocol_mark_layers_dirty(compositor);

    {
        const char *home = getenv("HOME");
        if (home != NULL) {
            char path[256];
            snprintf(path, sizeof(path), "%s/.lumo-rotation", home);
            FILE *fp = fopen(path, "w");
            if (fp != NULL) {
                fprintf(fp, "%s\n", lumo_rotation_name(rotation));
                fclose(fp);
            }
        }
    }

    if (output_name != NULL && !matched) {
        wlr_log(WLR_INFO,
            "output: rotation request for unknown output '%s'",
            output_name);
    }

    lumo_shell_state_broadcast_rotation(compositor, rotation);
}
