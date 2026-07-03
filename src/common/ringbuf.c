#include "ringbuf.h"
#include "alloc.h"
#include <string.h>

vmx_ringbuf_t *vmx_ringbuf_create(size_t capacity) {
    if (capacity == 0) return NULL;

    vmx_ringbuf_t *rb = vmx_calloc(1, sizeof(vmx_ringbuf_t));
    if (!rb) return NULL;

    rb->data = vmx_alloc(capacity);
    if (!rb->data) { vmx_free(rb); return NULL; }

    rb->capacity = capacity;
    rb->read_pos = 0;
    rb->write_pos = 0;
    rb->count = 0;
    return rb;
}

void vmx_ringbuf_destroy(vmx_ringbuf_t *rb) {
    if (!rb) return;
    vmx_free(rb->data);
    vmx_free(rb);
}

int vmx_ringbuf_write(vmx_ringbuf_t *rb, const uint8_t *data, size_t len) {
    if (!rb || !data) return -1;
    if (rb->count + len > rb->capacity) return -1;

    for (size_t i = 0; i < len; i++) {
        rb->data[rb->write_pos] = data[i];
        rb->write_pos = (rb->write_pos + 1) % rb->capacity;
    }
    rb->count += len;
    return 0;
}

int vmx_ringbuf_read(vmx_ringbuf_t *rb, uint8_t *out, size_t len) {
    if (!rb || !out) return -1;
    if (rb->count < len) return -1;

    for (size_t i = 0; i < len; i++) {
        out[i] = rb->data[rb->read_pos];
        rb->read_pos = (rb->read_pos + 1) % rb->capacity;
    }
    rb->count -= len;
    return 0;
}

int vmx_ringbuf_peek(const vmx_ringbuf_t *rb, uint8_t *out, size_t len) {
    if (!rb || !out) return -1;
    if (rb->count < len) return -1;

    size_t pos = rb->read_pos;
    for (size_t i = 0; i < len; i++) {
        out[i] = rb->data[pos];
        pos = (pos + 1) % rb->capacity;
    }
    return 0;
}

size_t vmx_ringbuf_available(const vmx_ringbuf_t *rb) {
    return rb ? rb->count : 0;
}

size_t vmx_ringbuf_space(const vmx_ringbuf_t *rb) {
    return rb ? rb->capacity - rb->count : 0;
}

void vmx_ringbuf_reset(vmx_ringbuf_t *rb) {
    if (!rb) return;
    rb->read_pos = 0;
    rb->write_pos = 0;
    rb->count = 0;
}

bool vmx_ringbuf_is_empty(const vmx_ringbuf_t *rb) {
    return rb ? rb->count == 0 : true;
}

bool vmx_ringbuf_is_full(const vmx_ringbuf_t *rb) {
    return rb ? rb->count == rb->capacity : false;
}
