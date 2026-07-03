#ifndef VMX_ALLOC_H
#define VMX_ALLOC_H
#include <stddef.h>
void *vmx_alloc(size_t size);
void *vmx_realloc_buf(void *ptr, size_t new_size);
void  vmx_free(void *ptr);
void *vmx_calloc(size_t count, size_t size);
char *vmx_strdup(const char *s);
#endif
