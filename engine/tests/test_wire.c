#include "bt3_wire.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static void put(uint8_t *p,uint32_t n,int big)
{for(int i=0;i<4;i++)p[big?3-i:i]=(uint8_t)(n>>(i*8));}
static void packet(uint8_t *p,unsigned type,int big)
{memset(p,0,2048);put(p,0x4e335442,big);put(p+4,type,big);}
int main(void)
{
 uint8_t native[2048],wire[2048],back[2048];
 /* Exact original protocol bytes for PING, not merely a round trip. */
 const uint8_t ping[]={0x42,0x54,0x33,0x4e,5,0,0,0,0x78,0x56,0x34,0x12};
 packet(native,5,1);put(native+8,0x12345678,1);
 assert(bt3_wire_encode(wire,native,12,1)==12 && !memcmp(wire,ping,12));
 assert(bt3_wire_decode(back,ping,12,1)==12 && !memcmp(back,native,12));
 for(unsigned type=1;type<=12;type++) {
  size_t n=8;
  packet(native,type,1);
  switch(type) {
  case 1:n=40;put(native+8,5,1);memcpy(native+12,"Player Xbox 360",15);put(native+32,0x4f500201,1);put(native+36,0x43410003,1);break;
  case 2:case 7:n=52;for(int i=2;i<6;i++)put(native+i*4,(unsigned)i,1);memcpy(native+24,"Jogador ç",11);put(native+44,0x4f500001,1);put(native+48,0x11223344,1);break;
  case 3:n=80;put(native+8,0x11223344,1);put(native+12,2,1);put(native+16,0xfffffff0,1);for(int i=20;i<44;i++)native[i]=(uint8_t)i;for(int i=44;i<80;i++)native[i]=(uint8_t)(255-i);break;
  case 4:case 5:case 6:n=12;put(native+8,0x12345678,1);break;
  case 10:n=22;put(native+8,123,1);put(native+12,6,1);memcpy(native+16,"Olá!\n",6);break;
  case 11:n=16;put(native+8,120,1);put(native+12,1,1);break;
  case 12:n=20;put(native+8,0x46505231,1);put(native+12,0x55667788,1);put(native+16,0x11223344,1);break;
  }
  assert(bt3_wire_encode(wire,native,n,1)==n);
  assert(bt3_wire_decode(back,wire,n,1)==n && !memcmp(back,native,n));
  if(type==1)assert(!memcmp(wire+12,native+12,20));
  if(type==2 || type==7)assert(!memcmp(wire+24,native+24,20));
  if(type==3)assert(!memcmp(wire+44,native+44,n-44));
  if(type==10)assert(!memcmp(wire+16,native+16,n-16));
  if(type==12){
   const uint8_t fp[]={0x42,0x54,0x33,0x4e,12,0,0,0,0x31,0x52,0x50,0x46,0x88,0x77,0x66,0x55,0x44,0x33,0x22,0x11};
   assert(!memcmp(wire,fp,sizeof(fp)));
  }
  assert(bt3_wire_encode(back,wire,n,0)==n && !memcmp(back,wire,n));
  assert(bt3_wire_decode(wire,wire,n,1)==n && !memcmp(wire,native,n));
 }
 /* Lobby HELLO_ACK has another layout. */
 packet(native,2,1);put(native+8,5,1);
 assert(bt3_wire_encode(wire,native,12,1)==12 && wire[8]==5);
 packet(native,3,1);put(native+12,17,1);assert(!bt3_wire_encode(wire,native,44,1));
 assert(!bt3_wire_decode(back,ping,7,1));
 packet(native,99,1);assert(!bt3_wire_encode(wire,native,8,1));
 packet(native,12,1);put(native+8,0x46505231,1);
 for(size_t n=0;n<28;n++)if(n!=20)assert(!bt3_wire_encode(wire,native,n,1));
 put(native+8,0,1);assert(!bt3_wire_encode(wire,native,20,1));
 puts("PASS: 12 shared packet types; exact fingerprint word order; Xbox/LE conversion; malformed sizes");
 return 0;
}
