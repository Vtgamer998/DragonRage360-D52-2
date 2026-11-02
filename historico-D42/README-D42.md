# DragonRage360 D42 — batalha local experimental

Esta versão chega à simulação original FIGHT com dois lutadores e o HUD original (retratos, barras e temporizador). Não é o jogo completo. Cenário 0, personagens 0 e 1; não há seleção completa pelo menu.

## Xbox 360 RGH

1. Extraia a pasta inteira. Coloque sua ISO DBZ3_SLUS_21678.ISO em isos/.
2. Execute default.xex da raiz. startup_test.txt contém original-battle.
3. Aguarde o carregamento e a sequência de início. O modo continua chamando Battle_StepFrame até o encerramento original, cancelamento, falha ou limite de 3.600 passos.
4. Back + Start cancela entre quadros e executa o encerramento original. Start sozinho segue mapeado à pausa; X equivale a Quadrado, Y a Triângulo, A a Cruz e B a Círculo. O combate com controle real precisa ser testado nesta versão.
5. Envie default.log após o teste. Preserve o pacote anterior para comparação.

A conclusão natural por KO e a tela de resultado ainda não foram demonstradas. O limite conta passos de simulação, não segundos reais. O modo Battle_Main completo não está liberado: NOT_READY continua 2.

## Modos de diagnóstico

Altere apenas o texto de startup_test.txt: original-fight executa uma sequência curta até quatro quadros em FIGHT; original-stage executa 33 quadros do cenário. original-battle é a batalha experimental com limite. A pasta xenia/ contém a variante própria para emulador; ela precisa da sua ISO em xenia/isos/. original-battle-check é exclusivo do emulador e cancela automaticamente depois de quatro quadros em FIGHT para testar a limpeza.

## Correções

- Corrigida a serialização big-endian dos pacotes nativos de desenho: RGBAQ, ST e XYZF. A antiga falha com Q=NaN vinha dos bytes de cor interpretados como Q. Não foi ocultada substituindo NaN por valores arbitrários.
- Validação integral dos pacotes antes da escrita; limites, formatos desconhecidos e sobreposição são rejeitados. Dados emprestados não são alterados.
- Corrigidas as máscaras dos flags originais do HUD e dos sprites para o ABI PowerPC big-endian. O HUD exibido é o do jogo.
- Mantidos os limites DMA/VIF, buffers de comando e vértices com fences, verificação de propriedade e encerramento após a sessão inicializada. Falhas sem quiescência comprovada preservam os proprietários e exigem reinício; recuperação transacional completa ainda está pendente.

## Desempenho e pendências

O log de console recebido nesta conversa pertence à D41: 33 quadros em 8,511942 s, aproximadamente 3,88 FPS. A meta de 30 FPS estáveis não foi atingida. Os testes D42 em Xenia/llvmpipe validam comportamento, não desempenho no Xbox. Ainda há contornos/linhas incorretos no terreno. Menus completos, áudio audível, saves originais, cenários 1 a 38 e online não estão concluídos/validados. Não há promessa de jogo completo neste pacote. O próximo gargalo medido é a interpretação dos programas VU dos lutadores: o caminho acelerado do cenário não acelera automaticamente skinning, efeitos e todos os desenhos dos personagens.

## Fontes e reprodução

Fontes e testes estão incluídos. Base Tenkaichi3Decomp: 3ca419c7966b11438e2dc3c9a7e1914f102ed545; xsdk: 8c58c7a3fb37bb261c5a11a8a1f4fa8a8bfd0d28, com engine/platform/xsdk-keep-kernel-lut.patch. GCC PowerPC xsdk 15.3. Use tools/build_d42.py --help para os caminhos externos do SDK, upstream, toolchain e cabeçalhos. SDK, toolchain, ISO e recursos comerciais não acompanham o pacote. Consulte LICENSES.md.

As evidências, hashes dos executáveis e limitações estão em RESULTADO-D42.json e evidencias-d42/. Os testes de serialização usam as estruturas reais do upstream compiladas para PowerPC e host, com ASAN/UBSAN; HUD, limites Ot_Send e cancelamento também têm testes específicos.
