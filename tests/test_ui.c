#include "demo_ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
 DemoUi u; demo_init(&u);
 assert(u.page==DEMO_HOME && u.selected_iso==-1);
 assert(demo_input(&u,DEMO_A)==DEMO_SCAN && u.page==DEMO_LIBRARY);
 assert(demo_input(&u,DEMO_DOWN)==DEMO_NONE && u.selection==0);
 assert(demo_input(&u,DEMO_A)==DEMO_NONE);
 u.count=8;demo_input(&u,DEMO_UP);assert(u.selection==7);
 demo_input(&u,DEMO_DOWN);assert(u.selection==0);
 assert(demo_input(&u,DEMO_A)==DEMO_PLAY);
 assert(demo_input(&u,DEMO_X)==DEMO_VERIFY);
 demo_input(&u,DEMO_B);assert(u.page==DEMO_HOME);
 u.page=DEMO_CONTROLS;assert(demo_input(&u,DEMO_X)==DEMO_GRAPHICS_TEST);
 assert(demo_input(&u,DEMO_Y)==DEMO_BODY_TEST);
 demo_input(&u,DEMO_B);assert(u.page==DEMO_HOME);
 demo_input(&u,DEMO_LB);assert(u.page==DEMO_CHEATS);
 demo_input(&u,DEMO_RB);assert(u.page==DEMO_HOME);
 u.page=DEMO_CHEATS;u.selection=0;assert(demo_input(&u,DEMO_A)==DEMO_NONE);assert(u.confirm_action==DEMO_SAVE_NEW);
 demo_input(&u,DEMO_B);assert(!u.confirm_action && !u.modal[0]);
 demo_input(&u,DEMO_A);assert(demo_input(&u,DEMO_A)==DEMO_SAVE_NEW);
 u.page=DEMO_ONLINE;u.selection=0;assert(demo_input(&u,DEMO_A)==DEMO_LOBBY_HOST);
 demo_input(&u,DEMO_DOWN);assert(demo_input(&u,DEMO_A)==DEMO_LOBBY_SEARCH);
 demo_input(&u,DEMO_DOWN);assert(demo_input(&u,DEMO_A)==DEMO_LOBBY_DIRECT);
 demo_input(&u,DEMO_DOWN);assert(demo_input(&u,DEMO_A)==DEMO_LOBBY_STOP);
 u.page=DEMO_SETTINGS;u.selection=0;
 assert(demo_input(&u,DEMO_A)==DEMO_SAVE && u.high_contrast);
 u.selection=1;assert(demo_input(&u,DEMO_RIGHT)==DEMO_SAVE && u.fast_repeat);
 strcpy(u.modal,"erro");demo_input(&u,DEMO_A);assert(!u.modal[0]);
 demo_input(&u,DEMO_BACK);assert(u.exit_prompt);
 demo_input(&u,DEMO_B);assert(!u.exit_prompt);
 demo_input(&u,DEMO_BACK);assert(demo_input(&u,DEMO_A)==DEMO_EXIT);
 puts("PASS: menus, empty library, wrapping, options, modal and exit confirmation");
 return 0;
}
