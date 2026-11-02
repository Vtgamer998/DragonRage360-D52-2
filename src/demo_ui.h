/* SPDX-License-Identifier: MIT */
#ifndef DEMO_UI_H
#define DEMO_UI_H
#include <stdint.h>
#include <stdbool.h>
enum { DEMO_HOME, DEMO_LIBRARY, DEMO_CONTROLS, DEMO_SETTINGS, DEMO_ABOUT, DEMO_ONLINE, DEMO_CHEATS, DEMO_PAGES };
enum { DEMO_NONE, DEMO_SCAN, DEMO_VERIFY, DEMO_SAVE, DEMO_EXIT, DEMO_NET_CHECK, DEMO_SAVE_NEW, DEMO_SAVE_UNLOCK, DEMO_LOBBY_HOST, DEMO_LOBBY_SEARCH, DEMO_LOBBY_DIRECT, DEMO_LOBBY_STOP, DEMO_RESOURCE_TEST, DEMO_GRAPHICS_TEST, DEMO_BODY_TEST, DEMO_TYPED_TEST, DEMO_PLAY };
enum { DEMO_UP=1, DEMO_DOWN=2, DEMO_LEFT=4, DEMO_RIGHT=8, DEMO_START=16,
       DEMO_BACK=32, DEMO_LB=256, DEMO_RB=512, DEMO_A=4096, DEMO_B=8192,
       DEMO_X=16384, DEMO_Y=32768 };
enum { DEMO_MAX_ISOS=32, DEMO_PATH=512 };
typedef struct {
    int page, selection, count, selected_iso, confirm_action;
    int players, lobby_role; /* D52: 0/2..2/2 and 0 none, 1 host, 2 client */
    bool high_contrast, fast_repeat, show_help, connected, exit_prompt;
    uint16_t buttons;
    int16_t lx,ly,rx,ry;
    unsigned lt,rt;
    char paths[DEMO_MAX_ISOS][DEMO_PATH];
    char names[DEMO_MAX_ISOS][100];
    char iso_name[100], status[160], modal[700];
    char network[240];
    bool iso_valid;
} DemoUi;
typedef struct {
    void (*text)(int x,int y,unsigned scale,uint32_t color,const char *s);
    void (*rect)(int x,int y,int w,int h,uint32_t color);
} DemoPaint;
void demo_init(DemoUi *ui);
int demo_input(DemoUi *ui, uint16_t pressed);
void demo_draw(const DemoUi *ui,const DemoPaint *paint,unsigned tick);
#endif
