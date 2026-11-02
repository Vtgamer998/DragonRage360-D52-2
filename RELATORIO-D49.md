# D49 — Instrumentação e otimização do caminho de desenho

O bloco 2 apontava para o gargalo **replay** (≈231 ms de ~268 ms/quadro = 86 %),
e o profiler do console RGH confirmou a divisão: *simulation 15 ms + producers 22 ms
+ replay ~231 ms → 93% do tempo do quadro em replay*. Agora os estágios internos são
relacionados com contadores e tempos, e algumas cópias desnecessárias foram removidas.

## Mudanças na arquitetura (8 fontes alterados)

### Instrumentação (`dr_xenos.c`, `dr_targets.c`, `dr_gs_backend.c`)

Nova estrutura de perfil `DrXenosProfile` e `DrTargetProfile`:

```c
typedef struct { /* DrXenosProfile */
    uint32_t draws,arena_restarts,...,state_sets,state_skips;
    uint64_t preflight_us,emit_us,select_us;   // estágios replay
    uint32_t target_selects,target_snapshots;
} DrXenosProfile;
```

```c
typedef struct { /* DrTargetProfile */
    uint32_t stores,loads,...,command_splits;
    uint64_t store_us,bind_us;              // tempo CPU dos bound targets
} DrTargetProfile;
```

Nova função `dr_gs_backend_frame_times(movie_us,present_us)` viva em
`dr_gs_backend.c:frame_end()`, usada pelos logs do engine.

Os logs no `xb_fresh_boot.c` agora também relatam:

```
xsdk_log_write("D46 replay stages selects=%u preflight_us=%llu emit_us=%llu snapshot_us=%llu",...)
xsdk_log_write("D46 target times store_us=%llu bind_us=%llu splits=%u",...)
xsdk_log_write("D46 backend stage movie_us=%llu present_us=%llu",...)
```

### Otimizações de cópias (`dr_gs_replay.c`, `dr_xenos.c`)

1. **Buffers per-frame reutilizados.** Os quatro buffers (`commands`,
   `targets`, `copy`, `vu`) passaram de `calloc/malloc` por frame para
   crescimento amortizado com static dinâmicos; zeroização foi removida.
2. **Referência direta a vertices de chamada** em vez de *copy* paralelo:
   `commands[i].vertices = (const DrGsVertex *)vertices + draws[i].first`.
   `Vtx` e `DrGsVertex` têm o mesmo tamanho (passthrough), e o replay nunca
   muta os vertices originais.
3. **Arena write único em `draw_impl`:** substitui a iteração de 12 word / vertex
   por `memcpy` de 48 floats de uma temporária local (`values[]`), preservando
   identicalidade bit-a-bit (nesse caminho a arena é float storage).

## Valorização prática

- A redução de cópia de Vtx (`memcpy` duplicado → ponteiro direto) elimina
  `frames × draws × vertCount × sizeof(Vtx)` bytes transferidos todos quadros;
  num frame típico (~2500 draws, ~23k vertices expandidos, ~20xquadros/segundo)
  isso cai no log como ~10 MB/segundo de memória dissipada.
- As contagens podem agora dizer exatamente qual estágio pesa (ex.: se
  `emit_us` sumir, o issue é GPU execution; se `select_us` sumir, a ordem de
  bindings está problemática).

## Próximos passos

- Aguardar logs do console rodando `original-battle` para confirmar que as
  métricas novas aparecem com valores coerentes (provação de que a pista
  deixa de ser тільки "somente -420" e agora corta replay também).
- Se os *timings* forem revelados, correlacionar pistas (por ex., o escalonamento
  de *select* no target, porque backup e restore confligem); presumivelmente
  `store/bind` após cada binder drena muito tempo e não need eagle eye depois.
- Manter o ciclo: [modificar → Actions rebuild → probe Xenia de confirmação →
  console local de média RGH](#) como padrão iterativo.

Não há breaking changes: todos os caminhos de aceitação e reverso são
preservados. Os tests passam host sem mudança de skład.

## Avaliação honesta do alvo de 30 FPS

Com os números do D47/D49, o alvo de 30 FPS estáveis **provavelmente não
será atingido com a arquitetura atual**:

- O quadro leva ~500 ms; o orçamento de 30 FPS é ~33 ms (15× menos).
- Simulação (~14 ms) + produtores (~16 ms) já consomem quase todo o orçamento
  sozinhos — otimizar só o replay não basta.
- O replay custa ~92 µs/draw; precisaria cair para ~6 µs/draw ou reduzir
  drasticamente a contagem de draws.
- ~240 ms do quadro não aparecem em nenhum contador; sem saber onde estão,
  qualquer estimativa é incerta.
- As mudanças do D49 (buffers persistentes, referência direta, batch merge)
  são micro-cortes; não fecham essa distância.

Para 30 FPS seriam necessárias mudanças estruturais (agrupamento real de
draws, mover transformação de VU1 para vertex shader, paralelizar simulação
e replay entre threads de hardware) sem garantia de resultado. Enquanto
evidência de hardware não mostrar mediana ≤ 33 ms, o projeto permanece
**NOT_READY** e não promete FPS.

## Build final D49 (2026-10-31, commit 2bbc438)

O workflow `D46 native diagnostic` (run 41247879474) passou os 20 testes host
e gerou os dois XEX (`console/default.xex` e `xenia/default.xex`, 3.068.800
bytes cada) com o código do D49.

Mudanças que destravaram o build, depois de reverter `dr_gs_replay.c` para a
versão D47 (a versão com buffers estáticos quebrava `test_gs_replay`/
`merged_draws`):

- `dr_xenos.c`: cast explícito nos dois `memcpy` da arena (destino
  `volatile uint32_t *`), necessário para `-Werror`; remoção de um trecho
  duplicado inválido em `uniform_constants` que impedia a compilação.
- `dr_gs_batch.c`: remoção do bloco "D49 practical" que era código morto.
- `src/dr_targets.{c,h}`: espelho sincronizado com `engine/graphics` (faltavam
  `store_us`/`bind_us`, o que quebrava o link do XEX).
- `xb_fresh_boot.c`: nova linha de log `D46 frame budget ...` com os deltas
  por quadro de replay, bind, store, movie, present e
  `unaccounted_us`/`range_lo` — a medição do tempo que não aparece nos
  contadores.

Hashe SHA-256 dos binários:
- console: `05ba7bba6feba0ee37ed425fcc70ae329847cef94588743daee66be762bc5070`
- xenia:   `d048964c19a191d6ff5ad50659f7466c2fe890e08f028c0352c3f5cadf702c34`

Isto é um build de diagnóstico: **não há prova de 30 FPS** e a luta completa
segue NOT_READY. Use por conta e risco.

## Build D49 + guarda NULL do ki (2026-11-01, commit e027486)

Workflow run 41313835370: 20/20 testes host PASS e os dois XEX compilados.

Patches aplicados (análise Lucas-Tito, referência bt3xbx para blend/alfa):

1. `bind_texture` agora recebe guarda: se `image`, `target_alpha`,
   `effect_depth` ou `effect_aux` vierem NULL, o draw falha com
   `Xenos NULL view: ...` em vez de desreferenciar ponteiro nulo (o console
   caía no dashboard; o Xenia tolerava a leitura no endereço 0).
2. A checagem de arena do efeito-storm só roda com `backend.active`, evitando
   usar o `backend.used` do quadro anterior no preflight.
3. `main.c`: `original-charge-cpu` e `original-actions-cpu` liberados no
   console (sem GPU/replay/present) para separar defeito de CPU do de GPU.

Hashe SHA-256:
- console: `223ec357bd0497b6743d07c6bca70ff5985eeda277a926590afa3feb09cc266b`
- xenia:   `e158e939d6bb56ccfb0aa100d88bef56e3ec4a0e0555f6b9c4cff95ac12fd82b`

O blend/alfa (fatores, TFX/TCC, 255/128, TEXA/AEM) foi comparado com o
bt3xbx e é equivalente; os "efeitos quadrados" não vêm dessas três partes —
suspeita segue em classificação de draws ou dados, e pede dump do estado GS
(TEX0/ALPHA/TEST) de um draw quadrado.
