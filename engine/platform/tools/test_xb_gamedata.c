/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xb_gamedata.h"
static uint8_t elf[0x300],dbzp[128],memory[4096],list[16+4*20],saved[sizeof(list)];
static int mode,reenter,reads;static unsigned checks;
static const uint32_t BASE=0x82001000;
static void p16(uint8_t *p,uint16_t x){p[0]=x;p[1]=x>>8;}
static void p32(uint8_t *p,uint32_t x){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(x>>(8*i));}
static uint64_t fnv(uint64_t h,const uint8_t *p,size_t n){while(n--)h=(h^*p++)*0x100000001b3ull;return h;}
static int rd(void *ctx,const char *name,uint8_t **buf,size_t *size) {
    (void)ctx;reads++;
    if(reenter) {
        assert(XbGameData_ClearTargets()==XBGD_STATE);
        assert(XbGameData_SetTargets(NULL,0)==XBGD_STATE);
        assert(XbGameData_Load(NULL,NULL,0,0,0)==XBGD_STATE);
    }
    const uint8_t *src;
    if(!strcmp(name,"SLUS_216.78")){*size=sizeof(elf);src=elf;}
    else {assert(!strcmp(name,"DBZP.BIN"));*size=sizeof(dbzp);src=dbzp;}
    *buf=malloc(*size);assert(*buf);memcpy(*buf,src,*size);
    return (mode==1 && src==elf) || (mode==2 && src==dbzp)?-1:0;
}
static void setup(void) {
    mode=reenter=reads=0;memset(memory,0x5a,sizeof(memory));memset(elf,0,sizeof(elf));
    memcpy(elf,"\177ELF\1\1\1",7);p16(elf+16,2);p16(elf+18,8);p32(elf+20,1);
    p16(elf+40,52);p32(elf+32,0x100);p16(elf+46,40);p16(elf+48,2);
    uint8_t *sh=elf+0x128;p32(sh+4,1);p32(sh+8,2);p32(sh+12,0x100000);p32(sh+16,0x40);p32(sh+20,64);
    for(unsigned i=0;i<64;i++)elf[0x40+i]=(uint8_t)(i+1);
    for(unsigned i=0;i<sizeof(dbzp);i++)dbzp[i]=(uint8_t)(0xa0+i);
    memset(list,0,sizeof(list));memcpy(list,"BT3X",4);p32(list+4,4);
    uint32_t rows[4][5]={{BASE+0x40,0,16,8,8},{BASE+0x10,0,0,8,4},{BASE+0x30,1,4,4,1},{BASE+0x20,0,8,4,2}};
    uint64_t hash=0xcbf29ce484222325ull;
    for(unsigned i=0;i<4;i++) {
        for(unsigned k=0;k<5;k++)p32(list+16+i*20+k*4,rows[i][k]);
        hash=fnv(hash,rows[i][1]?dbzp+rows[i][2]:elf+0x40+rows[i][2],rows[i][3]);
    }
    for(unsigned i=0;i<8;i++)list[8+i]=(uint8_t)(hash>>(8*i));
    memcpy(saved,list,sizeof(list));
    XbDataTarget maps[2]={{BASE,memory,256},{BASE+0x1000,memory+512,256}};
    assert(!XbGameData_ClearTargets() && !XbGameData_SetTargets(maps,2));
}
static void pristine(void){for(size_t i=0;i<sizeof(memory);i++)assert(memory[i]==0x5a);}
static int load(long bias,int mod){XbDataFiles files={rd,NULL};return XbGameData_Load(&files,list,sizeof(list),bias,mod);}
static void reject(int want){int rc=load(0,0);assert(rc==want);pristine();checks++;}
int main(void) {
    setup();reenter=1;assert(!load(0,0));
    assert(!memcmp(memory+0x10,"\4\3\2\1\10\7\6\5",8));
    assert(!memcmp(memory+0x20,"\12\11\14\13",4));
    assert(!memcmp(memory+0x30,"\244\245\246\247",4));
    assert(!memcmp(memory+0x40,"\30\27\26\25\24\23\22\21",8));checks++;
    setup();list[8]^=1;reject(XBGD_HASH);assert(!load(0,1));checks++;
    setup();p32(list+16+20*3,BASE+256);reject(XBGD_BAD_TARGET);
    setup();p32(list+16+20*3,BASE+0x14);reject(XBGD_OVERLAP);
    setup();p32(list+16+20*3+4,2);reject(XBGD_BAD_RECORD);
    setup();p32(list+16+20*3+8,64);reject(XBGD_BAD_RECORD); /* allocated-image hole */
    setup();p32(list+16+20*3+12,0);reject(XBGD_BAD_RECORD);
    setup();p32(list+16+20*3+16,3);reject(XBGD_BAD_RECORD);
    setup();p32(list+16+20*3+8,9);reject(XBGD_BAD_RECORD);
    setup();p32(list+16+20*3,BASE+0x21);reject(XBGD_BAD_TARGET);
    setup();assert(load(-0x7fffffffL,0)==XBGD_BAD_TARGET);pristine();checks++;
#if LONG_MAX > 0xffffffffLL
    setup();assert(load(LONG_MAX,0)==XBGD_BAD_TARGET);pristine();checks++;
#endif
    setup();mode=1;reject(XBGD_NO_ELF);setup();mode=2;reject(XBGD_NO_DBZP);
    setup();elf[4]=2;reject(XBGD_NO_ELF);setup();elf[5]=2;reject(XBGD_NO_ELF);
    setup();p16(elf+18,20);reject(XBGD_NO_ELF);setup();p16(elf+46,1);reject(XBGD_NO_ELF);
    setup();p32(elf+32,0x2f0);reject(XBGD_NO_ELF);
    setup();p32(elf+0x128+16,0x2f0);reject(XBGD_NO_ELF);
    setup();p32(elf+0x128+12,0xfffffff0);reject(XBGD_NO_ELF);
    setup();p16(elf+48,3);memcpy(elf+0x150,elf+0x128,40);reject(XBGD_NO_ELF);
    setup();p32(list+4,0);reject(XBGD_BAD_LIST);setup();p32(list+4,65537);reject(XBGD_BAD_LIST);
    setup();assert(!XbGameData_ClearTargets());reject(XBGD_NO_TARGETS);assert(!reads);
    setup();XbDataFiles files={rd,NULL};
    assert(XbGameData_Load(NULL,list,sizeof(list),0,0)==XBGD_BAD_LIST);
    assert(XbGameData_Load(&files,list,sizeof(list)-1,0,0)==XBGD_BAD_LIST);
    uint8_t extra[sizeof(list)+1];memcpy(extra,list,sizeof(list));extra[sizeof(list)]=0;
    assert(XbGameData_Load(&files,extra,sizeof(extra),0,0)==XBGD_BAD_LIST);pristine();checks+=3;
    /* Failed target registration is atomic; no physical or logical aliases. */
    XbDataTarget bad[2]={{BASE,memory,256},{BASE+512,memory+128,256}};
    assert(XbGameData_SetTargets(bad,2)==XBGD_BAD_TARGET);assert(!load(0,0));checks++;
    setup();bad[1]=(XbDataTarget){BASE+128,memory+512,256};
    assert(XbGameData_SetTargets(bad,2)==XBGD_BAD_TARGET);assert(!load(0,0));checks++;
    setup();bad[0]=(XbDataTarget){0xfffffff0,memory,32};
    assert(XbGameData_SetTargets(bad,1)==XBGD_BAD_TARGET);assert(!load(0,0));checks++;
    /* Nonzero load bias translates logical addresses, not native pointers. */
    setup();XbDataTarget shifted={BASE+0x2000,memory,256};
    assert(!XbGameData_SetTargets(&shifted,1) && !load(0x2000,0));checks++;
    /* Destination writes may overwrite the manifest itself. The prepared
     * run descriptors and checksum are already private before commit. */
    setup();for(unsigned i=0;i<4;i++)p32(list+16+20*i,BASE+0x80+16*i);
    memcpy(memory+0x80,list,sizeof(list));
    assert(!XbGameData_Load(&files,memory+0x80,sizeof(list),0,0));
    assert(!memcmp(memory+0x80,"\30\27\26\25\24\23\22\21",8));
    assert(!memcmp(memory+0x90,"\4\3\2\1\10\7\6\5",8));checks++;
    /* Deterministic malformed-record sweep with a late-record failure. */
    for(unsigned i=0;i<512;i++) {
        setup();p32(list+16+20*3+8,0xffff0000u+i*4);reject(XBGD_BAD_RECORD);
    }
    assert(!XbGameData_ClearTargets());
    printf("PASS: %u data-loader checks; typed BE runs, strict ELF coverage, complete preflight, no writes on failures, maps/bias, re-entry, source errors and checksum; host only\n",checks);
}
