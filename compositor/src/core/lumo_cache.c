#include <stdbool.h>
#include <stddef.h>

bool lumo_cache_init(void) {
    return true;
}

bool lumo_cache_put(const char *key, const void *data, size_t size) {
    (void)key; (void)data; (void)size;
    return true;
}

void *lumo_cache_get(const char *key, size_t *size_out) {
    (void)key;
    if (size_out) *size_out = 0;
    return NULL;
}

bool lumo_cache_put_surface(const char *key, void *surface) {
    (void)key; (void)surface;
    return true;
}

void *lumo_cache_get_surface(const char *key) {
    (void)key;
    return NULL;
}

void lumo_cache_stats(void) {
    // Stub
}
