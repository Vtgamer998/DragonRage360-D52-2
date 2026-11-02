# DragonRage360 · D52-2

Diagnóstico experimental para Xbox 360 (RGH) do *Budokai Tenkaichi 3*, com a
camada de rede **D52-1** (lobby UDP) e a **D52-2** (sincronização determinística
de entradas).

> Projeto independente, sem afiliação oficial. Ver `licenses/` para as licenças
> de cada componente.

## O que esta versão tem

| Componente | Estado |
| --- | --- |
| Lobby UDP D52-1 (discover/offer/join/accept/heartbeat) | implementado e testado em loopback (22/22) |
| Envio de controles por quadro (`INPUT`) | implementado em `src/lobby.c` |
| Hash de estado (`HASH`) | implementado (`compute_battle_state_hash`) |
| Retransmissão com clock wrap (12 s) | implementado (`dr_lobby_retransmit`) |
| Módulo `online_battle` | implementado, mas com **simulação 2D placeholder** |
| Integração com `Battle_Main` do bt3xbx | **não comprovada** |
| Rollback | **não implementado** (apenas log de desync) |
| XEX jogável com online | **pendente** |

## Estrutura

```
src/
  lobby.c, lobby.h          # protocolo D52-1 + entradas/hashes D52-2
  online_battle.c, .h       # ponte entre lobby e o motor (placeholder 2D)
  main.c                    # loop principal do diagnóstico
  bt3_wire.c, net_check.c   # diagnóstico de rede/protocolo
d50/
  lobby_selftest.c          # autoteste D52-1 (loopback)
  online_battle_selftest.c  # autoteste D52-2 (loopback)
  test_d52_lobby.c          # executável de teste D52-1
  test_d52_online.c         # executável de teste D52-2
```

## Testes no host (Linux)

```bash
make d52-lobby-test    # D52-1: lobby em loopback
make d52-online-test   # D52-2: lobby + online_battle em loopback
```

Os testes usam `gcc` com `-fsanitize=address,undefined` e rodam em
`127.0.0.1`. Eles **não** provam duas consoles, Wi-Fi, roteador nem a pilha de
rede do Xbox 360.

## Build do XEX

O diagnóstico usa `xsdk` (toolchain PowerPC próprio). O jogo original do
bt3xbx usa o XDK — a integração real entre os dois ainda não foi feita.

```bash
make          # gera build/default.elf
```

## Limitações conhecidas

- A simulação de luta dentro de `online_battle.c` é um placeholder 2D, não o
  motor original. O autoteste passar não comprova que o combate real é
  determinístico.
- A retransmissão de 12 s é boa para reconexão, mas longa demais para entradas
  em tempo real; a D53 deve usar uma janela curta (2–3 quadros).
- Ao detectar divergência de hash o jogo apenas registra o evento.

## Próximo marco: D53

1. **D53-A** – adaptador do motor (iniciar/atualizar/encerrar a luta real sem
   refatorar todo o bt3xbx).
2. **D53-B** – sincronização real (entradas por número de quadro, reprodução
   determinística, detecção de divergência).
3. **D53-C** – menus e partidas (seleção de personagem/cenário ligada ao lobby).
4. **D53-D** – build do XEX e validação no console RGH.

Recomendação: priorizar o adaptador do motor e a sincronização determinística
antes de implementar rollback.
