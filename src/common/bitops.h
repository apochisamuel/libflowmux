#ifndef VMX_BITOPS_H
#define VMX_BITOPS_H
#include <stdint.h>
#include <stdbool.h>
uint32_t vmx_popcount32(uint32_t x);
uint32_t vmx_clz32(uint32_t x);
uint32_t vmx_ctz32(uint32_t x);
uint32_t vmx_next_power_of_2(uint32_t x);
bool     vmx_is_power_of_2(uint32_t x);
uint32_t vmx_align_up(uint32_t x, uint32_t align);
uint32_t vmx_rotl32(uint32_t x, int r);
uint32_t vmx_rotr32(uint32_t x, int r);
#endif
