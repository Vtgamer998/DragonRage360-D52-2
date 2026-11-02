/* SPDX-License-Identifier: AGPL-3.0-only */
#ifndef DR_SAVE_UNLOCK_H
#define DR_SAVE_UNLOCK_H
#include <stddef.h>
#include <stdint.h>
enum { DR_SAVE_BYTES=0x4000 };
/* Raw USA SaveData file, not a PS2 card image, PSV or PSU container.
 * Fields always use PS2 little-endian order, also on Xbox's PowerPC. */
int dr_save_valid(const uint8_t *data,size_t size);
int dr_save_unlock(uint8_t *data,size_t size,int max_money);
void dr_save_new(uint8_t data[DR_SAVE_BYTES],int max_money);
void dr_save_checksum(uint8_t data[DR_SAVE_BYTES]);
#endif
