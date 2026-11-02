/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef XB_FIGHTER_TABLES_H
#define XB_FIGHTER_TABLES_H
#include <stddef.h>
enum {XB_FIGHTER_PARAM, XB_FIGHTER_ATTACK, XB_FIGHTER_KIBLAST, XB_FIGHTER_MOVE,
 XB_FIGHTER_SUPER, XB_FIGHTER_SKILL, XB_FIGHTER_AI, XB_FIGHTER_CAMERA, XB_FIGHTER_TABLES};
/* Converts only the schema proven by native readers. Attack output contains
 * the 163 records reachable through BtlAtk_GetRecord[Of], not the opaque tail.
 * Output is transactional; src and dst must not overlap. */
int XbFighterTable_Convert(unsigned kind,const void *src,size_t bytes,int big_endian,
 void *dst,size_t capacity,size_t *written);
#endif
