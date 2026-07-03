#ifndef VMX_RINGBUF_H
#define VMX_RINGBUF_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    uint8_t  *data;
    size_t    capacity;
    size_t    read_pos;
    size_t    write_pos;
    size_t    count;
} vmx_ringbuf_t;

vmx_ringbuf_t *vmx_ringbuf_create(size_t capacity);
void           vmx_ringbuf_destroy(vmx_ringbuf_t *rb);
int            vmx_ringbuf_write(vmx_ringbuf_t *rb, const uint8_t *data, size_t len);
int            vmx_ringbuf_read(vmx_ringbuf_t *rb, uint8_t *out, size_t len);
int            vmx_ringbuf_peek(const vmx_ringbuf_t *rb, uint8_t *out, size_t len);
size_t         vmx_ringbuf_available(const vmx_ringbuf_t *rb);
size_t         vmx_ringbuf_space(const vmx_ringbuf_t *rb);
void           vmx_ringbuf_reset(vmx_ringbuf_t *rb);
bool           vmx_ringbuf_is_empty(const vmx_ringbuf_t *rb);
bool           vmx_ringbuf_is_full(const vmx_ringbuf_t *rb);

#endif
