#ifndef VMX_UTIL_H
#define VMX_UTIL_H
#include <stdint.h>
#include <stddef.h>
uint16_t vmx_read_u16(const uint8_t *p);
uint32_t vmx_read_u32(const uint8_t *p);
uint64_t vmx_read_u64(const uint8_t *p);
void     vmx_write_u16(uint8_t *p, uint16_t v);
void     vmx_write_u32(uint8_t *p, uint32_t v);
void     vmx_write_u64(uint8_t *p, uint64_t v);
uint32_t vmx_hash_mix(uint32_t a, uint32_t b);
uint32_t vmx_fnv1a(const uint8_t *data, size_t len);
#endif
