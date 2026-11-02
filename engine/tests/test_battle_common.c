/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "port_compat.h"
#undef malloc
#include "xb_battle_common.h"
#include "battle/btl_ai_think.h"
#include "sys/common.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
CommonRes *gCommonRes;
AiThMgr *gBtlAi;
static uint32_t read_word(const void *p){uint32_t n;memcpy(&n,p,4);return n;}
int main(int argc,char **argv){
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);assert(!fseek(f,0,SEEK_END));long len=ftell(f);assert(len>0);rewind(f);
 void *pack=malloc((size_t)len);assert(pack && fread(pack,1,(size_t)len,f)==(size_t)len);fclose(f);
 XbBattleCommon *c=NULL;XbBattleCommonInfo info;assert(!XbBattleCommon_Prepare(pack,(size_t)len,&c,&info));
 unsigned expected[]={448,40,64,64,164,19};assert(!memcmp(info.roster_records,expected,sizeof(expected)));
 assert(info.ai_steps==228 && info.ai_rules==537 && info.ai_profiles==28);XbBattleCommon *bad=(void *)1;XbBattleCommonInfo badinfo;assert(XbBattleCommon_Prepare(pack,100,&bad,&badinfo)<0 && !bad);free(pack);
 const void *p;size_t n;for(unsigned i=0;i<6;i++)assert(!XbBattleCommon_GetRosterTable(c,i,&p,&n) && p && n);
 assert(!XbBattleCommon_GetRosterTable(c,4,&p,&n));assert(read_word(p)==40000 && read_word((char *)p+4)==40000);
 void *ai=NULL;size_t size=0;
#if UINTPTR_MAX==UINT32_MAX
 _Static_assert(sizeof(AiThMgr)==0xa60 && sizeof(AiThWork)==0x520,"native AI manager/work layout");
 _Static_assert(sizeof(AiThData)==0x148,"native common AI header");
 assert(!XbBattleCommon_GetAiData(c,&ai,&size));AiThData *d=ai;uint8_t *base=ai;
 assert((uint8_t *)d->act==base+0x148 && d->act->unk4==base+0x8d0);
 size_t at=0x148+d->size[0];
 for(unsigned i=0;i<8;i++){assert((uint8_t *)d->rules[i]==base+at);assert((uint8_t *)d->rules[i]->rules==base+at+8);at+=d->size[1+i];}
 for(unsigned i=0;i<32;i++){assert(d->profile[i]==(d->size[9+i]?base+at:NULL));at+=d->size[9+i];}
 /* Compare every relocated pointer with the REAL original binder, using
  * a narrow native file fixture exposing only its AI member. This fixture
  * is never passed to BtlChar_AllocAll or installed in the shipped XEX. */
 uint8_t *file=calloc(1,size+32);assert(file);uint32_t off=32;memcpy(file+16,&off,4);memcpy(file+32,base,size);
 CommonRes common={0};AiThMgr manager={0};common.data[0]=file;gCommonRes=&common;gBtlAi=&manager;manager.frame=123;
 AiThink_BindData(0);AiThData *original=manager.data;assert((uint8_t *)original==file+32 && manager.frame==0);
 assert((uint8_t *)original->act-(uint8_t *)original==(uint8_t *)d->act-base);
 assert((uint8_t *)original->act->unk4-(uint8_t *)original==(uint8_t *)d->act->unk4-base);
 for(unsigned i=0;i<8;i++){assert(original->rules[i]->count==d->rules[i]->count);assert((uint8_t *)original->rules[i]-(uint8_t *)original==(uint8_t *)d->rules[i]-base);assert((uint8_t *)original->rules[i]->rules-(uint8_t *)original==(uint8_t *)d->rules[i]->rules-base);}
 for(unsigned i=0;i<32;i++){assert(!!original->profile[i]==!!d->profile[i]);if(d->profile[i])assert(original->profile[i]-(uint8_t *)original==d->profile[i]-base);}
 gCommonRes=NULL;gBtlAi=NULL;free(file);
 puts("original AiThink_BindData pointer/section equivalence PASS (32-bit CPU)");
#else
 assert(XbBattleCommon_GetAiData(c,&ai,&size)<0 && !ai && !size);
#endif
 XbBattleCommon_Free(c);puts("owned 6 roster tables + AI 228 steps/537 rules/28 profiles PASS; source freed; no manager/battle boot");return 0;
}
