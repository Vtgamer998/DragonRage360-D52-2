# DragonRage360 · D53 — Adaptador do motor real + RNG determinístico (sobre a D52-2)

Esta é a continuação direta da [D52-1](https://github.com/Vtgamer998/DragonRage360)
(lobby UDP) e da D52-2 (lockstep com hash). A D52-1 **não foi reescrita**: o
protocolo de pareamento original foi preservado byte a byte, e as camadas
seguintes foram adicionadas **por cima**, sem alterar um único campo dos
pacotes de controle.

O objetivo da D52-2 continua valendo, e a D53 acrescenta a peça que faltava:

> Dado o mesmo par de entradas no mesmo número de quadro, as duas CPUs
> devem produzir exatamente o mesmo estado — e esse estado agora pode ser o do
> **motor real**, não o de uma simulação de substituição.

- **D53-A** construiu a ponte: `start`/`update`/`end` ligados a
  `Battle_Init`/`Battle_StepFrame`/`Battle_Term`, com hash do estado real.
- **D53-B** fecha o determinismo: o gerador aleatório é **re-semeado no início
  de cada batalha online** e o hash passa a cobrir **toda a simulação** —
  lutadores, roster, o segundo RNG do motor, IA, script e o estado do próprio
  `rand()`. Sem isso, dois hashes iguais sobre `gBattleWork` provavam muito
  pouco.

A D53 não destrava jogabilidade online no console; ela destrava a *ponte*
para ela. Ver "Plano da D53" no fim deste arquivo.

---

## O que foi implementado

### 0. Motor restaurado e `main.c` compilando (D53-A, cuidado nº 1)

`engine/platform/`, `engine/combat/`, `engine/net/`, `engine/resource/`,
`engine/schema/`, `engine/patches/` e `engine/capture/` foram restaurados a
partir da branch `d50-bt3xbx` do repositório original. Isso resolve a
dependência de `xb_platform.h` que impedia a compilação de `src/main.c`.

```
$ gcc -c -std=gnu17 -O1 -Wall -Wextra ... -DDR_XBOX360 src/main.c -o main.o
EXIT=0      # zero avisos, zero erros
```

Nada no motor original foi modificado: os arquivos são cópias fiéis das
fontes da D52-1.

### 1. Protocolo de sincronização sobre o lobby existente

Dois novos tipos de pacote UDP, numerados **8 (INPUT)** e **9 (HASH)**, depois
dos sete tipos de controle da D52-1 (1–7). A numeração preservada significa
que uma console atualizada **ainda faz pareamento** com uma console antiga na
camada de lobby — a D52-2 só entra em ação depois do `LOBBY_PAIRED`.

Layout de fio (little endian, nenhum struct nativo é transmitido):

```
INPUT  [0..3]="DRL1" [4..7]=1 [8..11]=8 [12..15]=nonce [16..19]=session
       [20..23]=entrada [24..27]=quadro [28..31]=ack
HASH   [0..3]="DRL1" [4..7]=1 [8..11]=9 [12..15]=nonce [16..19]=session
       [20..23]=hash do estado [24..27]=quadro [28..31]=0
```

**Por que `dr_lobby_tick` não engole esses pacotes:** o loop de recepção do
D52-1 recusa qualquer datagrama cujas palavras de trailer (20/24/28) não
estejam zeradas — exatamente o espaço que INPUT e HASH usam. Além disso, a
camada de sincronização usa `MSG_PEEK` antes de consumir e só retira da fila
um datagrama que realmente é INPUT/HASH. Um `HEARTBEAT` ou `LEAVE` esperando
na fila nunca é descartado.

### 2. Lockstep com número de quadro

O design não é "manda a entrada e segue o jogo". É **lockstep**:

- Ambos os lados numéram os quadros a partir de **1** (0 significa "nada ainda").
- Um quadro **só é simulado quando a entrada daquele quadro é conhecida dos
  dois lados**. Uma perda de datagrama *estagna* a simulação em vez de fazer
  as duas máquinas rodarem quadros diferentes.
- `dr_lobby_peek_input` / `dr_lobby_consume_input` são separados de propósito:
  consumir e descartar um quadro futuro perde a entrada **enquanto o ack já
  diz que ela chegou** — é exatamente assim que se trava uma sessão real.

### 3. Janela de 16 quadros com ack e retransmissão de 120 ms

- Janela de transmissão/recepção de **16 quadros** (`D52_INPUT_DEPTH`), com
  ack por quadro, descarte de duplicados e **reordenação**.
- Retransmissão de **120 ms**, até 6 tentativas. A janela de 12 s do
  `dr_lobby_tick` permanece, mas só serve para detectar link morto — é três
  ordens de grandeza lenta demais para esconder um buraco de entrada em
  combate.
- `dr_lobby_send_input` devolve **1** para um quadro já confirmado pelo peer.
  Devolver 0 faria o harness de lockstep perguntar uma vez por quadro e
  travar para sempre.

### 4. Hash de estado determinístico

- `compute_battle_state_hash()` usa **FNV-1a** sobre palavras explícitas.
  Sem padding de struct, sem ponto flutuante, sem RNG, sem ponteiros: bytes
  idênticos entram, o mesmo número sai, nas duas consoles.
- O hash é publicado a cada quadro e comparado contra o do peer, com um anel
  de 16 entradas para atrasos. A API `dr_online_battle_desync()` devolve o
  quadro e os dois hashes em caso de divergência.

### 5. Harness instanciável

`DrOnlineBattle` é um struct puro (sem heap, sem globais), com `side` 0/1.
O teste de host roda **os dois lados no mesmo processo**; o console mantém
uma única instância `static`.

### 6. Adaptador de engine (D53-A)

`src/engine_adapter.{h,c}` define a costura entre o harness de lockstep e
**qualquer** motor de batalha, como uma tabela de ponteiros:

```c
typedef struct {
    const char *name;
    int  (*start)(void *ctx);                        /* Battle_Init   */
    int  (*update)(void *ctx,uint32_t in0,uint32_t in1); /* Battle_StepFrame */
    int  (*end)(void *ctx);                          /* Battle_Term   */
    uint32_t (*hash)(void *ctx);                     /* FNV-1a do estado */
} DrEngineOps;
```

Há dois backends:

| Backend | Arquivo | Onde roda |
|---|---|---|
| `dr_reference_ops` | `src/reference_adapter.c` | testes de host e CI — simulação inteira, sem GPU, sem ISO |
| `dr_bt3xbx_ops` | `src/bt3xbx_adapter.c` | o XEX, sob `-DDR_HAVE_BT3XBX` |

A máquina de fases (`unbound → idle → running → ended`) é o que torna o
início e o fim seguros: uma chamada fora de ordem (update antes de start, end
duas vezes, start enquanto roda) vira **erro reportado** e o backend **não é
chamado**. Garantias concretas:

- `end()` roda **no máximo uma vez** por `start()` — nunca um `Battle_Term` duplo;
- um `start()` recusado deixa a instância `idle`, então dá para tentar de novo;
- um `update()` que falha mantém a instância `running`, então o `end()` ainda
  pode liberar o que o motor alocou.

Trocar a simulação de substituto pelo motor real é **uma mudança de ponteiro**,
não uma refactorização: `dr_online_battle_init_with()` já aceita o backend.

### 7. A ponte para o motor real (D53-A, cuidado nº 2)

`src/bt3xbx_adapter.c` **não modifica o motor**. Ele chama apenas os três
pontos de entrada que o próprio motor expõe e documenta em
`vendor/tenkaichi-original/include/battle/battle.h`:

```
Battle_Main      running=1; Battle_Init(); Battle_Loop(); Battle_Term();
Battle_Loop      while (!Battle_StepFrame()) {}
Battle_StepFrame UM quadro; devolve != 0 quando a luta termina
```

Como `Battle_StepFrame` existe e avança exatamente um quadro, dirigir o motor
pelo lockstep **não exige nenhuma mudança no motor**: `start → Battle_Init`,
`update → Battle_StepFrame`, `end → Battle_Term`.

**A entrada entra por `gXbPadPoll`.** O caminho no motor é

```
Battle_StepFrame() → Pad_Update() → scePad2Read() → Port_PadRead()
                                             → gXbPadPoll(socket, &st)  ← junta
```

`xb_pad.h` documenta `gXbPadPoll` como substituível (*"any other source (a
test, a recording…) replaces it here"*). Enquanto a partida está online, o
adaptador instala ali as duas palavras de entrada **sincronizadas** e guarda a
função anterior; no `end()` ele a devolve, então o jogo offline continua lendo
o controle real. Sem isso, os dois consoles simulariam o mesmo quadro com
entradas diferentes e divergiriam no primeiro quadro.

**O hash cobre o estado real, não um placeholder.** `bt_hash()` faz FNV-1a
sobre os campos de `gBattleWork` lidos **por nome** — nunca por um intervalo
de bytes crus, porque `BattleWork` contém `BattleMember`, cujo layout depende
do tamanho do ponteiro (no host `BattleWork` tem 7384 bytes; no PowerPC,
0x1A00 = 6656). E `BattleMember.data` / `.buf` são **pulados de propósito**:
são ponteiros do heap do loader, e um endereço de heap não faz parte da
simulação.

---

### 8. Determinismo de ponta a ponta (D53-B)

Duas coisas faltavam para que hashes iguais significassem sincronia real.

#### 8.1 O gerador aleatório é re-semeado no início de cada batalha

O vendor chama `rand()` **363 vezes em 46 arquivos**, incluindo simulação
(`btl_char_hit.c`, `btl_seq.c`, `gsc.c`). E os **menus** também desenham com
`rand()` — e os dois consoles navegam os menus de forma independente. Sem
re-semeamento, o estado que `Battle_Init` herdaria já seria diferente no lado
A e no lado B, e o primeiro quadro divergiria.

`src/dr_rand.c` reimplementa o `rand()` do newlib **bit a bit** (LCG
`state*6364136223846793005+1`, `(state>>32)&RAND_MAX`, estado inicial 1) e
expõe `dr_rand/dr_srand/dr_rand_state/dr_rand_draws/dr_rand_reset`. No build
do XEX (`-DDR_PROVIDE_LIBC_RAND`) ele **substitui** `rand()`/`srand()`, para
que todo desenho do motor caia no estado que o hash lê. Os testes de host
ficam com o `rand()` da plataforma.

`bt_start()` chama `dr_rand_reset()` **antes de `Battle_Init`**. Isso muda
apenas o BGM/cosmético sorteado no caminho online; o caminho offline não é
tocado.

#### 8.2 O hash cobre a simulação inteira

`bt_hash()` passou a misturar, além de `gBattleWork`:

| Bloco | O que entra | Como |
|---|---|---|
| Lutadores | pose, ação, membros, gauges, timers, flags — tudo até `objTbl` | faixa de bytes pointer-free (assert de layout) |
| Roster `gBtlChars` | `count/flags/frame`, o **segundo RNG** (`unk18`/`randState`), timer de palco, estado de clash, pedidos de som | campos por nome |
| Fila de mudança | os 8 pedidos campo a campo + `write/read/state/loaded/timeStop` | campo a campo, **pulando `cur`** (é ponteiro para dentro da própria fila) |
| IA e script | `BtlAiMgr_GetFrame/GetType/GetLevel`, `BtlScript_GetStory/IsEventRunning` | getters (os blocos são cheios de ponteiros) |
| RNG do libc | `dr_rand_state()` | o contador de draws torna visível "um lado chamou `rand()` uma vez a mais" |

Fora do hash, de propósito: `BattleMember.data/.buf`, `gBtlChars->chars/unk8/
unkC/tbl[]`, `unk38`, `unk278` e `change.cur` — tudo ponteiro ou endereço de
heap, que numa consola carregada em endereço aleatório diferiria sempre.

#### 8.3 Contrato de layout ABI-agnóstico

Os offsets absolutos do cabeçalho (pose em `0x10`, `objTbl` em `0x15E8`…)
assumem `u64` de oito bytes — mas `types.h` define `u64` como `unsigned
long`, que é **quatro bytes no PowerPC de 32 bits** e oito no host LP64. Ou
seja: os números documentados **não valem em nenhum dos dois ABIs**.

Os `_Static_assert` do adaptador por isso afirmam **relações**, não literais:
`prevPose` encosta em `pose`, `objTbl` encosta em `padFlagA + 6 palavras`,
a fila tem o ponteiro logo após os oito pedidos. Essas relações valem nos
dois ABIs e são exatamente o que a faixa de bytes do hash precisa.

> **Descoberta (reportada, não corrigida — o motor está preservado):** como
> `u64` tem 4 bytes no alvo, `sizeof(BtlMgrChr)` é `0x1504` e
> `BtlMgrPose` tem `0x7C`, enquanto `BtlChar_AllocAll` aloca `count * 0x1600`
> e os comentários documentam `0x1600`/`0xF0`. O acesso é `&chars[i]`
> (stride `sizeof`), então a alocação maior só desperdiça o rabo de cada
> slot — não há overflow —, mas os offsets dos comentários estão
> desatualizados para este `typedef`.

---

## O que os testes provam — e o que não provam

```
$ make d52-test
PASS: D52 lobby selftest connected role=host+client players=2
PASS: D52 lobby selftest explicit leave role=client players=0
PASS: D52 lobby selftest host timeout role=host players=1 after 12001 ms
PASS: D52 lobby selftest client search timeout role=client players=0 after 31256 ms with clock wrap
PASS: D52-1 loopback lobby selftest, host only

D52-2 selftest: loopback only; two-console session still needs a physical test
PASS: D52-2 lobby paired with the unmodified D52-1 handshake
PASS: D52-2 lockstep harness bound host=side0 client=side1
D52-2 fault injection: dropped 2 datagram(s), retransmission must recover
PASS: D52-2 converged after 1 extra rounds at frame 184
PASS: D52-2 both sides at frame 184
PASS: D52-2 state hash matches: 150DFCE4
PASS: D52-2 91 peer hashes verified per side, no desync
PASS: D52-2 loopback online battle selftest, host only
```

```
$ make d53-test
PASS: D53-A bind refuses NULL ops, a missing end() and NULL ctx
PASS: D53-A update/end refused before start, backend untouched
PASS: D53-A start/update/end sequence, no double start, no double end
PASS: D53-A restart after end works (rematch path)
PASS: D53-A failed start leaves the instance startable
PASS: D53-A failed update still allows a safe end
PASS: D53-A two instances, same 300-frame sequence, identical hash B4727BDF
PASS: D53-A different inputs produce different hashes (the hash is not constant)
PASS: D53-A harness lockstep through the adapter at frame 534, hash 46E05A17, 266 peer hashes verified
PASS: D53-A engine adapter lifecycle, determinism and lockstep

PASS: D53-A bt3xbx start calls Battle_Init once and installs the seam
PASS: D53-B bt3xbx start reseeds the RNG before Battle_Init (draws=2 state=C0B18CCF4E252D17)
PASS: D53-A bt3xbx synchronized inputs reach the engine (pad0=0004 pad1=1000)
PASS: D53-A bt3xbx hash moves with the simulation (8BCF72D4)
PASS: D53-A bt3xbx hash skips BattleMember.data (heap pointers stays out of the comparison)
PASS: D53-B bt3xbx hash covers fighters, roster, second RNG, AI, script and change queue
PASS: D53-B bt3xbx hash covers the libc RNG state
PASS: D53-B bt3xbx hash keeps the change queue's pointer out of the comparison
PASS: D53-A bt3xbx one Battle_Term per end, pad seam restored, no double release
PASS: D53-A bt3xbx replaying the same 30 frames reproduces hash 05E13575
PASS: D53-A bt3xbx different inputs give a different hash (05E13575 vs 8CEA09F5)
PASS: D53-A bt3xbx bridge call sequence, pad injection, hash semantics and reproducibility
```

```
$ make d53-rand-test
PASS: dr_rand matches newlib's rand() (state 1, LCG 6364136223846793005, RAND_MAX 0x7FFFFFFF), reseeds and resets cleanly
```

Compilado com `-Wall -Wextra -Werror -fsanitize=address,undefined` — sem um
único aviso.

**Provado:**

- O protocolo D52-1 não foi quebrado; dois lados convergem para o mesmo quadro
  e o mesmo hash mesmo com atrasos, reordenação, duplicatas e **perda forçada
  de datagramas** (a recuperação pela retransmissão de 120 ms foi verificada
  de ponta a ponta).
- **Critério de conclusão da D53-A:** duas instâncias executam a mesma
  sequência de 300 quadros através do adaptador e produzem **hashes idênticos
  em todos os quadros**, com inicialização e encerramento seguros.
- O hash **não é constante**: entradas diferentes dão hash diferente (sem
  essa checagem a anterior seria vazia).
- A máquina de fases impede start/end duplicados e toda chamada fora de ordem,
  sem nunca chegar ao backend.
- A ponte chama `Battle_Init` uma vez, um `Battle_StepFrame` por quadro,
  `Battle_Term` uma vez; injeta as entradas sincronizadas (o stub lê de volta
  `pad0=0004 pad1=1000`); restaura `gXbPadPoll` no fim; e o hash ignora
  `BattleMember.data`.
- **D53-B:** o `start` **re-semeia o RNG antes de `Battle_Init`** — o teste
  envenena o gerador com 3 desenhos e verifica que, após o `start`, o estado
  é exatamente o do newlib avançado 2 vezes (os 2 desenhos do próprio
  `Battle_Init` do stub), com as constantes calculadas no teste a partir da
  fórmula do newlib, não lidas da implementação.
- **D53-B:** o hash cobre **lutadores, roster, o segundo RNG do motor, IA,
  script, fila de mudança e o estado do `rand()`** — cada poke move o hash —,
  e **não** cobre o ponteiro `change.cur` — um endereço não o move.
- **D53-B:** `dr_rand` reproduz o `rand()` do newlib bit a bit (13 checagens
  contra as constantes do newlib: estado inicial, fórmula, `RAND_MAX`,
  `srand`, `reset`, contagem de desenhos).

**Não provado** (e nenhum teste em loopback pode provar):

- Sessão física entre duas consoles (Wi-Fi, roteador, NAT).
- A pilha de rede do Xbox 360.
- Que o **motor real** do bt3xbx é determinístico. O teste da ponte usa
  `d50/bt3xbx_stub.c`, que imita o motor nos pontos de que a ponte
  depende — mas não é a luta. Isso continua sendo D53-B/B-console: os dois
  lados reais trocando hashes.
- Que o XEX compila e roda. A toolchain PowerPC foi construída e todos os
  arquivos novos (`bt3xbx_adapter.c`, `dr_rand.c`, `main.c` nas duas
  configurações) compilam com `-Wall -Wextra -Werror` para `powerpc-xsdk-elf`;
  falta montar o link e testar no console.

---

## Limitações conhecidas

1. **`rand()` — tratado na D53-B, com uma ressalva.** O vendor chama `rand()`
   **363 vezes em 46 arquivos**, incluindo simulação (`btl_char_hit.c`,
   `btl_seq.c`, `gsc.c`) e não só desenho. A D53-B resolve isso por dois
   lados: `src/dr_rand.c` substitui o `rand()` no build do XEX (LCG fiel ao
   newlib) e `bt_start()` **re-semeia antes de `Battle_Init`**, porque os
   **menus** também desenham e os dois consoles navegam de forma
   independente. O estado do gerador está **dentro do hash**, então um lado
   que chame `rand()` uma vez a mais diverge no próprio quadro em que isso
   acontece. O que ainda não foi verificado é a sequência real de 363
   chamadas **no console** — só dois bins idênticos com as mesmas entradas
   podem provar isso.
2. **A ponte foi provada contra um stub, não contra o motor.** `d50/bt3xbx_stub.c`
   implementa os quatro símbolos que o adaptador chama e lê os controles por
   `gXbPadPoll` exatamente como o motor faz. Ele prova a sequência de chamadas
   e a injeção de entrada; não prova a luta.
3. **`dr_bt3xbx_ops` é singleton.** O motor mantém um `gBattleWork` estático,
   então só existe uma instância dele. O critério de "duas instâncias" foi
   provado no backend de referência, que é instanciável.
4. **Nenhum rollback.** Um desync é detectado e logado, não corrigido. Rollback
   é D54 e depende do determinismo estar provado primeiro.
5. **Sem limite de quadro por segundo.** O lockstep estagna se um lado parar
   de responder; o mecanismo de saída é o timeout de 12 s da D52-1.
6. **A temporização interna foi verificada, não eliminada.** O relógio de
   batalha avança por tabela fixa indexada por contador de quadros
   (`BtlClock_Tick`), não por relógio de parede; `Battle_UpdateWork` conta
   quadros. A única espera em tempo real no quadro é `Gfx_EndFrame(2)` (dois
   vsyncs), que regula o **ritmo** de renderização e não escreve estado de
   simulação. Se algum dia isso mudar, a divergência aparece no hash.

---

## Plano da D53

| Etapa | Descrição | Estado |
|---|---|---|
| **A** | Adaptador de engine: interface `start`/`update`/`end`, ponte para `Battle_Init`/`Battle_StepFrame`/`Battle_Term`, hash do estado real, dois backends (referência + bt3xbx) | **concluída** |
| **B** | Determinismo: re-semeio do RNG antes de `Battle_Init`, `rand()` fiel ao newlib, hash cobrindo roster/lutadores/IA/script/fila/estado do RNG, asserts de layout ABI-agnósticos | **concluída** (host + compilação PowerPC) |
| **B2** | Ligar `-DDR_HAVE_BT3XBX` ao build do XEX, linkar `bt3xbx_adapter.o` + `dr_rand.o`, gerar `default.xex` e validar no RGH | próxima |
| **C** | Menus e partidas online | |
| **D** | Sessão física entre duas consoles | |

A etapa A existe para que a troca do substituto pelo engine real seja uma
mudança de **um ponteiro de função**, não uma refactorização de milhares de
linhas. Ela cumpriu esse papel: `dr_online_battle_init_with(b, lobby, side,
&dr_bt3xbx_ops, &estado)` é tudo que falta para rodar a luta real pelo
lockstep. A etapa B fechou o que faltava para que hashes iguais significassem
sincronia. O que ainda não está provado é que o motor real *seja*
determinístico — isso só se vê no console, com os dois lados trocando hashes.

---

## Como rodar os testes

```sh
git clone https://github.com/Vtgamer998/DragonRage360-D52-2.git
cd DragonRage360-D52-2
make d52-test        # D52-1 e D52-2 (lobby + lockstep), com sanitizers
make d53-test        # D53-A/B: adaptador, ponte (stub) e RNG — 36 checagens
make d53-rand-test   # só o RNG, contra as constantes do newlib
```

Requer apenas `gcc` e `make` (Linux). Nenhuma toolchain de Xbox é usada nos
testes — todo o código de rede roda em loopback sobre UDP real, e a ponte para
o motor roda contra `d50/bt3xbx_stub.c`. A compilação para o alvo
(`powerpc-xsdk-elf-gcc`, `-Wall -Wextra -Werror`) foi verificada separadamente
para `src/bt3xbx_adapter.c`, `src/dr_rand.c` e `src/main.c` nas duas
configurações.

---

## Licença

O projeto é **AGPL-3.0** (`LICENSE`), com o detalhamento por módulo em
`LICENSES.md` e os avisos de terceiros em `licenses/`.

O código desta entrega é de licença mista, marcada arquivo a arquivo com
cabeçalho SPDX — os cabeçalhos são a fonte de verdade:

- `src/lobby.c`, `src/online_battle.c`, a camada D52-2 e o adaptador de engine
  (`src/engine_adapter.*`, `src/reference_adapter.c`, `src/bt3xbx_adapter.*`):
  **MIT** (herdado do cabeçalho do protocolo de lobby original, que já
  declarava MIT).
- Os testes em `d50/` e o resto de `src/`: **AGPL-3.0-or-later**.
- `engine/` (restaurado da D52-1): as licenças originais em
  `engine/LICENSE-UPSTREAM-AGPL-3.0.txt` e `engine/LICENSE-codec-MIT.txt`,
  com o aviso em cada cabeçalho de arquivo.

Como o executável final é um trabalho derivado combinado, é distribuído sob
AGPL-3.0, com as fontes nesta pasta.
