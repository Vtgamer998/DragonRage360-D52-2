/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Font command observation queue. This is not a speech synthesizer. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef struct {uint16_t text[256];int flags;} Event;
static Event queue[16];static unsigned first,count;static int dispatching;
static void (*announce)(const uint16_t *,int,void *);static void *context;
void XbA11y_SetConsumer(void (*fn)(const uint16_t *,int,void *),void *ctx){announce=fn;context=ctx;}
int XbA11y_Observe(int kind,const uint16_t *text,int flags) {
    if(kind || !text)return 0;
    Event e={{0},flags};size_t n=0;
    while(n<255 && text[n] && (!(flags&1) || (text[n]!=10 && text[n]!=13))){e.text[n]=text[n];n++;}
    if(!n || n==255)return -1;
    if(count){Event *last=&queue[(first+count-1)%16];if(last->flags==flags && !memcmp(last->text,e.text,sizeof(e.text)))return 0;}
    if(count==16){first=(first+1)%16;count--;}
    queue[(first+count++)%16]=e;return 1;
}
void PortA11y_FontText(int kind,int arg,int flags) {
    if(kind || !arg)return;
    (void)XbA11y_Observe(kind,(const uint16_t *)(uintptr_t)(uint32_t)arg,flags);
}
int XbA11y_Dispatch(void) {
    if(dispatching)return -1;
    if(!announce)return 0; /* retain events until a real consumer is installed */
    dispatching=1;void (*fn)(const uint16_t *,int,void *)=announce;void *ctx=context;
    unsigned n=count;
    for(unsigned i=0;i<n;i++) {
        Event e=queue[first];first=(first+1)%16;count--;
        fn(e.text,e.flags,ctx);
    }
    dispatching=0;return (int)n;
}
