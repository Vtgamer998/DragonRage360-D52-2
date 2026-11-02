# Integracao e compilacao no projeto atual

Os 12 modulos C compilaram para PowerPC com o SDK real. As notas de
nao compilado abaixo descrevem o pacote recebido e sao historicas.
Consulte ../../../RELATORIO-plataforma-0.4.md para o estado atual.

--- Documento original do anexo ---

# DragonRage360 — camada de plataforma, partes 1 a 4 (arquivos, memória, tempo, sistema, controles, dados do executável e conversão de recursos)

Escrita sem compilar para PowerPC e sem rodar no Xbox. Só foi feita uma checagem de sintaxe no PC
(`gcc -fsyntax-only`) com cabeçalhos falsos do xsdk. Espere erros na primeira compilação real.

## O que tem aqui

| Arquivo | Substitui (no port de PC) | O que faz |
| --- | --- | --- |
| `xb_port_addr.h` | `port/src/port_addr.h` | `Port_Ptr`/`Port_Addr`: endereço do jogo = endereço do host (ponteiros de 32 bits nativos). `-DXB_ADDR_WINDOW` liga o modo janela. |
| `xb_disc.c/.h` | (extração da ISO) | Monta a ISO uma vez para a execução inteira (`NtCreateFile`/`NtReadFile`, como o `main.c` do demo) e entrega `dr_disc`. |
| `xb_plat_file.c` | `plat_file.c` | API ADXF do jogo (`ADXF_OpenAfs`, `ADXF_ReadNw`, ...) lendo direto da ISO por `dr_game_file`. Mais `ADXPS2_*`, `ADXM_*`, `sceCd*`. |
| `xb_mem.h`, `xb_plat_mem.c` | `plat_mem.c` | Heap do jogo, regiões fixas (registradores 0x10000000, GS 0x12000000, scratchpad 0x70000000), `Port_LowAlloc`, `Port_GameBigAlloc`, e `XbMem_SelfTest`. |
| `xb_time.h`, `xb_plat_time.c` | `Port_VBlank` e `vblank_wait` de `plat_stub.c` | Ritmo do vblank (grade de 16,68 ms, mesma lógica do PC), `Port_BlankNs`, `Port_VBlankUsLeft`, `Port_PaceBehindUs`, `sceGsSyncV`/`sceGsSyncVCallback`. Relógio pelo time base do PowerPC. Ganchos por `XbTime_OnVBlank`. |
| `xb_plat_sys.c` | `plat_sys.c` + trechos de `plat_stub.c` | Chamadas de kernel/SIF/CD do PS2 (respostas do PC), heap do IOP, `Port_Rand`/`Port_Srand` (idêntico ao do PC: faz parte do estado da simulação) e `func_002BB390`. |
| `xb_pad.h`, `xb_plat_pad.c` | pads de `plat_stub.c`, `Port_PadRead`/vibração de `gs_input.c` | `scePad2*`, `sceVib*` e `Port_PadRead` a partir dos controles do Xbox (XInput via `XamInputGetState`). Mesmo mapa do PC: A=Cross, B=Circle, X=Square, Y=Triangle, LB/RB=L1/R1, LT/RT=L2/R2, Start/Back. |
| `xb_gamedata.h/.c`, `../tools/xb_strip_data.py` | `Port_LoadGameData` de `plat_mem.c`, `tools/strip_data.py` | Carrega os dados do `SLUS_216.78` e do `DBZP.BIN` (lidos da ISO montada) para dentro do programa a partir de uma lista `.dat` (formato `BT3X`), **invertendo os bytes por largura de unidade** (1, 2, 4 ou 8). A ferramenta gera a lista a partir do link do Xbox. |
| `xb_resconv.h/.c`, `../patches/bpe_endian.patch` | (nada no PC: lá não há conversão) | Onde a conversão de endianness dos recursos acontece: registro de conversores por id de arquivo, ganchos depois da leitura e depois do `Bpe_Decode`, ajudantes de troca de bytes e um relatório no log dos arquivos lidos sem conversor. **Não contém conversores de formato.** |

## Como ligar ao demo

1. Copie os arquivos para `src/` e `engine/platform/` e acrescente os `.c` ao `Makefile`, com `-Iengine/resource`.
2. Compile uma vez com `-DXB_USE_NTALLOC -DXB_USE_XAM` (veja a lista abaixo). Opcionais: `-DXB_USE_NTDELAY` (dorme no kernel em vez de girar no vblank) e `-DXB_PACE_LOG` (linha de ritmo no log a cada 60 blanks).
3. Antes de qualquer coisa do jogo, chame na ordem:
   `XbDisc_Mount(caminho_da_iso)`, `XbMem_Init()`, `XbMem_SelfTest()` e, só num programa sem os dados (feito por `xb_strip_data.py`), `XbGameData_LoadFromDisc("game:/programa.dat", 0, 0)`. Num build de desenvolvimento, com os dados ligados no próprio programa, não há `.dat` e nada disso é preciso.
4. Rode no console e me mande o `default.log`. As linhas `mem:` e `mem selftest:` dizem se o Xbox aceitou
   os endereços fixos.

## TODO-VERIFICAR (coisas que eu não consegui confirmar)

- **`NtAllocateVirtualMemory`**: usei a ordem de argumentos da implementação do emulador Xenia
  (`base*, size*, tipo, proteção, debug`). Confira contra `xsdk/kernel.h`; se o xsdk não declara a função
  ou usa outra assinatura, só `fixed_map()` em `xb_plat_mem.c` muda.
- **Endereços fixos baixos**: o jogo acessa 0x10000000, 0x12000000 e 0x70000000 por número. Não sei se o
  kernel do Xbox entrega esses endereços. `XbMem_Init` devolve uma máscara do que falhou e loga cada região.
- **Tamanho de leitura**: `NtReadFile` é chamada em pedaços de 1 MiB. Não conheço um limite, é só prudência.
- **Time base 49,875 MHz** (`xb_plat_time.c`): é o valor que conheço do Xenon. Confira com `-DXB_PACE_LOG`: o log deve dizer cerca de 16.6x ms entre blanks.
- **`NtDelayExecution`** (só com `-DXB_USE_NTDELAY`): confira a assinatura em `xsdk/kernel.h`. Sem a flag a espera gira no time base, correta mas ocupa um núcleo.
- **`XamInputGetState`/`XamInputSetState`** (só com `-DXB_USE_XAM`): declarei os dois como `extern` com os argumentos que conheço (índice, flags, estado). Veja se o xsdk os exporta e como os declara; só `xb_xam_poll` e `xb_xam_rumble` mudam. Sem a flag, `gXbPadPoll` fica nulo e os controles leem "parado".
- **Ritmo de vídeo**: o vblank aqui é por tempo (como o PC sem janela), não sincronizado com a tela real. Quando existir o GS no Xbox, ele decide.

- **Nomes na ISO** (`xb_gamedata.c`): `disc_read` pede `disc/SLUS_216.78` e `disc/BIN/DBZP.BIN` a `XbDisc_FindFile`. Não sei se o `dr_game_file` do demo aceita arquivos da raiz do disco; se não, só `disc_read` muda.
- **`xb_strip_data.py` não foi rodado num link real do Xbox** (não existe ainda). O que foi testado é o núcleo (`match_runs`, `tools/test_xb_strip.py`) e o carregador (`tools/test_xb_gamedata.c`), ambos com dados sintéticos. A parte que acha os objetos de dados (`objdump`, `nm`, mapa do linker, nomes `obj_data*/cod_XXXXXX.o`) é a do PC sem mudança: supõe que o build do Xbox nomeie e mapeie igual.
- **Flag `gPortDataStripped` num XEX**: a ferramenta altera o ELF. Num XEX comprimido/criptografado não dá para alterar depois; rode antes de gerar o XEX.
- **Estruturas com campos mistos**: cada execução tem uma só largura, então uma estrutura com `u8`, `u16` e `u32` seguidos sai da ferramenta em várias execuções. Isso só acerta se o montador do Xbox gerou o dado com as diretivas certas (`.byte`, `.half`, `.word`/`.long`). O que não casar em nenhuma largura fica no programa e aparece na lista "NOT matched" da ferramenta.

## Conversão de recursos (parte 4): o que existe e o que falta

**O que existe** (`xb_resconv`): o lugar certo para converter, não as conversões. O arquivo é lido em `ADXF_ReadNw`; o gancho chama o conversor registrado para o id do arquivo (`XbRes_Register(primeiro, ultimo, fn, empacotado)`). Para arquivo empacotado com BPE o conversor tem de rodar no resultado já descompactado, que só existe dentro do código do jogo: `patches/bpe_endian.patch` (aplicar em `src/sys/bpe.c`, compilar com `-DPORT_BIGENDIAN`) faz `Bpe_Decode` ler o cabeçalho de 8 bytes como little-endian (o resto do fluxo compactado são bytes, sem endianness) e chamar `XbRes_AfterBpe` no fim. `XbRes_SwapPlan(buf, n, "IH2B4x4*")` troca os bytes por um plano (B byte, H u16, I u32/float, Q u64/double, x pular; `*` repete até o fim) para escrever conversores curtos. Mudança no que estava na parte 1: `gXbFilePostRead` agora recebe também o deslocamento e o tamanho do arquivo, para distinguir leitura inteira de parcial (leitura parcial não é convertida e fica no log).

**Como saber o que falta**: ligue `XbRes_Install()` antes do jogo e rode. Cada arquivo lido sem conversor registra uma linha `res: file id N ... read with no converter` no `default.log`, uma vez por id; `XbRes_Report()` dá o resumo. Essa lista é o trabalho real.

**O que falta, e é grande**
- **Os formatos.** A documentação do repositório diz que os de modelo e textura "não são entendidos ainda" e que os arquivos `.AFS` "não foram abertos nem catalogados" (`docs/systems/graphics.md`, `files_and_assets.md`). Só alguns têm layout documentado e verificado: animação (cabeçalho com contagem de eventos e duração), tabelas de mensagens (offsets `u32`), arquivos de textura (contagem em +0, tabela em +4, entradas de 0x40 bytes), a tabela de itens (350 entradas de 0x28 bytes), modelos (campos de cabeçalho +0x4C..+0x58). Os conversores `dr_anim_prepare` e `dr_skeleton_prepare` do demo cobrem animações e esqueletos; devem poder ser registrados aqui com um adaptador pequeno (não vi as assinaturas, o demo não está nesta sessão).
- **`Res_RelocateOffsets`** (545 chamadas, ainda sem decompilar) transforma offsets em ponteiros depois da carga. O conversor tem de rodar antes dela e deixar os offsets na ordem do host.
- **Dados que o jogo lê de outro jeito**: o áudio ADX em streaming (cabeçalhos ADX são big-endian por definição: vale conferir se `snd_adx.c` do PC troca bytes e deixar de trocar aqui), `fight_panels.c` e os filmes `.PSS`.
- **Risco do rastreio**: um arquivo empacotado é lembrado pelo endereço do buffer em que foi lido. Se o jogo copiar o buffer antes de descompactar, o `Bpe_Decode` avisa no log ("not a registered packed file") e não converte. Uma leitura nova no mesmo buffer apaga o registro anterior.

**O problema maior que apareceu (fora do escopo desta camada): não há renderizador.** No PC, o `port/src/gs/` emula o GS e os microprogramas VU1 do PS2 (`gs_core.c`, `gs_vu1.c`, `gs_draw.c`, shaders GL). Nada disso existe no Xbox 360, e o jogo só desenha por esse caminho. Sem vídeo o jogo roda (`plat_stub.c` tem o modo sem janela), mas não mostra nada. Isso precisa de uma decisão sua antes de continuar.

## Limites desta parte

- **Sem mods, sem stages/músicas extras e sem aliases**: só o que está na ISO.
- **Sem caminhos de arquivo do host**: o áudio do PC (`snd_adx.c`, `snd_se.c`) e `fight_panels.c` usam
  `Port_FilePath` + `FILE*`. Uma ISO não tem isso. Eles precisam ser adaptados para `XbFile_Locate` +
  `XbFile_Read`, que estão em `xb_plat_file.c`.
- **Sem filmes**: `gPortMoviePath` existe vazio só para linkar.
- **Sem estado salvo (rollback)**: `Port_StateExtra`, `Port_LowRegionSize` e `Port_GameStackTop` não foram portados.
- **Port_VBlank é só o ritmo**: o do PC também chama `Port_AdxTick`, `PortA11y_GameTick`, `PortPanels_GameTick`, `Port_NetBeginTick`, `Port_SyncTest`, `Port_Trace`, `GsGpu_InterpPump`, pausa da janela de ajustes e a espera "low input lag". Nada disso está ligado. O que for reaproveitado (por exemplo `Port_AdxTick` de `plat_sndstate.c`) entra com `XbTime_OnVBlank`.
- **Controles**: sem teclado, sem touch, sem remapeamento, sem zona morta configurável (sticks passados direto, o padrão do PC), sem "Square nos clashes", sem gravação de entrada, sem online. Os 12 bytes de pressão ficam em zero, como no PC. Controle ausente lê como "parado" (como no PC); `scePad2GetState` devolve sempre 1.
- **Sem trava nas leituras da ISO**: o áudio em outra thread precisará de um lock.

## O que ainda não foi resolvido (e é grande)

1. **Endianness**: os arquivos da ISO são little-endian (MIPS) e o Xbox 360 é big-endian. `ADXF_ReadNw`
   devolve os bytes como estão no disco. O gancho `gXbFilePostRead` existe para os conversores do demo
   (`dr_anim_prepare`, `dr_skeleton_prepare`), mas só cobrem animações e esqueletos. As tabelas, a colisão,
   as texturas e os dados do executável (`SLUS_216.78`) ainda não têm conversão.
2. **Carga dos dados do jogo**: o PC lê o `SLUS_216.78` e o `DBZP.BIN` da ISO e copia as seções para o
   programa (`Port_LoadGameData`, guiado por um arquivo `.dat` gerado pelo link do PC). No Xbox esse
   `.dat` precisa ser gerado a partir do link do Xbox, e o `dr_elf_mips_span` do demo serve para ler as seções.
3. **Ponto flutuante**: o código do jogo precisa de soft-float (como no fork Android) e o xsdk usa hard-float.
4. **`irfix.py`**: o build do PC/Android corrige o IR do LLVM. O xsdk usa GCC; não sei se esses reparos
   são necessários em PowerPC de 32 bits.

## Sobre os 187 símbolos pendentes

- 80 já têm definição pronta em `port/src/` do repositório (áudio, controle, `Port_*`, `Dma_*`, ...).
- Dos 107 restantes, a maioria (`func_XXXXXXXX`, `dvCi*`, `htCi*`, `mfCi*`) só aparece como ponteiro em tabelas de
  dados, e o próprio `link.py` do PC gera um stub para eles. Falta confirmar `func_002967C0`, a única referenciada em C.

## Próximos módulos

os conversores de formato (a lista que o log do primeiro teste vai dar), a decisão sobre o renderizador e os stubs gerados.
