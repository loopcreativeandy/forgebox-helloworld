#pragma once
#include <stddef.h>
void *HostMalloc(size_t n); void HostFree(void *p); void *HostRealloc(void *p, size_t n);
void HostAssert(const char *f, int l);
#define SRAM_MALLOC(n) HostMalloc(n)
#define SRAM_FREE(p) HostFree(p)
#define SRAM_REALLOC(p, n) HostRealloc(p, n)
#define EXT_MALLOC(n) HostMalloc(n)
#define EXT_FREE(p) HostFree(p)
