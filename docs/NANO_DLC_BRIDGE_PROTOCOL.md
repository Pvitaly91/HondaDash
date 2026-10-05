# USB protocol мосту HondaDash M2b, version 1

Identity: `hondadash-dlc-bridge-lab-v1`, firmware `1.0.0`, backend `virtual=1`,
`physical_dlc_enabled=false`. Це власний протокол HondaDash, а не протокол Honda.
Він переносить незмінені DLC bytes вузького reference-профілю
`honda-dlc-kerpz-obd1-reference-v1`; його evidence та additive checksum описані у
[HONDA_DLC_PROTOCOL.md](HONDA_DLC_PROTOCOL.md) і
[HONDA_DLC_REFERENCE_PROFILE.md](HONDA_DLC_REFERENCE_PROFILE.md).
Checksum/handshake не підтверджують модель Nano, ECU або калібрування.

## Зовнішня оболонка

Використано формат M0/M1, але окремі identity, commands і семантику:

```text
A5 5A | envelope_version=01 | type:u8 | session:u32LE | request:u32LE |
payload_length:u8 | payload[0..64] | CRC16:u16LE
```

Максимум 79 bytes. CRC-16/CCITT-FALSE: polynomial `1021`, initial `FFFF`,
no reflection, xor-out `0000`; охоплює version..payload, без SOF і самого CRC.
Усі багатобайтові поля USB payload також little endian. Session/request ID
ненульові; це локальний контекст обгортки, не ID у відповіді ECU. `request=0`
зарезервовано для асинхронних Event. USB UART: 115200 8N1. Це не швидкість DLC.

Парсер потоковий, RX buffer79, TX ring158. Невалідний outer frame не запускає
команду й не є доказом внутрішнього DLC RX. M1 `HELLO=01` отримує Error, bridge
HELLO `30` не має значення M1 HELLO. Клієнт перевіряє exact identity, bridge
version1, firmware1.0.0, policy1, backend1, physical0 і capabilities до DLC operation.

## Команди

Кожне поле таблиці — byte, крім явно позначених LE16/LE32.

| Type hex | Операція | Payload | Термінальна відповідь |
| --- | --- | --- | --- |
| `30` | HELLO | `version=1, policy=1` | HelloInfo `B0` |
| `31` | NEW_EXPERIMENT | `policy=1` | Ack `B1` |
| `32` | INITIALIZE | `policy=1` | Result `B2` після TX +300ms |
| `33` | EXECUTE | `policy=1, expectedReplyLength, exactDlcRequest[5]` | Result `B2` |
| `34` | ABORT | `policy=1` | Ack `B1`; active operation також має Aborted Result |
| `35` | CONFIGURE | `policy=1, scenario, fault, delayLE16, gapLE16` | Ack `B1` |
| `36` | DIAGNOSTICS | `policy=1` | DiagnosticInfo `B4` |

Немає raw tunnel, arbitrary init script, фізичного backend selector, write/reset,
DTC або memory editor. Extra bytes, zero length, wrong checksum, unknown policy,
невідповідна expectedReplyLength і будь-яка інша адреса/довжина відхиляються
**до DLC TX**, навіть якщо outer CRC коректна. INIT надсилає тільки фіксовані
`68 6A F5 AF BF B3 B2 C1 DB B3 E9`. Для EXECUTE дозволено тільки:

| Читання | exactDlcRequest | expectedReplyLength |
| --- | --- | --- |
| RPM | `20 05 00 02 D9` | `05` |
| ECT | `20 05 10 01 CA` | `04` |
| TPS | `20 05 14 01 C6` | `04` |

Сума кожного DLC request/response modulo256 дорівнює нулю. CRC16 оболонки
та additive checksum DLC перевіряються незалежно. Bridge не декодує фізичних
величин і не додає адресу/ID/CRC до внутрішньої ECU-відповіді.

## Відповіді

`B0 HelloInfo`: `version, policy, backend, physical, capabilitiesLE16,
generationLE32, state, firmwareMajor, firmwareMinor, firmwarePatch,
identityLength, identityASCII[]`. Identity без NUL. Capabilities=7:
bit0 fixed init, bit1 exact read policy, bit2 virtual lab configuration.

`B1 Ack` і `BF Error`: `version, generationLE32, originalCommand, status` —
рівно7 bytes. Ack лише завершує control operation; він не є вимірюванням.
Помилка запиту без DLC TX не змінює попередню valid measurement.

`B2 Result` має fixed header17 bytes:

| Offset | Поле |
| --- | --- |
| 0 | bridge version=1 |
| 1..4 | generationLE32 |
| 5 | status |
| 6 | Initialize `32` або Execute `33` |
| 7 | read address; для init0 |
| 8 | read payload length; для init0 |
| 9 | фактична DLC TX length≤11 |
| 10 | фактична associated DLC RX length≤16 |
| 11..12 | txElapsedLE16: operation accepted → DLC TX complete/failed |
| 13..14 | responseElapsedLE16: DLC TX complete → terminal decision |
| 15..16 | maxGapLE16 між прийнятими associated RX bytes |
| 17.. | actual TX[], потім actual RX[] |

Actual TX містить лише прийняті DlcPort bytes, у тому числі частковий TX при
відмові. Actual RX містить пошкоджені/неповні bytes до втрати контексту;
наступні bytes передаються як unassociated Event. DLC context address/length
походить із whitelist-запиту мосту, не був прочитаний з ECU response.
Успішний Result повторно перевіряється desktop decoder/parser.

`B3 Event`: `version, generationLE32, sequenceLE16, status,
elapsedLE16, rxLength, rawRX[]`. Header11, raw≤16. Session поточного binding,
request0. Sequence починається з1 після NEW і зростає modulo65536; пропуск
позначає неповний trace. Elapsed — від NEW boundary до drain, saturating65535ms,
а не синхронізований clock. Status18 означає unassociated RX. Вони не створюють
вибірку і не підвищують свіжість. Event не приписує пізні bytes новому read.

`B4 DiagnosticInfo` (26 bytes): `version, generationLE32, state,
activeCommand, pendingDlcRx, pendingUsbLE16, dlcTxBytesLE32, parserErrorsLE32,
txOverflowsLE32, lostEventsLE32`. Counters saturate at UINT32_MAX. Reset firmware
скидає counters та binding; generation — лабораторне покоління в межах boot,
не глобальний унікальний ID.

## Стани, помилки та повтори

States: 0 NeedsExperiment, 1 ReadyForInit, 2 Initializing, 3 Ready,
4 Executing, 5 Faulted. Після boot/fresh HELLO потрібний явний NEW, потім INIT.
Fresh HELLO скасовує старий context, не відновлює polling; якщо був active
operation, його Aborted Result з фактичними TX/partial RX надсилається під
старим session/request перед новим HelloInfo. NEW може завершити
active operation як Aborted, створити новий VirtualHondaEcu experiment і
лише тоді повернути Ack з новим generation. CONFIG дозволений поза active
operation і зберігається через NEW; він не змінює whitelist. INIT/EXECUTE не
мають проміжного ACK: прийняття не плутається з terminal success.

| Status | Значення |
| --- | --- |
| 0 | Ok |
| 1 | Busy |
| 2 | BadCommand |
| 3 | BadPayload |
| 4 | NotBound |
| 5 | StaleRequest |
| 6 | PolicyDenied |
| 7 | NeedsNewExperiment |
| 8 | NotInitialized |
| 9 | DlcTotalTimeout |
| 10 | DlcInterbyteTimeout |
| 11 | DlcHeader |
| 12 | DlcLength |
| 13 | DlcChecksum |
| 14 | DlcTrailing |
| 15 | DlcTxTimeout |
| 16 | Aborted |
| 17 | Overflow: trace/result may be incomplete |
| 18 | DlcUnexpected / unassociated RX |
| 19 | IdConflict: last ID reused with different bytes |

Один pending operation. Bounded cache зберігає останній exact request frame
(≤79) і terminal reply (≤79). Exact duplicate pending EXECUTE не передає bytes
повторно й не пересуває deadline; completed duplicate отримує cached Result.
Менший request ID відхиляється, те саме ID з іншими bytes — IdConflict.
Нові IDs мають строго зростати в межах binding, wrap потребує нового session.
Клієнт не повинен автоматично повторювати невизначену операцію з новим ID.
Подальші control requests витісняють останній cache, без необмеженої історії.

DLC error/timeout/ABORT переводить engine у Faulted. Пізні RX продовжують
дренуватися. ABORT, HELLO та parser reset **не видаляють** queued virtual bytes.
Тільки NEW_EXPERIMENT створює явну лабораторну межу та відкидає старі events.
Це гарантія програмного backend, не спосіб доведеного recovery реального ECU.
ECT timeout → TPS тієї самої довжини не запускається; USB IDs цього не змінюють.

Переповнення TX queue скасовує engine, очищує недоставлені outer messages,
збільшує txOverflows/lostEvents та повертає видимий Error17. Це явна втрата
trace; такий Result не вважається частково успішним. Подальший DLC TX
заблокований до NEW. Дані не накопичуються нескінченно при повільному USB reader.

## Часові політики

Engine використовує uint32 monotonic milliseconds і unsigned subtraction;
робочі інтервали≤10000ms, коректні через millis wrap. Жодних sleep/delay.
Віртуальний byte-oriented DlcPort приймає TX bytes, окремо повідомляє txIdle.
Загальний DLC deadline відраховується **від DlcPort TX-complete**, а не USB
enqueue/Serial bytesToWrite0. Native tests перевіряють partial/zero-progress TX.

| Межа | Політика |
| --- | --- |
| DLC TX progress/completion | <100ms від початку embedded operation |
| Init | 300ms від завершення останнього init TX byte; ECU ACK не вигадується |
| Read total | 200ms від TX complete; byte на рівності deadline вже пізній |
| Interbyte | <50ms між bytes неповної відповіді; рівність означає timeout |
| Trailing observation | Після complete frame engine чекає решту повного200ms вікна |

Додаткові bytes у цьому вікні скасовують увесь result як DlcTrailing.
Об'єднаний фрагмент не має early return, який відкидає хвіст. Через observation
window максимальна сумарна лабораторна частота≈5 readings/s; частоти каналів
у GUI фактичні, бажаний scheduler interval не обіцяє 10Hz через цей backend.
Вікно є консервативною політикою HondaDash, а не доведеною максимальною
затримкою фізичного ECU. Tick після deadline не робить queued bytes своєчасними
заднім числом. Virtual DlcPort передає engine час запланованої byte-події на
власному device clock; тому gap60ms залишається помилкою навіть при доставці
всіх bytes одним tick або одним USB result. Це virtual timestamps, не
заявлена точність GPIO/UART hardware capture. MaxGap використовує ці події,
а terminal responseElapsed описує час рішення engine.

Час MCU і ПК має різні початки. Result переносить відносні durations, без
порівняння Nano millis із QElapsedTimer. USB fragmentation не є DLC interbyte
gap. Desktop має окремі boot/handshake/frame-send/result watchdog та обмеження
віку result; ці політики описані в
[NANO_DLC_BRIDGE_ARCHITECTURE.md](NANO_DLC_BRIDGE_ARCHITECTURE.md).

## Virtual ECU fixtures і faults

CONFIG scenario0=A, 1=B, 3=Boundary; changing scenario2 відхиляється. VirtualHondaEcu спочатку приймає bytes фіксованого INIT,
потім5 bytes запиту, сам перевіряє їх і вибирає static raw reply. Перетворень
RPM/ECT/TPS, host Sample або адреси каналу від BridgeClient тут немає.

| Scenario | RPM | ECT | TPS |
| --- | --- | --- | --- |
| A | `00 05 09 C3 2F` | `00 04 40 BC` | `00 04 58 A4` |
| B | `00 05 04 E1 16` | `00 04 20 DC` | `00 04 AE 4E` |
| Boundary | `00 05 FF FF FD` | `00 04 FF FD` | `00 04 18 E4` |

Fixtures reference-derived, не captured. Звичайна доставка — byte кожні2ms.
Fault0 normal,1 silent,2 delay-start,3 gap після другого byte,4 wrong header,
5 wrong length,6 checksum xor01,7 відсутній останній byte,8 prefix7E,
9 suffix7E. Delay/gap0..10000ms, використовуються тільки відповідними faults;
fault зберігається до CONFIG. Дефекти synthetic-fault, physical backend відсутній.

## Незалежні golden outer fixtures

Session `01020304`, request1 HELLO:
`A5 5A 01 30 04 03 02 01 01 00 00 00 02 01 01 B2 A0`.

Request2 NEW:
`A5 5A 01 31 04 03 02 01 02 00 00 00 01 01 64 BF`.

Request5 RPM EXECUTE:
`A5 5A 01 33 04 03 02 01 05 00 00 00 07 01 05 20 05 00 02 D9 F7 CC`.

Його Result для generation1, A, TX elapsed0, response window200, maxGap2:
`A5 5A 01 B2 04 03 02 01 05 00 00 00 1B 01 01 00 00 00 00 33 00 02 05 05 00 00 C8 00 02 00 20 05 00 02 D9 00 05 09 C3 2F 81 9E`.

Очікувані literals frozen у native tests; CRC незалежно звірений
`binascii.crc_hqx(data[2:],0xffff)` під час підготовки fixtures. Python не є
application або firmware runtime. DLC goldens окремо взяті з M2a таблиць,
не обчислюються production encoder у тесті очікуваної відповіді.
