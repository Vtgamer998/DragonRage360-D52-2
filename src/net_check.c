/* SPDX-License-Identifier: MIT
 * Socket use follows xsdk/tests/test_net.c (Mena, MIT).
 * This is a diagnostic, not the game's session/rollback implementation. */
#include "net_check.h"
#include "bt3_wire.h"
#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "xsdk/log.h"
#include "xsdk/net.h"
static uint32_t millis(void)
{
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)!=0) return 0;
    return (uint32_t)((uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000);
}
void demo_net_check(char *message,size_t cap)
{
    const int big=__BYTE_ORDER__==__ORDER_BIG_ENDIAN__;
    uint32_t native[3]={0x4E335442,5,0x12345678},back[3];
    uint8_t wire[12];
    const uint8_t expected[12]={0x42,0x54,0x33,0x4e,5,0,0,0,0x78,0x56,0x34,0x12};
    if(bt3_wire_encode(wire,native,12,big)!=12 || memcmp(wire,expected,12) ||
       bt3_wire_decode(back,wire,12,big)!=12 || memcmp(back,native,12)) {
        snprintf(message,cap,"Falha na serialização do protocolo.\nConsulte default.log.");
        xsdk_log_write("FAIL: BT3 wire byte order self-test");return;
    }
    xsdk_log_write("PASS: BT3 wire byte order self-test on native CPU");
    FILE *f=fopen("game:/net_host.txt","r");
    char ip[16];unsigned port;
    bool config=f && fscanf(f,"%15s %u",ip,&port)==2 && port>0 && port<=65535;
    if(f) fclose(f);
    struct sockaddr_in host={.sin_family=AF_INET};
    if(!config || inet_pton(AF_INET,ip,&host.sin_addr)!=1) {
        snprintf(message,cap,"Protocolo little-endian: OK.\n\nPara testar a conexão, crie net_host.txt\ncom o IP e a porta do seu servidor de eco.\nExemplo: 192.168.1.10 7370\nExecute net_echo.py no PC ou Termux.");return;
    }
    host.sin_port=htons((uint16_t)port);
    struct in_addr local; int error=xsdk_net_address(&local);
    if(error) {
        snprintf(message,cap,"Protocolo little-endian: OK.\nO console ainda não tem um endereço IPv4.\nErro %d. Confira a rede e tente novamente.",error);
        xsdk_log_write("FAIL: xsdk_net_address errno=%d",error);return;
    }
    int fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(fd<0) {
        snprintf(message,cap,"Protocolo OK. Falha ao abrir socket: %d.",errno);
        xsdk_log_write("FAIL: UDP socket errno=%d",errno);return;
    }
    native[2]=millis();
    bt3_wire_encode(wire,native,sizeof(native),big);
    uint32_t start=millis();bool matched=false;
    if(sendto(fd,wire,sizeof(wire),0,(const struct sockaddr *)&host,sizeof(host))==(ssize_t)sizeof(wire)) {
        struct pollfd wait={.fd=fd,.events=POLLIN};
        while((uint32_t)(millis()-start)<2000) {
            uint32_t elapsed=millis()-start;
            if(elapsed>=2000) break;
            int ready=poll(&wait,1,(int)(2000-elapsed));
            if(ready<=0) break;
            uint8_t reply[32];struct sockaddr_in from;
            socklen_t len=sizeof(from);
            ssize_t n=recvfrom(fd,reply,sizeof(reply),0,(struct sockaddr *)&from,&len);
            if(n==12 && from.sin_addr.s_addr==host.sin_addr.s_addr && from.sin_port==host.sin_port &&
               !memcmp(reply,wire,12)) {matched=true;break;}
        }
    }
    uint32_t elapsed=millis()-start;
    if(close(fd)!=0) xsdk_log_write("net check: close errno %d",errno);
    if(matched) {
        snprintf(message,cap,"Protocolo little-endian: OK.\nEco UDP: OK · ida e volta %u ms\nDestino: %s:%u\n\nTeste de conexão concluído.\nLutas e matchmaking ainda indisponíveis.",elapsed,ip,port);
        xsdk_log_write("PASS: UDP diagnostic echo %s:%u RTT=%u ms",ip,port,elapsed);
    } else {
        snprintf(message,cap,"Protocolo little-endian: OK.\nSem resposta UDP após até 2 segundos.\nDestino: %s:%u\nConfira script, firewall e IP.\n\nNenhuma partida foi iniciada.",ip,port);
        xsdk_log_write("FAIL: UDP diagnostic echo %s:%u",ip,port);
    }
}
