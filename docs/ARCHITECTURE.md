# Архітектура M0/M1, M2a/M2b/M2c та M3a

M3a ін'єктує `bench::Controller` у той самий `dlc::Session`. Controller володіє
bridge Client та незалежним USB control channel responder; обидва Transport
належать застосунку. Core/session/recording залишаються C++20 без Qt.
Суворі v2 identity/backend/capabilities відокремлені від незміненого v1 handshake M2b.

Шлях даних: `Session → Controller → Client → USB bridge → EndpointCore →
TransactionEngine → PhysicalDlcPort → Timer1 driver → wire → responder parser →
shared raw fixtures → wire → bridge Result → existing DLC parser/decoder → Model/Recorder`.
USB responder не має API для постачання вимірювань. Bridge-bench не володіє
VirtualHondaEcu. Native integration tests використовують ті самі endpoints і
driver, з open-drain бітовою моделлю замість GPIO; це не фізична осцилограма.

Зовнішнє recovery: stop bridge → responder QUIESCE (після TX-complete) →
нова generation → quiet/drain bridge → NEW(generation) → ARM(generation) → INIT.
USB/line timeout або неоднозначний EXECUTE блокує polling; автоматичного повтору немає.
Сигнальні байти не містять host request ID. Новий експеримент спеціальний лише для
власного лабораторного responder, не процедура відновлення заводського ECU.

Recorder v3 з `bench_schema_version=1` окремо зберігає два USB-рівні,
MCU-reported TX/RX/errors, boundaries, decoded partial samples та політику Model.
M2c цикл 320 ms, guard 200 ms і per-channel stale/hide збережені без автоподовження.
`physical_dlc_enabled` залишається старому virtual backend; bench має
`bench_io_enabled=true`, `vehicle_connection_allowed=false`, обидві identity/версії/порти.
Деталі: [контракт](TWO_NANO_BENCH.md), [драйвер](ONE_WIRE_DRIVER.md),
[схема](../hardware/two_nano_bench/SCHEMATIC.md).

M2a додає незалежну стандартну C++20 бібліотеку `hondadash_dlc`:
`dlc::Session → encoder → OfflineLink → ScriptedHondaEcu → fragmented RX →
parser → profile decoder → Model → widgets/Recorder`. M2b відокремлює
Session від OfflineLink через вузький `dlc::Link`; default constructor
залишає M2a offline. Ін'єкція `bridge::Client` додає тільки розпізнаний
virtual bridge, не довільний serial tunnel. Спільними
лишаються optional measurements, RawEvent, widgets і bounded recording.
Деталі: [HONDA_DLC_PROTOCOL.md](HONDA_DLC_PROTOCOL.md).

Шлях M2b: `dlc::Session → bridge::Client → outer codec → Transport →
BridgeEndpoint → TransactionEngine → DlcPort → VirtualHondaEcu → Result →
outer parser/identity/context checks → M2a parser/decoder → Model/Recorder`.
NativeTransport компілює ті самі embedded sources, що AVR. SerialTransport
обгортає тільки USB. DLC engine не залежить від Qt/host clocks; фізичного
DlcPort у M2b немає. Деталі: [NANO_DLC_BRIDGE_ARCHITECTURE.md](NANO_DLC_BRIDGE_ARCHITECTURE.md).

Sample має updatedMask (усі канали за замовчуванням для M0/M1), source,
reasons та політику числового діапазону. Незмінені канали не змінюють
lastValid/quality/host IDs. DecoderValidated допускає скінченні значення
поза demo-шкалою. Графік має окрему bounded історію кожного каналу; пропущений
bit не додає старого значення як нове. M2a recording v3 явно додає partial
semantics і per-channel ages; формат synthetic v2 збережено.

Нижче описано збережений synthetic шлях M0/M1, з його власним HELLO/CRC/IDs.
Ці правила не переносяться в Honda DLC.

Настільний C++20 Session не залежить від Qt, Windows API або Emulator.
Він отримує Transport за посиланням; transport живе довше за Session.
Усі операції сесії/транспорту виконує один потік. Qt SerialPort 6.8.3
належить тільки бібліотеці hondadash_serial; UI використовує Widgets.

```mermaid
flowchart LR
  Controls[Керування UI] --> Session
  Session --> Encoder
  Encoder --> Transport
  Transport --> Memory[InMemoryTransport / Emulator]
  Transport --> USB[QSerialPort / Nano]
  Memory --> Parser
  USB --> Parser
  Parser --> Session
  Session --> Decoder
  Decoder --> Model
  Model --> Dashboard
  Session --> Recorder
```

## Стан і часові межі

Stopped → Opening → BootWaiting → Handshaking (HELLO + INFO) → Running.
Невідновна помилка переводить у Faulted і закриває transport. Припинення
не запускає інший порт або автоматичну програмну симуляцію.

USB UI задає bootDelayMs=2000; in-memory — 0. handshakeDeadlineMs=5000
охоплює відкриття, запуск та обидві перевірки. HELLO має до трьох спроб,
без повторного відкриття порту. timeoutMs=300 — бюджет відповіді одного
запиту, pollMs=100 — інтервал snapshot. txTimeoutMs=500 починається від
прийняття кадру локальною TX-чергою. Response timeout починається лише
після спорожнення власної черги та Qt bytesToWrite; це не ACK пристрою.
TX timeout завершує сесію, тому недовідправлений кадр не перекривається
наступним. Timeout snapshot дозволяє наступне опитування; timeout control
або INFO завершує сесію з видимою невизначеністю стану команди.

Один pending request спільний для HELLO, INFO, controls та READ. Чотири
слоти desired state об'єднують часті зміни до останніх значень; revisions
зберігають зміни, що прийшли під час очікування ACK. Controls чергуються
з належними snapshot, щоб не витісняти опитування. ACK не є вимірюванням.
Непідтримувані capability команди не відправляються; UI їх вимикає.

Stop/reset очищує parser, pending та модель до NoData. Start закриває
старий transport і створює нову identity. Callbacks мають generation і
weak lifetime guard; callback попереднього запуску або знищеного Session
не діє. SerialTransport додатково створює новий QSerialPort на кожен Start.

Session ID — ненульовий 32-bit лічильник із випадковим початком процесу
(std::random_device); генератор ін'єктується в тестах. Це зменшує ризик
збігу після перезапуску, але не є криптографічним доказом або абсолютною
гарантією у просторі 32 bit. Повтор попередньої identity/нуль відхиляється.
Request ID зростає без wrap; вичерпання вимагає перепідключення.
Error 3 після reset endpoint викликає нову identity та HELLO на відкритому
порту; підтверджені одноразові faults повторно не озброюються.

## Обмеження та serial I/O

Serial: 115200, 8N1, no flow control; власний TX ≤256, Qt TX ≤128 байтів,
Qt RX buffer ≤4096. Один write на pump, RX до1024 байтів за callback,
фрагменти ≤256. Частковий/нульовий write продовжується асинхронно через
bytesWritten/таймер; blocking wait, sleep/processEvents відсутні.
Помилки параметрів, доступу, переповнення й I/O видимі та закривають порт.
DTR установлюється один раз після відкриття, RTS=false; це може reset Nano.
Відсутність modem control на PTY має окрему діагностику; політика DTR
не гарантує однакового reset для всіх USB-перетворювачів.

InMemoryTransport окремо володіє Emulator і фрагментами доставки; межа256
фрагментів, переповнення завершує сесію. Він також реалізує M1 controls
і duplicate policy. Embedded Endpoint використовує фіксовані буфери79/158,
без heap; його endpoint.cpp збирається C++11 для AVR і для host tests.

## Модель, UI і журнал

Model та прилади M0 збережено: сім optional каналів, NoData/Valid/Stale/
Unsupported/Invalid, stale після1000мс, приховування після3000мс. UI
показує лише прийнятий snapshot; графік обмежений і з розривами без даних.
Час firmware millis() і монотонний час ПК мають різні початки; timestamp
вимірювання — час отримання на ПК. Системний час потрібен лише журналу.

Recorder має bounded256 worker queue; disk I/O поза GUI. Переповнення,
open/write помилки видимі. UTF-8 CSV та JSONL з decimal point незалежним
від locale. Порожнє value означає відсутність; нуль залишається числом.

Format_version=2, app_version=0.3.0, source=simulation завжди, навіть USB.
Метадані: profile, scenario/seed на початку запису, transport(in-memory/
serial), endpoint, firmware, port, baud, created_unix_ms, clock, units.
Початок запису до handshake має endpoint=unrecognized; ready event згодом
фіксує розпізнану identity. Зміни controls видно у TX/ACK raw events.
TX_QUEUED — намір Session передати кадр; TX — точні байти, прийняті I/O;
TX_DRAINED — черги спорожнені; RX — всі фрагменти до parser, включно
з пошкодженими. ACK пристрою залишається окремою протокольною відповіддю.
