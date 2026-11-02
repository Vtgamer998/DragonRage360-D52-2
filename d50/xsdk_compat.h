/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef D50_XSDK_COMPAT_H
#define D50_XSDK_COMPAT_H
#include <stddef.h>
#include <stdint.h>
/* Small platform layer for the bt3xbx -> xsdk migration (D50 milestone 1).
 * Only file, memory, time and log are covered. It does not replace xtl.h or
 * Direct3D; the graphics probe below uses dr_xenos directly.
 * Host builds use libc; Xbox 360 builds use the same libc that the title
 * already links, plus xsdk_log_write for output. */
typedef struct {void *data;size_t size;} D50Buffer;
/* 0 on success, -1 when the file is absent, -2 on a read/size failure.
 * out is zeroed on every failure so callers never see a partial buffer. */
int D50_ReadFile(const char *path,D50Buffer *out);
void D50_ReleaseFile(D50Buffer *buffer);
void *D50_Alloc(size_t bytes); /* zeroed, NULL on failure */
void D50_Release(void *memory);
uint64_t D50_NowUs(void);
void D50_Log(const char *format,...) __attribute__((format(printf,1,2)));
#endif
