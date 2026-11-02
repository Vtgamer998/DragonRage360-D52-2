/* SPDX-License-Identifier: AGPL-3.0-only; reference from z3xox and contributors. */
#include <stdio.h>
#include <stdint.h>
#include "common.h"
#include "sys/save.h"
#include "sys/common.h"
SaveData data;
SaveData *gSaveData=&data;
static CommonRes resources;
CommonRes *gCommonRes=&resources;
static unsigned char items[12+350*40];
s32 Rand_Range(s32 limit){(void)limit;return 0;}
void Progress_ClearSession(void){}
extern s32 McCard_Checksum(u32*,s32,s32);
int main(int argc,char **argv){
 if(argc!=2 || sizeof(data)!=16384)return 2;
 ((ItemFile*)items)->itemOffset=12;resources.data[2]=items;
 Save_SetDefaults(&data);Save_UnlockAll(&data);Save_AddMoney(SAVE_MONEY_MAX);
 McCard_Checksum((u32*)&data,SAVE_SIZE,1);
 FILE *f=fopen(argv[1],"wb");if(!f)return 1;int ok=fwrite(&data,1,sizeof(data),f)==sizeof(data);fclose(f);return !ok;
}
