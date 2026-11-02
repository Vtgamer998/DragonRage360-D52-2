/* SPDX-License-Identifier: MIT */
#ifndef XB_ISO_FILE_H
#define XB_ISO_FILE_H
#include <stdint.h>
/* Single-owner, read-only Sony file calls over the currently mounted ISO.
 * Tokens are not host file descriptors. Reads return raw disc bytes. */
int32_t sceOpen(char *name,int32_t flags);
int32_t sceClose(int32_t fd);
int32_t sceLseek(int32_t fd,int32_t offset,int32_t whence);
int32_t sceRead(int32_t fd,void *buf,int32_t size);
int32_t sceCdSearchFile(void *file,char *name);
/* Frees all tokens without touching the mount, for orderly platform teardown. */
void XbIsoFiles_CloseAll(void);
int XbIsoFiles_Probe(void); /* bounded USA boot-ELF identity/read/seek probe, never loads it */
#endif
