#include "lumo/compositor.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static void lumo_touch_audit_clear_samples(struct lumo_compositor *compositor) {
    size_t count;

    if (compositor == NULL) {
        return;
    }

    count = lumo_shell_touch_audit_point_count();
    if (count > sizeof(compositor->touch_audit_samples) /
            sizeof(compositor->touch_audit_samples[0])) {
        count = sizeof(compositor->touch_audit_samples) /
            sizeof(compositor->touch_audit_samples[0]);
    }

    memset(compositor->touch_audit_samples, 0,
        count * sizeof(compositor->touch_audit_samples[0]));
    compositor->touch_audit_step = 0;
    compositor->touch_audit_completed_mask = 0;
    compositor->touch_audit_saved = false;
    compositor->touch_audit_profile_name[0] = '\0';
    compositor->touch_audit_device_name[0] = '\0';
    compositor->touch_audit_device_vendor = 0;
    compositor->touch_audit_device_product = 0;
}

static bool lumo_touch_audit_mkdir(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return false;
    }

    if (mkdir(path, 0755) == 0 || errno == EEXIST) {
        return true;
    }

    wlr_log_errno(WLR_ERROR, "touch-audit: failed to create %s", path);
    return false;
}

static void lumo_touch_audit_sanitize_name(
    char *buffer,
    size_t buffer_size,
    const char *value
) {
    size_t length = 0;

    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    if (value == NULL || value[0] == '\0') {
        snprintf(buffer, buffer_size, "touchscreen");
        return;
    }

    for (; *value != '\0' && length + 1 < buffer_size; ++value) {
        unsigned char ch = (unsigned char)*value;

        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
            buffer[length++] = (char)ch;
            continue;
        }
        if (ch >= 'A' && ch <= 'Z') {
            buffer[length++] = (char)(ch - 'A' + 'a');
            continue;
        }
        if (length > 0 && buffer[length - 1] != '-') {
            buffer[length++] = '-';
        }
    }

    while (length > 0 && buffer[length - 1] == '-') {
        --length;
    }

    if (length == 0) {
        snprintf(buffer, buffer_size, "touchscreen");
        return;
    }

    buffer[length] = '\0';
}

static bool lumo_touch_audit_profile_basename(
    struct lumo_compositor *compositor,
    char *buffer,
    size_t buffer_size
) {
    char slug[96];
    int written;

    if (compositor == NULL || buffer == NULL || buffer_size == 0) {
        return false;
    }

    lumo_touch_audit_sanitize_name(slug, sizeof(slug),
        compositor->touch_audit_device_name);
    written = snprintf(buffer, buffer_size, "%s-%04x-%04x-%s.json",
        slug,
        compositor->touch_audit_device_vendor & 0xFFFFu,
        compositor->touch_audit_device_product & 0xFFFFu,
        lumo_rotation_name(compositor->active_rotation));
    return written >= 0 && (size_t)written < buffer_size;
}

static bool lumo_touch_audit_profile_path(
    struct lumo_compositor *compositor,
    char *buffer,
    size_t buffer_size
) {
    const char *xdg_config_home;
    const char *home;
    char base_dir[256];
    char lumo_dir[320];
    char profile_dir[384];
    char basename[160];

    if (compositor == NULL || buffer == NULL || buffer_size == 0) {
        return false;
    }

    xdg_config_home = getenv("XDG_CONFIG_HOME");
    home = getenv("HOME");
    if (xdg_config_home != NULL && xdg_config_home[0] != '\0') {
        if (snprintf(base_dir, sizeof(base_dir), "%s", xdg_config_home) <
                0) {
            return false;
        }
    } else if (home != NULL && home[0] != '\0') {
        if (snprintf(base_dir, sizeof(base_dir), "%s/.config", home) <
                0) {
            return false;
        }
    } else {
        return false;
    }

    if (snprintf(lumo_dir, sizeof(lumo_dir), "%s/lumo", base_dir) < 0 ||
            snprintf(profile_dir, sizeof(profile_dir),
                "%s/touch-profiles", lumo_dir) < 0 ||
            !lumo_touch_audit_profile_basename(compositor, basename,
                sizeof(basename)) ||
            snprintf(buffer, buffer_size, "%s/%s", profile_dir, basename) < 0) {
        return false;
    }

    if (!lumo_touch_audit_mkdir(base_dir) ||
            !lumo_touch_audit_mkdir(lumo_dir) ||
            !lumo_touch_audit_mkdir(profile_dir)) {
        return false;
    }

    strncpy(compositor->touch_audit_profile_name, basename,
        sizeof(compositor->touch_audit_profile_name) - 1);
    compositor->touch_audit_profile_name[
        sizeof(compositor->touch_audit_profile_name) - 1] = '\0';
    return true;
}

static bool lumo_touch_audit_write_profile(
    struct lumo_compositor *compositor
) {
    char path[512];
    FILE *file;
    time_t now;
    struct tm now_tm = {0};
    char timestamp[64];
    size_t count;

    if (compositor == NULL || !lumo_touch_audit_profile_path(compositor, path,
            sizeof(path))) {
        return false;
    }

    file = fopen(path, "w");
    if (file == NULL) {
        wlr_log_errno(WLR_ERROR,
            "touch-audit: failed to open profile %s", path);
        return false;
    }

    now = time(NULL);
    if (localtime_r(&now, &now_tm) != NULL) {
        (void)strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S%z",
            &now_tm);
    } else {
        snprintf(timestamp, sizeof(timestamp), "unknown");
    }

    count = lumo_shell_touch_audit_point_count();
    if (count > sizeof(compositor->touch_audit_samples) /
            sizeof(compositor->touch_audit_samples[0])) {
        count = sizeof(compositor->touch_audit_samples) /
            sizeof(compositor->touch_audit_samples[0]);
    }

    fprintf(file, "{\n");
    fprintf(file, "  \"version\": 1,\n");
    fprintf(file, "  \"generated_at\": \"%s\",\n", timestamp);
    fprintf(file, "  \"rotation\": \"%s\",\n",
        lumo_rotation_name(compositor->active_rotation));
    fprintf(file, "  \"device\": {\n");
    fprintf(file, "    \"name\": \"%s\",\n",
        compositor->touch_audit_device_name[0] != '\0'
            ? compositor->touch_audit_device_name
            : "touchscreen");
    fprintf(file, "    \"vendor\": %u,\n",
        compositor->touch_audit_device_vendor);
    fprintf(file, "    \"product\": %u\n",
        compositor->touch_audit_device_product);
    fprintf(file, "  },\n");
    fprintf(file, "  \"points\": [\n");
    for (size_t i = 0; i < count; i++) {
        const struct lumo_touch_audit_sample *sample =
            &compositor->touch_audit_samples[i];
        const char *name = lumo_shell_touch_audit_point_name((uint32_t)i);

        fprintf(file, "    {\n");
        fprintf(file, "      \"name\": \"%s\",\n",
            name != NULL ? name : "unknown");
        fprintf(file, "      \"captured\": %s,\n",
            sample->captured ? "true" : "false");
        fprintf(file, "      \"raw_x_pct\": %.3f,\n", sample->raw_x_pct);
        fprintf(file, "      \"raw_y_pct\": %.3f,\n", sample->raw_y_pct);
        fprintf(file, "      \"logical_x_pct\": %.3f,\n",
            sample->logical_x_pct);
        fprintf(file, "      \"logical_y_pct\": %.3f\n",
            sample->logical_y_pct);
        fprintf(file, "    }%s\n", i + 1 < count ? "," : "");
    }
    fprintf(file, "  ]\n");
    fprintf(file, "}\n");

    if (fclose(file) != 0) {
        wlr_log_errno(WLR_ERROR,
            "touch-audit: failed to finalize profile %s", path);
        return false;
    }

    compositor->touch_audit_saved = true;
    wlr_log(WLR_INFO, "touch-audit: saved profile %s", path);
    return true;
}

static bool lumo_touch_audit_detect_point(
    const struct wlr_box *box,
    double lx,
    double ly,
    uint32_t *point_index
) {
    double local_x;
    double local_y;

    if (box == NULL || point_index == NULL || box->width <= 0 ||
            box->height <= 0) {
        return false;
    }

    local_x = lx - box->x;
    local_y = ly - box->y;
    for (uint32_t i = 0; i < lumo_shell_touch_audit_point_count(); i++) {
        struct lumo_rect rect = {0};

        if (!lumo_shell_touch_audit_point_rect((uint32_t)box->width,
                (uint32_t)box->height, i, &rect)) {
            continue;
        }
        if (lumo_rect_contains(&rect, local_x, local_y)) {
            *point_index = i;
            return true;
        }
    }

    return false;
}

void lumo_touch_audit_set_active(
    struct lumo_compositor *compositor,
    bool active
) {
    if (compositor == NULL || compositor->touch_audit_active == active) {
        return;
    }

    if (active) {
        if (compositor->launcher_visible) {
            lumo_protocol_set_launcher_visible(compositor, false);
        }
        if (compositor->keyboard_visible) {
            lumo_protocol_set_keyboard_visible(compositor, false);
        }
        lumo_touch_audit_clear_samples(compositor);
        compositor->touch_audit_active = true;
        lumo_protocol_set_scrim_state(compositor, LUMO_SCRIM_DIMMED);
        wlr_log(WLR_INFO, "touch-audit: active");
    } else {
        compositor->touch_audit_active = false;
        if (compositor->launcher_visible) {
            lumo_protocol_set_scrim_state(compositor, LUMO_SCRIM_MODAL);
        } else if (compositor->keyboard_visible) {
            lumo_protocol_set_scrim_state(compositor, LUMO_SCRIM_DIMMED);
        } else {
            lumo_protocol_set_scrim_state(compositor, LUMO_SCRIM_HIDDEN);
        }
        wlr_log(WLR_INFO, "touch-audit: inactive");
    }

    lumo_shell_state_broadcast_touch_audit(compositor);
    lumo_protocol_mark_layers_dirty(compositor);
}

void lumo_touch_audit_note_touch(
    struct lumo_compositor *compositor,
    const struct lumo_output *output,
    const struct wlr_input_device *device,
    const struct lumo_touch_point *point,
    double raw_x,
    double raw_y
) {
    struct wlr_box box = {0};
    uint32_t point_index = 0;
    struct lumo_touch_audit_sample *sample;

    if (compositor == NULL || !compositor->touch_audit_active ||
            output == NULL || output->wlr_output == NULL ||
            compositor->output_layout == NULL || point == NULL) {
        return;
    }

    wlr_output_layout_get_box(compositor->output_layout, output->wlr_output, &box);
    if (wlr_box_empty(&box) ||
            !lumo_touch_audit_detect_point(&box, point->lx, point->ly,
                &point_index)) {
        return;
    }

    if (point_index != compositor->touch_audit_step) {
        wlr_log(WLR_INFO,
            "touch-audit: saw %s while waiting for %s",
            lumo_shell_touch_audit_point_name(point_index),
            lumo_shell_touch_audit_point_name(compositor->touch_audit_step));
        return;
    }

    sample = &compositor->touch_audit_samples[point_index];
    sample->captured = true;
    sample->raw_x_pct = raw_x * 100.0;
    sample->raw_y_pct = raw_y * 100.0;
    sample->logical_x_pct = ((point->lx - box.x) / box.width) * 100.0;
    sample->logical_y_pct = ((point->ly - box.y) / box.height) * 100.0;
    compositor->touch_audit_completed_mask |= (1u << point_index);
    compositor->touch_audit_step++;

    if (device != NULL) {
        snprintf(compositor->touch_audit_device_name,
            sizeof(compositor->touch_audit_device_name), "%s",
            device->name != NULL ? device->name : "touchscreen");
    }

    wlr_log(WLR_INFO, "touch-audit: captured %s (%u/%zu)",
        lumo_shell_touch_audit_point_name(point_index),
        compositor->touch_audit_step,
        lumo_shell_touch_audit_point_count());
    lumo_shell_state_broadcast_touch_audit(compositor);

    if (compositor->touch_audit_step >= lumo_shell_touch_audit_point_count()) {
        (void)lumo_touch_audit_write_profile(compositor);
        lumo_shell_state_broadcast_touch_audit(compositor);
        lumo_touch_audit_set_active(compositor, false);
    }
}
