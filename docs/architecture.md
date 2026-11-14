# Arquitetura — REA Cruis'n USA

## Hardware original (Midway V-Unit)

| Componente | Função | Arquivos da ROM |
|---|---|---|
| TMS320C31 (DSP 32 bits, endereçamento por palavra) | CPU principal: lógica, física, projeção 3D | `cusa-l41.u10`–`u13` (programa) |
| ADSP-2105 (sistema DCS) | Som: samples PCM/ADPCM e música | `cusa.u2`–`u9` |
| Hardware de vídeo V-Unit | Polígonos texturizados | `cusa.u14`–`u29` (dados/texturas comprimidas em LZW, na janela de ROM) |
| PLDs | Lógica de decodificação | `a-19xxx.*.bin` (não usados no port) |

## Fatos verificados na ROM

- **Intercalação do programa:** palavra = `u10 | u11<<8 | u12<<16 | u13<<24` (u10 = bits D0–D7).
- **Vetores (palavras 0–4):** RESET `0x4AD5`, INT0 `0x4C34`, INT1 `0x4EDE` (tratador padrão), INT2 `0x8090`, INT3 `0x4D6F`. Vetores 5+ também apontam para `0x4EDE`.
- **Textos:** ASCII empacotado 4 caracteres por palavra, 1º caractere no byte menos significativo, terminado em 0. Exemplos: `INSERT COINS` @ `0x7456`, `PRESS START` @ `0x745B`, `CHOOSE CAR` @ `0x18A1`.
- **Ponto flutuante:** formato próprio do TMS320C3x (não IEEE) — ver `src/core/tms_float.hpp`.
- **EPROMs gráficas:** formam 4 bancos de 0x80000 palavras, intercalados como o programa (u14 = D0–D7 … u17 = D24–D31):

  | Banco | Endereço |
  |---|---|
  | u14–u17 | `0xC80000` |
  | u18–u21 | `0xD00000` |
  | u22–u25 | `0xD80000` |
  | u26–u29 | `0xE00000` |

  O restante da janela, até `0xFFFFFF`, lê 0 (sem espelhamento). Fonte: MAME `crusnusa`, região "maindata". Confirmado: os destinos dos fluxos LZW do RESET se encadeiam exatamente.
- **RAM de texturas (`0xA00000–0xBFFFFF`):** cada escrita grava só os 16 bits baixos, como 2 texels de 8 bits (`textureram_w`). Na leitura, os dois bytes voltam juntos.
- **Mapa de memória:** ver `src/core/vunit_hw.hpp` (baseado no driver `williams/midvunit.cpp` do MAME 0.289). A ROM fica em `0xC00000`; no reset de hardware as primeiras `0x20000` palavras são copiadas para a RAM em `0x000000`, de onde a CPU executa.

## Interrupções e VBLANK (confirmado)

| Vetor | Tratador | Função | Evidência |
|---|---|---|---|
| RESET | `0x4AD5` | Inicialização (traduzida em `src/core/boot/reset_routine.cpp`) | — |
| **INT0** | `0x4C34` | **VBLANK** | Salva o contexto inteiro; escreve a paleta; tem watchdog de software (reinicia o jogo após 300 quadros sem resposta do loop principal); contador `0xC93E` vai até 57 (≈ 1 s). A tabela de timing da ROM programa a INT0 na scanline **399**, a última visível. No MAME é a única IRQ, ligada ao temporizador de scanline. |
| INT1 | `0x4EDE` | Tratador padrão (só `RETI`) | Mesmo destino dos vetores 5 em diante |
| INT2 | `0x8090` | **Sincronismo de quadro do gabinete Slave em rede**: ajusta o `vtotal` (431/432/433) conforme a scanline atual | **Confirmado**: `0x7F18` só habilita a INT2 se as DIP switches indicam *Slave* (SW2:8, bit 0 = 1) **e** *Linking* ligado (SW2:6, bit 2 = 0). As DIPs vêm de `0x992000 >> 16`, guardadas em `0xC8CC` (rótulos no `crusnusa` do MAME). |
| INT3 | `0x4D6F` | **Fim de conversão do ADC0844**: lê volante, acelerador e freio em cadeia | **Confirmado**: no MAME, a linha INTR do ADC0844 vai para a INT3, com os canais 1 = volante, 2 = acelerador, 3 = freio. Traduzida em `src/core/irq/adc_handler.cpp`. |

### Timing de vídeo (tabela da ROM em `0xC102DA`, copiada pela sub-rotina `0x4ECC`)

| Reg. | Endereço | Valor | Significado |
|---|---|---|---|
| 0 | `0x980020` | `0x58F` | Scanline da INT0 = `0x18F` = 399 |
| 6 | `0x980026` | 666 | htotal |
| 7 | `0x980027` | 398 | início do blank vertical |
| 10 | `0x98002A` | 431 | fim do sync vertical |
| 11 | `0x98002B` | 432 | vtotal |

Clock de pixel 33,333333 MHz / 2 → **16.666.666,5 / (666 × 432) ≈ 57,928 Hz**. Esse valor está em `vunit::kRefreshHz` e é usado pelo `GameLoop`.

## Tratador do VBLANK (INT0, `0x4C34`–`0x4CED`) — traduzido

Tradução em `src/core/irq/vblank_handler.cpp`, executada a cada quadro do `GameLoop`.

1. Salva todo o contexto. Durante o tratador, `IE = INT1 | [0x8099DF]`: só a INT1 e, em gabinete Slave, a INT2 de sincronismo podem interromper. Usa wait states `0x470`.
2. `CALL 0x9E97`; copia `[0xC938]` para a paleta[0] (cor de fundo); `CALL 0x7F25`. Depois religa o GIE (`CALL 0x4C33`): o resto do tratador é interrompível.
3. **Watchdog de software:** fora do modo 1 (`[0xC8C5] & 0xF`), incrementa `0x8099E0`. O loop principal zera esse contador a cada volta (`0x4C02`). Acima de 300 quadros (~5 s), chama `0xAB1A` com `R0 = 0x800` e salta para o RESET.
4. Alimenta o watchdog de hardware (`0x5029`).
5. Evento pendente `[0xC93F]`: alterna `[0xC942]`, decrementa `[0xEB50]` e chama `0x8E4E`/`0x8E59` + `0x8E6D`.
6. **A cada 57 quadros (~1 s):** auditorias de tempo na CMOS (item `0x76` fora do modo 1; `0x77` fora dos modos 1/2; `[0xE871] + 0x91` no modo 4) e dois temporizadores regressivos (`0xE86A`, `0xE604`).
7. Contadores: `[0xC930]` +1 (a rotina `0x4EC7` espera ele mudar, é o "aguarda VBLANK" do jogo); `[0xC93A]` += 1/3420 (**minutos**, float TMS, assumindo 57 quadros/s); `[0xC932]` += 1,0.
8. Chama 8 rotinas por quadro: `0xAFC2`, `0x92B3`, `0x4CEF` (entradas), `0x8EB7`, `0x4E4F`, `0x75F1`, `0x4FAE`, `0x49B1`.
9. Restaura o contexto; `IE = INT0 | INT1 | INT3 | [0x8099DF]`; `RETI`.

**Peculiaridades preservadas:** em `0x4C80` o jogo grava inteiro 0 num campo float (`[0xC932]`). No formato TMS, `0x00000000` vale 1,0. Pelo mesmo motivo, floats zerados pela BSS partem de 1,0, salvo se alguma rotina ainda não traduzida os inicializa. A soma de floats trunca a mantissa a cada `STF`, como no hardware.

## Entradas (`0x4CEF` + INT3) — traduzidas

Caminho a cada quadro: `InputManager` (SDL2 → portas ativas em 0) → INT0 → `0x4CEF` → fim da INT0 → INT3 × 3.

| Porta | Conteúdo (ativo em 0) |
|---|---|
| `0x991060` IN0 | `0x1` ficha 1, `0x2` ficha 2, `0x4` start, `0x8` slam, `0x10` teste ("ENTER"), `0x40` serviço, `0x80` ficha 3, `0x100` volume −, `0x200` volume +, `0x400/0x800/0x1000/0x2000` marchas 4/3/2/1, `0x4000` ficha 4 |
| `0x991030` IN1 | `0x2` rádio, `0x10/0x20/0x40` vistas 1/2/3 |
| `0x992000` DIPs | SW2 (bits 0–7) e SW3 (8–15); o jogo usa a metade alta (`>> 16`). Padrão `0xF9FE` |
| `0x993000` ADC | Comando com `0x994000` bit 5 = 0; leitura com bit 6 = 0; dado nos bits 24–31 |

- **`0x4CEF`:** copia as DIPs para `[0xC8CC]` e monta `palavra = IN1 << 16 | IN0`. `[0xC937]` guarda a palavra invertida (1 = apertado). O debounce em `C8C9`/`C8CA`/`C8CB` faz `novos |= amostra1 & amostra2 & ~agora`: uma borda só conta depois de **duas** amostras soltas seguidas. Em seguida inicia o ADC no canal 1 e zera o estado da INT3.
- **Chave de teste (`0x4D61`):** uma borda alterna `[0xC939]`, e a `0x4E1B` abre o menu do operador em `0x0699`. No modo 1, a borda fica guardada para depois.
- **Volume (`0x4D3D`/`0x4D28`):** age por **nível**, então segurar o botão muda o volume a cada quadro. Usa `max(volume ± 1, mínimo)`, limitado a 0–255. O mínimo é o item `0x20` da CMOS ("MIN VOLUME LEVEL", na ROM). Grava o item 8 e o checksum (`0x99EE` → `0x99CA`: soma dos itens 0x00–0x2D, guardada no item 0x35) e manda o valor ao som (`0x910F`, R1 = volume).
- **INT3:** o estado `[0xC93D]` (0 → 1 → 2 → 3) encadeia volante (`[0xC8C8]`, Δ ≤ ±65), acelerador (`[0xC93B]`) e freio (`[0xC93C]`).
  - Filtros: volante e acelerador com pesos 2703/8192 (novo) e 2744/4096 (anterior); freio com 0,25 e 0,75. `FIX` arredonda para baixo.
  - **Efeito medido:** os valores filtrados nunca chegam ao valor físico. Volante 240 → 237, acelerador 200 → 197, freio 100 → 97, centro 128 → 125.
  - Os desvios atrasados (`BUD`/`BNED`) executam 3 instruções antes de saltar.

## Layout da RAM principal

| Faixa | Conteúdo | Evidência |
|---|---|---|
| `0x000000–0x00003F` | Vetores | Preservados na recópia do RESET |
| `0x000040–0x00C8C3` | Código e dados (cópia da ROM) | Todos os `CALL`s vistos caem aqui. Na ROM, a palavra `0xC8C4` (início da BSS) já é `0xFFFFFFFF` (não gravada). Mais adiante, até `0x17371`, há tabelas que o jogo lê direto pela janela da ROM (`0xC00000 + n`, ex.: timing em `0xC102DA`), não pela cópia em RAM |
| `0x00C8C4–0x01EFFE` | Variáveis (BSS), zeradas por `0x4E20`/`0x4E35` | Limites em `[0x4E1E]`, `[0x4E1F]`, `[0x4E33]`, `[0x4E34]` |
| `0x00C8EE–0x00C92D` | Buffer de saída do LZW (64 palavras) | `[0xA2AF]` |
| `0x00E5CA–0x00E5E3` | Estado do LZW: flags e contexto salvo | `0xA374` / `0xA38C` |
| `0x01976D–0x01B9F6` | Dicionário LZW: prefixos `+0`, sufixos `+0x1145` | `[0xA2AD]`, IR0 |
| `0x01B9F7–…` | Pilha de inversão do LZW | `[0xA2AE]` |
| `0x809800–0x809FFF` | RAM interna; pilha a partir de `0x809C00` | `SP = [0x63]` |

## CMOS / NVRAM (`0x9C0000`)

- A CMOS só tem 8 bits úteis por palavra, nos bits 24–31. Cada item de 32 bits ocupa 4 palavras com o **byte mais significativo primeiro**. É o único trecho big-endian encontrado até agora.
- Endereço do item `n` = `[0x9936] + 4·n` (`[0x9936] = 0x9C0000`).
- Para gravar, o jogo destrava a proteção (`0x995020 |= 0xC00`), grava com wait states lentos (`0x808064 = 0x90`) e trava de novo.
- Rotinas: `0x99E9` lê o item (`getItem`), `0x99F4` grava (`setItem`), `0x99DA` incrementa (`incrementItem`, auditorias). Tradução em `src/core/cmos/cmos_routines.cpp`.
- O item `0xFA` funciona como "assinatura": o RESET exige o valor `0x28`. Sem ele (CMOS virgem), o jogo segue para `0x5006`, provável reinicialização de fábrica.

## Rotina de RESET (`0x4AD5`–`0x4BAE`) — resumo

1. Configura os pinos XF0/XF1 (`IOF`), os wait states do barramento (`0x808064`) e o cache (`ST.CE`).
2. Carrega `SP = 0x809C00` (RAM interna, valor guardado em RAM `[0x63]`) e desliga as interrupções.
3. Liga a proteção da CMOS (`0x995020 = 0`) e zera a RAM interna (`0x809800`–`0x809FFF`).
4. Grava `0xFF` em `0x994000` (LED, watchdog e reset do som DCS), programa a página de vídeo e o timing (`CALL 0x4ECC`).
5. Copia a RAM para a RAM rápida (`0x400000`, 0x10001 palavras) e recopia o programa da ROM para a RAM, preservando os vetores.
6. Inicializa variáveis, o timer 0 (contador livre, `0x808020 = 0x201`, período `0xFFFFFFFF`) e cerca de 30 sub-rotinas.
7. `IE = INT0 | INT3`. As interrupções são ligadas com `CALL 0x4C33`, um truque: a rotina é um único `RETIU`, que liga o GIE.
8. Lê o ajuste 0xFA da CMOS (`CALL 0x99E9`). Se for diferente de `0x28`, segue para `0x5006` (tela "VERSION UPDATED"); senão chama `0x995C` e desvia para `0x4FEF` se houver carry, ou continua em `0x4BAF`.

### Recuperação da CMOS e reentrada em `0x4B28` — traduzidas (`src/core/boot/boot_sequence.cpp`)

| Destino | Tela (ROM, ponteiros `0xB0A6`–`0xB0A9`) | Depois |
|---|---|---|
| `0x5006` | "VERSION UPDATED" / "FACTORY SETTINGS RESTORED" | `0x5020`, `0x0DCE`, item 0xFA = 0x28, `BR 0x4B28` |
| `0x4FEF` | "CMOS ERROR" / "FACTORY ADJUSTMENTS RESTORED" | `0x5020`, `0x0CC0`, `BR 0x4B28` |

- Cada tela repete 9 × (`0x8E79`, `0x8E4E`, `[CE13] = 0x900000`, 101 × duas linhas via `0xA7F4`, `0x5029`).
- `0x4B28` grava 8 em `[0x8099E1]` e refaz o RESET a partir de `0x4B2B`. Com isso o contador de auditoria 0x78 não é incrementado de novo.
- **`0x4FEF` é inalcançável a partir do RESET nesta versão.** `0x995C` termina com `ANDN 1,ST`, ou seja, o carry sai sempre zerado.
- `0x995C` limita o item 0x70 a 0–10 e o 0x71 a 0–30, chama `0x9AA9` (recordes) e depois `0x9981`.
- Recordes na CMOS: 15 tabelas (R6: pistas 0..13 e corrida inteira 14) × 10 entradas (R7) de 8 palavras em
  `[9936]` + 0x3F8 + R6·0x50 + R7·8 (`0x9AD2`): tempo em 4 bytes (`0x9A0D`/`0x99F9`) e iniciais 1..3 + carro
  (bits 31..24; `0x9AEB`/`0x9B06`). `0x9AA9` confere as tabelas 0..9 (iniciais ' ' ou '0'..'Z', carro 0..10, tempo
  0..14400) e, com qualquer erro, `0x9A99` recria tudo com os padrões `[C736]` (pistas) e `[9A62]` (corrida inteira),
  2 palavras por entrada (tempo; carro:inicial3:inicial2:inicial1), e `0x9A77` grava 15 × −1 na tabela 15.
- Restauração de fábrica (`0x0DCE`, após "VERSION UPDATED"): zera 0xC3 contadores (`0x9972`), recria os recordes
  (`0x9A99`), ajustes 0x00..0x2D = padrões de `[C734]` com o ajuste 0 vindo dos DIPs de preço (`0x99A3`/`0x73B7`) e
  checksum, itens 0x70/0x71 = 0; depois o aviso `0x0E62` e a calibração `0x0CC0` (traduzidas no menu de serviço; o boot roda dentro da corrotina da CPU para que elas tenham quadros e entradas).
- Efeito visível: com os ajustes de fábrica, o atrato mostra "2 CREDITS TO CONTINUE / 1 COIN / 1 CREDIT" e a
  largada cria o aviso do ajuste 0x1A (`0x3C19`, 2 textos por 0x5B quadros).
- Particularidade: a validação deixa R1–R4, R6, R7, AR1, AR2, AR4 e AR5 alterados (inclusive dentro da `0x995C`).
- `0x9981` confere os ajustes 0x00–0x2D contra a tabela (mín, máx, padrão) em `[0xC733]` = `0xC12437`. A comparação é com sinal, e o valor fora da faixa vira o padrão, com checksum.
- A CMOS fica em `save/cmos.nv` (ver "CMOS persistente" mais abaixo): "VERSION UPDATED" só aparece no primeiro boot, ou em toda partida com `--cmos none`.

## Loop principal (`0x4BAF`–`0x4C32`) — traduzido (`src/core/main/main_loop.cpp`)

Entrada (`0x4BAF`): se o item 0x21 for ≤ 0, chama `0x9A99` e copia nele o item 0x27. Depois faz `[C92F] = 1`, `[C934] = [C935] = 0` e chama `0x4F08` e `0x7989`.

Cada iteração:

1. **Espera** `[0x980082] & 1` e `& 2` zerarem (DMA). Depois chama `0x4EE3` e `0xA323`.
2. **Espera** `[C930] ≥ [C931]`. É o limitador de quadros: a INT0 incrementa `[C930]`. Depois chama `0x7187`.
3. **Espera** `[C93F] == 0`, isto é, a troca de página pedida, que a INT0 executa e zera.
4. Corpo (`0x4BD6`):
   - Chama `0x7F5D`, `0x7989` e `0x794C`.
   - Faz `[C92F] = [C930]` (quadros gastos) e `[C930] = 0`.
   - Profiler (`0x4F08`).
   - Chamadas condicionais pelo modo `[C8C5]`:
     - `0x822B` se o bit 0x40 estiver ligado;
     - `0x9CFF` se o bit 0x100 estiver ligado e o modo for 4 ou 5;
     - `0x745E` no modo 2;
     - `0x4834` fora do modo 4.
   - Chama `0x4E1B`, `0x4DE6` e `0x21D0`.
   - **Zera o watchdog de software `[0x8099E0]`** (`0x4C02`).
   - Chama `0xA872`, `0x798F` e `0x774A`.
5. Volta ao topo. A exceção é o Slave do link no modo 2 com `[C8DB] == −2 ≠ [C8DC]`: chama `0x9166`, recarrega SP, chama `0x9503`, pede uma troca de página, espera e volta a `0x4BD6`.

Peculiaridade preservada: em `0x4BF8` o código lê `[C8C5]` em R0, mas o teste usa R4.

**Ritmo (com a `0x7187` traduzida):** a `0x7187` pede a troca de página e a INT0 a executa no VBLANK seguinte, antes de incrementar `[C930]`. Logo, com o corpo mais rápido que um quadro, cada iteração leva `[C931] + 1` quadros:

| `[C931]` | Quadros por iteração | Iterações/s |
|---|---|---|
| 0 | 1 | 57,9 |
| 1 | 2 | 29 |
| 2 | 3 | 19,3 |

**No port:** `MainLoop::step()` é uma máquina de estados retomável. Ela roda até a primeira espera não satisfeita, normalmente a troca de página em `0x4BD4`. A cada quadro, o `FrameRunner` roda a INT0/INT3 e depois `step()`.

Aproximações:

- no máximo um corpo (`0x4BD6`) por chamada, como guarda;
- o corpo é instantâneo;
- as interrupções só entram nos pontos de espera.

Medido:

- 400 quadros com `[C931] = 0` deram 400 iterações, sem RESET por watchdog;
- 100 quadros com `[C931] = 2` deram 34 iterações.

**Profiler (`0x4F27` / `0x4F08`):**

- Funcionamento:
  - Amostra o contador do timer 0 (`0x808024`) entre as etapas.
  - Acumula 16 amostras por iteração em `0xC947`–`0xC976`: amostras, somas e médias.
  - As médias usam o recíproco `0xA585` (Newton–Raphson).
- Limitação: o timer 0 ainda não é simulado, então as amostras valem 0. O MAME 0.289 simula o contador
  (`tms320c31_control_r` em `midvunit.cpp`: tempo desde o último reset do timer × metade do clock da CPU, com o
  bit `0x200` de clock interno). Nenhuma rotina lê as médias de `0xC947`–`0xC976`, então a diferença não aparece.

## Processos cooperativos (`0xA844`–`0xA93A`) — traduzido (`src/core/proc/process_manager.cpp`)

A lógica do jogo roda em **processos cooperativos**. O loop principal os escalona uma vez por iteração (`CALL $A872` em `0x4C04`).

**Listas:**
- ativa: `[C980]` (o pseudo-nó 0xC980 é `[0xA840]`);
- livre: `[C981]`.

A `0xA8F5` (chamada pela `0x95CB` no RESET) encadeia 0x69 PCBs de 0x3E palavras a partir de 0xEBE7.

| Offset do PCB | Campo |
|---|---|
| 0 | próximo |
| 1 | PCB + 0x2F |
| 2 | PC de retomada |
| 3 | ID (buscas usam `ID & máscara`) |
| 4 | quadros de sono |
| 5–8 | R4, R5, R6, R7 |
| 9–0xB | AR4, AR5, AR6 |
| 0xC–0x3D | variáveis do processo (`*+AR7(n)`) |

| Rotina | Função |
|---|---|
| `0xA872` | Escalonador: decrementa o sono de cada processo; se ≤ 0, guarda SP/PCB em `[C97F]`/`[C97E]`, restaura os registradores e salta ao PC com AR7 = PCB |
| `0xA874` | Adormece AR2 quadros. O `POP` do retorno vira o PC de retomada; SP volta a `[C97F]`; continua no próximo processo |
| `0xA896` | Morre (`BR`): PCB para a lista livre; continua do anterior |
| `0xA844` / `0xA868` | Cria (AR2 = PC, R2 = ID) no início / logo depois do atual. Sem PCB livre: erro 0x210 (`0xAB1A`) e carry |
| `0xA8AC` | Encerra o processo AR2 (se for o próprio: erro 0x223 e morre) |
| `0xA8CF` | Encerra todos com `ID & R1 == R0 & R1`, menos o atual |
| `0xA8E6` | Carry = existe `ID & R2 == AR2 & R2` |
| `0xA912` / `0xA90E` | Busca (do início / a partir de AR0); AR0 = PCB ou 0 |
| `0xA908` | Redireciona AR0 para o PC AR1 (sono 1) |
| `0xA91F` | Move o atual para depois de AR2 |
| `0x69D6` / `0x894D` | Contagens por ID; ambas incluem o pseudo-nó `[C983]`, como no original |
| `0x4DE6` | Um processo (ID 0x810) por bit dos 28 bits baixos de `[C8CB]`; o PC vem da tabela 0x4DFB |
| `0x95CB` | `0xA8F5` + inicializadores (`0x6FEB`, `0x7648`, `0x6754`, `0x8FF0`, `0x9019`, `0xAF55`) e `[C93E] = 0` |

**Contrato no port:** um processo traduzido é uma rotina C++ registrada no endereço do PC (início ou retomada). Para adormecer, ela faz:
1. `cpu.ret = retomada`;
2. `cpu.ar[2] = quadros`;
3. `call(0xA874)`;
4. `return`, para desenrolar a pilha C++ como o original descarta a pilha.

Para morrer: `call(0xA896)` e `return`. Entre quadros, só sobrevivem os registradores do PCB e as variáveis `*+AR7(n)`.

**Desvio do port:** um processo cujo PC ainda não foi traduzido volta sem adormecer. Nesse caso é re-agendado com sono 1 no mesmo PC. O `rea_cruisn` lista esses PCs (`untranslatedRuns`) ao terminar. Há também um teto de 0x1000 despachos por chamada, como guarda.

Todos os criadores de processos estão traduzidos: a lista enche desde o boot e `untranslatedRuns` fica vazio (a `tools/regress.sh` falha se aparecer algum PC sem tradução).

## Vídeo: VRAM, páginas, texto e números — traduzido (`src/core/video/`)

**VRAM** (`0x900000`–`0x97FFFF`): 16 bits por palavra, como o `memory_share_creator<uint16_t>` do MAME.
- Duas páginas de 512 × 512: página 0 em 0x900000, página 1 em 0x940000.
- Cada pixel é um índice de 15 bits na paleta `0x9E0000` (xRRRRRGGGGGBBBBB).
- `[0x980040]`: o bit 0 escolhe a página exibida; o bit 2, a página do DMA de polígonos.
- O software desenha em `[CE13]`.

`video::composeDisplay` monta a imagem 512 × 400 da página exibida (como o `screen_update` do MAME), e `IRenderer::blit` a apresenta. A opção `rea_cruisn --screenshot arq.ppm` grava o último quadro.

| Rotina | Função |
|---|---|
| `0x8E4E` / `0x8E59` / `0x8E65` | página de desenho / exibida (`[8E64] != 0` força a página 1) |
| `0x8E79`, `0x8E7D`, `0x8E88`, `0x8E8D`, `0x8E92`, `0x8E98` | limpezas e preenchimento de blocos, entre `[0x808064]` = 0x1000 / 0x470 |
| `0x8EB1` | zera a paleta |
| `0xA824` | pixel: `[[CE13] + y·512 + x]`; desliga e religa o GIE (`RETIU` em `0xA731`) |
| `0xABFC` | retângulo (AR2, R2)–(R3, RC) cor RS: topo, base, esquerda, direita (`0xAC23`); preserva R4, R5, AR3, DP |
| `0xAC23` | linha (AR2, R2)→(R3, RC) com cor RS; vertical/horizontal da menor à maior coordenada; inclinada percorre o eixo maior somando a inclinação (recíproco `0xA585` + `RND`, `FIX` = piso); preserva R4–R7, AR3, DP |
| `0xA7E5` | retângulo preenchido |
| `0xA7F4` | texto: fonte 8×7 em `[0xA730]` = 0xC122A8 (7 palavras por glyph, a partir de `'('`); espaço avança 8 |
| `0xA762` / `0xA74E` / `0xA7AC` | número → texto (decimal; decimal com 2 dígitos; hexadecimal). O menos é `'>'` na fonte |

**Aritmética** (`src/core/math/int_math.cpp`):
- `0xA531` / `0xA5A9`: `DIV_I` / `MOD_I` da TI, com truncamento do C. Divisão por 0 devolve 0 (desvio documentado).
- `0x8EB7`: aleatório; `[C8CE] = MPYI(x << 1 | bit, 0x794F)`.

**Tela de carga do RESET** (`src/core/boot/boot_screens.cpp`, `0x4F32`/`0x4F7D`/`0x4F8F`/`0x4F9B`):
- monta a paleta mínima (1 = branco, 0xB = cinza);
- desenha título, créditos, "VERSION 4.1", o papel no link (DIP) e as linhas "de rede";
- a moldura (`0xABFC`): retângulo de 4 linhas (`0xAC23`) com a cor RS.

Os textos têm chaves I18N (`BOOT_*`).

**Ajuste do port:** as DIPs padrão (`kDefaultDsw`) passam a valer antes do RESET. Antes, só o `InputManager`, criado depois, as aplicava, e a tela de carga lia Slave.

## Descompressor LZW de texturas (`0xA2B0`–`0xA3D6`) — traduzido (`src/core/gfx/lzw_loader.cpp`)

**Formato:**

| Código | Significado |
|---|---|
| `0x00`–`0xFF` | Literal |
| `0x100` | Fim do fluxo |
| `0x101` | Largura do código + 1 bit (começa com 9) |
| `0x102` | Limpa o dicionário; ponto onde a rotina pode ceder a vez |
| `≥ 0x103` | Entrada do dicionário |

- Os códigos são lidos do MSB para o LSB de cada palavra, sem alinhamento (`0xA2B0`).
- O `0x101` só é reconhecido a partir do 2º código depois de cada limpeza: o primeiro código, lido em `0xA338`, é sempre tratado como literal.
- A saída empacota 4 bytes por palavra, com o primeiro byte no LSB.
- A cada 64 palavras, `0xA2C9` copia o buffer em meias palavras para `*AR1++`, com wait states `0x1000`. Ela termina em `BUD $4C33` (`RETIU`), por isso **religa o GIE**.
- Bytes que sobram no fim (menos de 64 palavras) não são copiados, como no original. Nos fluxos do RESET isso não ocorre: todos têm tamanho múltiplo de 256 bytes.

**Entradas:**

- `0xA3C2(AR2 → {origem, destino})`:
  - se `[E5CA] = 0`, chama `0xA2F9`;
  - senão cria um processo (`0xA844`, entrada `0xA3D7`). Ele espera um quadro por vez (`0xA874`) até a descompressão atual acabar e então refaz o pedido.
- `0xA2F9(AR4 = origem, AR5 = destino)`: monta o contexto em `0xE5CF`–`0xE5E3`.
  - Com `[E5CB] ≠ 0` (RESET), descomprime tudo de uma vez. A cada fatia chama `0xA3A5`, a animação de "carregando":
    - apaga a barra vertical em x = `[E5E8]` (y 0x6F..0x74) com a cor 0;
    - soma `[E5E9]` a x e redesenha com a cor 3 (linha `0xAC23`);
    - x ≤ 240 → `[E5E9]` = 1; x ≥ 300 → `[E5E9]` = −1.
- `0xA323`: chamada pelo loop principal. Continua a descompressão e cede a vez no primeiro `0x102` depois de ≥ 0x800 palavras escritas (`[E5CD]`).

**Peculiaridades preservadas:**

- `0x9042`/`0x905C` salvam AR0–AR7/R0–R7 tirando o endereço de retorno com `POP BK`. Por isso BK termina valendo o endereço de retorno.
- DP, IR0, IR1, RC, RS e RE vazam para o chamador.
- A última volta do `RPTB` de saída lê uma palavra abaixo da pilha.

**Cargas do RESET (`0x4B76`), todas bloqueantes:**

| Origem | Destino | Bytes | Calls de `0xA323` no modo cooperativo |
|---|---|---|---|
| `0xD00000` | `0xA00000` | 218 624 | 16 |
| `0xD08A66` | `0xA1AB00` | 393 216 | — |
| `0xD160B4` | `0xA52900` | 2 540 800 | — |
| `0xC15000` | `0xBEFA00` | 124 160 | — |
| `0xE63488` | `0xB88B80` | 208 128 | — |

Os testes comparam a saída com uma simulação independente em Python (hash FNV‑1a).

**Aproximações do port:**

- O DMA não é simulado: as esperas em `[0x980082]` saem na hora. O MAME 0.289 faz igual: a fila informa sempre
  0 entradas (`dma_queue_entries_r`) e a leitura de `0x980083` (`dma_trigger_r`) desenha a fila inteira no ato.
- As cargas bloqueantes rodam sem interrupções entre as fatias.

## Lista de objetos de desenho (`0x7187`–`0x7251`) — traduzida (`src/core/scene/object_list.cpp`)

A lista é simplesmente encadeada e a célula da cabeça fica em `[[0x40]] = [0xC982]`.

| Campo | Uso |
|---|---|
| `+0` | Próximo |
| `+1` | Coordenada (float TMS) |
| `+0xF` | Tipo |
| `+0x1B` | Estrutura com 5 vizinhos em `+5`, `+0xB`, `+0x11`, `+0x17` e `+0x1D` |
| `+0x1C` | Chave de ordenação (inteiro com sinal) |

A ordem é **decrescente** pela chave, como no algoritmo do pintor: o mais distante é desenhado primeiro.

- **`0x7187`:**
  - Faz `[C93F] = 1` (pede a troca de página) e repete passadas de *bubble sort* até a INT0 zerar `[C93F]`. É a única saída: toda passada termina em `BR $7189`, e o teste de R6 em `0x71A0` nunca vê 0. Na prática, o jogo ordena a lista **enquanto espera o VBLANK**.
  - Depois, se `[C8C5] & 0x20 == 0`, chama `0x71B3`, `0x71E4`, `0xAF75`, `0x71BF`, `0x87B8` e `0xAE05`.
  - **No port**, a INT0 não roda dentro da rotina. A ordenação para quando uma passada inteira não troca nada; passadas extras não mudariam a lista. A espera da troca fica com o loop (`0x4BD4`). Um orçamento de 2²² comparações faz o papel do VBLANK em listas corrompidas (cíclicas).
- **`0x71B3`:** retira o primeiro objeto do tipo `0x100` e o guarda em `[C98C]` (0 se não houver).
- **`0x71E4`:**
  - Retira os objetos com `tipo & 0xF00 == 0x200` e os empilha, o que inverte a ordem.
  - Para cada um, escolhe entre os 5 vizinhos o de menor chave. Vizinho nulo vale `0x7FFF0000`; no empate fica o último. Procura esse vizinho na lista; se não achar, começa pela cabeça.
  - Insere o objeto antes do 1º elemento seguinte com chave ≤ a dele.
  - Se o 1º vizinho for do tipo `0x300`, só contam elementos com tipo < `0x310` ou `tipo & 0xF00 == 0x600`.
  - O trecho `0x7228`–`0x722A` é inalcançável, porque `[0x40] ≠ 0`.
- **`0x71BF`:** recoloca `[C98C]` no **fim** da lista, levando junto os objetos `0x21x` com chave ≤ a dele e `FIX(|Δcoord|) ≤ 2000`. Esses objetos são desenhados por último.
  - `FIX` arredonda para −∞ (`tmsFix`).
  - A subtração usa double em vez da precisão estendida de 32 bits do TMS.

### Listas de visibilidade e pool de objetos (`0x726F`, `0x7254`, `0x6FEB`) — traduzidas (`src/core/scene/object_list.cpp`)

Há duas listas de objetos: a **visível** `[[0x40]]` (`0xC982`), que a `0x7187` ordena e desenha, e a **distante** `[[0x41]]` (`0xC984`). O processo de sistema `0x903B` alterna as duas rotinas abaixo, uma por quadro.

- **`0x726F` (promove):** para cada objeto distante, calcula a profundidade = (pos `+1..+3` − câmera `[[0x45]]`) · eixo `[[0x47]+6..+8]` e grava `FIX(profundidade)` em `+0x1C`. Se profundidade > −5000,0 e `FIX − 75000 − raio(+0x1D) ≤ 0`, o objeto vai para o início da lista visível.
- **`0x7254` (rebaixa):** usa a profundidade gravada. Se ela for ≤ −5000, ou se `profundidade − 80000 − raio > 0`, o objeto vai para a lista distante.
- Toda troca de lista faz `+0xE ^= 0x3000`. A histerese é de 5000 unidades: entra a 75000 e sai a 80000.
- **`0x6FEB`:** esvazia as listas (`C982`, `C984`–`C988`, `C98A`, `C98B`, `E403`–`E406`) e encadeia o pool de 1100 objetos de 0x22 palavras (a partir de 0x10555) na lista livre `[C983]`. `[C989]` = 1100.
- **Ordem dos operandos:** o disassembly imprime as instruções de 3 operandos como `OP3 src1,src2,dst`, com src1 nos bits 15–8. Assim, `SUBF3 a,b,d` calcula `d = a − b`, o que foi conferido com `320c3x_ops.ipp` do MAME. As condições do C3x não usam V: LT = N, LE = N|Z, GT = !(N|Z), GE = !N.
- **Varredura das subtrações (2026-11-14):** depois do erro da colisão com o cenário (`0x221F`), todas as instruções sensíveis à ordem no código alcançável a partir das 1504 entradas traduzidas foram conferidas com a tradução: 221 no total (151 `SUBF3`, 40 `SUBRF`, 21 `SUBI3`, 8 `SUBRI`, 1 `CMPI3`; `CMPF3`, `ANDN3` e `SUBB3` não aparecem nesse código). Erros achados: o motor do volante (`0xAFE8`, velocidade = anterior − atual em vez de atual − anterior, o que invertia o amortecimento). Na mesma revisão: a mistura de paletas `0x5D00` limitava t a [0,08; 0,92], mas o original mistura com R7, copiado antes do limite (o limite não tem efeito), e a linha `0xAC23` não devolvia RS/RC como o original nas trocas de pontas.
- **Varredura das subtrações e comparações de 2 operandos (2026-11-14):** no mesmo código alcançável, as formas de 2 operandos (`SUBF`/`SUBI` src,dst = dst − src; `CMPF`/`CMPI` src,dst = flags de dst − src) foram conferidas uma a uma com a tradução: 660 instruções em 276 rotinas, incluindo as registradas de forma indireta. Ficaram de fora as que não dependem da ordem: operando imediato, src = dst, comparações seguidas só de EQ/NE/U e diferenças que só entram ao quadrado, em `ABSF` ou na volta de ângulo (`0x9308` + `ABSF`). Também não contam as comparações cujos flags são sobrescritos antes do desvio (`CMPI @($C989),R4` antes de `CALL $700A`). Nenhum erro encontrado. Casos estranhos do original que a tradução já repete: o `CMPI 6,AR3` da `0x1A75`, que nunca é verdadeiro, e o `LDFLT 0.0` da `0x1C7C`, que grava 0 como inteiro.
- **Guarda permanente:** as duas varreduras estão em `tests/operand_order_reviewed.txt`, e a
  `tools/check_operand_order.py` (etapa 5 da `tools/regress.sh`) falha se uma instrução sensível à ordem entrar no
  código traduzido sem estar nessa lista (ver "Ferramentas").

## Início do jogo e modo de atração — traduzidos (`src/core/game/`)

- **`0x9503` (`game_start.cpp`):** reinicia o jogo, como descrito no cabeçalho do arquivo. Zera a RAM interna, limpa a BSS, copia a RAM principal para 0x400000, reinicia os processos e a câmera, cria o processo de sistema `0x903B` e escolhe o caso por AR2:

  | AR2 | Caso |
  |---|---|
  | −1 | atração |
  | −2 | `0x9587` |
  | −3 | `0x95A4` |
  | −4 | `0x9596` |
  | −5 | `0x95B4` |
  | −6 | `0x95B6` |
  | −7 | `0x9572` |
  | −8 | espera o outro gabinete |
  | 1 | `0x95C4` |

  A tabela de casos está em `[0x955B]` = 0x9563 e é indexada com AR2 negativo.
- **Salto não-local:** `0x1E17` e `0x1E1C` descartam a pilha (`SP = [0x63]`), chamam a `0x9503` e saltam para `0x4C1B`. Quem as chama é um processo, ou seja, o meio do escalonador. **No port**, elas gravam `cpu.jump = 0x4C1B`; o `Scheduler::run` para de despachar, e o `MainLoop` consome o valor e segue em `SlaveDma0`.
- **Ciclo de atração:** a `0x1E1C` faz `[C8E8]` −1 → −2 → −3 → −4 → −1.
- **`0x3859` (`attract.cpp`):** carrega 5 paletas por ponteiro (`0x9F2F`) e um bloco de paletas por id (`0x9DF6`) e cria a cena `0x3889` (ID 0x560, AR6 = índice da cena, começando em `[C8CF]`).
  - Cada cena dura 211 quadros, ou acaba antes se `[E5FF]` mudar.
  - Por tempo: a cena seguinte só começa se o novo índice for ímpar; senão vai para `0x1E1C`.
  - Por `[E5FF]`: mata as cenas `0x56x` e avança. Com `[E5FF]` = −1, recomeça da cena 0. Com índice ≥ 15, vai para `0x1E1C`.
- **Cena `0x3889` (`hiscore_scene.cpp`): tabela de recordes em 3D.** Cada letra e cada dígito é um objeto de cena com o modelo do caractere. As letras vêm de `[B169]` + (c − 'A'), com '0' → 'O' e '1' → 'I'. Os caracteres das iniciais vêm de `[B168]`, e os dígitos de `[B16D]`. Os títulos são os nomes das rotas (`[0x3954 + rota]`: GOLDEN GATE PARK, SAN FRANCISCO, US 101, REDWOOD FOREST, BEVERLY HILLS, LA FREEWAY, DEATH VALLEY, …).
  - São 10 linhas da CMOS (`0x9AD2`: `[9936] + 0x3F8 + rota·0x50 + linha·8`; `0x9B06`: tempo de 32 bits + 4 bytes de iniciais).
  - O tempo fica em 1/55 s e é exibido como `mm:ss:cc` (`0x60BD`).
  - As linhas começam afastadas em z (−5000 − 500·n), deslizam 250 por quadro até z = 1000; depois a câmera se move por 40 quadros.
  - O filho `0x38CD` (destaque do recorde novo) está traduzido (`hiscore_scene.cpp`).
  - Auxiliares de objetos: `0x36B3`–`0x3737` (buscas por tipo, `object_query.cpp`), `0x7025`–`0x70C9`, `0x715F`, `0x440C`, `0x9ECD` (`scene_object.cpp`), `0x7298`/`0x72D2` (refaz e ordena as listas). Trigonometria (`math/trig.cpp`): `0x92DF`/`0x92E2` (tabela de 1/4 de onda), `0xB01A`/`0xB042` (polinomiais, Cody-Waite), `0x938A`/`0x939D` (rotação em Y) e `0x9377` (rotação em X).
  - A `0xA5A9` agora também devolve AR0 = dividendo e R1 = −|resto| (as flags refletem o resto, que a `0xB01A` usa).

### Tela de título (caso −2, processo `0xA93B`) — traduzida (`src/core/game/title_screen.cpp`)

- `0x9587` (caso −2): modo 2, recursos da pista (`0x59E3` → tabela `[[0xB0AD] + [C8CF] + 1] + [8C13]`; pista 0 = `0x8C14`, duas texturas LZW), processos `[C705]` e `[C706]`.
- `0xA93B`: logotipo **MIDWAY** em 3D (`[B0BF]`, flags 0x61 = polígonos lisos sombreados) em (0, −910, 5500), girando em Y. A câmera fica em y = −2780. O logotipo **Nintendo Ultra 64** (`0x1DCF`, processo `0x1DC3`) gira à parte.
- Duração: 890 quadros com a música do atrato (item 0x1E da CMOS; o som sai pela `0x910F`/`0x91E0`). Sem ela, a tela sai depois de 4 quadros com `[E5CA]` = 0 (`0xA986` → `0x1E1C`).
- `0x9343`: matriz de rotação a partir de 3 ângulos (ZYX). O original restaura R1–R3 trocados (preservado).
- Chamadas pela `0x9503`, traduzidas junto:
  - `0x93D7`: identidade na matriz da câmera. Sem ela, `[0x4B]` = 0 e os objetos não giram com a câmera; o MIDWAY virava uma linha.
  - `0x7409`/`0x7421`: ajustes da CMOS empacotados em `0xE542`.
  - `0x7F4D`: configura a placa de link `0x997000`.
- `0xA3D7`: processo que espera `[E5CA]` = 0 e carrega uma textura LZW.

### Caso −3 do atrato (`0x95A4`) — `0xA9C8`, `0x559E` e o roteiro de câmera traduzidos (`src/core/game/attract_demo.cpp`, `demo_camera.cpp`)

- `0x95A4` cria três processos: `[C70A]` = `0x559E` (ID 0x202), `[C70B]` = `0xA9C8` e `[C70C]` = `0x1E26` (cronômetro de 900 quadros, `[E84F]`).
- `0xA9C8` monta a moldura 0xC2F8F3 em (−150, −110, 436) e, sem `[C8CC] & 4`, as três placas da `0x1DE9`. Põe o logotipo da pista (`[[A9C7] + ((([C8CF] >> 1) − 1) & 3)]`) em z = 1368, espera 16 quadros e o gira em Y (0,017998 por quadro).
- `0x559E` é a demonstração: um interpretador de roteiro (`[[B0AC] + [C8CF] >> 1]`). Cada passo tem uma função de início, que pode ler argumentos do roteiro (`*AR6++`), a duração em AR5 e uma função por quadro; o terminador 0 repete o último passo. A câmera segue os carros do tipo 0x216 da lista `[0x57]` pelo traçado `[AA2F]`/`[E46E]`.
- Roteiros (`0xC1030B`, um por par de pistas): perseguição `0x5624`/`0x5671`/`0x5680`/`0x568F` (atrás ou à frente do N-ésimo carro, a −350/−300 de altura), panorâmica `0x5633` (arg.: índice do nó) e câmera presa ao carro `0x5869`/`0x5861` → `0x58EF` (a duração é o índice do nó em que o passo acaba). Detalhes em `demo_camera.hpp`.
- `0x596F` (carga da pista da demo) chama os carregadores por pista `[[8C12] + [E481]]` e `[[3965] + [E481]]`, `0x6754`, `0xAF55`, `0x3F8D` e cria os 9 carros (`[B0B6]`.., processo `0x50CB`, ID 0x200). No fim, `[C8CF]` += 2.
- `0x55DA`: a moldura entra de z = 10000 até 368 e desliza para (−120, −85) em 125 quadros.
- O laço de IA dos carros (`0x513A`), a física (`0x2EFD`, `0x2C39`…), a altura do chão `0x1F8B` e a reordenação da lista de objetos chamada pela `0x7187` (`0x87B8`, `0xAE05`, `0xAF75`) estão traduzidos (seções abaixo); a câmera da demo segue os carros pela pista.
- Sem carros do tipo 0x216, `0x57F0` trava no original (`BU $`). Esse laço (`0x581B`) fica sem tradução de propósito: com os 9 carros criados pela `0x596F`, o caminho não ocorre.

## Carga da pista (`0x8C14`–`0x8E48`, `0xAA30`–`0xAB18`) — traduzida (`src/core/track/track_loader.cpp`)

- 14 carregadores por pista em `[8C12]` = 0xC11057, indexados por `[E481]` (da tabela `[B0AD]`). Cada um carrega texturas LZW (`0xA3C2`), paletas (`0x9DF6`/`0x9DFE`/`0x9EC2`), ajusta `[E916]`, `[E47F]`, `[E8E3]`, `[E5F2]`/`[E5F3]` e o céu `[E3FD]`, e converte os trechos da pista (AR0..AR1). Um segundo carregador vem de `[3965]` = 0xC10652 (`0x3966`/`0x396B`/`0x396E`).
- Dados da pista em `[A103]` = 0xCD3923 (EPROMs u14..). Cada registro tem flags e 5 palavras: origem `+1..+3`, ângulo Y `+4` e a lista de pontos `+5`. Opcionais: +1 palavra se flags & 1, +4 se flags & 8, +1 se flags & 0x1000.
- `0xAA66`/`0xAA9D` geram a tabela de nós `[AA2F]` = 0x1CB3C. Cada nó tem 4 inteiros: x, y (bit 0 = flags bit 7), z e índice = trecho << 8 | faixa. Por trecho, os nós são ordenados pelo índice e a tabela termina com 4 × −1. Na pista 0 (trechos 1..0x57, mais 3 à frente): 849 nós, ~3448 unidades entre nós, coordenadas até ~2·10⁶.
- Listas: `[E404]` (carros, `0x6754`/`0x6762`/`0x676B`) e `[E8EC]` (objetos 0x730, `0xAF55`/`0xAF58`/`0xAF66`).

## Fluxo de trechos (`0x3F6F`–`0x4224`) — traduzido (`src/core/track/track_stream.cpp`)

- A janela `[3F69]` = 0xE408 guarda até 5 trechos carregados, com 5 palavras cada: marca `trecho << 8 | 0xAA`, registro, lista de nós, flags e número do trecho. `0x3F8D` posiciona no trecho `[E3FE]` (`0x3F6F`), carrega 5 trechos e cria o processo `0x3FE3` (ID 0x5AA).
- `0x3FE3` roda a cada 3 quadros nos modos 2 e 4:
  - carrega o próximo trecho quando ele está a ≤ `[3F6C]` da câmera `[0x45]`;
  - libera o primeiro trecho (`0x41D1`: objetos com a marca nas listas `[0x40]`/`[0x41]`, de volta ao pool `[C983]`) quando a câmera passa `[3F6D]` (modo 2: `[3F6E]`) do seu início;
  - no modo 4, avança `[E477]` conforme o carro de foco e chama `0xACCA`.
- `0x405A` cria um objeto (`0x700A`) por ponto do trecho: gira em Y, translada e guarda o tipo em `+0xF` e as flags em `+0xE`. Os tipos 0x300, 0x900 e 0x600 vão para as listas `[E403]`, `[E406]` e `[E405]`. Os nós 0xB00 viram 0x300 com índice `+0x1E` e entram na lista duplamente ligada `[E46E]`..`[E470]` (`+0x21` próximo, `+0x17` anterior), a lista que a câmera da demo usa. Pontos com 0x2000 chamam o tratador do tipo na tabela `[4500]` (`0x4212`; tratadores `0x43DE`, `0x4292`, `0x428C`, `0x43CB`, `0x42EE`, `0x4225`, `0x9B43`…, todos traduzidos; ver "Objetos animados do cenário").
- No 4º trecho, `0x405A` também cria a placa `[B0C3]`.

## Céu da pista (`0x822B`, `0x839B`, `0x83D1`) — traduzido (`src/core/video/sky_renderer.cpp`)

- O loop principal chama `0x822B` com `[C8C5]` & 0x40. Na demo e no jogo, `[C940]` = 1 desliga a limpeza `0x8E6D`, e é o céu que cobre a tela.
- A rolagem horizontal vem do yaw `[809807]` → `[E3F8]` (FIX(yaw·`[8224]`) dobrado em ±0x600). `[5A]` = câmera × rotY(−yaw).
- `0x839B` projeta os pontos (3 palavras cada, em `[0x4F]` = 0x809D00): x + rolagem (dobrado em `[8226]`) + `[0x53]`; 1,04·(y + `[E3FD]`) + `[0x54]`.
- Os quads vão direto à FIFO de DMA `0x600000`: espera `[980082]` & 4, envia 15 palavras e dispara com a leitura de `0x980083`.
  - Céu: 12 quads, cabeçalho 0x100, textura `0x9ECD([8383 + 2i])`.
  - Chão: 3 quads, cabeçalho 0x1E.
  - Com `[C8C5]` & 0x80 (pista 0 via `0x8C6C`), `0x83D1` desenha 12 quads de horizonte (tabela `[8430]`, textura do slot 0x31) até y = 0x190.
- No modo 4, o céu é recortado pela linha `[E3FA]`/`[E3F9]` (`0x94C9`/`0x94B4`, em `float_math.cpp`). `0x81D9` (modo 4) também está traduzida (`sky_renderer.cpp`).
- Peculiaridade do original: em `0x83D1`, o R7 calculado de `[80983C]` e depois de R0·`[822A]`/`[80980D]` (R0 entra com `[C8C5]`) é sobrescrito pela `0x839B` sem ser usado.

## Altura do chão (`0x1F8B`) — traduzido (`src/core/track/ground_height.cpp`)

- `0x1F8B` (AR4 = ponto float x,y,z) percorre a lista `[E403]` (trechos de estrada, tipo 0x300) e depois
  `[E406]` (tipo 0x900), encadeadas por `+0x20`. Candidato: distância XZ² ao centro `[obj+1]`/`[obj+3]` menor
  que o raio² (`[obj+0x1D]`, inteiro).
- `0x20FF` transforma os vértices do modelo `[obj+0xD]` para coordenadas relativas ao ponto
  (v' = M·v + (obj − p), M = 9 floats em `[obj+4]`) em `[0x4F]` e testa cada face (4 índices de byte na 1ª de
  5 palavras) no plano XZ: aresta p→q rejeita se x_p·z_q − z_p·x_q > |x_p − x_q| (faces em sentido horário; `SUBF3 a,b,d` = a − b). Acerto → `[E577..E579]` =
  vértices a, b, c, carry = 1.
- `0x20B4`: normal `0x9466` em `[E57B]`, altura R0 = −(n·a)/−n.y (`0xA509`), carry = 1. Sem acerto → carry = 0,
  R0 = 0,0. `0x1FB0` = `0x1F8B` com AR4 + 1.
- Efeito: a câmera de perseguição da demo (`0x5719`) passa a seguir o chão e a estrada aparece.

## Física vertical do carro (`0x2C39`) — traduzida (`src/core/game/car_physics.cpp`)

- `0x2185` (na criação): caixa do modelo → 5 pontos de contato em D + 0x47: centro de baixo e os 4 cantos
  (y = máxY, o y cresce para baixo); limites em `[D+0x3A..0x3F]` (x com margem de 25).
- `0x2C39` (laço do carro): `0x1FF6`, rumo `[D+0x2C]` → `[obj+0x15]`, matriz = rumo × atitude (`0x938A`,
  `0x9494`), `[D+0x46]` = distância lateral à reta do nó (`0x67E3`).
- `0x1FF6`/`0x2041`/`0x2074`: os pontos (D + 6·i: posição, altura do chão, velocidade vertical, objeto do chão)
  procuram o chão em `[E403]` e `[E406]` (`0x20B4`). Cada ponto cai com gravidade 4·`[C92F]` (×5 fora do
  tipo 0x330) até o chão; altura ≤ −9 encosta direto.
- `0x20D4`: a atitude vem do plano dos pontos 0, 1 e 2 (normal `0x9466` normalizada por `0x943B`), e o y do
  objeto = y do ponto 0 − `[D+0x48]`.
- Efeito: na demo os carros ficam apoiados na pista, no grid de largada.

## Largada (`0x1C9B`) — traduzida (`src/core/game/race_start.cpp`)

- Processo da contagem: dois passos da tabela `[B221]` (texto em (256, 160), som, função de link) a cada
  0x14 quadros, depois o "GO" de `[B222]`.
- No "GO": `[C8C5]` |= 0x200 (corrida ativa), `[E5EC]` = 0x5577 (solta o grid), `[E604]` = ajuste 0x1C·5 + 0x3C.
- Com link, o escravo segue o passo do mestre `[E63B]` e espera `[C8E1]` = 3 antes de começar.

## Dinâmica de direção (`0x2C18`) — traduzida (`src/core/game/car_dynamics.cpp`)

- Marcha automática `0x2E73` (rotação × acelerador), velocidade `0x2D83` (curva de torque `[2D68]`
  interpolada, marcha `[2D62]`, arrasto pelo tipo de chão dos 5 pontos, freio `[2915]`, derrapagem `[2916]`),
  derrapagem `0x2C9C`, rotação `0x2C92` (`[2C46]`, máx. 47).
- Esterço `0x2CEB`: o rumo desejado `[D+0x2D]` puxa o rumo `[D+0x2C]`; rodopio com `[D+0x31]`/`[D+0x32]`.
- Batida no muro `0x2F6B`: com um dos pontos 1..4 sem chão, toca um som (`0x3159`/`0x3168`), empurra o carro
  para dentro e alinha o rumo à pista (`0x3037`) ou inicia um rodopio.
- Velocidade do objeto = matriz `[0x5B]`·(0, 0, `[D+0x27]`); posição += velocidade (`0x9081`).
- Sem `RETS` no fim: segue direto em `0x2C39` (física vertical), chamada explícita no port.

## IA dos carros (`0x513A`/`0x5140`) — traduzida (`src/core/game/drone_ai.cpp`)

- Segue os nós da pista (`[AR7+0x10]`, índice `[AR7+0x13]`), avançando a menos de 5000.
- Mapa de ocupação lateral `[538C]` (50 faixas de 100): bloqueios da pista (`0x53A2`/`0x66A7`), outros carros
  (`0x5441`, marca 0x2000 + quadros até a colisão) e o jogador (0x4000). `0x53DA` escolhe a abertura de 8 faixas
  mais próxima do alvo e freia atrás do jogador.
- Esterço = diferença de rumo até o ponto-alvo ÷ quadros até o nó, ×2,5, limitado a ±0,09; acelerador
  `[AR7+0xF]` variado ao acaso (`0x52E4`, mais forte quando atrás do jogador).
- Fora da janela de trechos, o carro anda pelo caminho grosso `[AR7+0x12]` (`0x52B8`/`0x532B`) e volta em `0x523C`.
- Fim de corrida `0x51C2`: contagem `[AR7+0x29]` pela ordem de chegada `[E8A2]`; depois o carro fica parado (`0x51EA`).

## Rodas e carroceria (`0x8F68`) — traduzidas (`src/core/game/car_wheels.cpp`)

- Um processo por carro (ID 0x230, criado em `0x8F3E`); peças na lista `[obj+0x1A]` (próxima `+0`, matriz `+7`, tipo `+0x12`).
- Com o grid preso (`[E5EC]` = 0x33BB) só acompanha a velocidade; longe da câmera (`[obj+0x1C]` > 0x4E20) não anima.
- `[5A]` = rotY(esterço·1,5); giro `[P+0xE]` += velocidade·0,02; `[5B]` = rotX(giro); `[5C]` = `[5A]`·`[5B]` (`0x9481`).
- Peça tipo 1 recebe `[5B]` (traseira), tipo > 1 recebe `[5C]` (dianteira), tipo < 0 é ignorada; tipo 0 é a carroceria
  (`0x8FAE`) e encerra o quadro (dorme 1; sem carroceria dorme 3).
- `0x8FAE`: arfagem = −(acumulado da aceleração·0,06, ×0,25) (×2 para frente, ×1 para trás) + rotação·(−0,0005), em ±0,1
  → `[D+0x42]`; rolagem = média da variação do rumo·velocidade·0,006 → `[D+0x43]` (±0,1), ×2,2 limitada a ±0,1 para a
  matriz; ambas zeradas no ar. Matriz da peça = rotX(arfagem)·rotZ(rolagem).
- Particularidade: a correção de ±2π da variação do rumo (`0x8FD4`) usa `ADDI`, que soma só o campo sinal+mantissa
  do registrador estendido (o expoente fica); a tradução reproduz isso (`addiFloatFields`).

## Posição e tráfego da corrida (`0x659F`, `0x65ED`, `0x6639`) — traduzidos (`src/core/game/race_traffic.cpp`)

- `0x659F` (a cada 0xE quadros) → `0x65A5`: `[E607]` = 1 + carros tipo 0x216 à frente do focado `[E876]`/`[E878]`;
  `[E5F1]` = carros com `[obj+0x1C]` em −5000..30000. "À frente": índice de nó `+0x1E` maior (ou `[D+0x57]`
  maior quando `[D+0x58]` ≠ 0, ou `[[obj+0x17]+0x25]` > 0); no mesmo nó, distância² ao nó seguinte menos
  `[D+0x46]`² (lateral, `0x67E3`).
- `0x65ED` (sem link): espera 0x258 quadros; ciclos de 2·(`[E5F2]` + rnd(`[E5F3]`)); com `[E5EB]` ≠ 0, grid solto e
  `[E5EE]` − 1 ≤ `[E5F4]`, sorteia pela posição (≤ 1 sempre; ≤ 4: < 70; ≤ 7: < 60; senão ≤ 35 em 100) e cria
  `[B12B]` = `0xA3FF` (ID 0x214); espera esse carro sair de 25000 de `[E46D]` (`0x4204`).
- `0x6639`: `[E5F5]` = 10; fora do modo 2 espera o focado andar 11000; ciclos de 50 quadros (posição ≤ 2) ou
  `[E5F2]` + rnd(`[E5F3]`); limiares 75/60/35, no máximo 8 carros próximos e `0x6680` → cria `[B12C]` = `0x9765`
  (ID 0x213).
- Particularidade: os sorteios usam `BLT`/`BGT` sem `BEQ`; o valor igual ao limiar cai no teste seguinte (reproduzido).
- `0x6628` numera o carro (`[E5F5]` 10..0x7F, | 0x80 sem o bit 0 de `[C8CC]`) em `[AR5+0x59]`.

## Pisca de paleta (`0x1F2A`) — traduzido (`src/core/video/palette_blink.cpp`)

- Slot da textura 0x2E (`0x9ECD`); envia 5 cores de `[1F3C]` para slot + 0xFB (`0x9F5C`) e dorme a duração do passo
  (4 passos 0x6B40/0x7FE0, 2/4 quadros; −1 volta ao início).

## Colisão entre carros (`0x2863`, `0x2645`) — traduzida (`src/core/game/car_collision.cpp`)

- `0x28A8`: 8 cantos da caixa `[D+0x3A..0x3F]` (x mais rápido) → mundo por matriz `+4..+0xC` + posição.
- `0x2863`: caixas em `[0x4F]` (0x809D00/0x809D30), 12 planos (tabela `[28E7]`, `0x9466`) em 0x809D60; carry se algum
  canto de uma caixa está dentro (n·v ≥ d) das 6 faces da outra; AR3 = y do canto em contato.
- `0x2645`: filtros (tipo 0x270 / 0x260 / |Δy| > 750), `0x8928`, direção `[0x5D]` e |Δv| (`0x2633`), som (`0x2835`),
  separação ±½·|Δv|, batida forte do focado (`0x23EF`), choque ¼ elástico + ¾ centro de massa ± 10·dir, rumos novos.
- `0x2727` (uma vez por carro): impulso de giro `[E58F]` = ±15·2·|r|·(1 − |r̂·p̂|)/massa; escolhe rodopio (`0x27A6`),
  giro forte (`0x27C3`), giro leve (`0x27DC`) ou empurrão (`0x27F7`) pela velocidade, sorteios (`0x8EE9`) e, no focado,
  pelo desvio da pista (`0x2824`) e por quem está à frente (`0x278F`/`0x5375` → `[E590]`).
- Particularidades: `0x3037` não preserva AR0/AR2 (a 2ª chamada da `0x2727` herda o AR0); o rodopio sobrescreve R4;
  `0x281D` faz `LDI` inteiro em R3 que depois é lido como float.

## Colisão com objetos do cenário (`0x221F`) — traduzida (`src/core/game/scenery_collision.cpp`)

- Chamada pela `0x2207` (carro AR0 × lista AR1) quando os círculos se tocam e o objeto não tem a flag 0x10.
- Teste fino: cantos da caixa do carro (`0x28A8`) e o centro do objeto contra as 4 arestas do retângulo de cima
  (cantos 2→6→7→3→2, tabela `[290C]`); qualquer lado ≤ 0 → fora. **Correção (2026-11-14):** a aresta é
  R0 = `[AR4+1]` − `[AR3+1]` e R1 = `[AR3−1]` − `[AR4−1]` (`SUBF3 a,b,d` = a − b); a tradução tinha as duas
  subtrações invertidas, o que trocava o sinal do teste e fazia **nenhuma** colisão com o cenário acontecer (postes,
  cavaletes etc. eram atravessados). O teste unitário usava uma caixa espelhada (`+0x3B` > `+0x3A`) que escondia o erro;
  hoje usa a mesma orientação dos carros do jogo.
- Despacho por `[obj+0xF]`:
  - `& 0xF0` = 0x60: ignorado;
  - `& 0xFF0` = 0x750: `[obj+0x1B]` = 1, som com intervalo mínimo `[C62C]` (`0x6971`), `0x6959` (modelo `0x6997`,
    `0x6AD3`/`0x6A66` em `roadside_events.cpp`);
  - `& 0xF0` = 0x30 (arremessado, |Δy| ≤ 250): velocidade (sen/cos do rumo ± 0,1) · (0,8..1,45) · 1,5 · v do carro,
    vy ≤ −65 · …; processo de voo `0x2334` (PCB em `[obj+0x17]`, bits 23 e 27);
  - `& 0xF0` = 0x20 e os demais: tombado: gira para o rumo do carro, sai da lista `[0x58]` (`0x23D1`), o carro perde
    max(20 %, 10) da velocidade, processo `0x239C` gira R7 = v·0,03 (0,1..0,7) por quadro até 1,5 rad;
  - `& 0xFFF` = 0x615: 1..3 processos `[B124]` = 0x685D (ID 0x615, detritos, em `object_effects.cpp`) e depois sólido;
  - `& 0xF0` = 0x40: sólido: empurra o carro |Δv|·dir (`0x2633`), v · 0,6 (mín. 37), giro curto ±0,02 para a pista
    ou rodada (sorteio 500/1000 acima de 37); batida de frente (≥ π/2) inverte o rumo desejado; no 0x615 o objeto
    balança (`0x23B0`: R7, ·−0,4 por 4 quadros, ·−0,6 a cada 4 até |R7| ≤ 0,01).
- Som: carro focado → `0x9188`; outro carro → `0x316E` (volume 220·(1 − dist/50000)).
- O voo `0x2334` soma gravidade 2·`[C92F]`, gira pela matriz em `[AR7+0xE]` e quica no chão (`0x1FB0`, altura 155 ou
  35 no tipo 0x750) com ½ da velocidade; termina com vy > 100 ou vy < 20 no chão (`0x7100` no tipo 0x750).
- Particularidades: o `0x6959` sempre volta com carry = 0, então o arremesso via 0x750 não ocorre; no caminho 0x750 o
  `0x6997` grava −1 em `[AR4+0x21]` com AR4 ainda apontando para a tabela de arestas (0x809D1F); o `0x23D1` sai com
  `RETSEQ` sem desempilhar AR1 se o objeto não estiver na lista.

## Fumaça dos carros (`0x87D7`, `0x88EB`, `0x8928`) — traduzida (`src/core/game/smoke_effects.cpp`)

- Três origens, todas só para o carro focado, fora do atrato (`[C8C5]` & 0xF ≠ 2), com `[D+0x37]` ≠ 0 e com menos
  de 2 efeitos ativos (`0x894D`):
  - `0x87D7` (arrancada, de `0x30CC`): 6 nuvens espalhadas na largura do carro e empurradas para a traseira (`0x8960`);
  - `0x88EB` (raspão, de `0x2FA7`): 2 nuvens no canto do lado do muro (sinal de `[D+0x46]` e do ângulo ao nó);
  - `0x8928` (batida, de `0x2645`): 2 nuvens no canto em contato (AR3), 80 acima.
- Cada efeito é um processo `0x8860` (ID 0x540, criado com AR7 = `[E877]`) com até 6 objetos tipo 0x766 em
  `[AR7+0x13..]`; o quadro da animação avança por `[obj+0x13]` (lista de `[C6C0]`, fim negativo).
- A cada quadro a nuvem anda (1,5 · v · `[C92F]` − 100) no rumo do carro e é colada no chão (`0x1F8B`) a `[obj+0x12]`
  de altura; é descartada ao fim da animação, a ≤ 1000 da câmera ou fora do chão (`0x7100`). A cada 2 quadros entra
  mais uma (`0x8820`, até 4). Sem nuvens, o processo morre.
- `0x9042`/`0x905C` (salvar/restaurar R0–R7 e AR0–AR7) viraram uma cópia local dos registradores.
- Particularidade: se a `0x87D7` não conseguir criar nenhuma nuvem, sai pelo `0x88E7` sem desempilhar o `0x9042`.

## Objetos animados do cenário (tabela `[4500]`) — traduzidos (`src/core/game/track_props.cpp`, `roaming_objects.cpp`)

- `[4500]` = 0x4501: pares (tipo, tratador) terminados em 0; o fluxo de trechos chama o tratador com AR4 = objeto.
- Traduzidos:
  - `0x4292` (0x460): só `0xAF58`; `0x428C` (0x461–0x463): `0xAF58` e tipo 0x731;
  - animação por lista de modelos AR6 (negativo recomeça), processo ID 0x850, PCB em `[AR4+0x17]`, bit 23:
    `0x43DE` (0x40A, 0x465) e `0x43CB` (0x466) com fase aleatória → `0x4402`; `0x4293`/`0x429C` (0x481/0x482) →
    `0x4401`; `0x42D6`/`0x42C3` (0x4A1/0x4A2) → `0x4308`; `0x42EE` (0x467) → `0x4308` + som contínuo `0x4313`;
  - `0x4225` (0x469): modelo sorteado de `[423D]` e andarilho `0x4242` (ID 0x16 trocado por 0x800).
- Processos: `0x4308`/`0x4401`/`0x4402` esperam 1–2 quadros por troca; `0x43C0` espera 2–3; `0x4313` toca o som 0xB7
  com volume 7·(75 − |dist|/1000) (×4 atrás), para e morre a mais de 20000 atrás; `0x4242` anda em passadas
  aleatórias e às vezes dá meia-volta.
- `0x4330`: som com volume; se já for o som contínuo do canal `[E8FF]`/`[E908]`, só ajusta o volume (`0x9114`).
- Particularidade: o andarilho multiplica a passada por `[C92F]` lido como float (inteiro 1 ≈ 1,0000001).
- Também traduzidos: `0x4366`, `0x438E`, `0x43AB` (0x741/0x742) em `track_props.cpp`; `0x4440` (0x498) e `0x9B43`/`0x9B46`
  (0x470–0x476) em `roaming_objects.cpp` (detalhes em "Traduções finais", mais abaixo).

## Carros de tráfego (`0x9765`, `0xA3FF`) — traduzidos (`src/core/game/traffic_cars.cpp`)

- Criados por `0x6639` (`[B12C]` = `0x9765`, ID 0x213, mesmo sentido) e `0x65ED` (`[B12B]` = `0xA3FF`, ID 0x214, contramão).
- Montagem como os carros da CPU (`0x700A`, `0x8F3E`, `0xAE3A`), textura aleatória (`0x668C`), faixa `[AR7+0xC]` = 2..3, nascimento
  no penúltimo nó carregado deslocado pela faixa: `[0x5D]` (= 0x809855, lido também por DP) = rotY(rumo)·(faixa, 0, 0),
  faixas da tabela `[6594]` (lista normal ou de trecho bloqueado, `0x66A7`).
- `0x9765`: segue `+0x17`; modelos com flag 1 ziguezagueiam após 1,1 min (seno de `[AR7+0x23]`, amplitude 2600..3120,
  centro ±576) enquanto o jogador está entre os 5 primeiros; buzina a ≤ 7500 do focado; na trajetória do focado
  (`0x67B0`, ≤ 400 da reta) e à frente: buzina (70%) ou som e freio; batido → destroço (`0x98B9`, fumaça `[C625..C627]`).
- `0xA3FF`: segue `+0x21`; 10% trocam de faixa a cada 30..80 quadros; buzina quando o focado vem a ≥ 127 e perto;
  freia na trajetória do focado; acelerador 0,35..0,5; ao sair da estrada vira destroço (`0xA4EE`).
- Esterço = diferença de rumo até o alvo ÷ quadros até lá (`0x94A7`). Fim: `0x98A1` (lista `[0x57]`, `0x9032`,
  processo das peças `0xA8AC`, `0x7100`). Com link: avisos `0x7B25`/`0x9924` e carro parado `0xA718`.
- Particularidades: `0x67B0` multiplica pelo contador `[C92F]` lido como float (≈ 1); `0x981D` trava (`BU $`) se o
  índice-alvo sai da janela; `0xA726` grava em `[D+0x31]` um `LDI 0` sobre o float do rumo.

## UART do controlador de movimento (`0x4834`) — traduzida (`src/core/io/serial_printer.cpp`)

- O dispositivo é o controlador da plataforma de movimento de 3 eixos (Galil), não uma impressora nem um modem:
  os textos enviados ("XA=", "YA=", "ZA=", "SH;J G 10,…") e as mensagens ("MOTION SETUP TESTING...",
  "X ENCODER FAILURE") mostram isso. O nome do arquivo e os nomes `modem*` no código (e "DIP do modem" aqui)
  vêm de uma leitura antiga.

- Só com o ajuste 0x34 da CMOS = 1 e o bit 0x40 de `[0x992000]` desligado. UART no latch `0x995000`: endereço 9
  envia um caractere, 8 recebe (bits 8–15), 10 dá o estado (bit 15 = carry do `LSH 17`). `0x4689` espera e, no
  modo 1, alimenta o watchdog.
- `0x4834` imprime 3 linhas (`[C7B7..C7CB]`: número, prefixo `0x77D1`, `0x77E7`×2) e trata a resposta (`0x4A4E`):
  `G` (ajuste 0x80 = 1), `V $hh` (aviso `[EB52]`), `W`/`S`/`Q` (mensagem na tela) e `X`/`Y`/`Z $hh` → `[EBB8..EBBA]`.
  Sem resposta em 1000 tentativas: ajuste 0x34 = 0.
- `0x4954`/`0x48C4` (esperam a INT0 em laço ativo, `0x4EC7`) estão traduzidas em `service/service_motion.cpp`.
- Instalação da plataforma (`0x4530`, chamada no RESET): `0x49A7` (`[EBB7]` = 0), ajuste 0x34 = 1 e 0x80 = 0; sem o DIP do modem
  (`0x4693`: bit 0x40 de `[0x992000]` >> 16 ligado, como no padrão 0xF9FE) volta. Com o DIP, o diálogo com o
  controlador continua em `0x4539` (traduzida em `service/service_motion.cpp`). `0x469D`: Z = ajuste 0x34 = 1. O port ganhou o flag Z (`kStZero`) no ST.

## Reagrupamentos da lista visível (`0xAF75`, `0x87B8`, `0xAE05`) — traduzidos (`src/core/scene/object_list.cpp`)

- `0xAF75`: objetos 0x7xx saem da lista e entram depois do nó de pista mais próximo (`[E46F]` por `+0x21`, ou o nó já
  guardado em `+0x1E` se tiver bits de lista), herdando `+0xE & 0x3000`; no modo 5, depois dos 0x310/0x400/0x40A do nó.
- `0x87B8`: objetos 0x102 vão para logo depois do 0x100. `0xAE05`: o par de 0x101 sai das listas (`0x715F`) e entra
  depois do 0x100. `0xAE1F`: matriz da carroceria (peça tipo 0) de um carro.

## Carros da CPU (`0x50CB`) — traduzidos (criação em `src/core/game/drone_car.cpp`, laço de IA em `drone_ai.cpp`)

- Tabela `[5051]` = 0x5052, com 6 palavras por carro: modelo, nós à frente na largada, lateral, fator de velocidade, `+4` e textura. O objeto do carro fica em `[E8A3 + n]`.
- Etapas da criação:
  - `0x700A` cria o objeto;
  - `0x8F3E`/`0xAE6A` montam as peças e o processo `0x8F68` (ID 0x230);
  - `0xAE3A` → `0x2945` monta os dados do carro (0x5B palavras em `[obj+0x1B]`);
  - `0x509A` calcula o fator de velocidade;
  - `0x54FD` põe o carro na pista (passos de 5500 a partir do 2º trecho da lista, mais o deslocamento lateral);
  - o tipo passa a 0x216.
- Laço `0x513A`:
  - com `[E5EC]` = 0x33BB (grid da demo), só chama a física `0x2EFD` + `0x2C39`;
  - senão, segue os nós da pista (`[AR7+0x10]`/`[AR7+0x13]`), regula a velocidade (`0x52E4`, `0x532B`, tabelas `[2D62]`/`[2D68]`/`[2D7D]`), desvia dos outros carros (`0x53A2`, `0x538D`, `0x539A`, `0x5441`, `0x53DA`: ocupação de faixas em `[538C]` = 0xE8AF) e toca a buzina (`0x54CB`).
- A física (`0x2EFD`, `0x2C39`, `0x2C18`, `0x3037`, `0x2185`, `0x2C92`, `0x2E73`, `0x67E3`) grava o nó da pista sob o carro (`[dados+0x37]`). A câmera da demo depende desse nó (`0x584F`).

## Textos de créditos (`0x745E`) — traduzidos (`src/core/game/credits_display.cpp`)

- Chamada a cada iteração do loop principal. Primeiro chama `0x7594` (indicação de volume enquanto `[E56D]` > 0) e `0x762E` (aviso "MOTION OFF" quando o bit 0x40 de `[0x992000] >> 16` está desligado e o item 0x34 da CMOS ≠ 1).
- Com `[E541]` = 0:
  - jogo grátis (item 0x19 = 1): "PRESS START" piscando (±20 quadros em `[E56C]`), y = 360;
  - atrato com `[C8E8]` ímpar e sem créditos nem moedas (itens 0x71 e 0x70), ou modo 7: só "INSERT COINS" piscando (±30), y = 360. Confirmado na captura: aparece sobre a tabela de recordes e na tela de título;
  - senão, um bloco centrado: "PRESS START" ou "n CREDITS TO START / TO CONTINUE" (±45), o saldo "c  m@n CREDITS" (`@` é a barra da fonte) ou "INSERT COINS", e até 3 linhas da tabela de preços (estilo `0x76E2`, paleta `[B121]`).
- `0x73B7`: registro de preço pelos DIPs de moeda/país (`~[0x992000] >> 24` com os 8 bits invertidos): `[[B083] + (d & 0x1F)·4 + (d & 0x60) >> 5]`, limitado a `[B085]`; R0 = índice em `[B080]`.
- Tabela de preços (`0x73A0`): registro de 11 palavras. Com o item 0x17 = 1 vem de `[B080]` + (item 0 & 0x7F) · 11; senão é o personalizado `0xE542`. Os campos são lidos por pequenas rotinas em `0xC10042`–`0xC1009E`, código na ROM de dados.
- Criação de textos (`src/core/video/text_overlay.cpp`):
  - `0x7660` cria (sem testar o pool vazio, como no original); `0x7651` cria com sombra (AR1 = frente, AR0 = sombra +2/+2, modo 0x800);
  - 14 rotinas de estilo `0x7676`–`0x771F` (altura, página, paleta, fonte);
  - `0x77E7` concatena textos.
- Pools (`src/core/scene/node_pools.cpp`): `0x8EF1` inicia uma lista livre; `0x8F01`/`0x8F13`/`0x8F1F`/`0x8F29` alocam, inserem e movem nós; `0x7648`, `0x8FF0` e `0x9019`/`0x9024`/`0x9032` são os pools de textos, de matrizes e de 0x19 blocos de 0x5B palavras.
- `0x75F1` (contadores mecânicos de moedas, a cada 8 quadros, porta `0x995020`) e `0x7F25` (pulso da placa de link no Master; sai com o GIE desligado, como no original).

## Fichas e START (`0x7314`–`0x739F`, `0x1D7F`–`0x1DC2`) — traduzidos (`src/core/game/coin_start.cpp`)

- A tabela de portas `[4DFA]` cria um processo por borda de subida: moeda 1 → `0x7314`, moeda 2 → `0x7321`,
  START → `0x1D92` (moedas 3/4 e crédito de serviço: `0x7338`, `0x7344`, `0x7350`).
- Moeda: som 0x102, auditoria (0x64/0x65/0x67), `0x7C12` (link), pulsos do contador mecânico
  (`0xC1008E`…`0xC100A8`, campo +0xA do registro de preços) em `[E573]`/`[E574]` e R2 = unidades
  (`0xC1000F`…`0xC10035`, campo +0) → `0x7360`.
- `0x7360`: unidades → item 0x70 e bônus 0xF7 (`0x99E2`, soma ao item); cada `0xC10042` unidades vira um
  crédito (item 0x71, limitado ao item 0x2D, auditoria 0x69); o bônus `0xC1004E` dá um crédito extra.
- `0x1D92`: nos modos 3/5/6/7/8 só marca `[E631]` = 1; no 4 nada; senão `0x1D7F` (carry = créditos ≥
  `0xC10068` ou jogo grátis). Sem créditos: som 0x105 uma vez (`[C8E6]`). Com créditos: desconta, modo 3,
  `SP = [0x63]`, `0x9503` com AR2 = 1 e `BR $4C1B`.
- Com os DIPs padrão (registro 7), começar custa **3 créditos** e continuar, 2.

## Fluxo da partida (`0x1775`) — traduzido (`src/core/game/game_flow.cpp`)

- **Chamada local:** `[PCB+1]` é a pilha do processo (começa em PCB + 0x2F). A chamada empilha `[ret]` e
  salta para `[alvo]` (tabelas `[B1E7…B1F0]`, `[C6E7…C6F5]`); o retorno desempilha e faz `BU R0`. No port:
  `localCall`/`localReturn` (despacho de cauda) e `processJump`, que grava o PC do PCB antes de saltar
  (alvo pendente → o escalonador reexecuta o alvo, não o chamador).
- `0x1775` → `0x1722` (sincronia com o link; sem link: `0x7A5F`, 1 quadro, `0x793A`, `[E63D]` = 1,
  `0x7F42`) → `0x1783` (auditorias, item 0x21 − 1, confere itens 0x8F/0x90, espera `[0x809A79]`, som 0xF3,
  `0x317C`) → `0x6C8F` (seleção de percurso, traduzida) → `0x17C5` → `0x59EC` (carro) → `0x17D2` →
  `0x18A6` → `0x17DA` (espera `[E63F]`) → `0x17E0` (tempo = item 0x1C · 5 + 60, cria `[B1F1]`, morre).
- Tela de espera do link `0x169C`…`0x171F` (3 painéis ou o fundo `0x6FD4`), `0x168C` (fim da espera).
- Atrato: `0x181F` (tabelas `[8C12]`, `[18A5]`, `[3965]` por percurso) e `0x180C` (encerra e volta a `0x17A7`).
- Auxiliares: `0x1C5B` (R5 = acelerador > repouso + 20), `0x317C` (itens 1..7 → floats `[E89A]`),
  `0x7F42`/`0x7F4D` (papel no link, `[0x997000]`), `0x9937`, `0x9EEC`, `0x6DFB`, `0xB06F`.

## Seleção de percurso (`0x6C8F`–`0x6FE8`) — traduzida (`src/core/game/course_select.cpp`)

- Chamada local `[B1EB]` do fluxo. Contexto comum de processos em `src/core/game/process_ctx.hpp`
  (`ProcCtx`: `var(n)` = `*+AR7(n)`, `sleep`, `lcall`/`lret`, `k(pc)` = float curto da instrução,
  `text`, `waitDma`).
- Espera ativa por `[C93F]` (troca de página feita pela INT0): no port, dorme 1 quadro em `0x6C9E`
  até a INT0 zerar a flag (o original prende a CPU no laço).
- Mapa: `0x440C` com `[C6E6]`; `0x6ECD` guarda as posições finais (+0x11..0x13), sobe os objetos e
  monta a cadeia `+0x1F` (`[EA31]`); tipo 0x40C = carrinho, 0x410 = modelo alternativo, 0x8A1/0x8A2/0x405
  ganham processos de animação (`0x5CD0`, `0x5CC0`, `0x6F8C`); `0x6EA2`/`0x6EB0` descem os objetos.
- Escolha (12 s): `0x1C62` (volante → faixa `[EA47]`, 12 faixas), `0x6E4F` (percurso = faixa, −1
  acima de 5; nome sobre o marcador `0x400 + percurso`), `0x6E0D` (botões de vista → especiais
  0x401/0x406/0x409), `0x6F93` (carrinho), `0xAF13` (imagem), `0x1D38` (tempo), `0x1C85` (borda do
  acelerador confirma).
- Final (`0x6DB4`): percurso 5 → `[E636]` = 1 e pista 0; senão `[E636]` = 2 e pista `[[6FC5] + n]`
  (especiais: 0xB/1/0); `0x6F2A` destaca o marcador; `0x6DEB` libera as paletas (`0x8BE4`) e
  prepara a pista (`0x3F6F`).

## Seleção de câmbio e de carro (`0x59EC`–`0x5DDD`, `0x1942`–`0x1C5A`) — traduzida (`src/core/game/car_select.cpp`)

- Chamada local `[B1ED]` do fluxo. Câmbio: cena `[C6D0]` (`0x440C`), peças organizadas por `0x5BEB`
  (`[EA23]` alavanca, `[EA24]` volante, `[EA28]`/`[EA29]` placas), deslizam (`0x5B0F`) e descem (`0x5AC1`).
  O volante move a alavanca (`0x5B66`, histerese 0,5) e escolhe `[E633]`; a paleta mistura-se (`0x5D00`).
- Garagem: `0x44BB` (lista girada por `[E402]`) com `[C6D5]`, `0x1A27` cria os carros 0x481..0x484 sobre
  as bases 0x401..0x404 (placas 0x601..0x604). `0x1AB0`/`0x1D52` escolhem `[E632]`; o carro escolhido sobe,
  acende (`0x9FB6`) e gira (`0x1B59`); `0x1A5C` mostra as fichas técnicas. Acelerar ou 12 s → `[E641]` = 1.

## Entrada na corrida (`0x18A6`–`0x1A26`, `0x1859`, `0x2939`, `0x2987`, `0x9CE3`) — traduzida (`src/core/game/race_entry.cpp`)

- Chamada local `[B1EF]` = `0x18A6`: inicializa o percurso (`[[8C12] + [E634]]`, `0x3F8D`), modo 0x2C3,
  título `[18A0]`; espera `[E641]`; desloca a garagem (cadeia `[E47D]`, câmera `[0x45]`, objetos `[B207]`) por
  576 − `[[B202] + carro]` girado pelo rumo; auditoria 0xE1 + carro (ou 0xE5 + variante), `[E640]`.
- Sem atrato: chamada local `0x1617` (só o caminho sem link, `0x1679`/`0x167F`); com link → `0x161F` (espera na entrada da corrida, traduzida).
- `0x1926`: `0x1859` (zera o estado da corrida; processos `0x6639`, `0x65ED`, `0x1F2A`, `0x659F`, `0x9CE3` e
  os 9 carros da CPU `0x50CB`), processos `[B20A]`..`[B20E]` (peças, porta `0x195D`, carro `0x196B`), 71 quadros.
- `0x196B`: câmera atrás do carro (`0x1841`: (0, −400, −2200) girado) com suavização `[198B..]`, o carro vira
  para o rumo (`0x9308`/`0x94A7`, 26 quadros), vira processo 0x110, `0x2987` prepara o jogador (`[E876]`,
  modelo `[961B]` + 11 · modelo, `0x8F3E`, `0x2945`, `0x2939`), anda até 700 de `[[E46E] + 0x21]` e salta
  para `0x29B8` (processo do carro do jogador, traduzido — seção abaixo) com `[E63F]` = 1 e modo 0xC4.

## Carro do jogador (`0x29B8`, laço `0x2A5C`) — traduzido (`src/core/game/player_car.cpp`)

- Entradas: `0x29B8` (saída da garagem: parado no rumo `[E402]`, paleta `0x9ECD` do modelo, câmera
  (0, −960, −960) girada, `[E871]` = 1) e `0x29EE` (largada do atrato: objeto novo, nó `[[E46E]+0x21]`, câmera
  480 (+1152 no gabinete da direita) adiante). Ambas caem em `0x2A3F`: câmera 2200/−400, processo da vista
  `[C61B..C61D]`, luz de freio apagada, `[E87D]`/`[E87E]` = 1, modo |= 0x100.
- Laço (1 quadro, dorme em `0x2B60`): abandono `[E635]` → `0x1D59` (itens 0xF7/0x75, mata 0x2xx/0x8xx, espera
  DMA e troca de página e reinicia em `0x1E17`); `[E87B]` conta quadros em 1º; tempo `[E604]` ≤ 0 → `0x3C71`
  (`[E53F]` = 1, auditoria 0x97 e rotina da pista `[[396F]+[E481]]` = `0x3970`–`0x3996`, linhas de chegada em `race_events.cpp`); modo 5 → `0x2976` (só a câmera
  até o modo voltar a 4, então `0x29EE`).
- Dirigindo: `0x2CD3` (tolerância fora da estrada `[E86A]`: 20 no asfalto 0x30x; 0 → reposição `0x2BF5` no nó
  mais próximo, 722 para o lado), `[obj+8]` < 0,01 também repõe; marcha `0x2E61` (manual pelos bits 0x400/0x800/
  0x1000/0x2000 de `[C937]`, automática `0x2E73`); acelerador `0x2E57` (subindo de marcha: ×2 e, acima de 0,7, som
  0x1C8 e chamas `[C61E]` + fumaça `[C61F]`); freio `0x2E0C` (quadrado; luzes `0x2E3B`/`0x2E31` copiam 4 cores
  de `[2E27]`/`[2E2C]` para a paleta do carro); `0x2D83`, velocímetro `[E605]` = velocidade·0,49, `0x2C9C`,
  fator `[C8EB]`; volante `0x2E88` (curva do ajuste 0x26, ganho por velocidade, `[2E87]`) → `0x2CEB`; bordas
  `0x2F63` (alinhamento `0x2F43`, impacto `0x2F75`, fora da estrada `0x2FE8`, que pode cair na cauda `0x2FB0`);
  rodopio `0x2F17`; matrizes, velocidade (`0x9099`), posição `0x9081` e chão `0x1FF6`.
- Rodopio (`[E873]` ≠ 0): `0x5567` integra os ângulos `*+AR7(0xF..0x11)` (`0x25D6`), salto `[D+3]`, e termina
  com `[obj+0x12]` ≥ 60 (som 0x1EF, nova vista `[C61B]` se `*+AR7(0x21)` = 0).
- Câmera: `0x2EE6` faz a transição de distância/altura; vista 0 = cabine (matriz transposta do carro, balanço
  `[D+0x42]`/`[D+0x43]`, suavização 80/20 de `0x318C`/`0x3195`, que na ROM guarda a matriz na pilha — no port é
  uma cópia local); vistas 1/2 = perseguição (rumo da velocidade a 20% por quadro, `0x2BB7` testa o chão a ±170
  nas laterais e `0x2BBD` puxa o rumo 0,1 rad para a pista; `0x2BE5` ergue a câmera que entrou no chão).
  Botões de vista: eventos 20/21/22 → processos `0x2EB4`/`0x2EB9`/`0x2EBE` (0/2200/3840 de distância).
- Sons (`0x2C4C` motor por `0x91D0`; `0x3097` derrapagem, freio, ponto morto, vento, cascalho; `0x3143`/`0x3148`),
  faixas sonoras `0x304A` (tranco ±15 e tremor do volante em `[EA48]`), `0x67EC` → `[D+0x46]`; só com link:
  `0x7ADB` (sincronia do carro, `link/link_car_sync.cpp`) e `0x2B61` (vácuo `[E87E]`/potência `[E87D]`).
- **Quirks preservados:** `0x2B19` carrega R6 em vez de DP e lê a palavra 0x9807 da ROM; `0x29C3`..`0x29C6`
  levam todas as variantes ≥ 4 ao índice 4; em `0x2B6E` R3 (0/1) é sobrescrito por dx², e a comparação
  `CMPI 1,R3` usa os bits do float.
- `car_dynamics.cpp` ganhou os pontos de entrada `0x2F75` (impacto), `0x2FB0` (cauda) e `0x2FE8`.

## Efeitos do carro do jogador (`0xAD93`, `0x86FC`) — traduzidos (`src/core/game/player_effects.cpp`)

- `0xAD93`: chamas do escapamento — dois objetos 0x101 nas posições `[B141]` + 3·modelo (o 2º espelhado e girado
  π), 8 quadros da animação `[B140]` presos à carroceria (`0xAE1F`, `0x947F`, `0xADE0`); depois apaga todos.
- `0x86FC`: fumaça dos pneus — um processo por vez (`[E918]`), um par de nuvens 0x102 nas rodas traseiras (peças
  `+0x12` = 1, `0x8780`/`0x877E`) a cada 2 quadros com freio ≥ 0,4 acima de 20 ou derrapagem ≥ 0,25 (até 20), paleta
  `0x9EDB([C6BF])` no asfalto, animação `[C6BD]`/`[C6BE]`; as nuvens recuam no rumo do carro e seguem o chão.
- Sem estes dois, os processos ficavam vivos (PC não traduzido re-executado), o pool de 0x69 PCBs enchia
  (erro 0x210 em `0xAB1A`) e os botões de vista não conseguiam criar o processo.

## HUD da corrida (`0x9CFF`) — traduzida (`src/core/game/race_hud.cpp`)

- `[E60F]` desloca a HUD (150 = fora): `0x9CE3` a traz (−6 × 25) e `0x9CF5` a leva (+6 × 25, depois 150).
- Ordem por quadro: conta-giros `0x9E22` (22 barras de `[B1C4]` → `[B1C5]`, apagadas acima de
  rotação·0,458; marcha 1..4 → luz 4..1; paleta do slot 0x40), radar `0x60DF`, aviso do modem `[B18C]`,
  molduras `[B190..B192]`, marcha `[B193]`, logotipos `[B19E..B1A1]`, tempo `[E604]`, velocímetro `[E605]`
  (×1,6665 com o item 0x22), retângulo `0x9E59`, tempo decorrido `[E85F]` (`0x6099`: M + `[6095]` + SS +
  `[6095]` + CC, com `0x60BD` dividindo por 3300/55), posição `[E607]` (`[B1A5]`, 3 palavras por lugar).
  Acumula `[E47E]` += velocidade·`[E87E]`·`[C92F]` e `[E608]` += `[C92F]`.
- Contagem regressiva (modo 4): em 10 toca 0x13E; abaixo de 10, 0x240 quando ≤ 3 (uma vez por valor, `[E610]`).
- Radar: caixa 0x1CC..0x1F4 × 0x64..0xC8 (modo 0x2008); posições relativas ao carro escaladas por
  `[C614]`/`[C615]`, giradas pela matriz de rumo `[5A]`; x = fix + 0x1DE, y = −fix(z·0,3) + 0x94; cor 3 para
  carros da CPU (0x216), 0xC para os demais; o adversário do link fica preso à borda com seta acima/abaixo
  conforme o nó `+57`; `[E867]` = jogador à frente.
- **Quirk:** o aviso de fora da estrada (`[E86A]` < 10) usa `ANDN $F` em vez de `AND $F`; nunca aparece.
- `0xAF48`: imagem com sombra (deslocada 2,2 com paleta 0xD00).
- Letreiro `0x1F56`: nome `[[3953]+[E481]]` em y = 360, x desliza de −100 até 256 (20 % por quadro, 21
  quadros), fica 50 quadros e sai para −100. Logotipo `0x86C5` (item 0x25): objeto `[B166]` entra até
  x = −200, anima `[B167]` 18 vezes (2 quadros) quando `[E84C]` ≠ 0 e sai 34 × 6 para a esquerda.

## Eventos de pista (`0xACCA`), chegada e objetos empurrados — traduzidos (`src/core/game/race_events.cpp`)

- `0xACCA`: AR0 = índice → `CALLU [[ACD0]+AR0]` (tabela 0xACD1, 46 entradas). 1 = checkpoint `0xAD03`
  (`[E869]`++; ao passar `[E63A]`: `0x7A36`, `[E63A]`++, `[E604]` += `[E868]`, som 0xFF no modo 4); bits de
  `[C8C5]` 0x40/0x80/0x400/0x800; `0xAD45`/`0xAD4F` ajustam névoa/céu (`[E3FB]`, `[E3FD]`, `[E5F2..E5F4]`);
  sons 0x15C..0x171; `0x90DD` gira a paleta do céu `[E916]` (mod 8); processos `[C619]` (0x214) e `[C62A]`
  (0x500); 32..46 = linhas de chegada.
- Chegada `0x3970`–`0x3996` → `0x3998`: `[E480]` = limite, `[E481]` = etapa (1..0xE). Só no modo 4: modo 5
  (limpa 0x400), `[C8DA]` |= 0x800, dificuldade `0x5090` (`[E639]` +0,07 se 1º, senão −0,035, em
  0,8..1,25), item 0xDF = etapa, auditoria 0x9A + 2·(etapa−1), `0x3C7A` para os sons do motor, `[E5F4]` =
  0xB, `[E868]` = item 0x1D, posição `0x65A5` e o processo `[B0D7]` (ID 0x16, via `0xA868`).
- `0x691B`: objetos `(+F & 0xFF0) == 0x750` com subtipo n ∉ {0, 3} andam `[C62B + 5(n−1) + 3]` e `+4` no
  rumo −`[809807]` e testam o carro (`0x221F`); se continuam ativos, voltam à posição. **Quirk:** se o
  subtipo zera no primeiro passo, `POP AR3 / RETSU` desempilha o retorno e sai direto da `0x691B`.
- Horizonte `0x81D9` (em `src/core/video/sky_renderer.cpp`): menor y projetado dos nós `[E46F]` com
  `(+E & 0x3000) == 0x1000` e x em 0..512 → `[E3F9]` (+`[E3FB]`, mínimo 0) e `[E3FA]`.

## Batida forte, carro arremessado e efeitos de objetos — traduzidos (`car_crash.cpp`, `object_effects.cpp`)

- `0x23EF` (da resposta de colisão `0x2645`, AR0 = jogador, AR1 = outro carro): mata as animações do outro,
  dá a ele rumo ± 0,1 e velocidade 0,75·(0,8..1,25)·v/massa, salto até −65, e cria `[2477]` = `0x2478` (0x290).
  O jogador fica com v/2·`[E87E]`², e o outro com pelo menos 1,25× isso. Sem capotagem em curso, 40 % de chance
  de capotar (`0x5543`: `[E873]` = 1, vertical −60, vista salva em `*+AR7(21)` do PCB `[E877]`); senão meia-volta
  (π em `+2D`, modo 0xF) ou, fora da cabine, giro em `+32`. Som 0x48/0x63 pelos modelos `[23ED]`/`[23EE]`.
- `0x2478`: fase no ar (gravidade 2·dt, quica com −0,5 e som `[3085]` até vy < 35) e fase rolando (dt passos
  por quadro, atrito 0,98, giro 0,96); termina de pé (vira tipo 0x2x parado, `0x2C18`) ou de rodas para cima
  (espera ser empurrado). Some com `+1C` < −6000 ou vy > 300 (`0x676B`, `0x9032`, `0x7100`).
  **Quirk:** a troca de sinal do giro é testada com `XOR3` dos bits do float (bit 31 = expoente).
- `0x1FB6`: menor altura do chão sob os 8 cantos da caixa ×0,7 (`0x28AE`, base y ×1) nas listas `[E403]`/`[E406]`.
- `0x677E` (nuvem: deriva ±6, sobe 10/quadro, animação `[67AF]`), `0x6822` (partículas 0x520, até 5 em
  `[E5F6]`, trios `[685C]`), `0x685D` (detritos dos 0x615, até 5 em `[E5F7]`: caem de 1500 acima, quicam até
  4 vezes, som 0x12F/0x132). **Quirk:** saídas antecipadas não devolvem o contador.
- `0x43AB`/`0x4366`/`0x438E` (em `track_props.cpp`): objetos 0x767/0x768 assentados no chão (`0x439E`),
  paleta sorteada da tabela `[4365]`, animação pelos processos `[B0CB..B0CD]`.

## Pós-corrida, resultados, recorde e continue — traduzidos (`post_race.cpp`, `results_boards.cpp`, `screen_doors.cpp`, `hiscore_entry.cpp`, `continue_screen.cpp`)

- `[B0D7]` = `0x3A46` (chegada `0x3998` ou fim de tempo, ID 0x16): para sons, mata os processos da corrida,
  zera as listas, cria `0x3A64` (resultados), `0x2976` e `0x903B`, e reentra no laço principal
  (`SP = [0x63]`, `BR 4C1B` → `cpu.jump = 0x4C1B`). Detalhes da decisão final em `post_race.hpp`.
- Placares (`results_boards.cpp`): velocidade média `0x3DCF`, recorde `0x3E4F`, tempo `0x3ED8`, posição
  `0x3F01`, aviso de recorde `0x3E82`/`0x3E87`, letreiro do link `0x39CF`; o nome da etapa no
  cross-country (`0x3CDF`, `0x3D31`), a subida da câmera sobre o mapa (`0x3C4A`, atan2 `0x931A`), os textos
  piscando (`0x3CB3`) e, em 1º lugar, a sequência local do pódio `0x864D` → queda `0x869E` (passo de quadros
  `0x86B5`, que desvia para o código na janela da ROM em `0xC1019E`).
- Portas de transição (`screen_doors.cpp`): fechar `0x5DE0`, abrir `0x5EC0`, luz das paletas `0x5FD2`.
- Iniciais do recorde (`hiscore_entry.cpp`): `0x31A6` (só com o item 0x1F e tempo que entra na tabela,
  `0x32E7`); letra pelo volante, confirmação pelo pedal, relógio de 20; cena 3D do pódio que grava
  (`0x9B28` empurra as linhas, `0x9A7F` marca a nova; `0x9B18` acha a linha, comparação estrita `<`).
  A subida da placa (`0x34BE`, `DBU AR5` de 39 a 0) faz o último passo com AR5 = 0, isto é, divide a sobra da
  posição por 0 (`0xA509`): no hardware a sobra é exatamente 0 (o passo anterior divide por 1, e o `RND` da
  `0xA509` arredonda o recíproco para 1,0) e o resultado é 0. Com `RND` truncado o recíproco saía 0,99999994, a
  sobra ficava ≠ 0 e o passo final jogava a placa para ~10³⁸: ela sumia ao chegar ao lugar, deixando só a moldura
  com os parafusos. Corrigido em 2026-11-14 com `tmsRound()` (`tms_float.hpp`).
- Continue (`continue_screen.cpp`): `0x1E2C` com relógio de 20 (`0x1EE0`), linhas de preço e créditos
  (`0x75E4`, `0x75AF` em `credits_display.cpp`), reinício do modem `0x4936` (`serial_printer.cpp`);
  devolve AR6 = 1 quando o jogador continua.
- **Lição (`0x2C18`):** a dinâmica de direção não tem `RETS` — cai direto em `0x2C39`. Sem essa chamada no
  port, o rumo dos carros da CPU não era atualizado, eles saíam da pista e a demo de atração estourava a
  pilha por volta do quadro 4800.

## Driver de comandos de som (`0x90E6`–`0x92C7`) — traduzido (`src/core/audio/sound_commands.cpp`)

- A placa DCS recebe palavras de 16 bits (`0x9211`): byte alto e depois o baixo. Cada byte é escrito como `0xFF00|b`, `0xFD00|b` (estrobo) e `0xFF00|b` em `0x995000`. O `0xFF00|b` também vai para `0x9A0000`. Com `[809A79]` ≠ 0 (placa reiniciando), nada é enviado.
- 4 canais de 9 palavras em `0xE8F0`. Cada som (`[90A6]` = 0x8974 + número) é uma sequência de pares (prioridade, `comando | quadros << 16`). A `0x92B3` (INT0) avança os passos a cada quadro.
- Prioridade: a `0x91E0` toca no canal dos bits 8–11 se a prioridade nova for maior, ou igual sem a flag 0x2000. A flag 0x1000 trava o canal. A `0x9186`/`0x9188` escolhe entre os canais 1 e 2 e não toca no modo 2 (atrato).
- Volume: geral `0x55AA` (`0x910F`) e por canal `0x55AB + canal` (`0x9114`), seguidos de `(v << 8) | (~v & 0xFF)`.
- Reinício (`0x92AD`): nas etapas da `0x92B3`, o quadro 2 pulsa o bit 1 de `0x994000` e programa o latch 0 (0x3C, 0x1C); o quadro 4 envia a palavra 0; o quadro 6 termina e envia o volume do item 8 da CMOS.
- Os comandos chegam à placa DCS (`src/core/audio/dcs/`: programa do ADSP-2105 traduzido, memória e periféricos simulados), e o `AudioManager` (`src/core/audio/audio_manager.*`)
  roda a placa a cada quadro (31 250 / ~57,9 amostras, resto acumulado) e entrega o PCM à saída da plataforma
  (`IAudio::queue`; no SDL, `SdlAudio`), com gravação WAV opcional (`--wav`, little-endian explícito).

## Slots de paleta (`0x9DF6`–`0x9FB5`) — traduzido (`src/core/video/palette_slots.cpp`)

A paleta (`0x9E0000`, 32768 cores) é dividida em 128 slots de 256 cores, e o campo `+0x10` dos objetos de cena guarda `slot << 8`. As tabelas e o formato estão no cabeçalho do arquivo.

- `0x9EFA` carrega por id, com contagem de referências; a origem vem de `[[0x9E7B] + id]`.
- `0x9F2F` carrega por ponteiro; `0x9EDB`/`0x9ECD` procuram o slot.
- `0x9F9F` libera e zera a contagem, sem decrementá-la (como no original).
- A cópia é adiada: `0x9F5C` põe o pedido numa fila LIFO, e a INT0 (`0x9E97`) copia até 13 por quadro. O 13º fica na fila e é copiado de novo no quadro seguinte.
- Cabeçalho < 0: palavras empacotadas, 2 cores cada. A 1ª recebe a palavra inteira e a 2ª os 16 bits altos.
- **Antes**, o que parecia ser a "textura" (`+0x10`) é a base de paleta. Sem esses carregadores, a `0x700A` travaria em `0x701F`.
- `0x95F5` (chamada pela `0x9503`) reinicia a fila (`0x9E80`) e carrega as paletas básicas (`[C710]`–`[C730]` e as listas de ids `[C716]`/`[C71C]`). A `0x9503` zera a BSS (`0xC8EE`–`0x1EFFF`), que contém a fila em `CC0F`/`CC10`. Sem a `0x95F5`, a `0x9F84` pegaria nós no endereço 0 e corromperia o vetor de RESET.

## Link entre gabinetes (`0x7F5D`–`0x81D8`) — traduzido (`src/core/link/link_routines.cpp`)

**Condições para agir.** Chamada no início do corpo do loop (`0x4BD6`).

- **`[0x990000] & 0xF`:** precisa valer 4. É o caso sempre: `intcs_r` do MAME devolve 4.
- **DIP "Linking" (SW2:6, bit 2 de `[C8CC]`):** precisa estar ligada. De fábrica vem **desligada**, e então a rotina retorna na hora.
- **Com o link ligado:** espera `[C93D] == 3` (fim da cadeia de INT3), faz `[E591] = 1` e executa o lado indicado pela SW2:8.

**Placa (`0x997000`, MAME `comcs`):**

| Endereço | Uso |
|---|---|
| `+0` | Dados nos bits 16–31 |
| `+1` | Flags: `0x20` COMCOE (linhas de controle `(dado >> 4) & 0xF00`), `0x40` COMDOE (linhas de dados 0–7) |

- A leitura devolve o barramento sem as linhas que a própria placa dirige.
- O Master dirige 0x800/0x400 e o strobe dele é 0x400 (`0xC400`/`0xC000`).
- O Slave dirige 0x200/0x100 e o strobe dele é 0x200 (`0x3200`/`0x3000`).
- A `0x7F06` (boot) liga COMCOE.

**Protocolo (Master):**

1. **Handshake** com timeout em quadros (`[C930]`): 120 quadros, ou `[C930] + 1` (máx. 4) se `[C8D2]` indicar que a última tentativa estourou o tempo.
   - O Slave sobe 0x100 ("pronto").
   - O Master sobe 0x400 e espera 0x100 baixar.
   - 0x200 alto logo no início significa que o Slave desistiu.
2. **Desliga o GIE** e espera o Slave subir 0x200, **sem timeout**.
3. **Envia:** `n` (pares de bytes, alto primeiro), `~n`, os dados (`[C8D3]` bytes de `0x1F000`, um por palavra, com padding 0) e o checksum de 16 bits.
   - Cada byte vai num nível do strobe, mantido por `RPTS $12` (~20 instruções), sem confirmação por byte.
4. **Inverte o sentido:** espera 0x200 baixar (≤ 31 leituras) e sobe 0x400.
5. **Recebe:** em `0x1F258`, cada byte vai para os `bits 16–23` de uma palavra.
   - Cada borda tem timeout de 300 leituras (`DBEQ`/`DBNE AR3`).
   - Confere `n`/`~n`, `0 < n < 0x140` e o checksum.
   - `[C8D4]` = pares − 1.
6. **Contadores e fim:** sucesso em `[0x809A17]`; erro em `[0x809A16]`, com `[C8D4] = 0`. Termina com `CALL $4C33` (`RETIU`), que religa o GIE.

O **Slave** (`0x80C1`) é o espelho: recebe primeiro e depois envia. Contadores em `[0x809A19]`/`[0x809A18]`. Diferenças em relação ao Master:

- não valida o tamanho a enviar;
- aceita `n = 0`;
- se o strobe do Master não baixar em 10 000 leituras, retorna sem contar erro.

**No port:**

- **`IBusyWait`** (`src/core/busy_wait.hpp`): as esperas informam as instruções gastas (~431 600 por quadro, a 25 MIPS).
- **`FrameBusyWait`:** roda os quadros do `FrameRunner` dentro da espera, respeitando GIE/IE. É assim que a INT0 avança `[C930]` nos timeouts.
  - Watchdog de hardware como no MAME (`reset_line_w`): cada borda de subida do bit 3 de `0x994000` o alimenta (a `0x5029`
    da INT0 inverte o bit a cada quadro). O MAME não configura o período, então o dispositivo fica desligado e nunca
    reinicia a placa; a ROM concorda (o código de falha no LED fica segundos sem alimentá-lo). O port também o deixa
    desligado por padrão; `--hw-watchdog N` liga um prazo de N quadros sem borda (174 = os 3 s do MAME quando ativo).
  - Os quadros gastos dentro de esperas são descontados do `GameLoop`.
- **`LinkPort`/`LinkCable`** (`src/core/io/link_port.*`): sozinha, a porta reproduz o loopback do MAME. Com o cabo, o barramento é o OR das placas.
- **Dois executáveis** (`--link master` / `--link slave`, `src/platform/link_shm.cpp`): a porta ganha um `ILinkWire`;
  o fio é um arquivo mapeado em memória com, por lado, as linhas dirigidas, o relógio de instruções e um sinal de
  vida. O relógio avança com as esperas ocupadas do link (`spin`) e completa 431 600 instruções por quadro, exceto
  quando o quadro termina dentro de uma espera do link (o gancho do `FrameBusyWait` cede no meio dela). Completar
  nesse ponto fazia o outro lado ler a porta um quadro inteiro sem o byte seguinte e estourar o timeout no meio do
  pacote; era a causa do ~1 % de erros. Como na
  co-simulação, quem fica à frente depois de uma pausa espera o outro, e uma leitura da porta espera o outro alcançar
  este relógio; sem sinal de vida por 1 s, o cabo conta como desligado. Medido: 1500 quadros de atração com 510/509
  trocas certas e 1 erro (no encerramento); numa partida ligada de 6000 quadros, ~3220 trocas por lado e 0 erros
  (antes da correção acima, ~1 %), e os dois gabinetes entram juntos na corrida com o carro do outro em `[E592]`.
- **Validação:**
  - Gabinete sozinho com o link ligado: o Master estoura os 120 quadros e conta erro.
  - Co-simulação Master ↔ Slave (`tests/test_link.cpp`): duas máquinas em threads que se revezam pelo relógio de instruções trocam 37 e 300 bytes sem erro. As duas traduções se validam uma contra a outra.

### Pacote de envio do link (`0x7989`, `0x798F`, `0x7998`, `0x79AF`, `0x7A41`) — traduzido (`src/core/link/link_packet.cpp`)

**Ciclo do pacote no loop principal:**

1. A `0x7989` (início do corpo) faz `[C8D3] = 0` e anexa a mensagem de estado (`0x7A41`).
2. Ao longo da iteração, outras rotinas anexam mensagens com `0x7998` (bloco de RC + 1 palavras) e `0x79AF` (uma palavra, em AR2). São 25 pontos na ROM.
3. A `0x798F` (fim do corpo) anexa a palavra 0, que marca o fim.
4. A `0x7F5D` da iteração seguinte envia o pacote.

**Regras de anexação:**

- Cada palavra vira **um byte** no cabo (só o byte baixo é enviado). Por isso as mensagens quebram valores grandes com `LSH −8`.
- Só há anexação com a DIP Linking ligada e se o total couber em 0x258 palavras. Senão, a mensagem é descartada em silêncio.
- A `0x7998` não restaura RC: depois de anexar, RC = −1.

**Mensagem 1 (estado)**, montada no rascunho `[0x7939]` = 0x809A1B (RAM interna):

| Palavras | Conteúdo |
|---|---|
| 0 | `1` (tipo) |
| 1–4 | `[C8C5]` (modo), LSB primeiro |
| 5–6 | `[C8DA]` |
| 7 | `[C8E4]` |
| 8 | `[E607]` |
| 9 | `[C8E8]` |
| 10 | `FIX([E639] × 100)` |

O produto é exato tanto em double quanto na precisão estendida do TMS (24 + 7 bits de mantissa).

### Mensagens recebidas (`0x794C`), tratadores e emissores — traduzido (`src/core/link/link_messages.cpp`)

A `0x794C` lê o pacote recebido em `[0x7F16]` = 0x1F258. Cada palavra traz um byte, em `bits 16–23`.

- Só age com Linking ligada, `[C8D1] != 0` e `[C8D4] != 0`. Como `[C8D4]` = pares − 1, um pacote de um só par é ignorado.
- Para cada mensagem, lê o tipo e chama `tabela[tipo]` (`[0x7966]` = 0x7967, 33 entradas). O tratador consome o próprio payload avançando AR2.
- Termina com AR2 ≥ início + 2·([C8D4] + 1) ou com tipo ≥ 0x21. No fim, `[C8D4] = 0` (R0 também fica 0).

| Tipo | Tratador | Efeito | Emissor |
|---|---|---|---|
| 0 | `0x79BF` | fim (RETS) | `0x798F` |
| 1 | `0x79E7` | estado do outro em `C8D6/C8D9/C8E5/C8E2/C8DB(+C8DC)/C8D5`; modo 2 encerra a corrida; bit 0x800 zera `[E604]`; modo 4 pode chamar `0xAD00` | `0x7A41` |
| 2 | `0x7ABE` | `[E604]` (2 bytes), `[C93E]` = byte << 16 | `0x7A8D` |
| 3 | `0x7AA3` | `[C8DF]` (com sinal), `[C8E0]`, `[C8D7]` | `0x7A98` |
| 4 | `0x7AB2` | `[C8D8]` | `0x7AB7` |
| 5 | `0x7A6B` | início de corrida (com `[E594]`), senão responde 0xA | `0x7A61` |
| 6, 8 | `0x7A8A` | `[C8E3] = 0` | `0x7A88` |
| 9 / 0xA | `0x79DD` / `0x79E2` | `[C8DE] = 1 / 2` | `0x79D3` / `0x79D8` |
| 0xB | `0x7A5C` | `[C8DD] = 0` | `0x7A5F` |
| 0xE | `0x7BC3` | 1 byte | — |
| 0xF | `0x7BB0` | 3 bytes → processo `0x9761` | — |
| 0x16–0x18 | `0x79C8/CB/CE` | `[E63B] = 1/2/3` | `0x79C0/C2/C4` |
| 0x19 | `0x7BD1` | `[E8AD]`, `[E8AE]` (24 bits) | `0x7BE0` |
| 0x1A–0x1D | `0x7BFA` | `[C8E1] = 3` | `0x7BF6` |
| 0x1E | `0x7BFD` | 1 byte → `0x90E6` se `[C8E3]` | `0x7C07` |
| 0x1F | `0x7C0F` | `[E597] = 1` | `0x7C12` |
| 0x20 | `0x7C14` | `[9A6B]`, `[9A6C]`; responde com 0x20 | `0x7C1C` |

**Comportamentos do original preservados:**

- **Valor 1:** o byte de `FIX([E639] × 100)` é lido sem sinal. Um valor negativo enviado volta positivo.
- **Tipo 5:** o tratador não salva AR2 (`AR2 = 0xF8` para `0x99DA` e depois o rascunho da `0x7A98`). A leitura do pacote continua desse endereço.
- **`0x7C1C`:** faz `OR R1,R0` com R0 = `[C8CC]` (lido para o `TSTB`), não com o contador `[9A6C]`. O byte enviado é `DIPs | 0xA0/0xB0`.

**Desvio do port:** se o tratador do tipo ainda não estiver traduzido, a leitura para ali. Assim o payload não consumido não é lido como tipos.

Os 33 tipos têm tratador. Os que faltavam:

| Tipo | Tratador | Emissor | Efeito | Arquivo |
|---|---|---|---|---|
| 7 / 0xC / 0x12, 0x15 | `0x7B58` / `0x7B49` / `0x7B43` | `0x7ADB` / `0x7AD7` / `0x7ACF` | estado completo de um carro (46 palavras: posição, matriz, ângulos, `+26`, `+1F`, `+20`, `+2B`, `+57`) no carro `[E592]`, no slot `[[508E] + n]` ou no carro com o mesmo `+59` em `[E404]` (`0x7B34`) | `link_car_sync.cpp` |
| 0xD | `0x5278` | `0x5255`/`0x525A` (+ `POP` / `BU $A718`) | carro de tráfego passa de um gabinete ao outro conforme as voltas; quem recebe o retoma em 0x5208/0x5225/0x52A8 | `link_car_sync.cpp`, `drone_ai.cpp` |
| 0x10 | `0x992B` | `0x9924` | o carro retoma em 0x98A9 (remoção sem aviso) | `traffic_cars.cpp` |
| 0x11 | `0x98F2` | `0x98E4` | o carro retoma em 0x9903 com PCB+5 (R4) = flags; 0x9903 o devolve à janela (laço `0x97D3` ou destroço `0x98C6`) ou o remove | `traffic_cars.cpp` |
| 0x13 / 0x14 | `0x258F` / `0x25AD` | `0x2587` / `0x2597` | fim da batida: o processo do carro retoma em 0x2560, ou recebe as 6 palavras `*+AR7(C..11)` e retoma em 0x255E/0x2478/0x24CF/0x257B | `link_car_sync.cpp` |

Como no original, os tratadores trocam AR4/AR5/AR7 (o laço `0x794C` não os salva).

**Carro do outro gabinete** (`src/core/game/link_car.cpp`): o processo de carro com R4 ≥ 8 vai para `0xA66E`, que cria o
carro do modelo `[C8D8]` em `[E592]`, o põe no início da janela e cria o objeto `[C98B]`; o laço `0xA6EA` só faz o
contato com o chão e envia o tipo 0x19 (a posição chega pelo tipo 7). A `0xA6F8` (loop principal) escala `[C98B]`
pela distância (mín. 16), o põe 35·escala + 20 acima do carro e logo depois dele na lista de desenho.

**Espera na entrada da corrida** (`0x161F`, `race_entry.cpp`): com o link, a `0x1617` espera o outro gabinete mandar a
escolha do carro (`[C8D8]` ≥ 0, mensagem 4 enviada a cada quadro). Sem o link (ou se ele cair) conta `[E604]` (20, ou
menos com START/VIEW) e corre sozinha (`0x1679`); depois de `[E63C]` quadros mostra os textos `[B1D8]`/`[B1D9]`
(e `[B1DA]` sem link).

A `0x793A` (chamada no boot pela `0x4E20`) zera esse estado: `[E596] = [E592]`; zera `C8DA`, `C8D9`, `C8D6`, `C8E3`, `E594`, `C8DD`, `E592`, `E404` e 10 palavras de `[0x508E]` = 0xE8A3; `[C8D7] = [C8D8] = −1`.

## Pipeline 3D (`0x0064`–`0x05F4`) — traduzido (`src/core/render/pipeline3d.cpp`)

O loop principal chama a `0x0064` uma vez por quadro (`0x4BE7`). Ela desenha as listas `[[0x43]]`, `[[0x40]]`, `[[0x42]]` e `[[0x44]]` e envia cada polígono à fila de DMA `0x600000`. O cabeçalho do arquivo tem os ponteiros (`[0x45]`–`[0x62]`, `[0x5F5]`) e os formatos.

- **Objeto (`0x0091`)**:
  - posição relativa à câmera, girada pela matriz `[0x47]`;
  - profundidade (`FIX(Z)`) em `+0x1C` e escolha do LOD (`+0x18`/`+0x19`);
  - descarte por esfera (raio = 1ª palavra do modelo);
  - matriz dos vértices: câmera × objeto (`[0x4B]`), *billboard* `[0x4D]` (flag 8), só a câmera (flag 0x80) ou o próprio objeto (flag 1).
- **Projeção**: `sx = X·T + 256`, `sy = Y·T·1,040039 + 200`, com `T = [[0x52] + clamp(FIX(Z) >> 4, −0x50, 0x1387)]`.
- **Polígonos**: descarte de face traseira pelo produto vetorial em tela.
  - Objeto perto (z − r ≤ 1000): descarta polígonos inteiramente atrás da câmera e quads "torcidos". Quads com coordenadas fora de ±1023 e caixa maior que 0x7FF são subdivididos em 4, recursivamente, com interpolação de u/v (`0x032F`, pilha de 16 palavras por quad em `0x809889`).
  - Pacote de 15 palavras: cabeçalho, base de paleta (`[[0x62] + id] >> 16 << 8`), 4 × (x, y), A, A >> 16, B, B >> 16, C.
  - Variantes: flag 0x400 = base de paleta fixa (`+0x10`); flag 0x40 = polígonos lisos sombreados pela normal (`0x059E`, 10 palavras, modo `0x200 | FIX(8 − 8·N·L)`); flag 0x800 = articulado, com matriz e pivô por osso (`0x01BC`; ossos com `+0x12 < 0` usam 4 pontos de outro objeto, `0x0250`).
- **Modo 4** (`[C8C5] & 0xF == 4` e `[E871]`): matriz 2×2 em XZ (`0x0169`).
- Os operandos das operações paralelas `MPYF3||SUBF3` foram conferidos com o `mpysubf_0..3` do MAME. Os dois testes de face traseira (`0x046D` e `0x02D8`) dão a mesma fórmula: `(y5 − y2)(x5 − x4) − (x5 − x2)(y5 − y4) > 0`.
- **Desvios do port**:
  - A aritmética usa double, com o truncamento do `STF` só onde o original relê a memória. O C31 guarda as somas em registradores de 40 bits, então um vértice pode, raramente, cair 1 pixel ao lado.
  - Os registradores de saída não são reproduzidos (o chamador não os usa), exceto R4 e DP.
  - A trava de depuração (`[C92E]`) e a espera da fila só contam em `Pipeline3dStats::hangs`.
- **DMA**: a fila do hardware guarda só os 16 bits baixos de cada palavra (`uint16_t m_dma_data[16]` no MAME). O `DmaRenderer` faz o mesmo, porque a ROM manda `A` inteiro na palavra de u/v.
- Resultado: a cena de recordes do atrato ("Hot Times – San Francisco", 6 linhas com placas e tempos 00:00:00) aparece com texturas e paletas corretas.

### Sub-rotinas traduzidas

| Endereço | Função | Arquivo |
|---|---|---|
| `0x4ECC` | Copia a tabela de timing de vídeo | `src/core/boot/system_routines.cpp` |
| `0x4E1B` | Se `[0xC939] != 0`, desvia para `0x0699` | idem |
| `0x4E20` / `0x4E35` | Zeram as variáveis do sistema / BSS | idem |
| `0x5029` | Alimenta o watchdog (bit 3 de `0x994000`) | idem |
| `0x4EE3` | Pulsos de controle (bit 0 de `0x994000`, bit 1 de `0x980080`) | idem |
| `0x4EF9` | Liga o timer 0 | idem |
| `0x7F06` / `0x7F18` | Configuram a placa de link e habilitam a INT2 | idem |
| `0x4C34` (INT0) | Tratador do VBLANK | `src/core/irq/vblank_handler.cpp` |
| `0x4D6F` (INT3) | Leitura do ADC (volante, acelerador, freio) | `src/core/irq/adc_handler.cpp` |
| `0x4CEF`, `0x4D61`, `0x4D3D`, `0x4D28` | Entradas, chave de teste, volume ± | `src/core/io/input_routines.cpp` |
| `0x99CA` / `0x99EE` | Checksum dos ajustes / gravar item + checksum | `src/core/cmos/cmos_routines.cpp` |
| `0x993F`–`0x9A1F` | Acesso à CMOS | `src/core/cmos/cmos_routines.cpp` |
| `0x995C` / `0x9981` | Validação da CMOS: limites + tabela (mín, máx, padrão) | `src/core/cmos/cmos_routines.cpp` |
| `0x9A77`, `0x9A99`, `0x9AA9`, `0x9AD2`, `0x9ADC`, `0x9AEB`, `0x9B06` | Tabelas de recordes na CMOS: endereço, gravar/ler entrada, recriar com os padrões, validar | `src/core/cmos/cmos_routines.cpp` |
| `0x5020` | Prepara o atrato após a recuperação | `src/core/boot/system_routines.cpp` |
| `0x4FEF` / `0x5006` + `0x4B28` | Telas de recuperação e reentrada no RESET | `src/core/boot/boot_sequence.cpp` |
| `0x4BAF`–`0x4C32` | Loop principal | `src/core/main/main_loop.cpp` |
| `0x4F27` / `0x4F08` / `0xA585` | Profiler e recíproco | `src/core/main/profiler_routines.cpp` |
| `0x7989`, `0x798F`, `0x7998`, `0x79AF`, `0x7A41` | Pacote de envio do link | `src/core/link/link_packet.cpp` |
| `0x794C`, `0x793A`, tratadores e emissores `0x79BF`–`0x7C29` (exceto a sincronização do carro) | Mensagens do link | `src/core/link/link_messages.cpp` |
| `0x8E4E`–`0x8EB1`, `0xA731`, `0xA74E`–`0xA83F`, `0xABFC`, `0xAC23` | Vídeo: páginas, preenchimentos, pixel, linha, retângulo, texto, números | `src/core/video/video_routines.cpp` |
| `0xA531`, `0xA5A9`, `0x8EB7` | Divisão, resto, aleatório | `src/core/math/int_math.cpp` |
| `0x4F32`, `0x4F7D`, `0x4F8F`, `0x4F9B` | Tela de carga do RESET | `src/core/boot/boot_screens.cpp` |
| `0xA844`–`0xA93A`, `0x4DE6`, `0x95CB`, `0x69D6`, `0x894D` | Processos cooperativos e escalonador | `src/core/proc/process_manager.cpp` |
| `0x7F5D` (Master `0x7F6F`, Slave `0x80C1`) | Troca de pacotes pelo link | `src/core/link/link_routines.cpp` |
| `0x7187`, `0x71B3`, `0x71BF`, `0x71E4` | Ordenação e reagrupamento da lista de objetos | `src/core/scene/object_list.cpp` |
| `0x726F`, `0x7254`, `0x6FEB` | Listas de visibilidade e pool de objetos | `src/core/scene/object_list.cpp` |
| `0x9503`, casos `0x9563`–`0x95C9`, `0x1E17`, `0x1E1C`, `0x1E26`, processo `0x903B` | Início/reinício do jogo | `src/core/game/game_start.cpp` |
| `0x3859`, `0x3869`, `0x3872` | Controlador do modo de atração | `src/core/game/attract.cpp` |
| `0x3889` (+ retomadas), `0x3738`–`0x3953`, `0x60BD`, `0x9AD2`, `0x9B06` | Cena 3D da tabela de recordes | `src/core/game/hiscore_scene.cpp` |
| `0x36B3`–`0x3737`, `0x9EDB` | Buscas e operações por tipo | `src/core/scene/object_query.cpp` |
| `0x7025`–`0x70C9`, `0x715F`, `0x440C`, `0x9ECD` | Objetos de cena | `src/core/scene/scene_object.cpp` |
| `0x7298`, `0x72D2` | Refaz e ordena as listas | `src/core/scene/object_list.cpp` |
| `0x92DF`, `0x92E2`, `0x9377`, `0x938A`, `0x939D`, `0xB01A`, `0xB042` | Trigonometria e matrizes de rotação | `src/core/math/trig.cpp` |
| `0x9DF6`, `0x9DFE`, `0x9E80`, `0x9E97`, `0x9EC2`, `0x9EFA`, `0x9F2F`, `0x9F5C`, `0x9F72`, `0x9F84`, `0x9F8F`, `0x9F9F` | Slots de paleta e fila de cópia | `src/core/video/palette_slots.cpp` |
| `0xA2B0`, `0xA2C9`, `0xA2F9`, `0xA323`, `0xA374`, `0xA38C`, `0xA3A5`, `0xA3C2` | Descompressor LZW de texturas e animação de carregamento | `src/core/gfx/lzw_loader.cpp` |
| `0x0064`–`0x05F4` (`0x0091`, `0x0166`, `0x0169`, `0x01BC`, `0x0250`, `0x028D`, `0x0309`, `0x032F`, `0x0453`, `0x0498`, `0x059E`) | Pipeline 3D: transformação, projeção, descarte, subdivisão e fila de DMA | `src/core/render/pipeline3d.cpp` |
| `0x947F`, `0x9481`, `0x93F1`, `0x9494` | Matrizes 3×3: produto, cópia, produto in-place | `src/core/math/matrix.cpp` |
| `0x95F5`, `0x96FF` | Paletas básicas do jogo | `src/core/video/palette_slots.cpp` |
| `0xA93B`/`0xA984`, `0x1DC3`/`0x1DCE`, `0x1DCF`, `0x59E3`, `0x8C14`, `0xA3D7`/`0xA3D9` | Tela de título do atrato | `src/core/game/title_screen.cpp` |
| `0x9343`, `0x93D7` | Matriz por ângulos ZYX; identidade | `src/core/math/matrix.cpp` |
| `0x7409`, `0x7421`, `0x7F4D` | Ajustes da CMOS empacotados; configuração da placa de link | `src/core/game/game_start.cpp` |
| `0x38CD` (+ `0x38E1`, `0x38E4`), `0x38E5`, `0x9A92` | Pisca a linha do recorde novo (cores `[38CB]`, brilho `[38CC]` com 20 % de chance) | `src/core/game/hiscore_scene.cpp` |
| `0xAE27` | Próximo objeto com o mesmo tipo | `src/core/scene/object_query.cpp` |
| `0x8EE9` | Aleatório em [0, 1000) se < AR2 (carry) | `src/core/math/int_math.cpp` |
| `0x774A`, `0x7720`, `0xAED6` | Textos 2D sobrepostos (fonte em textura via quads de DMA), largura; envio do modelo de quad | `src/core/video/text_overlay.cpp`, `video_routines.cpp` |
| `0x7651`, `0x765F`, `0x7660`, `0x7676`–`0x771F`, `0x7450`, `0x7594` | Criação de textos (com sombra), estilos, paleta, indicação de volume | `src/core/video/text_overlay.cpp` |
| `0x745E`, `0x762E`, `0x73A0`, `0x73B3`, `0x73B7`, `0x77E7`, `0xC10042`–`0xC1009E` | Textos de créditos, tabela de preços (inclusive pelos DIPs), concatenação | `src/core/game/credits_display.cpp` |
| `0x8EF1`, `0x8F01`, `0x8F13`, `0x8F1F`, `0x8F29`, `0x7648`, `0x8FF0`, `0x9001`, `0x9011`, `0x9019`, `0x9024`, `0x9032` | Listas e pools de nós | `src/core/scene/node_pools.cpp` |
| `0x75F1`, `0x4C33` | Contadores de moedas; `RETIU` (religa o GIE) | `src/core/cmos/cmos_routines.cpp` |
| `0x7F25` | Pulso da placa de link (Master) | `src/core/link/link_routines.cpp` |
| `0x90E6`–`0x92C7` (`0x9211`, `0x91E0`, `0x92B3`, `0x910F`, `0x9114`, `0x9131`, `0x9166`, `0x9186`/`0x9188`, `0x92AD`…) | Driver de comandos de som: 4 canais em `0xE8F0`, sequências, prioridades, reinício em etapas | `src/core/audio/sound_commands.cpp` |
| `0x4E4F`, `0xAFC2`, `0xB001`, `0x4FAE`, `0x49B1`, `0x4996`, `0x49A7` | Lâmpadas, motor do volante, sequência secreta de botões (porta `0x995000`), pedido do controlador de movimento | `src/core/io/cabinet_outputs.cpp` |
| `0xA9C8`, `0xA9F5`, `0xAA05`, `0x1DE9`, `0x7309` | Caso −3: moldura, placas e logotipo da pista; objeto em posição inteira | `src/core/game/title_screen.cpp`, `scene/scene_object.cpp` |
| `0xA509`, `0xA5F1`, `0x931A`, `0x9308`, `0x9313`, `0x93C4`, `0x93E8`, `0x93FD`, `0x9414`, `0x941E` | Divisão e raiz (Newton-Raphson), atan2 pela tabela, normalização de ângulos, vetor/matriz | `src/core/math/float_math.cpp` |
| `0x21D0`, `0x2207`, `0x260E`, `0x25E8`, `0x21E9`–`0x21F1`, `0x238C` | Simulação por modo; proximidade carro × objetos e carro × carro | `src/core/game/car_proximity.cpp` |
| `0x559E`/`0x55B4`/`0x55C5`, `0x55DA`/`0x55F0`, `0x5620`, `0x58DD`, `0x58E8`, `0x5943`, `0x5968`, `0x596F`, `0x59AF` | Demo do atrato: interpretador de roteiro, moldura, câmera, carga da pista e dos carros | `src/core/game/attract_demo.cpp` |
| `0x5624`–`0x591A` (`0x569E`, `0x56C5`, `0x56EF`, `0x5719`, `0x5784`, `0x57F0`, `0x581E`, `0x5883`, `0x58EF`…) | Roteiro de câmera da demo: perseguição, panorâmica, câmera presa ao carro | `src/core/game/demo_camera.cpp` |
| `0x8C14`–`0x8E48`, `0x3966`–`0x396E`, `0xAA30`–`0xAB18`, `0x6754`/`0x6762`/`0x676B`, `0xAF55`/`0xAF58`/`0xAF66` | Carregadores por pista, nós da pista `[AA2F]`, listas de carros e objetos | `src/core/track/track_loader.cpp` |
| `0x3F6F`, `0x3F8D`, `0x3FE3`, `0x405A`, `0x4197`, `0x41B2`, `0x41BE`, `0x41D1`/`0x41DE`, `0x4204`, `0x4212` | Fluxo de trechos: carga/liberação de objetos da pista, lista de nós | `src/core/track/track_stream.cpp` |
| `0x822B`, `0x839B`, `0x83D1` | Céu e horizonte da pista por DMA direto | `src/core/video/sky_renderer.cpp` |
| `0x1F8B`, `0x1F8F`, `0x1FB0`, `0x20B4`, `0x20FF`, `0x9466` | Altura do chão sob um ponto (listas `[E403]`/`[E406]`, ponto-em-polígono XZ, plano) | `src/core/track/ground_height.cpp` |
| `0x1C9B`, `0x1CB5`, `0x1CC4`, `0x1CCC`, `0x1CD1`, `0x1CDE`, `0x1CF7`, `0x1CFB`, `0x3C19`, `0x3C1B`, `0x3C48` | Largada: contagem, liberação do grid e aviso do ajuste 0x1A | `src/core/game/race_start.cpp` |
| `0x8F68`, `0x8F72`, `0x8FAE` | Rodas (giro/esterço) e carroceria (arfagem/rolagem) | `src/core/game/car_wheels.cpp` |
| `0x5268`, `0x659F`, `0x65A5`, `0x65ED`, `0x65F2`, `0x65F8`, `0x661A`, `0x6628`, `0x6636`, `0x6639`, `0x663C`, `0x6641`, `0x6645`, `0x6653`, `0x665D`, `0x6680` | Posição na corrida e geradores de tráfego | `src/core/game/race_traffic.cpp` |
| `0x1F2A`, `0x1F31` | Pisca de paleta (lâmpadas amarelas) | `src/core/video/palette_blink.cpp` |
| `0x2633`, `0x2645`, `0x2727`, `0x278F`, `0x281D`, `0x2824`, `0x2835`, `0x2863`, `0x28A8`, `0x5375` | Colisão entre carros: caixas orientadas e resposta | `src/core/game/car_collision.cpp` |
| `0x221F`, `0x2334`, `0x2380`, `0x239C`, `0x23AC`, `0x23B0`, `0x23B9`, `0x23C4`, `0x23D1`, `0x6959`, `0x6971`, `0x6997` | Colisão com objetos do cenário: sólido, arremessado, tombado, balanço | `src/core/game/scenery_collision.cpp` |
| `0x316E` | Som com volume pela distância `[AR4+0x1C]` | `src/core/game/car_dynamics.cpp` |
| `0x87D7`, `0x8820`, `0x8860`, `0x889E`, `0x88B2`, `0x88EB`, `0x8928`, `0x8960` | Fumaça dos carros: arrancada, raspão, batida | `src/core/game/smoke_effects.cpp` |
| `0x4225`, `0x4242`, `0x428B`, `0x428C`, `0x4292`, `0x4293`, `0x429C`, `0x42C3`, `0x42D6`, `0x42EE`, `0x4308`, `0x4312`, `0x4313`, `0x4315`, `0x4330`, `0x43C0`, `0x43CA`, `0x43CB`, `0x43DE`, `0x4401`, `0x4402`, `0x440B` | Objetos animados do cenário (tabela `[4500]`) | `src/core/game/track_props.cpp` |
| `0x3160` | Som aleatório com volume dado | `src/core/game/car_dynamics.cpp` |
| `0x668C`, `0x66BC`, `0x66DB`, `0x66E3`, `0x66EB`, `0x66F3`, `0x6708`, `0x670F`, `0x6739`, `0x6740`, `0x67B0`, `0x68F4`, `0x7B25`, `0x9891`, `0x9924`, `0x9761`, `0x9765`, `0x9890`, `0x98E1`, `0xA3FF`, `0xA4ED`, `0xA507`, `0xA718`, `0xA72C` | Carros de tráfego (mesmo sentido e contramão) | `src/core/game/traffic_cars.cpp` |
| `0xAEA8`, `0xAEAD` | Rodas simples (só giro) | `src/core/game/car_wheels.cpp` |
| `0x4530`, `0x4689`, `0x4693`, `0x469D`, `0x4834`, `0x48D2`, `0x49A7`, `0x49B8`, `0x49CE`, `0x49EA`, `0x4A2E`, `0x4A41`, `0x4A4E`, `0x77D1` | UART do controlador de movimento, início da plataforma e cópia de texto | `src/core/io/serial_printer.cpp` |
| `0xAF75`, `0x87B8`, `0xAE05`, `0xAE27`, `0xAE1F` | Reagrupamentos da lista visível | `src/core/scene/object_list.cpp` |
| `0x2C18`, `0x2C92`, `0x2C9C`, `0x2CEB`, `0x2D83`, `0x2E73`, `0x2F2C`, `0x2F6B`, `0x3031`, `0x3035`, `0x3037`, `0x303A`, `0x3159`, `0x3168`, `0x9081`, `0x908B` | Dinâmica de direção do carro | `src/core/game/car_dynamics.cpp` |
| `0x513A`, `0x5140`, `0x51C2`, `0x51EA`, `0x51F4`, `0x5208`–`0x523C`, `0x52B8`, `0x52E4`, `0x532B`, `0x538D`, `0x539A`, `0x53A2`, `0x53DA`, `0x5441`, `0x54AA`, `0x54B5`, `0x54CB`, `0x66A7`, `0x94A7` | IA dos carros da CPU | `src/core/game/drone_ai.cpp` |
| `0x8EDB`, `0x8EE0`, `0x8EE3`, `0x8EC2`, `0x8ED1` | Aleatórios inteiros e float | `src/core/math/int_math.cpp` |
| `0x2185`, `0x2C39`, `0x1FF6`, `0x2041`, `0x2074`, `0x20D4`, `0x67E3`, `0x67EC`, `0x943B` | Física vertical do carro: pontos de contato, gravidade, atitude pelo chão, distância lateral | `src/core/game/car_physics.cpp` (`0x943B` em `float_math.cpp`) |
| `0x50CB` (criação), `0x2EFD`, `0x509A`, `0x54FD`, `0x5360`, `0x536C`, `0x2945`, `0x8F3E`, `0xAE3A`, `0xAE6A`, `0x9076` | Carros da CPU: criação, dados, peças, posição de largada | `src/core/game/drone_car.cpp` |
| `0x70CA`, `0x70CF`, `0x70E5`, `0x70FB` | Remoção das listas `[[0x59]]`, `[[0x58]]`, `[[0x56]]`; fim do processo do objeto | `src/core/scene/scene_object.cpp` |
| `0x7100`, `0x7143` | Libera o objeto: peças (bit 11 → `0x9011`), listas marcadas em `+0xE` (bit 22 `[[AF54]]`, 23 processo, 28 `[[0x56]]`, 21 `[[0x59]]`, 31 `[[0x58]]`), lista visível `[0x40]`/`[0x41]`/`[0x42]` (por `+0xE & 0x3000`) e devolve a `[C983]`; `0x7143` libera todos de um tipo | `src/core/scene/scene_object.cpp` |

Toda chamada passa pelo `SubroutineDispatcher`; um endereço sem tradução ficaria registrado como pendente (salvaguarda). Hoje não há nenhum: `rea_cruisn --pending` sai vazio e a `tools/regress.sh` falha se aparecer algum.

## Menu de serviço (`0x05F6`–`0x154C`) — traduzido (`src/core/service/`)

Entrada: a chave de teste (IN0 bit 0x10) alterna `[C939]` pela borda em `[C8CB]` (`0x4D61`, fora do modo 1);
o loop principal chama `0x4E1B`, que desvia (BR) para `0x0699`. O menu **não volta** ao loop: `0x0678` descarta
a pilha (SP = `[0x63]`), espera DMA/blitter, põe o modo 1 (`[C8C5]`, sem o watchdog de software), IE = INT0|INT3,
mata os processos (`0x95CB`) e carrega a paleta `[C7E0]`. Daí em diante tudo é espera ocupada sobre `[C930]`
(quadros da INT0) e `[C8C9]` (teclas filtradas, ativas em 0).

**Teclas:** ENTER = a própria chave de teste (0x10), VOLUME − (0x100) desce, VOLUME + (0x200) sobe, SERVICE
(0x40) volta ao menu anterior (exceto no principal). As telas testam o **nível** de ENTER, não a borda: um toque
longo atravessa vários níveis (menu → submenu → teste) e testes como a DIP test (`0x093D`) e a switch test
(`0x0866`) saem se ENTER ainda estiver apertada depois do primeiro desenho. É o comportamento do ROM.

**Laço genérico** (`0x0A4F` zera o cursor, `0x0A52` mantém): AR5 = tabela de pares (texto, rotina), `[AR5−1]` =
rotina do valor de cada item, `[AR5−2]` = cabeçalho próprio, AR6 = número de itens, AR7 = título. Rolagem de
10 em 10 por `[EBDE]` (20 visíveis). Repetição de tecla: R7 = 550 000 na 1ª, 275 000 nas seguintes (laço de 9
ciclos `0x0AB0`–`0x0AB5`). A rotina `0x0AE0` como item = sair do submenu.

| Tabela | Endereço | Conteúdo |
|---|---|---|
| principal `[C7E1]` | `0x10D8` | `0x0A4C` diagnósticos, `0x14D0` auditorias, `0x0A3B` ajustes, `0x0A46` utilitários, `0x0CC0` calibração, `0x0D1F` volume, `0x4B28` EXIT TO GAME OVER |
| diagnósticos | `0x10E7` | `0x0724` chaves, `0x0933` DIPs, `0x6381` CPU, `0x0B02` som, `0x0B65` volante, `0x06B3` monitor, `0x0BA5` burn-in, `0x0A36` movimento, `0x1155` lâmpadas |
| ajustes | `0x110A` | valores por `0x0A40` (tabela `[0A3F]` = `0xC13077`), escolhas `0x0B9B`/`0x0DFB`–`0x0E41`, números `0x0ECB`–`0x0F41` |
| preços | `0x0BBB`/`0x0BDC`, `0x0CA0` | padrão por país (`[0BDB]`, `0x0BF5`) e personalizado (`0x1032` ×13, `0x0FE7`, `0x0FF2`); cópia para a CMOS em `0x73D6` |
| movimento | `0x112F` | `0x46A1`/`0x46A3`/`0x46A5`/`0x46A7` eixos (burn-in senoidal), `0x13D5` chaves de segurança, `0x460B` HARD MOTION OFF — ver abaixo |

### Plataforma de movimento (`src/core/service/service_motion.cpp`)

A versão com movimento tem uma plataforma de 3 eixos (frente, traseiro esquerdo, traseiro direito) com um
controlador Galil ligado pelo UART do latch `0x995000` (`io/serial_printer.cpp`: `0x48D2` envia texto, `0x4A4E`
trata a resposta, `0x49B8`/`0x49CE` estado e recepção). Os sinais de segurança (tapetes, feixe, chave de falha,
botão de parada) chegam nos bits 8–15 de IN1 (`[0x991030]`, máscara `[0x4995]` = 0xFF80). Tudo depende do DIP
de movimento (`0x4693`) e do ajuste 0x34 (`0x469D`).

- `0x4530` (RESET) → `0x4539`: instalação ("MOTION SETUP TESTING..."), fins de curso superior e inferior
  (`[EBB8..EBBA]` bits 8/4 por eixo), erros `0x4644` (causa pela tabela `[4684]`), `0x4A72`/`0x4A9D` (eixo sem
  chave), desligamento lento `0x4615`, configuração `0x4954`/`0x48C4`.
- Menu: `0x46A1`–`0x46A7` → `0x46A8` "MOTION BURNIN": senoides (`0xB01A`) com passos `[C795..C797]` × 0,8 em
  `0x47E8`; sai com ENTER; erro de segurança → `0x13D5` e BU `0x0699`. Sem o DIP: `0x4961` ("WARNING MOTION DIP
  SWITCH OFF"); sem o ajuste: `0x4979` ("WARNING MOTION NOT DETECTED"). `0x460B`: reinicia o controlador
  (`0x490B`, `0x4A65`) e envia `[C78D]`.
- Corrida: `0x472A` (processo do carro) → `0x472C`: consulta periódica, botão de parada, e a inclinação pelas
  alturas das 4 rodas (`+7`, `+0xD`, `+0x13`, `+0x19`) com a curva (2v − v²/150)/150, ±0,9, mais rolagem/arfagem
  (`+0x42`/`+0x43` × 6), ±1; `0x47E8` converte em 26000 ± 17000 e envia "ZA=/YA=/XA=" quando o eixo muda ≥ 200.
  Peculiaridade: Y e X guardam R0 (`STI R0`) em `[EBB6]`/`[EBB5]`, não a posição.
- `0x4991`: atraso de R0 · 5000 voltas (5 ciclos cada).

Modelo: sem placa de movimento a leitura de `0x995000` devolve 0 (o latch é só de escrita; o MAME não mapeia a
leitura). Antes o port devolvia o último valor escrito, o que fazia o laço de comandos `0x49EA` receber 0xFB
para sempre.

Caixas: `0x0E62` mensagem por 31 quadros, `0x0E7D` escolha (VOLUME + alterna; VOLUME − não faz nada, os dois
ramos de `0x0EB8` vão para `0x0EBE`; saída carry = R5 == 0), `0x0F5B` valor numérico com repetição
(`[EBE1]`/`[EBE2]`). Auditorias `0x14D0`: tabela paginada `[C8BC]` = `0x1430` (−3 linha em branco, −1 fim de
página, −2 recomeça; item 0x5A00xx = valor calculado por `0x1419`/`0x1426`). Pela sequência secreta, VIEW 1 abre
`0xAB79`, o registro de erros da CMOS gravado por `0xAB1A` (item 0xDE = entradas, 0x40 palavras cada a partir de
`[9936]` + 0x8B7): 5 por página com "LOG n" e 16 valores em hexadecimal; VIEW 3 pagina, VIEW 1 + 2 + 3 apaga,
VIEW 1 ou ENTER saem.

**Saídas não locais.** EXIT TO GAME OVER é `0x4B28` (a reentrada do RESET), e o burn-in sai com BU `0x0699`
(pilha zerada). A sequência secreta da INT0 (`0x4FAE`) faz BR `0x0669` (auditorias com `[EBBB]` = 1, depois
`0x4B28`). No port essas saídas lançam `NonLocalJump{pc}` (`core/non_local_jump.hpp`), apanhado no topo da
corrotina da CPU: `0x4B28` → `BootSequence::reenter()` + `MainLoop::reset()`; outros PCs → `subs.call(pc)`.

**Corrotina da CPU** (`core/cpu_coroutine.hpp`). Como o menu espera ocupado sem voltar ao loop, o código
principal roda numa thread própria em alternância estrita com a plataforma: `resume()` uma vez por quadro, e a
CPU devolve o controle com `yield()` no fim de cada passo do loop principal e dentro das esperas
(`FrameBusyWait::setFrameHook`). Só uma thread executa por vez; não há acesso concorrente à memória emulada.

**Tempo.** As esperas são contadas em ciclos (431 600 por quadro) e entregues a `IBusyWait::spin`. Como o menu
desenha pixel a pixel pela CPU e as telas leem o nível das teclas, o custo do desenho decide o que um toque
alcança; `service::Ctx::call` cobra uma estimativa antes de cada rotina de desenho (texto 1000 ciclos por
caractere, linha 30 por pixel, retângulo 2 por pixel, limpeza de página 262 144 / 131 072). O burn-in desliga
o GIE e alimenta o watchdog de hardware por conta própria (`IBusyWait::feedWatchdog`).

Peculiaridades preservadas: `0x10A6`/`0x106B`/`0x0BF5` alinham à direita pelo R0 devolvido por `0xA762`
(números grandes passam da moldura); `0x73D6` grava R2 (o campo anterior) nos itens 0x13–0x16; nas auditorias
a chave de IN1 bit 0x10 com `[EBBB]` = 0 age como "próxima página".

## Placa de som DCS (ADSP-2105) — traduzida (`src/core/audio/dcs/`)

Hardware (MAME `dcs.cpp`, variante `DCS_AUDIO_2K`/`REV_DCS1`, ADSP-2105 a 10 MHz; `midvunit.cpp`):

- **ROMs:** região de 16 bits com cada byte de `cusa.u2`…`u9` no byte baixo e 0xFF no alto; a ROM `u(2+k)` começa
  na palavra 0x100000·k. Banco b (`DM(0x3000)` = b & 0x7FF) = 0x1000 palavras a partir da palavra b·0x1000:
  a ROM `u(2 + (b >> 8))`, deslocamento (b & 0xFF)·0x1000 bytes se (b & 0xFF) < 0x80; senão tudo 0xFFFF.
  A janela `DM(0x2000–0x2FFF)` lê `0xFF00 | byte`.
- **Memória:** PM interna `0x0000–0x03FF`; PM externa 2K × 24 bits em `0x0800–0x0FFF` (espelhos até `0x1FFF`).
  `DM(0x0000–0x07FF)` (espelho `0x1800`) é **a mesma RAM**: lê os bits 8–23 de `PM(0x0800 + n)` e a escrita
  preserva o byte baixo. DM interna `0x3800–0x39FF`. Registradores de controle `0x3FE0–0x3FFF`
  (`0x3FEF` autobuffer do SPORT1, `0x3FF0–0x3FF2` SPORT1, `0x3FFB–0x3FFD` timer, `0x3FFF` controle do sistema:
  bit 0x0200 força um novo boot a partir do banco atual, bit 0x0800 liga o SPORT1).
- **Boot:** página do banco atual, palavra i = bytes 4i..4i+2 (o 4º é ignorado), 8·(byte 3 + 1) palavras.
  Banco 0 = autoteste (RAM, checksums das ROMs; informa o resultado pelo trinco `0x3400`), que termina com banco 1
  e `DM(0x3FFF)` = 0x0208 → boot do residente (banco 1, 1024 palavras). O residente carrega overlays na PM
  externa com `0x03F1` (3 bytes por palavra, pula o 4º): banco 0 + 0x800, 0x200 palavras (inicialização,
  CALL `0x0800`), e banco 2, 0x300 palavras (sistema principal; tabela de saltos em `0x0800`…).
- **Comandos:** a CPU principal escreve um byte em `0x9A0000` (`data_w(data & 0xFF)`): o trinco de entrada guarda o
  byte e aciona a IRQ2 (vetor `0x0004` → `0x0383`); a leitura de `DM(0x3400)` devolve o byte e reconhece
  (limpa a IRQ2). Nada é lido de volta pelo Cruis'n USA. O bit 1 de `0x994000` é o reset do DCS.
- **Saída:** SPORT1 em autobuffer (`0x3FEF`: I = bits 9–11, M = bits 7–8 (+ bit 2 do I), L = o mesmo índice do I);
  taxa = 10 MHz / (32 · (SCLKDIV + 1)) = 31 250 Hz com SCLKDIV = 9 (`0x3FF1`). O MAME copia meio buffer por vez e
  pulsa a IRQ1 a cada volta completa do buffer. Amostras de 16 bits com sinal, mono, sem escala.

O programa extraído tem 1024 + 0x200 + 0x300 palavras (`assets/rom_extracted/dcs_*.bin`, listagens `dcs_*.asm`
geradas com `unidasm -arch adsp21xx`). Está traduzido para C++ do mesmo jeito que o programa principal:

- `adsp_core.hpp`: registradores e semântica do ADSP-21xx (flags da ALU como no MAME, MAC fracionário/inteiro com
  arredondamento sem viés, deslocador, DAGs com buffers circulares e bit reverso, pilha de contadores, banco
  secundário, MSTAT/IMASK/ASTAT empilhados nas interrupções).
- `dcs_board.cpp`: memória, ROMs, trinco (fila de bytes; cada um é uma IRQ2), SPORT1 em autobuffer e o relógio.
  O programa roda numa corrotina; o tempo do DSP só avança nas esperas (`burn()`): laços de NOPs do autoteste e a
  espera do ponteiro I7 no laço principal. O cálculo entre esperas conta como instantâneo; como o programa sempre
  espera a metade livre do buffer de 480 amostras antes de escrever, o som não muda.
- `dcs_selftest.cpp` (banco 0, `0x0022`–`0x0175`): espera ~0,25 s por um byte (pula o teste e sobe o banco 1);
  senão testa a RAM, soma as ROMs (tabela do banco 0x24), toca o bipe (`0x0130`/`0x0154`) e sobe o banco 1.
  A leitura em `0x003A` descarta o trinco, então só um byte chegado depois dela pula o teste.
- `dcs_resident.cpp` (banco 1): carrega os overlays, recebe bytes na IRQ2 (`0x0383`: volumes `0x55AA`–`0x55AE`,
  PCM `0x55CC`, demais comandos na fila de 16), interpreta os roteiros dos 4 canais (ops 0–0xE), faz a
  transformada inversa de 128 pontos, a sobreposição com janela e escreve 240 amostras por quadro.
- `dcs_overlays.cpp`: inicialização (`0x0800`) e o sistema principal: `0x0803` (início de fluxo), `0x0853`
  (quadro de um canal: fatores por Huffman `0x0A49`, coeficientes por tabela de códigos ou PCM de n bits, leitura
  de bits `0x0A27` com reservatório de 2 palavras), `0x0A7E` (rampas, volumes exponenciais, contadores).

No jogo, `main.cpp` liga a placa ao barramento (`0x9A0000`, `0x994000`), roda 31 250 / 57,9 amostras por quadro e
as enfileira no SDL (`SDL_QueueAudio`). A fila é mantida entre ~40 e ~140 ms tirando ou repetindo 1 amostra em
32 (a diferença entre o relógio do jogo e o da placa de som do PC); só acima de ~0,5 s, numa travada longa, o
quadro é descartado. `--wav arquivo` grava a saída. Como o boot do port
é instantâneo, os bytes da etapa de reinício chegam durante a espera do autoteste e o bipe de energização não toca
(no gabinete a CPU principal demora mais para chegar lá). `REA_DCS_TRACE=1` registra cada byte recebido.
Numa corrida roteirizada (3 fichas — o padrão da CMOS é 3 créditos por partida —, START, seleções e
acelerador) a placa recebe ~3600 bytes em 9000 quadros: músicas, efeitos, volumes por canal (`0x55AB`–`0x55AE`)
e o motor (`0x55CC` + par), e a saída fica contínua, sem saturar.

## Ferramentas

- `tools/extract_rom.sh`: extrai `rom/*.zip` para `assets/rom_extracted/` (só para desmontar; o jogo lê o `.zip`).
- `tools/interleave_program.py`: gera `program.bin` (palavras little-endian).
- `tools/extract_dcs.py`: extrai o programa do ADSP-2105 (boot do banco 0, residente do banco 1, overlays) para `dcs_*.bin`.
- `tools/regress.sh [--quick]` (no container): regressão de ponta a ponta (testes, calibração com CMOS nova, corrida, link e ordem dos operandos).
- `tools/check_operand_order.py [--aceitar]` (no container, também a etapa 5 da `regress.sh`): desmonta o programa,
  segue o código alcançável a partir de `--list-translated` e exige que toda instrução sensível à ordem dos
  operandos esteja em `tests/operand_order_reviewed.txt` (958 hoje: as 221 e as 660 das duas varreduras de "Ordem dos operandos", mais
  77 diferenças que só entram ao quadrado ou em `ABSF`). As que faltam saem agrupadas pela rotina e pelo arquivo que
  a registra; `--aceitar` grava a lista depois da conferência.
- `tools/disasm.sh <endereço> [n]`: desmonta com o `unidasm` (MAME 0.289, compilado no Dockerfile, arquitetura `tms320c3x`; para o som use `adsp21xx`).
- `program.asm` com o disassembly completo: `unidasm assets/rom_extracted/program.bin -arch tms320c3x > assets/rom_extracted/program.asm`.

## Recursos e pacotes distribuíveis

- `ResourceManager::openRom(probe)` devolve uma `RomSource` (`src/core/rom_source.cpp`): `assets/rom_extracted/`,
  subpasta de `rom/` com os chips ou qualquer `rom/*.zip` (ordem alfabética). `ZipRomSource` lê só o fim do arquivo e o diretório central
  e descompacta com `inflateRaw` (deflate próprio, no estilo do `puff` do zlib, com CRC-32); sem dependências.
  `MemoryBus::loadProgramRom`/`loadGraphicsRoms` e `DcsBoard::loadRoms` aceitam pasta ou `RomSource`.
- Identificação (`src/core/rom_identity.cpp`): `verifyRom` (por `RomSource::crc`: a pasta calcula, o zip responde
  pelo diretório central) confere os 28 chips (programa, gráficos, som) com o
  CRC-32 da L4.1 (`knownL41Chips`, os do conjunto em que o port foi traduzido); `main.cpp` avisa cada chip diferente
  (console + `SDL_ShowSimpleMessageBox`) e segue. Sem `cusa-l41.u10`, `ResourceManager::findOtherVersion` procura
  `cusa-l*.u10` de outra versão; `confirmOtherVersion` (`main.cpp`) pergunta (padrão: sair; `--allow-other-version`
  aceita sem janela) e, aceito, `openRomChain` junta essa fonte às demais (`ChainRomSource`: o set do MAME de outra
  versão só traz o programa; gráficos e som vêm do set da L4.1). Aí só gráficos e som são conferidos. Com mais de uma versão (set "merged" ou um `.zip` por versão), `ResourceManager::findVersions` lista as versões (rótulo pelo nome do chip, `versionLabel`: `cusa-l21.u10` → 2.1; a 4.1 primeiro) e `chooseVersion` (`main.cpp`) abre o menu (`SDL_ShowMessageBox`, um botão por versão); `--versao V` escolhe sem janela. As rotinas traduzidas são as da L4.1, então outra versão costuma girar num laço sem `yield()`: `CpuCoroutine::setStallLimit` (10 s) faz `resume()` desistir, e o `main.cpp` avisa e termina com `std::_Exit` (a thread da CPU não pode ser parada). `zipMatchesL41` compara pelo diretório central (sem
  descompactar) para avisar quando `assets/rom_extracted/` encobre um `.zip` diferente.
- Raiz (`ResourceManager::findRoot`): `--root`, `REA_ROOT`, pasta do executável (`SDL_GetBasePath`) e até 3 acima,
  pasta atual e `REA_SOURCE_DIR`; vale a primeira com `assets/lang/`.
- `cmake --install` (padrão `build/dist` num build novo) monta executável + `assets/lang` + pastas vazias
  (`assets/textures|audio|config`, `rom/`, `save/`) + `packaging/LEIA-ME.txt` e `packaging/rom/LEIA-ME.txt` + as licenças (`LICENSE`, `packaging/LICENCAS-TERCEIROS.txt` e, em
  `licencas/`, os textos das bibliotecas que vão no pacote de cada plataforma: runtime do GCC; no Windows também SDL2 e
  MinGW-w64; na Web, Emscripten, musl, libc++ e SDL2); `cpack` gera o `.zip` (Linux ~1,4 MB, Windows ~2,5 MB com a `SDL2.dll`, Web ~1,2 MB). No Windows
  copia as DLLs (`$<TARGET_RUNTIME_DLLS>`). Nenhum dado da ROM vai no pacote.

## Versão para navegador (Emscripten) — plataforma do port

- Mesmo `rea_core`, `rea_platform_sdl` e `main.cpp`; o `CMakeLists.txt` troca o SDL2 do sistema pelo port do
  Emscripten (`-sUSE_SDL=2`), liga `-fexceptions` (os desvios não locais e o `Stop` da corrotina são exceções) e
  gera `build-web/bin/index.html|js|wasm|data` com o shell `src/platform/web/shell.html`. Sem Threads, testes nem
  Doxygen nesse build. Serviço `web` do `docker-compose.yml` (imagem `emscripten/emsdk:3.1.74`).
- Laço: `GameLoop::run` sem mudanças; `SdlClock::sleep` chama `emscripten_sleep` em todo quadro (mesmo 0 ms), e o
  ASYNCIFY desenrola e retoma a pilha para a página tratar eventos, desenho e áudio. `-sASYNCIFY_STACK_SIZE=1 MB`,
  `-sSTACK_SIZE=8 MB` (o código traduzido aninha muitas rotinas).
- `CpuCoroutine` (`__EMSCRIPTEN__`): fibra (`emscripten_fiber_init`, pilha C de 4 MB e pilha ASYNCIFY de 1 MB) no
  lugar da thread; `resume()`/`yield()` = `emscripten_fiber_swap`. A entrada da fibra nunca retorna (no fim volta à
  plataforma para sempre); o destrutor marca `stopping_` e retoma a fibra para o `yield()` lançar `Stop`. Prazo de
  travada: `MemoryBus::readWord` chama `CpuCoroutine::checkStall()`, que a cada 2^20 leituras compara
  `emscripten_get_now()` com o início do `resume()`; vencido (`setStallLimit`, 5 s no navegador), lança `Stalled`,
  que `run()` captura (`stalled_`); `resume()` retorna false e o `main.cpp` segue o caminho do PC (mensagem,
  `Module.reaExit(1)`, `std::_Exit`). Caso real: a 2.1 gira no `copyRamTest` (`0x6548`, contagem ~4 bilhões).
  Custo: nenhuma diferença medível na corrida de referência (57,2 quadros/s, mesma captura).
- `main.cpp`: no navegador `chooseVersion`/`confirmOtherVersion`/avisos usam `prompt`/`confirm`/`alert` (`EM_JS`);
  `--link` é recusado; depois de gravar a CMOS chama `Module.reaSyncSave` (MEMFS → IndexedDB); o `main()` embrulha
  `reaMain()` e chama `Module.reaExit(código)` no fim (o `onExit` do Emscripten não dispara: o áudio do SDL mantém o
  runtime vivo). Esc não fecha o jogo (no navegador ele sai da tela cheia).
- Página: o `.zip` escolhido vai para `/rea/rom` (MEMFS) e, se "Guardar a ROM" estiver marcado, também para
  `/rea/guardada` (IDBFS); `/rea/save` é IDBFS; `index.data` traz só `assets/lang`. O jogo roda com
  `callMain(['--root', '/rea'])`. O SDL2 do Emscripten desenha no elemento `#canvas`.
- Validação: com a CMOS calibrada da regressão e o roteiro de corrida (`--press`, 4200 quadros), a captura e os
  contadores (polígonos, quads, processos, DCS) são idênticos aos do executável nativo; no Chrome headless sem GPU,
  57,2 quadros/s.

## Re-skin de texturas (`src/core/video/texture_pack.*`) — recurso do port

- A RAM de texturas (`0xA00000`–`0xBFFFFF`, 2 texels por palavra, 4 MB) é vista como 64 páginas de 256 × 256
  (o renderizador endereça `texbase + v·256 + u`, com `texbase` = palavra 14 do DMA × 256).
- `TexturePack::loadDir(assets/textures)` carrega `page_NN.bmp` (8 bits, BI_RGB, 256 × 256, de baixo para cima ou
  de cima para baixo). `DmaRenderer::setTexturePack` faz a busca do texel consultar o pacote antes da RAM.
- `--dump-textures DIR` grava as páginas não vazias com a paleta `[pixdata + i]` do último quad texturizado que
  começou na página (`DmaRenderer::lastPalette`). Campos dos BMPs escritos/lidos byte a byte em little-endian.
- Verificado: as páginas 04/08 (céu de Golden Gate Park) trocadas por um xadrez aparecem na corrida; o resto da
  cena fica idêntico.

## Sons externos (`src/core/audio/sound_overrides.*`) — recurso do port

- O `AudioManager` põe no barramento a própria porta da placa (`HostPort`) em vez da `DcsBoard`. Sem sons externos,
  cada byte segue direto (como antes). Com sons externos, os bytes viram palavras (alto, baixo). As palavras de
  parâmetro (depois de `0x55AA`–`0x55AF` e `0x55CC`) sempre seguem; o parâmetro de `0x55AA` (volume, `v << 8 | ~v`)
  vira o volume das vozes externas. `0x0000` para as vozes. Um comando com `cmd_XXXX[_loop].wav` toca o WAV
  (reamostrado para 31 250 Hz mono na carga) e não chega à placa.
- Verificado: com `cmd_0085.wav` (tom de 440 Hz), as 3 fichas tocam o tom (RMS ≈ 12000 · 0,707 · 200/255) em vez do
  som da ROM; os demais comandos seguem para a placa.

## Traduções finais (registro)

Rotinas e recursos concluídos por último, guardados aqui como registro.

- Revisão de 2026-11-13: as rotinas que estiveram na lista "A confirmar" (sub-rotinas do RESET e da INT0, `0x4834`, `0x21D0`,
  `0x4E1B` → `0x0699`, `0xAB1A`, `0xAF75`/`0x87B8`/`0xAE05`, `0x9166`, `0x92AD`, os processos `0x559E`, `0xA9C8`,
  `0x1775`, `0x50CB`, `0x1C9B`, `0x1F2A`, `0x659F`, `0x65ED`, `0x6639`, `0x822B`, `0xACCA`, `0x62D5`, `0x6381`,
  `0x1F8B`, `0x2BE5`, `0x8E6D` → `0xAECC`, `0x0E62`, `0xA7F4`) estão traduzidas (conferido em `build/translated.txt`).
  `0xA133` (`[C707]`/`[C70E]`) é um bloco de dados LZW, não código.
- Os processos dos casos −7 (`0x154D`, tela "HEAD 2 HEAD / JOIN IN NOW" no gabinete parado durante o link) e −6 (`0xA1EA`, créditos finais com VIEW2+VIEW3 apertados e VIEW1 solto no reinício) estão em
  `src/core/game/link_intro.cpp`.
- Tratadores de link: todos traduzidos e verificados com dois executáveis ligados (`--link master/slave`).
- `0x0CC0` (CALIBRATE CONTROLS) traduzida em `src/core/service/service_calibration.cpp`: telas `[C855]`–`[C85F]`,
  cada uma com `0x05FA`/`0x05F6`/`0x0600`; grava pela `0x99EE` os itens 1 = acelerador em repouso, 6 = freio em
  repouso, 5 = volante no centro, 3 = volante à esquerda, 4 = à direita, 2 = acelerador no fundo e, se
  `[C8CC]` & 0x20 = 0, 7 = freio no fundo. Valores: `[C93B]`, `[C93C]`, `[C8C8]` (filtrados pela INT3).
- CMOS persistente (`src/core/cmos/cmos_file.cpp`): `save/cmos.nv` = 0x2000 palavras LE (32 KiB), lida antes do
  RESET e gravada quando muda (conferida a cada 300 quadros) e ao sair; `--cmos arquivo|none`. Com o link, o
  Slave usa `save/cmos_slave.nv` (cada gabinete tem a sua).
- O RESET (`BootSequence::run`) passou para dentro da corrotina da CPU: as esperas da `0x0E62`/`0x0CC0` no boot
  rodam quadros (INT0/INT3, entradas) como no menu de serviço. Os reinícios por watchdog também voltam por ali.
- INT2 (`0x8090`–`0x80C0`, no meio do código do link): só age em gabinete Slave. Ajusta o vtotal (`0x98002B` = 0x1AF/0x1B0/0x1B1) conforme a scanline atual, para sincronizar o vídeo com o Master.
- O processo `[C6BA]` = `0x90B9` (ID 0x5C6, criado pela `0x90F3`) está traduzido em `src/core/game/music_title.cpp`:
  não toca música (o comando sai na hora, pela `0x91E0`), só mostra o nome da faixa por `[E914]` = 0x28 quadros
  na corrida (texto `[[90B0] + [E916]]` com `0x7651` e objeto `[C6B9]` com `0xAF48`).
- Com a `0x49B1`, todas as rotinas chamadas pela INT0 estão traduzidas.
- Também `0x472A` além da guarda do DIP do modem e `0x7ADB` (sincronia do carro no link). As antigas pendências
  do boot (`0x0CC0` e `0x0E62`, chamadas com a CMOS vazia) rodam agora dentro da corrotina, com quadros e entradas.
- Autoteste de energização traduzido (`src/core/boot/power_on_test.cpp`): `0x62D5` testa a RAM rápida
  0x400000 (U56–U60; falha = pisca o rótulo no LED e trava em `0x631F`); `0x6381` testa paleta 0x9E0000
  (U85/U87) e VRAM (U94/U95/U101/U102), restaura a paleta (`0x065F`, tabela `[C7DF]`), desenha a tela
  "CPU BOARD TEST" (desenho da placa, caixa verde/vermelha por chip), soma as 20 EPROMs (`0x6552`; somas
  esperadas em `0x656C`–`0x657F`, conferidas) e testa RAM de texturas (U69–U76) e CMOS (U31, `0x6403`).
  O teste de RAM `0x64FE` roda da cópia em 0x809D00 (`0x6548`); as pausas de NOPs do código no LED (`0x632F`: ~0,32 s por meia piscada, ~1,4 s por dígito) passam como quadros
  (`registerPowerOnPauses`), e a falha da `0x62D5` pisca para sempre (`BU $631F`).
- Tratadores `[4500]` restantes (`src/core/game/roaming_objects.cpp`): `0x9B43`/`0x9B46` (tipos 0x470–0x476,
  só sem link) criam o líder `0x9B4E` (ID 0x261) e seguidores `0x9B49` (ID 0x260): viram "carros" (dados
  `0x2945`, lista `[E404]` via `0x6762`/`0x676B`), andam a `[E8EA]` (170..280, o líder regula para chegar ao
  alvo `[E8E7]`/`[E8E9]` junto com o jogador) e somem com `[E8EB]`. Batida (`0x9BEF` → `0xC101FB`, chamada pela
  `0x2645`): acima de 150 cada 0x260 encadeado ganha `0x9BF1` (voo, quique, rolagem, de pé dirige com
  `0x2C18`). `0x4440`/`0x4473`/`0x447F` → processos `0x4451`/`0x448E`/`0x4495` (objetos 0x498 que deslizam).
- Forçando a pista (`--poke 0xE634=N@1250-2600,0xE481=N@1250-2600`) as 14 pistas carregam e aparecem.
- Eventos das pistas 9 e 11 (`src/core/game/roadside_events.cpp`), criados por `0x8DAD`/`0x8DF6` e ativos com o
  item 0x23: animais à beira da estrada (`0x69E1` → `0x69FF`, ID 0x751, com som ao chegar perto; `0x6A1D` →
  `0x6A34`, ID 0x752, animado), um objeto que percorre a linha de pontos `[AA2F]` (`0x6B14` → `0x6B2D`) e solta
  objetos de tela 0x103 (`0x6BF5`, também de `0x6BD7`). Batida num objeto 0x750 (`0x6959`): `0x6A66` estilhaça
  (pedaços `[C62B]`, voo `[2333]`, processo `0x6AAC`) e `0x6AD3` lança; `0x9042`/`0x905C` (salvar/restaurar todos
  os registradores) viram uma cópia do estado no C++.
- Créditos finais (`src/core/game/credits.cpp`): `0xA1F1` (chamada local do pós-corrida depois do encerramento)
  interpreta o roteiro `[A2AB]` (comandos de linha, estilo, pausa e fim), com o anel de objetos `0xA1A1`, a
  reciclagem dos textos que saem por cima (`0xA179`) e os efeitos `0xA24C`/`0xA26D`.
- Carro especial 0x270 da pista 2 (`src/core/game/special_car.cpp`): o evento `0xAD22` cria `0x7CFA`
  (ID 0x214). Faz 1..2 passadas no mesmo sentido (`0x7C86`, trechos `+17`, some a 22000 do focado) e, com 50%
  de chance, uma no sentido contrário (`0x7D39`, trechos `+21`) que acompanha o jogador a 1750..3500; sai de cena
  por 101 quadros (`0x7E08`) e solta tudo em `0x7E29`. Dirige com `0x7E64`, motor 0x135 em `0x7EC6`, rodas em
  `0x7E3F` + processo `0x7E63` (→ `0xC10000`, ID 0x230). Peculiaridade preservada: a passada no mesmo sentido usa
  `*+AR7(20)` (aceleração) sem inicializá-lo, então vale o que o processo anterior deixou no PCB.
  O céu dessa pista usa `0x94C9`/`0x94B4` (reta por dois pontos e distância à reta), agora em `float_math.cpp`.
- Encerramento (`src/core/game/ending.cpp`): `0x8481` roda como chamada local do pós-corrida quando o item
  0x24 = 1. Câmera em (0, −600, −12000), cenário `[B153]`, dois textos `[847F]`/`[8480]`, legendas `0x8632`,
  animações por tipo de objeto (`0x858C` → `0x860F`/`0x861C`), volume do som 0xBA pela distância (`0x853F`),
  trilha `0x855F`; aproxima a câmera por 181 quadros, espera 300, fecha os painéis e recua. Os 4 painéis
  (`0xB07E` = `0xC1014F`) abrem em `0xB07B`/`0xB07C` (`0xC100B1`/`0xC100B3`, chamados por `0xB06F`) e fecham em
  `0xB07D` (`0xC1010A`, por `0xB075`); `[E5F8]` = 1 no fim.
- `0x4834`: serial do controlador de movimento (Galil, "SH;J G 10,…"), só no gabinete com movimento (item 0x34 = 1).

## A confirmar

- Semântica de `0x980080` (bit 3), de `0x980041` e da porta `0x991050`. O MAME 0.289 também não as conhece
  (`src/mame/williams/midvunit.cpp`, só consultado como referência), e o port se comporta igual a ele:
  - `0x980080`: mapeado como `noprw()` (leituras e gravações sem efeito), no V-Unit e no V-Unit Plus. No port, a
    gravação fica guardada e nada a usa (`vunit::kVideoMisc`).
  - `0x980041`: não mapeado; o MAME só trata `0x980040` (controle de página: o bit 0 troca a página exibida). A
    gravação do RESET (`0x10E`) não tem efeito nos dois.
  - `0x991050`: a linha do mapa está comentada ("seems to be another port"), então a leitura dá 0 no MAME; no port
    também dá 0 (porta nunca gravada). O RESET guarda o valor em `[0x8099E1]`.
  Confirmar o que esses sinais fazem na placa exigiria o esquema elétrico ou uma placa real.

## Camadas do código

```
src/platform/sdl/   ← única camada que conhece SDL2 (janela, input, áudio, relógio)
src/platform/web/   ← shell.html da versão para navegador (Emscripten; mesmo código SDL2)
src/platform/link_shm.*  ← cabo de link entre dois executáveis (arquivo em memória compartilhada; sem SDL2)
        │ implementa
src/core/interfaces.hpp  (IRenderer, IInput, IAudio, IClock)
        │ usada por
src/core/      MemoryBus, GameLoop (passo fixo + VBLANK), ResourceManager, tms_float
src/ui/        LocalizationManager (textos do arcade em en, por chave)
```

Alvos CMake: `rea_core` (sem SDL2) → `rea_platform_sdl` → `rea_cruisn`; testes em `rea_core_tests`.

## Fluxo de trabalho

```bash
docker compose build dev                   # 1x (só a imagem de desenvolvimento; sem o nome, monta todas)
docker compose run --rm dev                # shell no container (compilação e testes; sem vídeo/áudio do host)
tools/extract_rom.sh                       # rom/*.zip → assets/rom_extracted/
cmake -S . -B build -G Ninja && cmake --build build
ctest --test-dir build --output-on-failure
exit                                       # volta ao hospedeiro
docker compose run --rm game build/bin/rea_cruisn  # jogo: janela via Wayland/X11 e som do hospedeiro
```

Windows: compilação cruzada no serviço `win` (`packaging/Dockerfile.win`: MinGW-w64 GCC 13 POSIX + SDL2 2.30.9 para
MinGW em `/opt/sdl2-mingw` + Wine) com `cmake/mingw-w64-x86_64.cmake` → `build-win/`. No MinGW o `rea_cruisn` e o
`rea_core_tests` ligam com `-static` (só a `SDL2.dll` fica fora; o install a copia por `$<TARGET_RUNTIME_DLLS>`);
`rea_cruisn` é programa de janela (`WIN32_EXECUTABLE`, WinMain do SDL2main); o `main()` do Windows embrulha `reaMain()` e, com `--console`, chama `AllocConsole` + `freopen("CONOUT$")` e espera Enter no fim; `reaMain` chama `SetConsoleOutputCP(CP_UTF8)`. Portabilidade achada pelo Wine: `M_PI` não é padrão (testes usam
`rea::test::kPi`) e o Windows não apaga arquivo ainda mapeado (o teste do cabo fecha os dois lados antes do
`remove`). No Wine: 508 verificações e a corrida de referência idêntica à do Linux; o pacote também foi testado num Windows
de verdade (roda). Compilar direto no Windows (Visual Studio + vcpkg ou MSYS2) não foi testado.
