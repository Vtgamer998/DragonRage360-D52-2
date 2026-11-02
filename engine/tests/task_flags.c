/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "battle/eft_ae.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#undef BTL_TASK_BITS
#define BTL_TASK_BITS(task) DO_NOT_USE_NATIVE_BITFIELD_LAYOUT
BtlTask *gBtlTaskCur;
static int updates,resets,draws,terms;
static void update(BtlTask *p){assert(p==gBtlTaskCur);updates++;}
static void reset(BtlTask *p){(void)p;resets++;}
static void draw(BtlTask *p){(void)p;draws++;}
static void term(BtlTask *p){(void)p;terms++;}
void BtlTaskList_Destroy(BtlTaskList *p){assert(!p);}
void BtlTask_Kill(BtlTask *);
/* ORIGINAL_FUNCTIONS */
int main(void){
 BtlTask p;BtlTaskList list;BtlTaskCls cls={.update=update,.reset=reset,.draw=draw,.term=term};
 memset(&p,0,sizeof(p));memset(&list,0,sizeof(list));p.list=&list;list.head=list.tail=&p;
 BtlTask_Init(&p,&cls,NULL);assert(p.bits==252);
 BtlTaskList_Reset(&list,0);assert(resets==1 && terms==0 && list.head==&p);
 BtlTaskList_Draw(&list);assert(draws==0);
 BtlTaskList_Update(&list);assert(updates==1 && p.bits==254 && list.head==&p);
 BtlTaskList_Draw(&list);assert(draws==1);
 BtlTask_SetDead(&p);assert(p.bits==255);
 BtlTaskList_Reset(&list,0);assert(resets==2 && terms==1 && !list.head && !list.tail && list.free==&p);
 puts("PASS original task lifecycle with explicit PS2 bits: 0xFC survives reset; update sets bit1; dead bit0 alone triggers destruction; C bitfield access forbidden");
}
