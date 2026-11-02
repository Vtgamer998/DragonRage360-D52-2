# DragonRage360 D52-1 — teste do menu online e do lobby UDP/IP

> **Versão de teste, não finalizada. Uso por conta e risco.**
> Este pacote é um **diagnóstico xsdk**, não o jogo jogável e não substitui o
> `default.xex` funcional do bt3xbx. O estado do projeto continua `NOT_READY=2`.
> O lobby ainda não transporta entradas de luta, não inicia batalha e não prova
> FPS nem luta jogável.

## O que o D52-1 adiciona

- Menu `Dragon Net` com **Criar sala LAN**, **Partida rápida LAN**,
  **Conectar por IP** e **Encerrar conexão**.
- Contador `Jogadores: n/2` e papel (`host`, `cliente` ou `sem sessão`).
- Lobby UDP `DRL1` com descoberta, oferta, confirmação, heartbeat,
  retransmissão, sala cheia, `LEAVE` e timeout de 12 s.
- Logs no `default.log` com `lobby event=start/connected/disconnected/timeout/closed/error`,
  papel, número de jogadores, peer e estado.
- `lobby_host.txt` aceita apenas o IPv4 (porta UDP padrão 7371) ou `IP PORTA`.
- Autoteste de loopback no console pelo token `d52-lobby-selftest`.

## Instalação separada (não mexa na pasta do bt3xbx)

```text
Hdd1:/Games/DragonRage360-D52/
├── default.xex
└── lobby_host.txt      (somente no console cliente)
```

Use o `default.xex` **de console** deste release, não o `default-xenia.xex`.

## Teste A — menu e controle (um console)

1. Inicie o `default.xex` sem `startup_test.txt`.
2. Abra `Dragon Net` e confirme que a tela mostra os quatro itens.
3. Use o direcional e A para percorrer as opções; B volta.
4. Confirme que a linha `Jogadores: 0/2 · sem sessão` aparece.

Critério: o menu aparece no Xbox 360 e o controle seleciona as opções.

## Teste B — autoteste de loopback (um console)

Crie `startup_test.txt` ao lado do XEX com **apenas**:

```text
d52-lobby-selftest
```

Rode o XEX e envie o `default.log`. Procure:

```text
startup: D52 lobby selftest requested token=d52-lobby-selftest
PASS: D52 lobby selftest connected role=host+client players=2 ...
PASS: D52 lobby selftest explicit leave role=client players=0
PASS: D52 lobby selftest host timeout role=host players=1 after 12001 ms
PASS: D52 lobby selftest client search timeout role=client players=0 after 31256 ms with clock wrap
PASS: D52 lobby selftest loopback pairing, explicit leave, host timeout, client search timeout; two-console session still needs a physical test
```

O autoteste usa `127.0.0.1` e prova a máquina de estados e o socket local do
console. Ele **não** prova uma sessão entre dois consoles.

Se aparecer `FAIL`, envie o log completo. Um `FAIL` de loopback pode significar
que o stack do console não entrega UDP em `127.0.0.1`; isso não autoriza dizer
que o online funciona.

## Teste C — dois consoles RGH

### No console host

1. Abra `Dragon Net` → **Criar sala LAN**.
2. O log deve mostrar `lobby event=start role=host players=1` e
   `lobby event=hosting role=host players=1`.

### No console cliente

1. Crie `lobby_host.txt` ao lado do XEX com o IPv4 do host:

   ```text
   192.168.1.10
   ```

   ou, para uma porta diferente:

   ```text
   192.168.1.10 7371
   ```

2. Abra `Dragon Net` → **Conectar por IP**.
3. O log deve mostrar `lobby event=start role=client players=1`, depois
   `lobby event=joining` e `lobby event=connected role=client players=2`.

### Confirmação nos dois consoles

- Os dois `default.log` devem ter `lobby event=connected` com o peer correto.
- A tela deve mostrar `Jogadores: 2/2 · host` no host e
  `Jogadores: 2/2 · cliente` no cliente.
- A mensagem deve indicar `Lobby conectado`.

### Desconexão e timeout

1. No cliente, escolha **Encerrar conexão**. O host deve voltar a
   `Jogadores: 1/2 · host` e o log deve registrar `disconnected`.
2. Reconecte e puxe o cabo de rede do cliente. Em até 12 s o host deve voltar a
   `hosting`/`players=1` com `lobby event=timeout`, sem travar.
3. Repita com o host encerrando a sala; o cliente deve sair sem travar.

## O que este teste ainda não prova

- Luta, FPS, replays ou sincronização de estado (isso é D52-2).
- Que o `default.xex` do bt3xbx foi alterado (ele não foi).
- Que o online funciona fora da mesma LAN sem NAT/port forwarding.
- Que o loopback do console funciona em todos os modelos.

## Rede

- Os dois consoles devem estar na mesma LAN.
- UDP 7371 é a porta padrão do host.
- A descoberta por broadcast pode ser bloqueada pelo roteador; o caminho
  **Conectar por IP** não depende de broadcast.
- `lobby_host.txt` fica na raiz do jogo, ao lado do `default.xex`.

## Evidência para enviar

1. `default.log` do host.
2. `default.log` do cliente.
3. Uma foto ou vídeo curto mostrando `Jogadores: 2/2` e a mensagem
   `Lobby conectado`.
4. O resultado do teste de cabo puxado/timeout.

Sem esses itens, o D52-1 continua **em validação**, não concluído.
