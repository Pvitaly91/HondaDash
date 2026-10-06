# M3a — дві Nano, окрема низьковольтна лабораторна лінія

Стенд має два USB endpoint і одну фізичну напівдуплексну лінію 9600 baud,
8N1, idle HIGH. Це власний стенд із reference-derived емулятором ECU.
**Не підключати до автомобіля. Фізичний обмін і електричні параметри:
NOT VERIFIED**, доки не виконано ручну перевірку з реальними платами.

Схема, точні компоненти, піни та порядок монтажу:
[hardware/two_nano_bench](../hardware/two_nano_bench/README.md).
Ресурси Timer1, input capture, обмеження ISR та часовий розрахунок:
[ONE_WIRE_DRIVER.md](ONE_WIRE_DRIVER.md).
Команди двопортової перевірки: [TWO_NANO_ACCEPTANCE.md](TWO_NANO_ACCEPTANCE.md).

## Шлях байтів і межі повторного використання

```text
ПК Session → BridgeClient → USB Nano №1
  → EndpointCore → TransactionEngine → PhysicalDlcPort → timer/GPIO driver
  → низьковольтна лінія
  → driver Nano №2 → ResponderEndpoint request parser → shared raw fixtures
  → driver TX → лінія → driver RX Nano №1 → Result USB
  → наявний DLC parser / decoder → partial Sample → Model / Recorder / GUI
```

USB Nano №2 передає тільки control/identity/diagnostics. Він не передає
готові RPM/ECT/TPS для оновлення моделі та не пересилає робочі DLC replies
між платами через ПК. Nano №1 не містить активного VirtualHondaEcu:
`EndpointCore` зберігає тільки посилання на фізичний DlcPort. У його sketch
немає `virtual_ecu.cpp`; відсутній peer означає timeout, без fallback.
Старий `BridgeEndpoint` M2b окремо володіє VirtualHondaEcu і зберігає свої
golden frames, identity та поведінку. M1 не використовує новий драйвер.

Спільні `reference_fixtures.hpp` — єдине джерело raw A/B/Boundary для M2b
і M3a. Responder повторно використовує `TransactionEngine::allowed`.
Initialization залишається точною 11-byte послідовністю; ECU HELLO/ACK
не вигадується. Після неї responder мовчить і допускає читання після300ms.
Whitelist: RPM `20 05 00 02 D9`, ECT `20 05 10 01 CA`,
TPS `20 05 14 01 C6`. Довільні bytes від USB не проходять у лінію.
Правильний CRC власної USB-оболонки не скасовує whitelist.

Приймач зберігає фактичний MCU timestamp stop-bit sample разом із байтом.
Timestamp не є часом запланованої virtual-події. Adapter переводить вік
цього byte з0.5µs clock до millis за одночасним snapshot обох clocks.
У desktop абсолютний Nano clock не віднімається від host clock.
`Sample.time` — host receipt; `freshnessSince` — host request-start lower bound.

## USB identities і версія2

Outer envelope залишається власним envelope v1 `A5 5A`, session/request IDs
і CRC16 з [NANO_DLC_BRIDGE_PROTOCOL.md](NANO_DLC_BRIDGE_PROTOCOL.md).
Ці IDs не вставляються у фізичні DLC bytes. Payload protocol/policy обох
нових endpoint дорівнює2, firmware1.0.0. Старий virtual endpoint приймає
тільки свою exact v1/policy1 identity; strict handshake не послаблено.

| Пристрій | Identity | Backend | Capabilities |
| --- | --- | ---: | ---: |
| Nano №1 bridge-bench | `hondadash-dlc-bridge-bench-v1` | 2 | 15 |
| Nano №2 responder-bench | `hondadash-dlc-responder-bench-v1` | 3 | 24 |

Capability bits protocol2: bit0 whitelist execution, bit1 raw Result,
bit2 bridge diagnostics, bit3 low-voltage bench I/O, bit4 responder USB control.

HELLO command0x30 payload `[2,2]`. HELLO_INFO0xB0 має:
`version,policy,backend,bench_io_enabled=1,capsLE16,generationLE32,state,`
`firmwareMajor,minor,patch,identityLength,identityASCII`.
У protocol2 byte3 означає **bench_io_enabled**, а не старе
physical_dlc_enabled. Vehicle connection залишається forbidden.
Identity є твердженням firmware, не криптографічним доказом або доказом
фізичного підключення саме другої Nano.

ACK0xB1 / ERROR0xBF: `[2,generationLE32,command,status]`.
Фрагментація, finite buffers і duplicate/id-conflict правила збережено:
повтор того самого outer ID віддає cached ACK/result і не повторює TX.
Це не дозволяє desktop повторювати невизначений EXECUTE під новим ID.

Bridge-bench:

| Command | Payload | Дія |
| --- | --- | --- |
| NEW0x31 | `[2,peerGenerationLE32]` | Окрема bridge boundary після drain/idle; без TX |
| INIT0x32 | `[2]` | Точні11 wake bytes, потім300ms, без ECU ACK |
| EXECUTE0x33 | `[2,expectedSize,exactRequest5]` | Одне з трьох whitelist читань |
| ABORT0x34 | `[2]` | Відпустити локальний TX, зберегти late RX |
| CONFIG0x35 | — | Завжди PolicyDenied; raw fixtures належать responder |
| DIAG0x36 | `[2]` | Власні MCU counters, без DLC TX |

Responder-bench:

| Command | Payload | Дія |
| --- | --- | --- |
| QUIESCE0x40 | `[2]` | Заборонити нові replies; ACK після завершення started TX |
| ARM0x41 | `[2,exactGenerationLE32]` | Дозволити parser поточної generation; без TX |
| CONFIG0x35 | `[2,scenario,fault,delayLE16,gapLE16]` | Вибрати raw A/B/Boundary та один fault |
| DIAG0x36 | `[2]` | Стан/лічильники; готових вимірювань немає |

Scenario0/1/3 — A/B/Boundary. Fault0..9 збережено: none, silent, delay,
gap, header, length, checksum, truncation, noise, trailing. Delay/gap≤10000ms.
Fault одноразовий: споживається лише після правильного whitelist read,
ніколи не пошкоджує USB ACK, необхідний для відновлення. CONFIG не змінює
вже початої/запланованої відповіді. Керування не потрапляє в DLC лінію.

## Точна процедура нового експерименту

1. Desktop перевіряє **обидві** strict identities на двох явно заданих,
   різних портах. Wrong/missing endpoint завершує перевірку до INIT/EXECUTE.
2. ABORT Nano №1 зупиняє нові запити, локальний передавач відпускає шину.
   Старі RX продовжують дренуватися в Event/trace; peer не очищується.
3. QUIESCE через USB Nano №2 забороняє нові replies. Відкладена відповідь,
   передавання якої ще не почато, скасовується й рахується в `canceled`.
   Уже почата відповідь завершується фізичним stop bit; до цього ACK немає.
4. Responder збільшує generation й повертає ACK. Desktop чекає щонайменше
   20ms, дренуючи bridge. NEW bridge додатково вимагає порожні RX/late queues
   та щонайменше10ms спостереженої вільної HIGH шини; стан RX/TX/resync
   драйвера теж має бути idle. Це software-перевірка, не вимірювання аналізатором.
5. Desktop передає acknowledged responder generation у bridge NEW. Bridge
   перевіряє ненульову нову generation, локально очищує тільки engine/fault
   і ACK-ає. Відповідач залишається quiescent; NEW не може видалити його events.
6. Desktop ARM-ить exact responder generation. Після ACK налаштовує A/B/fault
   і тільки тоді дозволяє bridge INIT та штатний scheduler.

Generation comparison діє в межах одного USB binding. Новий перевірений
HELLO bridge дозволяє повторну процедуру після reset лише responder, чий
лічильник починається наново. HELLO не очищує лінію/peer. Після reset будь-якої
плати потрібні повторні identity checks, QUIESCE, drain, NEW, ARM та INIT.
Ця процедура керує нашим лабораторним responder; вона нічого не доводить
про recovery заводського ECU. Переданий peerGeneration — твердження
координатора, а не міжплатна криптографічна перевірка.

Після timeout/checksum/trailing/framing/overflow/line/collision error
TransactionEngine фіксує fault і припиняє опитування. Пізній ECT не може
стати TPS: response не має address/host ID, тому новий запит заборонений
до явної boundary. ABORT і parser reset не очищують external peer.
Reset MCU програмно не доводить фізичне звільнення GPIO при зависанні;
електричний reset-state окремо обґрунтований схемою.

## Діагностика та часовий бюджет

RESULT/Event layouts збережено з payload version2. RESULT `txLength`
фізичного backend рахує лише байти, чий stop bit завершився, за окремим
modulo16 completion counter. Незавершений byte при collision/ABORT не
видається за повністю переданий; error позначає можливу часткову передачу.
`txElapsed` — millis від початку engine до спостереженого physical TXdone,
`responseElapsed` — від TXdone до terminal decision. Main-loop квантованість
залишається видимою. Сирі RX включають bytes перед framing/checksum failure;
приймач передає error окремо від порожньої черги.

Нові Status20/21/22/23: DlcFraming, DlcRxOverflow, DlcLine, DlcCollision.
Інші codes незмінні. Власне echo перевіряється драйвером по бітах і не
вставляється в RX. Жодні «перші N байтів відповіді» не відкидаються.

Bridge DIAG має52bytes: старі26 полів зі source MCU, peerGenerationLE32
на offset26, потім11 saturatingLE16 driver counters. Responder DIAG60bytes:
version/gen, armed/quiescing/initialized/replySize/pendingRx до offset10,
7LE32 counters requests/replies/canceled/lineErrors/parserErrors/queuedTxBytes/
txOverflows до offset38, потім ті самі11 driver counters. Їх порядок:
rxBytes,txBytes,echoBytes,falseStarts,framing,rxOverflow,txOverflow,stuckLow,
collisions,timing,lineBusy. `queuedTxBytes` не називається completed TX.
Це дані MCU, не логічного аналізатора.

Збережено повне200ms observation window. Фізичний read TX містить50bits,
розрахунково5.2ms при208 timer ticks/bit; initialization110bits≈11.44ms.
Тепер ці витрати входять у measured firmware duration. M2c slot320ms і
stale/hide1400/4200,2100/6300,1400/4200 не змінюються автоматично.
Порушення бюджету показується через achieved rate/age/Stale, без прихованого TTL.
Ці розрахунки та bit-level model не є фізичним timing measurement.

## Програмна перевірка та hardware scope

`bench_embedded` компілює ті самі endpoint/engine/driver C++ sources і
використовує event-driven wired-AND bit line: кожен драйвер тягне LOW або
відпускає, HIGH створює модель підтяжки. Перевіряються identities, whitelist,
A/B/Boundary, фізична тривалість TX в моделі, fault/no further TX, late ECT,
QUIESCE під час TX, delayed reply cancellation, reset лише bridge/peer,
відсутність fallback, timer wrap, collision→engine fault і release.
Драйверні256-byte/skew/framing/overflow тести описані в ONE_WIRE_DRIVER.
Наскрізні desktop тести повторно використовують Client/Session/decoder/Recorder.

Модель не є SPICE або instruction-level AVR simulator. AVR build і static
stack estimates не підтверджують physical ISR latency, USB UART overrun,
stack high-water, HIGH/LOW, фронти, струм або роботу двох реальних Nano.
Обладнання, USB-chip, аналізатор і конкретний ECU не визначаються з коду.
Ручний bring-up/checklist знаходиться в hardware README та acceptance docs.

### Статична оцінка стеку

Pinned avr-g++7.3.0 diagnostic non-LTO `-Os -fstack-usage` звіти зберігаються
поряд із `.map`, ELF symbols і disassembly. Для перевірених поточних функцій
bridge найдовший помітний шлях publication при boundary:
receive26 + dispatch94 + publishResult120 + reply97 + enqueue13 = **350bytes**.
Responder: receive18 + dispatch91 + tick29 + finishQuiesce13 + ack30 +
reply97 + enqueue13 = **391bytes**. Це суми diagnostic frames, а не виміряні
піки deployed LTO firmware. Конкретні числа можуть змінитися після compiler
або source revision; належить користуватися `.su` відповідного artifact.

Окремий запас **256bytes** закладається для sketch/main, virtual adapters,
Arduino core, library helper calls і переривання; це припущення оцінки,
а не доказ верхньої межі. Timer compare diagnostic chain18 + Driver::timer19
+ pushRx4 + increment2 =43bytes; ISR за замовчуванням не вкладені. У результаті
попередні planning estimates bridge606 і responder647bytes порівнюються
з `2048 - (.data+.bss)` у size report. Hardware stack high-water і найдовший
фактичний ISR latency лишаються NOT VERIFIED. Не трактувати static reserve
як заміну вимірюванню на двох платах під одночасним USB/DLC навантаженням.

## Команди складання, явного upload і acceptance

З кореня репозиторію, Windows PowerShell, CLI1.2.2/AVR Boards1.8.6:

```powershell
.\scripts\build-firmware.ps1 -Firmware all
# Для першого встановлення зафіксованих build tools: додайте -Bootstrap.
.\scripts\build-windows.ps1 -QtPath .\path\to\Qt\6.8.3\msvc2022_64 -Configuration Debug,Release
```

`all` складає synthetic/bridge-lab/bridge-bench/responder-bench для обох
`arduino:avr:nano:cpu=atmega328` і `arduino:avr:nano:cpu=atmega328old`.
Збірка не відкриває портів. Кожна папка `build/firmware[-VARIANT]/CPU`
містить plain `.ino.hex`, ELF, map, memory/symbol/stack/disassembly reports.
Не використовуйте `with_bootloader.hex`; EEPROM/fuses/bootloader не змінюються.

Наступні приклади **не виконано**. Власник має явно вибрати правильний
порт і варіант кожної Nano, спочатку без сигнального з'єднання. COM7/COM8 —
приклади, не виявлені пристрої. `-WhatIf` перевіряє вибір без upload:

```powershell
.\scripts\upload-firmware.ps1 -Firmware bridge-bench -Port COM7 -Fqbn arduino:avr:nano:cpu=atmega328 -WhatIf
.\scripts\upload-firmware.ps1 -Firmware responder-bench -Port COM8 -Fqbn arduino:avr:nano:cpu=atmega328old -WhatIf
```

Для свідомо дозволеного реального upload власник виконує відповідну
команду без `-WhatIf`. GUI/acceptance ніколи не викликають upload helper.
Після окремої USB перевірки, монтажу без живлення та hardware-checklist:

```powershell
.\build\windows\Release\HondaDashBridgeCheck.exe --backend two-nano-bench --bridge-port COM7 --responder-port COM8 --report .\build\acceptance-two-nano
```

Linux, з кореня репозиторію:

```sh
bash scripts/build-firmware.sh --firmware all
bash scripts/upload-firmware.sh --firmware bridge-bench --port /dev/ttyUSB0 --fqbn arduino:avr:nano:cpu=atmega328 --dry-run
bash scripts/upload-firmware.sh --firmware responder-bench --port /dev/ttyUSB1 --fqbn arduino:avr:nano:cpu=atmega328old --dry-run
./build/linux/HondaDashBridgeCheck --backend two-nano-bench --bridge-port /dev/ttyUSB0 --responder-port /dev/ttyUSB1 --report ./build/acceptance-two-nano
```

Підставте власні порти; `--dry-run` не відкриває їх. У Windows ZIP console
розташована поруч із HondaDash.exe; запустіть її з розпакованої папки.
Фактичний upload/USB/line acceptance цього етапу: **NOT VERIFIED**.
