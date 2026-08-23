#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void    __ghsLock(void);
void    __ghsUnlock(void);
void    __ghs_flock_create(uint32_t *outFlockIdx);
void    __ghs_flock_destroy(uint32_t flockIdx);
void    __ghs_flock_file(uint32_t flockIdx);
void    __ghs_funlock_file(uint32_t flockIdx);
int32_t __ghs_ftrylock_file(uint32_t flockIdx);
int    *__gh_errno_ptr(void);
int     __gh_get_errno(void);
void    __gh_set_errno(int error);
void __ghs_mtx_init(void **outMutex);
void __ghs_mtx_dst(void **mutex);
void __ghs_mtx_lock(void **mutex);
void __ghs_mtx_unlock(void **mutex);

#ifdef __cplusplus
}
#endif