/* SPDX-License-Identifier: MIT
 * Palette and launcher sections adapted from Dragon Rage's Color.kt / HomeScreen.kt.
 * This is a native C adaptation, not the Android Kotlin runtime. See licenses/.
 */
#include "demo_ui.h"
#include <stdio.h>
#include <string.h>

static const uint32_t ORANGE=0xFFFF8A1F, GOLD=0xFFFFD54F, BLUE=0xFF6FD3FF;
static const uint32_t WHITE=0xFFF6F2EB, MUTED=0xFFB6BED6, PANEL=0xFF161A2E;
static const char *const tabs[]={"Início","Biblioteca","Controle","Opções","Créditos","Online","Trapaças"};
static int rows(const DemoUi *u)
{
    if(u->page==DEMO_HOME) return 4;
    if(u->page==DEMO_LIBRARY) return u->count;
    if(u->page==DEMO_SETTINGS) return 3;
    if(u->page==DEMO_ONLINE) return 4;
    if(u->page==DEMO_CHEATS) return 2;
    return 0;
}
void demo_init(DemoUi *u)
{
    memset(u,0,sizeof(*u)); u->selected_iso=-1; u->show_help=true;
    snprintf(u->status,sizeof(u->status),"Escolha uma ISO da sua cópia do jogo.");
}
static void page(DemoUi *u,int p) { u->page=p; u->selection=0; }
int demo_input(DemoUi *u,uint16_t b)
{
    if(u->exit_prompt) {
        if(b&DEMO_A) return DEMO_EXIT;
        if(b&(DEMO_B|DEMO_BACK)) u->exit_prompt=false;
        return DEMO_NONE;
    }
    if(u->modal[0]) {
        if(b&DEMO_A) {
            int action=u->confirm_action;u->confirm_action=0;u->modal[0]=0;
            return action;
        }
        if(b&(DEMO_B|DEMO_BACK)) {u->confirm_action=0;u->modal[0]=0;}
        return DEMO_NONE;
    }
    if(b&DEMO_BACK) { u->exit_prompt=true; return DEMO_NONE; }
    if(b&DEMO_B) { page(u,DEMO_HOME); return DEMO_NONE; }
    if(b&(DEMO_LB|DEMO_RB)) {
        page(u,(u->page+((b&DEMO_RB)?1:DEMO_PAGES-1))%DEMO_PAGES);
        return u->page==DEMO_LIBRARY ? DEMO_SCAN:DEMO_NONE;
    }
    if(b&DEMO_Y) return u->page==DEMO_CONTROLS?DEMO_BODY_TEST:DEMO_SCAN;
    if((b&DEMO_X) && u->page==DEMO_CONTROLS) return DEMO_GRAPHICS_TEST;
    if((b&DEMO_X) && u->page==DEMO_LIBRARY && u->count) return DEMO_VERIFY;
    int n=rows(u);
    if(n && b&DEMO_UP) u->selection=(u->selection+n-1)%n;
    if(n && b&DEMO_DOWN) u->selection=(u->selection+1)%n;
    bool change=b&(DEMO_A|DEMO_LEFT|DEMO_RIGHT);
    if(u->page==DEMO_SETTINGS && change) {
        if(u->selection==0) u->high_contrast=!u->high_contrast;
        if(u->selection==1) u->fast_repeat=!u->fast_repeat;
        if(u->selection==2) u->show_help=!u->show_help;
        return DEMO_SAVE;
    }
    if(!(b&(DEMO_A|DEMO_START))) return DEMO_NONE;
    if(u->page==DEMO_LIBRARY && u->count) return DEMO_PLAY;
    if(u->page==DEMO_ONLINE) {
        const int actions[]={DEMO_LOBBY_HOST,DEMO_LOBBY_SEARCH,DEMO_LOBBY_DIRECT,DEMO_LOBBY_STOP};
        return actions[u->selection];
    }
    if(u->page==DEMO_CHEATS) {
        u->confirm_action=u->selection?DEMO_SAVE_UNLOCK:DEMO_SAVE_NEW;
        snprintf(u->modal,sizeof(u->modal),"Criar uma cópia com conteúdo desbloqueado?\nPersonagens, cenários, músicas e itens.\nZeni: 9.999.999.\n\nNenhum save existente será substituído.\nA confirmar · B cancelar.");
        return DEMO_NONE;
    }
    if(u->page==DEMO_HOME) {
        switch(u->selection) {
        case 0: page(u,DEMO_LIBRARY); return DEMO_SCAN;
        case 1: page(u,DEMO_CONTROLS); break;
        case 2: page(u,DEMO_SETTINGS); break;
        case 3: page(u,DEMO_ONLINE); break;
        }
    }
    return DEMO_NONE;
}
static void text(const DemoPaint *p,int x,int y,unsigned scale,uint32_t c,const char *s)
{ p->text(x,y,scale,c,s); }
static void row(const DemoUi *u,const DemoPaint *p,int index,int y,const char *label,const char *detail)
{
    bool selected=u->selection==index;
    uint32_t c=selected?ORANGE:PANEL;
    if(u->high_contrast) c=selected?0xFFFFFFFF:0xFF000000;
    p->rect(410,y,790,64,c);
    text(p,432,y+10,1,selected?0xFF211000:WHITE,label);
    text(p,432,y+34,1,selected?0xFF302215:MUTED,detail);
    if(selected) text(p,1160,y+20,1,0xFF211000,">");
}
void demo_draw(const DemoUi *u,const DemoPaint *p,unsigned tick)
{
    char b[180];
    p->rect(0,0,1280,720,0xFF0D0F1C);
    p->rect(0,0,1280,6,ORANGE);
    p->rect(52,52,68,68,ORANGE);
    text(p,66,66,2,0xFF211000,"DR");
    text(p,144,48,2,WHITE,"DRAGON RAGE");
    text(p,146,94,1,GOLD,"XBOX 360  /  DIAGNOSTICO D46");
    text(p,930,56,1,u->connected?0xFF3DDC84:GOLD,u->connected?"CONTROLE CONECTADO":"CONECTE O CONTROLE");
    text(p,930,88,1,MUTED,"Jogo local · Biblioteca de ISOs");
    p->rect(52,154,300,480,PANEL);
    for(int i=0;i<DEMO_PAGES;i++) {
        int y=174+i*64;
        if(i==u->page) p->rect(64,y-6,276,52,u->high_contrast?WHITE:0xFF7A3500);
        text(p,84,y+8,1,i==u->page?(u->high_contrast?0xFF000000:GOLD):MUTED,tabs[i]);
        if(i==u->page) text(p,306,y+8,1,ORANGE,">");
    }
    text(p,410,158,2,WHITE,tabs[u->page]);
    text(p,410,207,1,MUTED,"Budokai Tenkaichi 3 · projeto experimental");
    if(u->page==DEMO_HOME) {
        row(u,p,0,254,"Selecionar ISO",u->iso_valid?"ISO reconhecida · ver detalhes na biblioteca":"Sua ISO fica separada desta demonstração");
        row(u,p,1,330,"Controle Xbox 360","Teste os botões, gatilhos e analógicos");
        row(u,p,2,406,"Configurar interface","Contraste, ajuda e velocidade de navegação");
        row(u,p,3,482,"Dragon Net","Veja o estado da adaptação online");
        text(p,410,584,1,BLUE,"Motor de luta: ainda em desenvolvimento.");
    } else if(u->page==DEMO_LIBRARY) {
        if(!u->count) {
            text(p,410,270,1,GOLD,"Nenhuma ISO encontrada.");
            text(p,410,316,1,WHITE,"Copie o arquivo para a pasta isos/");
            text(p,410,348,1,MUTED,"ao lado do default.xex ou em usb0:/isos/.");
            text(p,410,398,1,BLUE,"Y  Procurar novamente");
        } else {
            int start=(u->selection/4)*4;
            for(int i=start;i<u->count && i<start+4;i++) {
                char label[110]; snprintf(label,sizeof(label),"%02d  %.85s",i+1,u->names[i]);
                row(u,p,i,254+(i-start)*76,label,"A  Iniciar jogo / luta local");
            }
            snprintf(b,sizeof(b),"%d de %d · Y atualizar · X verificar ISO",u->selection+1,u->count);
            text(p,410,574,1,BLUE,b);
        }
    } else if(u->page==DEMO_CONTROLS) {
        text(p,410,262,1,GOLD,"A confirmar   B voltar   LB/RB trocar aba");
        text(p,410,302,1,WHITE,"Direcional / analógico esquerdo: navegar");
        text(p,410,342,1,WHITE,"Y modelo da ISO   Back sair da demo");
        snprintf(b,sizeof(b),"Botões: %04X   LT: %3u   RT: %3u",u->buttons,u->lt,u->rt);
        text(p,410,416,1,BLUE,b);
        snprintf(b,sizeof(b),"Esquerdo X:%6d  Y:%6d",u->lx,u->ly); text(p,410,456,1,WHITE,b);
        snprintf(b,sizeof(b),"Direito  X:%6d  Y:%6d",u->rx,u->ry); text(p,410,496,1,WHITE,b);
        text(p,410,560,1,MUTED,"X testar gráficos · luta indisponível");
    } else if(u->page==DEMO_SETTINGS) {
        row(u,p,0,254,"Contraste alto",u->high_contrast?"Ligado":"Desligado");
        row(u,p,1,330,"Repetição de navegação",u->fast_repeat?"Rápida":"Normal");
        row(u,p,2,406,"Ajuda na tela",u->show_help?"Ligada":"Desligada");
        text(p,410,524,1,BLUE,"A ou esquerda/direita para alterar.");
        text(p,410,560,1,MUTED,"As opções são salvas ao lado do XEX.");
    } else if(u->page==DEMO_ONLINE) {
        row(u,p,0,254,"Criar sala LAN","Console host · uma vaga para cliente");
        row(u,p,1,330,"Partida rápida LAN","Descobre e conecta ao primeiro host livre");
        row(u,p,2,406,"Conectar por IP","Leia lobby_host.txt · IPv4 e porta UDP");
        row(u,p,3,482,"Encerrar conexão","Fecha sua sala ou sai do host");
        snprintf(b,sizeof(b),"Jogadores: %d/2 · %s",u->players,
                 u->lobby_role==1?"host":u->lobby_role==2?"cliente":"sem sessão");
        text(p,410,526,1,GOLD,b);
        text(p,410,562,1,BLUE,u->network[0]?u->network:"Rede desligada. Lutas ainda indisponíveis.");
    } else if(u->page==DEMO_CHEATS) {
        row(u,p,0,254,"Criar save desbloqueado","Novo arquivo RAW · conteúdo liberado e Zeni máximo");
        row(u,p,1,330,"Desbloquear meu save importado","Usa save_import/ e grava uma cópia em exports/");
        text(p,410,428,1,GOLD,"Seus saves existentes são preservados.");
        text(p,410,470,1,MUTED,"Aplicado a um arquivo separado para uso offline.");
        text(p,410,512,1,MUTED,"Não comprova todos os modos concluídos.");
        text(p,410,554,1,BLUE,"A luta do port ainda precisa ser integrada.");
    } else {
        text(p,410,264,1,GOLD,"Dragon Rage / Tenkaichi3DecompAndroid");
        text(p,410,306,1,WHITE,"dfdx047 · z3xox e colaboradores");
        text(p,410,364,1,GOLD,"xsdk / xenosworks");
        text(p,410,406,1,WHITE,"Mena e colaboradores · SDK aberto");
        text(p,410,464,1,MUTED,"Interface adaptada de HomeScreen e Color.kt.");
        text(p,410,502,1,MUTED,"Licenças e fontes acompanham a demo.");
        text(p,410,550,1,BLUE,"Projeto independente, sem afiliação oficial.");
    }
    p->rect(52,650,1176,2,0xFF38415B);
    if(u->show_help) text(p,64,673,1,MUTED,"A confirmar    B voltar    LB/RB abas    Y atualizar    Back sair");
    if(u->modal[0] || u->exit_prompt) {
        p->rect(370,230,850,350,0xFF050713);
        p->rect(370,230,850,6,ORANGE);
        text(p,400,260,2,GOLD,u->exit_prompt?"Sair da demo?":"Informações");
        text(p,400,324,1,WHITE,u->exit_prompt?"Voltar ao Aurora / dashboard?":u->modal);
        text(p,400,530,1,BLUE,u->exit_prompt?"A sair    B continuar":u->confirm_action?"A confirmar    B cancelar":"A / B fechar");
    }
    (void)tick;
}
