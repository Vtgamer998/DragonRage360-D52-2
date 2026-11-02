# DragonRage360 · D52-2 — Determinismo e sincronização de entradas online

Esta é a continuação direta da [D52-1](https://github.com/Vtgamer998/DragonRage360)
(lobby UDP). A D52-1 **não foi reescrita**: o protocolo de pareamento original
foi preservado byte a byte, e a D52-2 foi adicionada **por cima**, sem alterar
um único campo dos pacotes de controle.

O objetivo da D52-2 é simples de enunciar e difícil de cumprir:

> Dado o mesmo par de entradas no mesmo número de quadro, as duas CPUs
> (PowerPC do Xbox 360 e o seu PC de teste) devem produzir exatamente o mesmo
> estado, e as duas consoles devem concordar com isso a cada quadro.

Sem isso, não existe multiplayer online real — existe apenas dois jogos
rodando lado a lado.

---

## O que foi implementado

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
PASS: D52-2 state hash matches: DFC3DFBA
PASS: D52-2 91 peer hashes verified per side, no desync
PASS: D52-2 loopback online battle selftest, host only
```

Compilado com `-Wall -Wextra -Werror -fsanitize=address,undefined` — sem um
único aviso.

**Provado:** o protocolo D52-1 não foi quebrado; dois lados convergem para o
mesmo quadro e o mesmo hash mesmo com atrasos, reordenação, duplicatas e
**perda forçada de datagramas** (a recuperação pela retransmissão de 120 ms
foi verificada de ponta a ponta).

**Não provado** (e nenhum teste em loopback pode provar):

- Sessão física entre duas consoles (Wi-Fi, roteador, NAT).
- A pilha de rede do Xbox 360.
- Que o loop de batalha real do bt3xbx é determinístico — `sim_step()` ainda é
  uma simulação inteira de substituição.

---

## Limitações conhecidas

1. **`sim_step()` é um substituto.** A D53 substitui essa função pelo
   `Battle_Main` real do bt3xbx. Tudo o mais (numeração de quadros, fila de
   entradas, comparação de hash) permanece como está.
2. **Nenhum rollback.** Um desync é detectado e logado, não corrigido. Rollback
   é D54 e depende do determinismo estar provado primeiro — a sua recomendação
   de priorizar o adaptador de engine antes do rollback está correta.
3. **O build do XEX não foi executado aqui.** Não há toolchain PowerPC neste
   ambiente. `src/main.c` não compila neste repositório por causa de
   `xb_platform.h` ausente (`engine/platform/` não está no repo) — uma lacuna
   pré-existente, não introduzida pela D52-2.
4. **Sem limite de quadro por segundo.** O lockstep estagna se um lado parar
   de responder; o mecanismo de saída é o timeout de 12 s da D52-1.

---

## Plano da D53

| Etapa | Descrição | Estado |
|---|---|---|
| **A** | Adaptador de engine: interface `start`/`update`/`end` para trocar `sim_step()` pelo `Battle_Main` real sem refatorar o engine inteiro | próximo |
| **B** | Sincronização real com números de quadro sobre o loop de batalha do bt3xbx | depende de A |
| **C** | Menus e partidas online | |
| **D** | Build do XEX e validação de console | |

O adaptador da etapa A existe para que a troca do substituto pelo engine real
seja uma mudança de **um ponteiro de função**, não uma refactorização de
milhares de linhas. É a peça que falta para provar que o determinismo escala
do substituto inteiro para o jogo de verdade.

---

## Como rodar os testes

```sh
git clone https://github.com/Vtgamer998/DragonRage360-D52-2.git
cd DragonRage360-D52-2
make d52-test        # D52-1 e D52-2, com sanitizers
```

Requer apenas `gcc` e `make` (Linux). Nenhuma toolchain de Xbox é usada nos
testes — todo o código de rede roda em loopback sobre UDP real.

---

## Licença

O projeto é **AGPL-3.0** (`LICENSE`), com o detalhamento por módulo em
`LICENSES.md` e os avisos de terceiros em `licenses/`.

O código desta entrega é de licença mista, marcada arquivo a arquivo com
cabeçalho SPDX — os cabeçalhos são a fonte de verdade:

- `src/lobby.c`, `src/online_battle.c` e a camada D52-2: **MIT**
  (herdado do cabeçalho do protocolo de lobby original, que já declarava MIT).
- Os testes em `d50/` e a maior parte de `src/`: **AGPL-3.0-or-later**.

Como o executável final é um trabalho derivado combinado, é distribuído sob
AGPL-3.0, com as fontes nesta pasta.
