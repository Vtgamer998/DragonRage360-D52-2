/* SPDX-License-Identifier: MIT */
#ifndef XB_FILE_H
#define XB_FILE_H
#include <stdint.h>
#include <stddef.h>
#include "dr_disc.h"
extern void (*gXbFilePostRead)(int,int,uint64_t,uint64_t,void*,size_t);
int XbFile_Locate(int,int,const char*,dr_span*);
size_t XbFile_Read(const dr_span*,uint64_t,void*,size_t);
int ADXF_LoadPartitionNw(int,char*,void*,void*);
void *ADXF_OpenAfs(int,int);
void *ADXF_Open(char*,void*);
void ADXF_Close(void*);
int ADXF_GetFsizeSct(void*);
int ADXF_ReadNw(void*,int,void*);
int ADXF_GetStat(void*);
#endif
