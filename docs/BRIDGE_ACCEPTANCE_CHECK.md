# M2c — повторювана перевірка virtual bridge

Цей документ описує незмінені `native`/`serial` backend. Окремий M3a
`two-nano-bench` використовує той самий Runner з двома обов'язковими портами
та зовнішнім QUIESCE/generation recovery: [TWO_NANO_ACCEPTANCE.md](TWO_NANO_ACCEPTANCE.md).

HondaDashBridgeCheck використовує той самий acceptance::Runner у CLI,
нативних тестах і Linux PTY. Шлях production:
dlc::Session → bridge::Client → Transport → BridgeEndpoint → VirtualHondaEcu
→ USB parser → DLC parser/decoder → Model → Recorder. Значення не підставляються
в модель. Native компілює ті самі C++11 firmware sources, що й Nano.

Утиліта входить у Windows ZIP. Звичайний запуск GUI не запускає acceptance.
Явна команда нижче дозволяє описані лабораторні NEW/CONFIG/INIT/READ/fault
операції. Немає сканування портів, upload, вибору firmware або фізичного DLC.

## Команди

Повністю розпакуйте пакет. У PowerShell із його папки:

    .\HondaDashBridgeCheck.exe --backend native --report '.\reports\native'
    .\HondaDashBridgeCheck.exe --backend serial --port '<ВАШ_COM>' --report '.\reports\serial'

Linux після складання:

    ./build/linux-local/HondaDashBridgeCheck --backend native --report ./build/acceptance-native
    ./build/linux-local/HondaDashBridgeCheck --backend serial --port '<ВАШ_PORT>' --report ./build/acceptance-serial

Замініть placeholder порту власним конкретним портом. Serial вимагає вже
встановленої bridge-lab firmware; M1 synthetic не підходить. Під'єднуйте лише
USB до однієї Nano без автомобіля, DLC або GPIO-проводів. Закрийте GUI/Serial
Monitor, які можуть тримати цей порт. Upload залишається окремою явною
операцією, описаною в [NANO_DLC_BRIDGE_TESTING.md](NANO_DLC_BRIDGE_TESTING.md).

Аргументи backend і report обов'язкові. Native не приймає port. Serial ніколи
не підставляє порт автоматично й не переходить на native при помилці.
Simulation-only збірка підтримує native; serial повертає помилку параметрів.
--help показує точний інтерфейс. Ctrl+C / SIGINT / SIGTERM скасовує check;
скасування завершує журнал і повертає окремий ненульовий код.

## Сценарій та межі

1. Відкрити тільки вибраний transport. Serial чекає 1800 ms boot allowance;
   це host policy, а не вимірювання часу boot конкретної плати.
2. Виконати лише HELLO. Production Client та runner перевіряють exact identity
   hondadash-dlc-bridge-lab-v1, firmware 1.0.0, protocol1, policy1,
   capabilities7, backend virtual та physical DLC disabled.
   До успішної перевірки NEW/CONFIG/fault не надсилаються.
3. Явний NEW, CONFIG A, INIT і штатні читання. Дочекатися прийнятих partial
   samples усіх трьох каналів: 750 RPM, 61 °C, 32% TPS.
4. CONFIG B через production протокол. Дочекатися нових прийнятих значень
   усіх трьох каналів: 1500 RPM, 89 °C, 75% TPS.
5. Почати **10000 ms** нормального спостереження на першому tick, коли всі
   три B-канали вже Valid. Первинний NoData та перехід A→B поза цим вікном;
   усередині вікна немає довільно вилучених проміжків. Будь-який Stale,
   Invalid або hidden у штатному вікні означає FAIL.
6. CONFIG контрольованої inner DLC checksum помилки. Дочекатися саме
   DlcChecksum; інша USB/DLC помилка не зараховується як очікуваний успіх.
7. Тримати transport відкритим і продовжувати tick/drain/raw recording.
   Перевірити відсутність наступних read intents, а потім Stale і приховування
   всіх трьох каналів. Жоден ABORT/reset не стирає late bytes.
8. Явно прибрати fault, вибрати A і почати новий experiment. Дочекатися всіх
   трьох нових A значень через parser/decoder.
9. Зупинити сесію, закрити transport, дренувати й закрити Recorder; перевірити
   помилки запису і записати JSON.

Кожна фаза, окрім спостереження, має 8000 ms deadline; загалом 45000 ms.
Runner перевіряє умови й керований монотонний час, а не «sleep і прочитати».
CLI має Qt event loop із tick2ms; точність пробудження ОС не гарантується.
Кінцеве закриття Recorder приєднує worker і перевіряє flush; це console
finalization, не GUI polling. Завислий системний disk I/O не має гарантії
завершення за logical watchdog, зовнішній CTest має окремий process timeout.

Policy bridge-weighted-cycle-v1 і bridge-bounded-age-v1 застосовуються
явно: RPM/TPS stale1400/hide4200ms, ECT stale2100/hide6300ms. На рівності
deadline значення ще Valid/видиме; перехід настає при age > threshold.
Обґрунтування бюджету — [POLLING_AND_FRESHNESS.md](POLLING_AND_FRESHNESS.md).
200ms guard збережено; throughput фізичного ECU цим не вимірюється.

## JSON та журнали

Папка report містить report.json: exit code/result, desktop version/SHA, OS,
вибраний backend/порт/baud, перевірені firmware identity/version/protocol/policy,
profile, межі й результати фаз, часові метрики, policy та thresholds, byte
counters, шляхи до raw.jsonl і measurements.csv. Recorder створює окрему
унікальну підпапку в report/journals; повторний check оновлює report.json,
зберігаючи попередні journals. До відкриття transport він записує RUNNING:
попередній PASS не залишається результатом перерваного нового запуску.
Лише завершений check записує остаточний PASS або FAIL.
Використовуйте окремі report directories для
порівняння окремих запусків. Шляхи підтримують Unicode; числа locale-independent.

Частоти рахуються за повним нормальним вікном, окремо для кожного каналу й
сумарно. Інтервали request/receipt, age at receipt, host request-to-result та
firmware operation мають n/min/median/p95/max. Percentile — nearest rank
ceil(p*n) із останніх не більш ніж 256 samples, median = p50. Для цих
розподілів count/min/max охоплюють усе вікно; retained показує розмір вибірки.
Scheduler delay походить із production Session, має окремо вказаний scope:
початковий experiment до завершення нормального вікна, останні 256 значень.
Порожній розподіл має null, а не вигадані нулі.

Sample.time — час приймання на ПК. Freshness залишається lower bound від
host request start; це оцінка давності обміну, а не timestamp фізичного сенсора.
Firmware operation — повідомлені відносні TX elapsed + terminal RX decision
elapsed. Протокол v1 не передає момент першого повного правильного payload:
time_to_correct_dlc_payload_ms=null, «не вимірюється». Абсолютні clocks Nano
і ПК не віднімаються. Model оновлюється синхронно в callback приймання.

Журнал містить actual USB TX/RX і окремі bridge-reported DLC факти. Навіть
невідповідний endpoint має bounded handshake trace; bad outer CRC не стає
довіреними inner bytes. Recorder queue256, handshake trace128, метрики256.
Overflow чи помилка диска означають FAIL. Після fault late RX продовжують
записуватися до явної межі нового experiment.

| Exit code | Значення |
| --- | --- |
| 0 | Усі програмні сценарії PASS |
| 2 | Неправильні/відсутні параметри або SerialPort відсутній у збірці |
| 3 | Endpoint/transport/identity чи неочікувана protocol-помилка |
| 4 | Phase/overall/client watchdog timeout |
| 5 | Acceptance assertion failure, зокрема втрата штатної свіжості |
| 6 | Помилка Recorder, queue або disk I/O |
| 7 | Явне скасування |
| 8 | Не вдалося створити/записати report |

Якщо запис report неможливий, stderr/exit8 є результатом; JSON не обіцяється.
Жодна з цих помилок не перетворюється на PASS.

## Автоматичні й ручні перевірки

CTest acceptance виконує той самий runner керованим часом із production
NativeTransport: успіх, wrong identity/M1, outer CRC, неочікуваний DLC
checksum, timeout, cancellation, Recorder/report failure та порушення
freshness-бюджету. Harness обмежено чекає Recorder worker поза tick; logical
time від цього не змінюється. acceptance_native_cli запускає справжній CLI
у реальному часі. Linux acceptance_pty використовує той самий runner,
production QSerialPort, kernel PTY та той самий embedded endpoint, а також
перевіряє відсутній і зайнятий explicit port. Старі M1/M2b PTY збережені.

Пакувальний тест запускає native check саме з розпакованого Windows ZIP
без developer Qt paths. CI публікує report і journals як test artifacts.
Модельний час, PTY real-time та фізичний USB мають різні measurement scopes;
їх не слід трактувати як ідентичні вимірювання.

Будь-який serial PASS може походити від PTY або іншого software peer.
hardware_verified=false, physical Nano, модель плати/USB-чип, фізичний
Reset, висмикування USB, electrical DLC та real ECU залишаються
**NOT VERIFIED**. Ці ручні кроки не симулюються і не позначаються виконаними.
