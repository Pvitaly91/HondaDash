# M2c: розклад, давність і вимірювання

M3a повторно використовує цю фіксовану політику для зовнішнього стенда двох
Nano. Наведені нижче baseline/after вимірювання належать M2c virtual backend;
їх не видають за фізичні результати. Для bench окремі v2 identity/protocol,
реальна тривалість TX та hardware scope: [TWO_NANO_BENCH.md](TWO_NANO_BENCH.md).

Ця політика належить HondaDash. Вона не задає заводські інтервали датчиків,
не вимірює швидкість справжнього ECU і не підтверджує фізичну Nano.
Шлях даних незмінний: Session → BridgeClient → Transport → те саме embedded
ядро → VirtualHondaEcu → DLC parser/decoder → Model → Recorder/UI.
Firmware 1.0.0, USB protocol1, read policy1 і whitelist не змінено.

## Відтворення початкової проблеми

До зміни production-коду запущено M2b `1644a7296febb5d190e117237d6c5c7ab069d0b6`
із NativeTransport, справжнім embedded engine, ticks1ms і незалежним clock MCU.
Перші три канали заповнились на host917ms. Вимірюване вікно починається саме
там і триває600000ms; початковий NoData виключено, інших виключень немає.
Початковий файл: `build/reports/polling-baseline-original.json`. Його queue
показники були зняті після tick і не фіксували тимчасовий enqueue; відтворювані
нові звіти додатково вимірюють resident buffers у NativeTransport.

| M2b baseline | RPM | TPS | ECT |
| --- | ---: | ---: | ---: |
| Прийняті / Valid decoded | 1194 /1194 | 1194 /1194 | 597 /597 |
| Частота у вікні, Hz | 1.990 | 1.990 | 0.995 |
| Інтервал запитів min/median/p95/max, ms | 402/402/603/603 | 402/402/603/603 | 1005/1005/1005/1005 |
| Давність при прийманні, ms | 201 | 201 | 201 |
| Максимальна давність між прийманнями, ms | 803 | 803 | 1205 |
| Події Stale / тривалість, ms | 0 /0 | 0 /0 | 597 /122385 |
| Приховування | 0 | 0 | 0 |

Сумарно2985 транзакцій,4.975Hz; host request-to-result201ms;
timeout/fault/discarded0. Причина ECT Stale: новий ECT request починався через
1005ms, результат приходив ще через201ms, а lastValid зберігав консервативний
час попереднього request start. Поріг1000ms був коротшим за власний цикл
доставки. Коректна checksum цього не виправляє.

Після виправлення нормальний steady window починається на1155ms; для
ticks1/7/20/3/13ms та USB50ms — на1470ms. Обидва тривають600000ms.
У таблиці трійки каналів наведено в порядку RPM/TPS/ECT; latency —
min/median/p95/max. Це фактичні model-time результати, відтворені також у
Windows Debug; їх не прирівнюємо до real-time чи фізичного USB.

| Показник після M2c | Normal ticks1ms, USB0 | Ticks≤20ms, USB50 | Поза бюджетом USB600 |
| --- | --- | --- | --- |
| Вікно, ms | 600000 | 600000 | 60000 |
| Accepted / Valid, сумарно | 1875 /1875 | 1818 /1818 | 74 /74 |
| Сумарно транзакцій/с | 3.125 | 3.03 | 1.23333 |
| Частоти RPM/TPS/ECT, Hz | 1.25 /1.25 /.625 | 1.21333 /1.21167 /.605 | .5 /.5 /.233333 |
| Host request→result, ms | 201/201/201/201 | 264/264/280/280 | 801/801/801/801 |
| Firmware TX+terminal decision, ms | 201/201/201/201 | 204/204/220/220 | 201/201/201/201 |
| Максимальна давність RPM/TPS/ECT, ms | 1160/1160/1800 | 1275/1275/1935 | 3203/3203/4805 |
| Stale duration RPM/TPS/ECT, ms | 0/0/0 | 0/0/0 | 42000/42000/40500 |
| Приховування | 0/0/0 | 0/0/0 | 0/0/0 |

Timeout/fault/discarded0 в усіх трьох сценаріях. Поза бюджетом результат
801ms ще проходить maxResultAge1200ms, але дані чесно застарівають між
читаннями. ECT Stale у legacy policy при тому самому bounded jitter/USB50
становить264682ms; після M2c —0ms. Зменшення частоти явне: завданням є
стабільний чесний стан, а не throughput gain.

## Явна bridge policy

`bridge-weighted-cycle-v1`, version1, застосовується лише через
`dlc::bridgePollingSettings()` до `deviceTimed()` Link. M0/M1 і direct-offline
M2a зберігають старий розклад і1000/3000ms. Default `dlc::Session(client)` також
залишає legacy-поведінку для відтворення baseline; GUI і acceptance runner
явно обирають нову policy.

П'ять слотів по320ms: **RPM → TPS → ECT → RPM → TPS**, потім повторення.
RPM/TPS мають по два слоти, ECT один; це фіксована справедлива вага2:2:1.
Бажані середні інтервали800/800/1600ms, відповідно1.25/1.25/0.625Hz,
сумарно3.125 транзакції/с. Інтервали RPM/TPS чергуються640 і960ms;
ECT має1600ms. Це свідомий запас у чинній пропускній здатності.
Version1 приймає лише слот320ms; іншу тривалість не можна непомітно
поєднати з зафіксованими порогами цієї policy.

Одночасно активне одне читання, черги майбутніх запитів немає. Курсор
пересувається лише після успішної передачі операції executor. Невдалий
enqueue не позначає канал опитаним. Наступний слот відраховується від
фактичного початку попереднього: після пропущеного часу не буде catch-up.
Повільний транспорт відсуває слоти, зменшує achieved rate і збільшує age;
не додає запити, не змінює TTL і не витісняє ECT.

Для normal service budget максимум очікуваного start interval включає
960/1600ms плюс100ms допустимого накопичення tick jitter. Miss counter
порівнюється із цією межею, тому передбачений інтервал960ms не називається
провалом середньої цілі800ms. Якщо тест задає перевантажені цілі100/100/1000ms,
misses порівнюються із заданою меншою ціллю. Вага і обмеження слота лишаються
незмінними; requested і achieved показуються окремо. Політика не обіцяє
одночасні10Hz RPM,10Hz TPS і1Hz ECT.

## Часовий бюджет і пороги

Задокументований нормальний бюджет: інтервал виклику tick≤20ms і native USB
delivery delay≤50ms, звичайний virtual ECU, відсутність queue overflow.
Результат вкладається у320ms: наступний embedded tick≤20ms,200ms observation,
округлення рішення до tick≤20ms, delivery≤50ms і tick доставки≤20ms, разом≤310ms.
320ms залишає запас до слота. За п'ять слотів додаткове округлення початків
до tick≤100ms. Для serial це умовний бюджет, а не обіцянка планувальника ОС:
вихід за нього має чесно впливати на давність.

`bridge-bounded-age-v1`, version1:

| Канал | Максимальний плановий start gap | Наступний result budget | Tick reserve | Stale | Hide |
| --- | ---: | ---: | ---: | ---: | ---: |
| RPM | 960ms | 320ms | 100ms | 1400ms | 4200ms |
| TPS | 960ms | 320ms | 100ms | 1400ms | 4200ms |
| ECT | 1600ms | 320ms | 100ms | 2100ms | 6300ms |

Stale округлено вгору від суми бюджету; Hide дорівнює3×Stale і є кінцевою
політикою відображення. Необрані канали зберігають defaults1000/3000ms,
але reference-профіль позначає їх Unsupported. Перехід Stale при `age > stale`,
приховування при `age > hide`: рівність ще не прострочена, як у M0/M1.
Після fault старі дані переходять у Stale і ховаються за цими межами;
оновлення ACK, CONFIG, diagnostics чи інших каналів меж не пересувають.

Settings передаються конструктору Model/Session і не змінюються посеред
експерименту. Перехід режиму створює нову session/model; старі дані не
омолоджуються. UI і Recorder отримують ті самі ефективні per-channel thresholds.
Metadata v3 додає scheduler/freshness policy/version, requested intervals,
per-channel stale/hide, timing estimate source і measurement scope.

`Sample.time` завжди host receipt. `freshnessSince` — host request start,
консервативна оцінка давності обміну, **не доведений момент вимірювання ECU**.
Немає віднімання абсолютного Nano millis від clock ПК. `updatedMask` і
окремі lastValid не дозволяють RPM освіжити ECT. Нуль залишається числом.
Результат, прийнятий пізно, може одразу бути Stale; вік понад1200ms відхиляє
BridgeClient. `maxResultAgeMs` не збільшено, USB watchdog не став DLC deadline.

## Що вимірюється

`Session.metrics()` має cumulative accepted/valid/submitted/miss/stale/hide
counters та bounded distributions останніх256 спостережень. `observations`
показує загальне число, `n` — реально використану кількість≤256. Статистики
min/median/p95/max: nearest rank `ceil(p*n)`, median p=.5, p95=.95. Порожня
вибірка повертає null; singleton має рівні числа з явним n=1. UI не показує
правдоподібну частоту до двох прийнятих оновлень каналу.

Bridge `channelHz` і aggregate `responseHz` рахують accepted reads за bounded
вікно10s, з явною довжиною спостереження під час warm-up. Timestamp queues
обмежені512 на канал; M2a legacy залишає своє1s вікно. Один пакет оновлює
лише свій канал. Частота малювання стрілки не є частотою нових вимірювань.

Розрізняються:

- host request-to-result: від початку запиту ПК до прийнятого результату;
- firmware operation: reported TX duration + час до terminal decision;
- observation200ms: незмінена політика engine, а не час першого payload;
- receipt→model apply:0ms на дискретній шкалі caller, синхронний шлях;
- час до першого правильного DLC payload: **не вимірюється protocol v1**.

Queue metrics NativeTransport знімаються під час enqueue USB і між викликами
embedded receive/tick/read; це observed resident buffers, не вимірювання UART
фізичної Nano. Повні звіти тесту додатково зберігають max age, інтервали
початків/оновлень, age at receipt, schedule delay, fault/timeout/discarded.
Немає telemetry heap/queue в embedded firmware; код firmware не змінено.

## Відтворювані тести і звіти

```text
polling_tests --report-dir <directory>
```

CTest target `polling` створює `timing-reports/` у build directory:

- `baseline-normal.json`, `after-normal.json`: однакові ticks1ms, USB0,
  по600000ms після першого заповнення;
- `baseline-budget-jitter-usb50.json`, `after-budget-jitter-usb50.json`:
  однаковий повтор1/7/20/3/13ms і USB50, також по600000ms;
- `after-overload.json`: цілі100/100/1000ms, незмінна finite service policy;
- `after-outside-budget-usb600.json`: затримка понад normal budget,
  видимий Stale при ще допустимому віці result.

Тест використовує тільки production Session/Client/NativeTransport/embedded
decoder/model, без готових Sample чи прискореного відповідача. Device clock
починається біля uint32 wrap. Окремі assertions перевіряють delayed already-Stale
result, відхилення >1200ms, fault→no polling→hide, no-data після Stop/reconnect,
CONFIG ACK без оновлення, RPM без поновлення ECT, restart і bounded statistics.
Попередні bridge/bridge_embedded тести продовжують перевіряти checksum,
trailing bytes, late ECT→TPS, duplicates/old sessions та whitelist.

200ms observation залишається повним: не додається швидкого режиму або
скороченого guard. M2c виправляє часову узгодженість, а не заявляє прискорення.
Real-time/PTY/GUI acceptance наводиться окремо від model-time report.
Наступний M3 усе ще потребує електричного інтерфейсу та фізичних вимірювань.
