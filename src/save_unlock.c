/* SPDX-License-Identifier: AGPL-3.0-only
 * Save layout / unlock behavior adapted from Tenkaichi3DecompAndroid:
 * include/sys/save.h, src/sys/save.c, McCard_Checksum in src/battle/stgm_a_b.c,
 * commit 3ca419c7966b11438e2dc3c9a7e1914f102ed545; z3xox and contributors.
 * Existing custom-character records are preserved rather than cleared.
 * Item NEW flags aren't randomized; owning the item is sufficient for unlock.
 */
#include "save_unlock.h"
#include <string.h>
static uint32_t get32(const uint8_t *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(uint8_t *p,uint32_t n)
{for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(i*8));}
static void put64(uint8_t *p,uint64_t n)
{for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(n>>(i*8));}
static uint32_t sum(const uint8_t *p)
{
    uint32_t value=0;
    for(unsigned i=8;i<DR_SAVE_BYTES;i++)value=(value+((uint32_t)p[i]<<((i&1)*8)))&0xffff;
    return value;
}
void dr_save_checksum(uint8_t data[DR_SAVE_BYTES])
{
    uint32_t value=sum(data);put32(data,value>>8);put32(data+4,value&255);
}
int dr_save_valid(const uint8_t *data,size_t size)
{
    if(!data || size!=DR_SAVE_BYTES)return 0;
    uint32_t value=sum(data);
    return get32(data)==value>>8 && get32(data+4)==(value&255);
}
int dr_save_unlock(uint8_t *data,size_t size,int max_money)
{
    if(!dr_save_valid(data,size))return 0;
    put32(data+8,get32(data+8)|0x7f);
    for(unsigned slot=0;slot<9;slot++) {
        unsigned offset=0x10+slot*0x10;
        put32(data+offset,get32(data+offset)|3);
        for(unsigned k=1;k<=3;k++)put32(data+offset+k*4,0xffff);
    }
    put32(data+0x208,get32(data+0x208)|1);put32(data+0x20c,20);
    put64(data+0xc10,UINT64_MAX);put64(data+0xc18,UINT64_MAX);
    /* Character ids 128..160 inclusive, 33 bits; retain reserved bits of existing saves. */
    for(unsigned bit=0;bit<33;bit++)data[0xc20+bit/8]|=(uint8_t)(1u<<(bit%8));
    for(unsigned bit=0;bit<35;bit++)data[0xc28+bit/8]|=(uint8_t)(1u<<(bit%8));
    put32(data+0xc30,get32(data+0xc30)|0x000fffff);
    for(unsigned i=0;i<350;i++)data[0x2ec8+i]|=1;
    put32(data+0x3028,max_money?9999999:4850000);
    dr_save_checksum(data);return 1;
}
void dr_save_new(uint8_t data[DR_SAVE_BYTES],int max_money)
{
    static const unsigned keys[8]={2,1,0,3,4,5,6,7};
    static const unsigned rules[6]={3,2,2,0,0,0};
    memset(data,0,DR_SAVE_BYTES);
    put32(data+0xc,1);put32(data+0x77c,99);put32(data+0x1608,7);
    for(unsigned i=0;i<6;i++)put32(data+0xc34+i*4,rules[i]);
    for(unsigned player=0;player<2;player++)for(unsigned k=0;k<8;k++) {
        put32(data+0x160c+player*32+k*4,keys[k]);
        put32(data+0x164c+player*32+k*4,keys[k]);
    }
    put32(data+0x16a8,9);put32(data+0x16ac,9);
    for(unsigned i=0;i<14;i++)put32(data+0x2d40+i*0x1c+0x18,UINT32_MAX);
    dr_save_checksum(data);(void)dr_save_unlock(data,DR_SAVE_BYTES,max_money);
}
