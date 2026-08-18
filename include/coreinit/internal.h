#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int __os_snprintf(char *buf, size_t n, const char *format, ...);

#ifdef __cplusplus
}
#endif