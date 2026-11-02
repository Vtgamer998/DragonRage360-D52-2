# Atualização 0.29 — recursos completos e heap do motor

ISO completa baixada, com executáveis e três AFS extraídos e validados.
Heap nativo corrigido: 3.016 verificações passaram. Dados reais: 698 modelos
e 57.259 animações não vazias validados em host; modelo VU em pose de referência.
383/383 unidades PowerPC, sem conflito ABI; vínculo com 137 nomes pendentes.
**A luta ainda não roda. Battle_Main segue fora do XEX. Sem teste em Xbox.**
O default.xex é o diagnóstico 0.27 inalterado. Consulte RELATORIO-0.29.md.

--- Histórico ---

# Atualizacao: ABI de chamadas adaptada

O modo native-ps2 preserva a aritmetica por helpers de bits, com assinaturas
hard-float. Compilacao PowerPC: 380/380, nenhuma rejeicao de ABI; 163 nomes
ainda bloqueiam o vinculo. Nao ha Battle_Main no XEX nem teste em console.
Veja ../../RELATORIO-plataforma-0.4.md. O relato abaixo e historico.

# Integração original de Battle_Main — ainda bloqueada

`battle.c` é uma cópia exata de `src/battle/battle.c` do upstream
`3ca419c7966b11438e2dc3c9a7e1914f102ed545`. Não substitui a luta por uma
simulação nova nem define funções vazias para satisfazer o linker.

O alvo `battle-link` compila os módulos originais e tenta ligá-los com os
objetos nativos, exigindo `Battle_Main` como raiz do vínculo. Isso verifica
dependências; não chama a luta e não modifica o launcher. O dma.c original
é excluído porque contém MMIO PS2 e conflita com o subconjunto nativo.
O main original é renomeado para fornecer seus auxiliares sem substituir
o ponto de entrada do Xbox.

As tabelas de `port/data` são estruturas sem os valores do jogo. Não há
inicialização nativa completa, conversão endian de todos os dados nem
validação em execução. Mesmo um vínculo bem-sucedido não comprovaria luta.

O motor usa `soft-float`, e SDK/renderizador usam `hard-float`. O linker
rejeita a mistura. Não remova a verificação de ABI: funções que recebem ou
retornam float precisam de uma fronteira adaptada e validada. Tampouco
troque indiscriminadamente a aritmética da simulação para obter um ELF.

`newlib_compat.h` declara explicitamente as duas funções renomeadas pelo
shim upstream que estavam sem protótipos, preservando seus tipos float.

Para reproduzir, após compilar os objetos do launcher:

```sh
make battle-link UPSTREAM=/caminho/Tenkaichi3DecompAndroid \
  CC=/caminho/powerpc-xsdk-elf-gcc XSDK=/caminho/xsdk
```

O upstream deve estar na revisão acima. O SDK usado é
`57b02f1e60667e37d22c3aa613d23d2486514ff8`, com GCC xsdk 15.3.0.
Logs completos ficam em `build/battle-integration/`. Uma falha retorna
código diferente de zero e remove o ELF incompleto. Não há geração de XEX.
Evidências desta tentativa: `../reports/battle-integration/`.

Próxima implementação necessária: fronteira de ABI, serviços de plataforma
reais e dados endian corretos; depois inicialização equivalente a Game_Main,
Battle_Init/Loop/Term, controles, IA, colisões, animações e renderização
executados juntos. Não se deve chamar Battle_Main antes dessas condições.
