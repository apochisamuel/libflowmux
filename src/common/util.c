#include "util.h"

uint16_t vmx_read_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

uint32_t vmx_read_u32(const uint8_t *p) {
    return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

uint64_t vmx_read_u64(const uint8_t *p) {
    uint64_t lo = vmx_read_u32(p);
    uint64_t hi = vmx_read_u32(p + 4);
    return lo | (hi << 32);
}

void vmx_write_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

void vmx_write_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

void vmx_write_u64(uint8_t *p, uint64_t v) {
    vmx_write_u32(p, (uint32_t)(v & 0xFFFFFFFF));
    vmx_write_u32(p + 4, (uint32_t)(v >> 32));
}

uint32_t vmx_hash_mix(uint32_t a, uint32_t b) {
    a ^= b;
    a = ((a << 16) | (a >> 16));
    a *= 0x45D9F3B;
    a ^= a >> 16;
    a *= 0x45D9F3B;
    a ^= a >> 16;
    return a;
}

uint32_t vmx_fnv1a(const uint8_t *data, size_t len) {
    uint32_t h = 0x811C9DC5;
    for (size_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 0x01000193;
    }
    return h;
}
