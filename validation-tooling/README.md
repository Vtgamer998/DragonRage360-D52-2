# Ferramentas de validação no host

xsdk/: cabeçalhos, implementação do cursor para consulta e simulador Python de shaders da revisão 8c58c7a3fb37bb261c5a11a8a1f4fa8a8bfd0d28. MIT; LICENSE incluída. É um subconjunto para testes, não um SDK capaz de ligar XEX.

vuasm.py: scripts/vuasm.py do Tenkaichi3Decomp 7b43a75bd8b0aeabcce0a22c45e1ad66162ac9d7. Monta os VSM originais incluídos em arquivos temporários de teste. Não distribuir os binários gerados; os testes os deixam em build/. Os avisos/licenças do upstream estão em vendor/tenkaichi-original/.

Nada aqui substitui execução no PowerPC/Xenos.

As transcrições de instruções Xenos mantêm os avisos BSD de Xenia/freedreno; veja xsdk/THIRD_PARTY_NOTICES.md e os cabeçalhos dos próprios arquivos.
