# M2b: bridge core та virtual ECU

M2b — лабораторний шлях майбутнього адаптера. `hardware_verified=false`,
`live_enabled=false`, `backend=virtual`, `physical_dlc_enabled=false`.
USB до Nano не означає вимірювання автомобіля. Постійний напис GUI:
**ТЕСТОВИЙ МІСТ — ВІРТУАЛЬНИЙ ECU — ФІЗИЧНИЙ DLC ВИМКНЕНО**.

## Компоненти та повторне використання

```text
UI → dlc::Session → M2a encoder → dlc::Link / bridge::Client
    → outer USB encoder → NativeTransport або SerialTransport
    → той самий BridgeEndpoint C++11 → TransactionEngine → DlcPort
    → VirtualHondaEcu → fragmented inner RX → terminal Result
    → outer parser/context checks → M2a DLC parser → profile decoder
    → partial Sample → Model → widgets/chart/Recorder
```

`hondadash_dlc`, `hondadash_bridge`, model і recording — стандартний C++20,
без Qt/Windows API. `nano_bridge` компілює ті самі `endpoint.cpp`,
`transaction_engine.cpp`, `virtual_ecu.cpp` як C++11 для host tests і AVR.
Qt Widgets належить UI; QSerialPort належить існуючій transport-обгортці.
Simulation-only містить native bridge і M2a без SerialPort; build без GUI
та SerialPort взагалі не потребує Qt.

`dlc::Session` за замовчуванням володіє OfflineLink (збережений M2a).
Інший constructor приймає non-owning `Link&`; він має жити довше за Session.
Link виконує start/init, один exact read, abort, tick, raw/fault/received callbacks
та повідомляє, чи сам володіє DLC deadlines. Окремий LabControl задає raw
сценарій/fault. UI не записує чисел до Model. BridgeClient не декодує RPM/ECT/TPS:
він перевіряє USB identity/context і повертає справжні inner bytes до M2a parser.
Firmware не містить формул вимірювань, тільки raw A/B/Boundary fixtures.

`BridgeEndpoint` відокремлює outer framing/dispatch/cache від TransactionEngine.
Engine працює через вузький DlcPort (byte TX, TX-idle, byte RX, pending count),
у M2b реалізований лише VirtualHondaEcu. Немає physical backend або runtime
вибору GPIO. Non-owning interface має protected nonvirtual destructor, тому
AVR не підключає deleting destructor/heap. `.ino` тільки дренує Serial та tick.

## Дві часові області

MCU використовує uint32 millis з unsigned subtraction, ПК — монотонний uint64
Time. Їхні epoch не порівнюються. NativeTransport дає device clock окремий
offset; тести проходять wrap. Bridge Result переносить відносні TX elapsed,
response observation elapsed, max observed interbyte gap. Це факти engine,
не синхронізація clocks і не фізичний timestamp ECU. Virtual DlcPort повертає
час запланованого byte event у device domain: coalescing USB/host tick не
приховує virtual gap>=50ms. Engine спершу перевіряє total deadline за поточним
device time: прострочений poll не робить bytes своєчасними заднім числом.

Engine чекає DLC TX-complete, потім застосовує init300ms, total200ms,
interbyte50ms; guard DLC TX100ms. USB queue completion не запускає ці таймери.
Валідна відповідь спостерігається до кінця всього200ms вікна: будь-який хвіст
скасовує Result. Максимум близько5 reads/s сумарно; фактичні частоти видно
окремо для кожного каналу. Повний опис меж і рівності deadline —
[NANO_DLC_BRIDGE_PROTOCOL.md](NANO_DLC_BRIDGE_PROTOCOL.md).

Host settings окремі: open2000ms, boot0ms native/1500ms USB, TX500ms, handshake2000ms,
control acceptance1000ms, terminal result1500ms, max result age1200ms.
Watchdog спрацьовує на рівності; USB retry невизначеного EXECUTE відсутній.
Втрата результату не дає дозволу повторити DLC під новим ID.
Control acceptance стосується ACK для NEW/CONFIG/ABORT. INIT/EXECUTE не мають
проміжного ACK: ПК дізнається про їх виконання лише з terminal Result;
1500ms покривають очікування прийняття, embedded operation і доставку USB.
API `diagnostics()` показує відносні факти останнього Result. Протокольна
DIAGNOSTICS36 окремо повертає embedded counters; GUI не опитує ці counters
періодично й не видає локальні host counters за діагностику Nano.

`Sample.time` залишається фактичним host receipt time. Bridge додає optional
`freshnessSince=host request start`: це консервативна нижня межа acquisition,
яка не вимагає clock sync. Model використовує min(receipt,lower bound) для
lastValid/age; результат може одразу бути Stale. Після1200ms результат не
оновлює модель взагалі. Затримка USB з'їдає свіжість, а не подовжує її.
Default stale1000ms/hide3000ms збережено: ECT при scheduler1Hz і observation
window може коротко бути Stale навіть без дефекту. Це видима давність,
не помилка checksum; графік не дублює старий ECT під часовою міткою RPM.

## Життєвий цикл і неоднозначність

«Старт» відкриває transport та розпізнає exact bridge identity, firmware1.0.0, protocol1,
policy1, capabilities7, virtual backend і disabled physical DLC. Окрема кнопка
«Новий експеримент» посилає NEW, CONFIG, fixed INIT і тільки потім reads.
Відкритий COM, recognized bridge, init complete та accepted read — різні стани.
M1 і bridge взаємно відхиляють identity/commands. Автовибору порту або fallback
на native endpoint немає. Firmware reset потребує нового handshake та явного
експерименту; старі host callbacks/IDs/generation не дають нових вимірювань.
Публічний `Client::connect` завжди виконує тільки handshake. `Session::start`
є окремим явним запитом нового experiment; після нього executor завершує
NEW/CONFIG/INIT перед polling. Повторне відкриття NativeTransport саме по собі
не скидає embedded endpoint або queued virtual RX.

На MCU і host один активний request. Помилка DLC, timeout, trailing bytes чи
невизначеність зупиняють polling. ECU response не має адреси/ID: ECT timeout
не дозволяє TPS тієї самої довжини. ABORT, HELLO та parser reset залишають
queued virtual RX для drain/журналу. Лише explicit NEW замінює virtual ECU
і створює видиму boundary. Це лабораторна гарантія; recovery фізичного ECU
так не доведено.
Окремий reset firmware також скидає volatile ECU/link state й потребує
повторного handshake; його не можна трактувати як продовження старого experiment.

Duplicate pending frame не запускає DLC повторно і не пересуває deadline;
bounded cache останнього exact request/result відповідає completed duplicate.
Desktop duplicate/stale results журналює без model update. Невалідний outer CRC
залишається USB RX, без довірених inner facts. Sequence gap/overflow робить
втрату trace видимою і забороняє продовження експерименту.

## Буфери та запис

Embedded RX79, TX158, last request79/last reply79, associated RX16/late RX16;
virtual response queue фіксована. NativeTransport delivery capacity512 bytes,
desktop parser79, plot640 на канал, recording queue256. `.ino` обробляє до32
USB RX та до32 TX bytes за loop, engine виконує обмежену кількість byte steps.
Немає heap/String/exceptions або blocking delay у firmware. Slow reader/overflow
призводить до явної втрати trace/fault, без безмежного накопичення.

Recording v3 зберігає існуючий CSV header67 columns і partial sample semantics.
M0/M1 v2 та M2a v3 продовжують стару поведінку. Additive M2b metadata:
outer_protocol, bridge_identity/version, backend, read_policy_version,
physical_dlc_enabled=false, freshness_time_policy=host_request_start_lower_bound.
Профіль/evidence залишаються M2a; source завжди simulation, навіть з Nano USB.

JSONL розрізняє `usb_tx/usb_rx` (фактичний host transport) і `bridge_dlc_tx`,
`bridge_dlc_rx`, `bridge_result`, `bridge_event` (bridge_reported). Typed `bridge`
містить generation, operation, sequence, status, tx/rx_elapsed_ms, max_gap_ms.
Host time та host IDs не вважаються IDs внутрішнього DLC. Raw bad/truncated/late
RX також записуються; unassociated bytes не стають sample. `bridge_boundary`
відмічає NEW. CSV last_valid_ms/age_ms для bridge використовують lower bound;
JSON sample додатково має freshness_lower_bound_ms. Вже Stale, але прийняте
число записується із Stale, без втрати значення та без поновлення давності.
Формат v2 відхиляє typed bridge facts/partial samples видимою помилкою.

Код та fixtures написані для цього проєкту. M2a pinned evidence/license
limitations збережено; код або ресурси Hondash/HondaEcu не переносяться.
Залежності не змінено; наявні оригінальні license texts входять до artifacts.
