#include "alloc.h"
#include <stdlib.h>
#include <string.h>

void *vmx_alloc(size_t size) {
    if (size == 0) return NULL;
    return malloc(size);
}

void *vmx_realloc_buf(void *ptr, size_t new_size) {
    if (new_size == 0) { free(ptr); return NULL; }
    return realloc(ptr, new_size);
}

void vmx_free(void *ptr) {
    free(ptr);
}

void *vmx_calloc(size_t count, size_t size) {
    return calloc(count, size);
}

char *vmx_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *d = malloc(len);
    if (d) memcpy(d, s, len);
    return d;
}
