# Licenças do projeto

D47: cache CLUT em `engine/capture/port/src/gs/gs_draw.c` adaptado de
`port/src/gs/gs_draw.c` de z3xox/Tenkaichi3Decomp, revisão
363e92c20af366db9a9050abe395b3416e2195fc, AGPL-3.0-only.
Licença preservada em engine/LICENSE-UPSTREAM-AGPL-3.0.txt.

O módulo de desbloqueio deriva de src/sys/save.c e do formato descrito por
Tenkaichi3DecompAndroid, de z3xox e colaboradores. save_unlock.c, save_export.c
e os testes de referência são AGPL-3.0-only. O texto está em LICENSE.
O executável combinado é distribuído sob AGPL-3.0, com suas fontes nesta pasta.

A interface, leitor ISO e código de diagnóstico anterior mantêm suas licenças
MIT nos cabeçalhos e em licenses/launcher-original-MIT.txt. Os avisos do xsdk
(MIT/BSD), Dragon Rage UI (MIT) e fonte bitmap estão em licenses/.
As ferramentas GCC/newlib usadas no build têm licenças próprias; os executáveis
das ferramentas não estão incluídos. O título liga código de newlib/libgcc;
os avisos e a exceção de runtime estão incluídos em licenses/. Não há dados de jogo extraídos de uma ISO nesta entrega.

Módulos de recursos 0.6: src/dr_anim.c, dr_bpe.c, dr_resource.h e
 dr_resource_selftest.c e correspondentes em engine/resource/:
AGPL-3.0-or-later. Formato e BPE baseados no código publicado de Dragon Rage /
Tenkaichi3DecompAndroid (dfdx047, z3xox e colaboradores). O patch mantém as
licenças do upstream. Veja engine/LICENSE-UPSTREAM-AGPL-3.0.txt.

0.7: dr_container.c/h e dr_skeleton.c seguem AGPL-3.0-or-later.
As cópias de dr_disc.c/h em engine/resource/ preservam MIT.

0.8: dr_gs_texture.c/h, dr_xenos.c/h, dr_graphics_demo.c/h, GLSL e testes
gráficos: AGPL-3.0-or-later. Tabelas/equações de textura adaptadas de
Tenkaichi3DecompAndroid port/src/gs/gs_core.c, autores dfdx047 / z3xox e
colaboradores. Uso das APIs e lifetime baseados no sample cube MIT do xsdk
(Mena e colaboradores); o aviso MIT do xsdk já acompanha em licenses/.
Shaders gerados de GLSL próprio por glslang e xost/xsc aberto.

0.9: vu/dr_vu1.c, wrappers, autoteste e patch VU: AGPL-3.0-or-later,
baseados no interpretador gs_vu1.c de dfdx047 / z3xox e colaboradores.
O código gerado mantém a procedência e inclui gerador e interface fonte.
Os microprogramas sintéticos dos testes foram escritos neste projeto; nenhum
microprograma comercial da ISO acompanha o pacote.

0.10–0.14: targets/replay, leitores facial/corpo/ELF, pipeline VU1 e
visualizador, shaders de restauração e seus testes: AGPL-3.0-or-later.
Procedência de layouts e microprograma: Tenkaichi3DecompAndroid revisão
3ca419c7966b11438e2dc3c9a7e1914f102ed545. O programa VU comercial é lido
apenas da ISO do usuário; não acompanha os fontes ou o executável.
A interpretação rejeita NaN em FTOI para evitar conversão indefinida em C;
isso não declara emulação fiel desse caso do PS2.

0.15: dr_texfile.c/h, testes e ferramenta de referência: AGPL-3.0-or-later.
Layouts e referência: gfxm_b_b.h, gfxm_b_b.c e port/tools/menu_font.py do
upstream fixado. Nenhum pixel comercial foi incorporado ao código.

0.16: vínculo TEX0/STQ, seleção facial e testes: AGPL-3.0-or-later.
Referências: BtlObj_BindTables e ObjDraw_DrawParts do upstream fixado.
A textura e as listas faciais são lidas da ISO do usuário, sem dados
comerciais embutidos nos fontes ou no XEX.

0.19: fontes de captura/núcleo/headers em engine/capture/ derivados do upstream
AGPL-3.0 na revisão 3ca419c7966b11438e2dc3c9a7e1914f102ed545; alterações
locais no patch xenos-backend-registration.patch. Os novos adaptadores de
renderização/diagnóstico e testes são AGPL-3.0-or-later.

0.20: shaders dr_vu0.vert, dr_vu4.vert e dr_vu6.vert derivados dos shaders
AGPL do upstream 3ca419c7966b11438e2dc3c9a7e1914f102ed545. Os headers
compilados correspondentes, alterações na ponte/núcleo e testes acompanham
a entrega. O teste numérico usa ferramentas do xsdk/xost fixado, sob as
licenças indicadas nas dependências; não contém microprograma de ISO.

0.21: dr_outline.frag e dr_dclut.frag adaptam os shaders AGPL outline.frag
e dclut.frag do upstream 3ca419c7966b11438e2dc3c9a7e1914f102ed545.
Os headers compilados, consumidores, testes e auditoria acompanham a
entrega. A paleta do diagnóstico é sintética, sem recursos comerciais.

Plataforma 0.4: arquivos xb_* recebidos com SPDX MIT preservados; adaptadores
de recursos e BPE/heap adaptados sao AGPL-3.0-or-later. O passe de compilacao
ps2_float_pass.cc e seus testes/ferramentas indicados sao GPL-3.0-or-later;
texto em licenses/float-pass-GPL-3.txt. Ele nao e incorporado ao XEX.


0.32: engine/platform/platform/xb_adxt.c está sob AGPL-3.0-or-later, com algoritmo de ADX adaptado de port/src/gs/snd_adx.c do upstream 3ca419c7966b11438e2dc3c9a7e1914f102ed545. O transporte de áudio usa as APIs públicas do xsdk (MIT). Os fontes e avisos das versões anteriores permanecem no pacote.

0.33: xb_mpeg2.c, xb_movie_source.c, xb_mpeg2.h e testes/ferramentas de vídeo sob AGPL-3.0-or-later. O decodificador adapta port/src/plat_mpeg2.c do upstream 3ca419c7966b11438e2dc3c9a7e1914f102ed545, preservando a origem das tabelas VLC/IDCT e algoritmos. A referência externa de imagem é FFmpeg executado em host; não é ligado ao XEX e nenhum vídeo comercial acompanha o pacote.
