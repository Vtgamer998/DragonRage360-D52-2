/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "dr_gs_batch.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
static DrGsVertex vertices[4096];
static float vuv[4096*12],u[76],v[76];
static DrXenosTargetCommand seed(unsigned first){
 DrXenosTargetCommand c;memset(&c,0,sizeof c);c.target=2;c.command.vertices=vertices+first;c.command.count=3;c.command.texture=1;
 c.command.draw.logical_width=c.command.draw.logical_height=1024;c.command.draw.depth=1;
 c.command.draw.gs_material=1;c.command.draw.scissor[2]=c.command.draw.scissor[3]=1024;return c;
}
static void reject(DrXenosTargetCommand a,DrXenosTargetCommand b){
 DrXenosTargetCommand old=a,other=b;assert(!dr_gs_batch_merge(&a,&b));assert(!memcmp(&a,&old,sizeof a) && !memcmp(&b,&other,sizeof b));
}
int main(void){
 DrXenosTargetCommand a=seed(0),b=seed(3),old;
 old=b;assert(dr_gs_batch_merge(&a,&b) && a.command.count==6 && !memcmp(&b,&old,sizeof b));
 unsigned state_cases=0;
 for(size_t i=0;i<sizeof(DrXenosDraw);i++){
  if(i>=offsetof(DrXenosDraw,vu_uniform) && i<offsetof(DrXenosDraw,vu_uniform)+sizeof(a.command.draw.vu_uniform))continue;
  a=seed(0);b=seed(3);((unsigned char *)&b.command.draw)[i]^=1;reject(a,b);state_cases++;
 }
 for(unsigned i=1;i<=3;i++){a=seed(0);b=seed(3);b.snapshot=i;reject(a,b);a.snapshot=i;b.snapshot=0;reject(a,b);}
 a=seed(0);b=seed(3);b.target=3;reject(a,b);b=seed(3);b.command.texture=2;reject(a,b);
 b=seed(4);reject(a,b);b=seed(3);b.command.count=0;reject(a,b);
 b=seed(3);a.command.count=4;b.command.count=5;b.command.vertices=vertices+4;reject(a,b); /* 4+5 must not hide two invalid triangle counts. */
 a=seed(0);b=seed(3);a.command.count=1020;b.command.vertices=vertices+1020;assert(dr_gs_batch_merge(&a,&b) && a.command.count==1023);b.command.vertices=vertices+1023;reject(a,b);
 a=seed(0);b=seed(3);a.command.vertices=(const DrGsVertex *)(UINTPTR_MAX-10);reject(a,b);
 for(unsigned kind=1;kind<=3;kind++){
  a=seed(0);b=seed(3);a.command.vertices=b.command.vertices=NULL;a.command.vu_vertices=vuv;b.command.vu_vertices=vuv+36;
  a.command.draw.vu_program=b.command.draw.vu_program=kind;a.command.draw.vu_uniform=u;b.command.draw.vu_uniform=v;
  memset(u,0,sizeof u);memset(v,0,sizeof v);u[70]=v[70]=1;
  old=a;assert(dr_gs_batch_merge(&a,&b) && a.command.count==6);
  for(unsigned i=0;i<76;i++){a=old;v[i]+=1;reject(a,b);v[i]-=1;}
  a=old;b.command.draw.vu_uniform=NULL;reject(a,b);
  b.command.draw.vu_uniform=v;b.command.vu_vertices=vuv+48;reject(a,b);
 }
 /* Simple draws only: feedback reads, DATE, snapshots, effects and stencil are barriers even when equal. */
 for(unsigned field=0;field<4;field++){
  a=seed(0);b=seed(3);switch(field){case 0:a.command.draw.textured=b.command.draw.textured=2;break;case 1:a.command.draw.date=b.command.draw.date=1;break;case 2:a.command.draw.effect=b.command.draw.effect=1;break;default:a.command.draw.stencil=b.command.draw.stencil=1;}
  reject(a,b);
 }
 DrXenosTargetCommand grouped[1000];unsigned groups=0,total=0;
 for(unsigned i=0;i<1000;i++){b=seed(i*3);if(!groups || !dr_gs_batch_merge(&grouped[groups-1],&b))grouped[groups++]=b;}
 assert(groups==3);
 for(unsigned i=0;i<groups;i++){assert(grouped[i].command.vertices==vertices+total);assert(grouped[i].command.count<=1023);total+=grouped[i].command.count;}
 assert(total==3000);
 printf("PASS %u state-byte mutations, 228 uniform mutations, ordering/barriers/counts/overflow; synthetic 1000 compatible triangles -> 3 bounded draws\n",state_cases);
}
