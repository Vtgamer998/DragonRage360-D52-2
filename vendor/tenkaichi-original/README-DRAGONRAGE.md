# Fontes originais incorporados ao DragonRage360

Origem: https://github.com/z3xox/Tenkaichi3Decomp
Revisão: 7b43a75bd8b0aeabcce0a22c45e1ad66162ac9d7

O pacote inclui TODOS os arquivos de src/ e include/ dessa revisão, as formas
portáveis das tabelas em port/data/, port/include/, o preparador oficial
portsrc.py, o driver headless e três cabeçalhos usados pelos menus portáveis.
São 566 arquivos originais, conferidos pelo hash Git de cada blob.

Não é uma cópia do repositório inteiro: renderizadores Vulkan/OpenGL, SDL,
instalador e bibliotecas do PC continuam no checkout upstream separado.
Não contém ISO ou dados comerciais extraídos do disco.

Fluxo do jogo, já implementado nesses fontes:
Game_Main -> Overlay_Load -> Progress_Main (menus) -> Battle_Main
Battle_Main -> Battle_Init -> Battle_Loop -> Battle_Term

Há 272 unidades C na lista do preparador original, 72 delas de menus.
Os corpos C dos trechos INCLUDE_ASM são habilitados pelo preparador oficial;
não foi necessário inventar funções vazias ou outra simulação de luta.

O executável nativo fornecido continua sendo a D42, compilada anteriormente
sobre a base 3ca419c. Este snapshot mais novo NÃO foi automaticamente colocado
no XEX: migrar de revisão exige revisão dos patches e validação PowerPC.
Os 74 testes de sintaxe foram executados no host, não no Xbox.

Para repetir a conferência e a preparação sintática:
  python3 adaptation/tools/audit_original.py
ou:
  make original-source-audit

Veja IMPORT-MANIFEST.json e validation-original/report.json na raiz do projeto.
As licenças e avisos originais estão preservados. O código do port usa AGPL;
os fontes do jogo têm a titularidade descrita no README original do upstream.
