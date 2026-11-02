#include "demo_ui.h"
#include <stdio.h>
#include <stdlib.h>
static void str(const char *s) {
 putchar('"'); for(;*s;s++) {if(*s=='"'||*s=='\\')putchar('\\'); if(*s=='\n'){fputs("\\n",stdout);continue;}putchar(*s);}putchar('"');
}
static void text(int x,int y,unsigned scale,uint32_t color,const char *s) {
 printf("[\"text\",%d,%d,%u,%u,",x,y,scale,color);str(s);puts("]");
}
static void rect(int x,int y,int w,int h,uint32_t color) {printf("[\"rect\",%d,%d,%d,%d,%u]\n",x,y,w,h,color);}
int main(int argc,char **argv) {
 DemoUi u;demo_init(&u);u.connected=true;
 if(argc>1)u.page=atoi(argv[1]);
 const DemoPaint p={text,rect};demo_draw(&u,&p,0);return 0;
}
