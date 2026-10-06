# M3a: перевірка двох Nano

`HondaDashBridgeCheck` використовує той самий `acceptance::Runner`, Session,
M2c scheduler, parser/profile decoder, Model та Recorder, що native/serial
перевірка M2c. Новий `bench::Controller` додає два незалежні USB control paths.
Дані приладів надходять виключно через сигнальну лінію responder → bridge.
USB responder передає identity, керування й діагностику; у цьому каналі немає
шляху оновлення Model. Відсутній peer не запускає virtual fallback.

Постійний статус: **СТЕНД: ДВІ NANO — ЕМУЛЯТОР ECU — НЕ ПІДКЛЮЧАТИ ДО АВТОМОБІЛЯ**.
Reference-derived дані залишаються `source=simulation`; GPIO bench увімкнений,
автомобільне підключення заборонене. Identity — заява firmware, не
криптографічна автентифікація і не електричний доказ складу стенда.

## Явний запуск

Спочатку виконайте bring-up зі [схеми стенда](../hardware/two_nano_bench/README.md).
Bridge Nano повинна мати `nano_dlc_bridge_bench`, responder Nano —
`nano_dlc_responder_bench`. Старі `nano_synthetic` і `nano_dlc_bridge_lab`
цілком окремі. Закрийте GUI та Serial Monitor перед явним upload або CLI check.
Команда check не прошиває, не reset-ить ECU, не шукає і не перебирає порти.

З повністю розпакованого Windows ZIP:

```powershell
.\HondaDashBridgeCheck.exe --backend two-nano-bench --bridge-port COM7 --responder-port COM8 --report .\reports\two-nano
```

Linux:

```sh
./HondaDashBridgeCheck --backend two-nano-bench --bridge-port /dev/ttyUSB0 --responder-port /dev/ttyUSB1 --report ./reports/two-nano
```

Порти в прикладах замініть двома власними явно визначеними портами. Звірте
роль кожної плати спочатку окремо через USB без сигнального з'єднання;
схему монтують і змінюють тільки без живлення. Не використовуйте VIN або
автомобільні 12 V. Дотримуйтеся обмежень живлення/під'єднання в hardware README.
Обидва аргументи обов'язкові; однакові порти, Windows COM aliases та
наявні filesystem aliases відхиляються. `--port` належить лише старому
`--backend serial`. Simulation-only CLI не має serial bench backend.

## Перевірки до передавання у лінію

Обидва USB UART працюють на 115200 8N1. Це не baud сигнальної лінії.
Обгортка A5/5A envelope v1 залишається та сама; payload protocol/policy
для M3a мають version2/policy2. Перевіряються всі поля exact identity:

| Роль | Identity | Backend | Capabilities | Firmware |
| --- | --- | ---: | ---: | --- |
| Bridge | `hondadash-dlc-bridge-bench-v1` | 2 | 15 | 1.0.0 |
| Responder | `hondadash-dlc-responder-bench-v1` | 3 | 24 | 1.0.0 |

Для v2 поле3 означає `bench_io_enabled=1`; старе `physical_dlc_enabled`
не переноситься з протилежною семантикою. M2b strict v1 handshake не змінений.
До успішних двох HELLO немає NEW, ARM, CONFIG, INIT або READ. Переплутані
порти, M1, M2b чи невідомі firmware завершують check з помилкою без line TX.

## Межа нового експерименту

1. Controller припиняє нові reads і надсилає bridge ABORT. Він звільняє
   власний TX, але не очищує responder або його відкладені відповіді.
2. Окремий USB QUIESCE responder скасовує відкладені відповіді та чекає
   завершення вже розпочатого фізичного TX, включно зі stop bit. Лише після
   цього ACK повертає нову ненульову generation.
3. Controller продовжує bridge drain щонайменше20ms; отримані пізні RX
   залишаються в trace без host read association. Модель не оновлюється.
4. Bridge NEW містить підтверджену peer generation. Firmware незалежно
   перевіряє дренований RX, помилки драйвера та idle HIGH щонайменше10ms.
   NEW не викликає reset зовнішнього responder.
5. Лише після NEW ACK controller надсилає ARM з точною peer generation.
   ARM ACK не створює DLC TX. CONFIG A/B/fault надходить лише через USB peer.
6. Після ARM та CONFIG ACK bridge отримує whitelist INIT, далі звичайні reads.

Generation перевіряється в межах чинного USB binding. Після reset будь-якої
плати потрібні повторні strict HELLO і повна межа експерименту. HELLO не
очищує queued RX. Це спеціальна процедура власного лабораторного responder;
вона нічого не доводить щодо recovery заводського ECU.

## Один Runner і сценарій

Runner виконує обидва handshake → явну межу → A (750/61/32) → B
(1500/89/75) → повне10000ms вікно нормальної свіжості → контрольовану inner
DLC checksum помилку → відсутність нових reads, drain, Stale й hide усіх
трьох каналів → явну межу відновлення → нові A → фінальний QUIESCE ACK і
закриття обох портів та журналу. Якщо final quiescence не підтверджена,
успішний сценарій не стає PASS. Ctrl+C скасовує check з окремим кодом.

Усі deadlines — host monotonic time; GUI/tick не використовує sleep або
busy-wait. Native/serial M2c зберігають стару семантику. Exit codes:
0 PASS,2 аргументи,3 endpoint,4 timeout,5 assertion,6 recording,7 cancel,
8 report write. Deadline фази8000ms, загальний45000ms; final control closure
має окремий bounded deadline.

Збережено guard200ms та M2c cycle320ms RPM/TPS/ECT/RPM/TPS,
stale/hide1400/4200ms для RPM/TPS і2100/6300ms для ECT.
Фізичний TX тепер займає ненульовий час. Report містить host
request→result і MCU-reported TX/RX terminal duration; over-budget показує
Stale/failure без прихованого підвищення TTL. Час першого правильного payload
не вимірюється чинним wire contract. Значення показників не є характеристиками ECU.

## Журнали й докази

`report.json` містить desktop SHA/version, дві firmware identity/version,
protocol/policy, порти, backend, firmware targets, source, scope, policies,
метрики й результати фаз. Additive recording v3 додає `bench_schema_version=1`,
`bench_io_enabled`, `vehicle_connection_allowed`, responder metadata.
Окремі raw kinds: `usb_tx/usb_rx` ПК↔bridge,
`responder_usb_tx/responder_usb_rx` ПК↔responder control,
`bridge_dlc_tx/bridge_dlc_rx` MCU-reported line bytes, `bridge_event`
unassociated bytes, `bench_boundary` і `bench_fault`.
MCU facts не називаються вимірюванням зовнішнього аналізатора.

`bench_integration_tests` використовує production Controller/Runner з двома
embedded endpoints, `PhysicalDlcPort` і тими самими driver state machines,
що AVR. Wired-AND event model формує HIGH підтяжкою логічної моделі; обидва
драйвери можуть лише LOW/release. Fixtures не пересилаються через ПК й
не підставляються в decoder. Це software bit-level model, не SPICE,
instruction-level AVR simulator або осцилограма Nano.

Без реального запуску окремо лишаються **NOT VERIFIED**: фізичний USB
кожної Nano, Nano/USB-chip identities, обмін двох плат, точність ISR,
HIGH/LOW і фронти, reset/disconnect за дозволеною схемою, реальний ECU.
Автомобільний електричний інтерфейс поза M3a. Software/bit-level/AVR PASS
не переводить ці статуси у VERIFIED.
