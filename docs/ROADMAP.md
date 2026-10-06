# Етапи HondaDash

## M0 — програмна емуляція

Наскрізний request/byte-stream/session/model/UI/recording шлях без
обладнання. Synthetic-demo-v1 є власним тестовим форматом. Досягнутий
обсяг не підтверджує Honda сумісність. Replay CSV/JSONL, розширені
оцінки продуктивності та складний редактор панелі залишаються окремими
можливими програмними завданнями.

## M1 — USB обмін із класичною Nano

Реалізовано незалежний Session, InMemoryTransport, асинхронний Qt
SerialPort та синтетичну firmware класичної Nano. Host firmware,
наскрізний Session і Linux PTY перевіряються окремо від AVR compilation.
Додано керування сценаріями/seed, ручними значеннями і faults через bytes.
Обидві Nano-конфігурації складаються; static SRAM budget1536 перевіряється.

Фізичний hardware status: **NOT VERIFIED**. Наступна апаратна перевірка —
одна Nano, тільки USB, за docs/NANO_USB_TESTING.md: manual values,
reset/disconnect/reconnect та журнал. Лише фактичний тест тим самим
Windows пакетом може підтвердити ПК ↔ USB ↔ Nano. Honda сумісності M1
не заявляє. Windows COM end-to-end без обладнання не підтверджено.
## M2a — досліджений протокол і offline implementation

Реалізовано незалежний C++20 Honda DLC encoder/parser/profile/session та
ScriptedHondaEcu. Whitelist трьох читань, RPM/ECT/TPS, per-channel freshness,
лабораторний GUI і recording v3 перевіряються незалежними reference-derived
fixtures. Evidence містить pinned revisions, походження форків і суперечності.
Hardware_verified=false, live_enabled=false; captures відсутні. Це не статус
«підтверджений Honda» і не перевірка P07/P1G/P28. M0/M1 збережено окремо.

## M2b — інтеграція з firmware-мостом Nano та програмні перевірки

Реалізовано ПК ↔ власна USB-оболонка ↔ C++11 bridge core ↔ virtual ECU.
Те саме embedded ядро працює нативно на ПК та компілюється для класичної
Nano; окрема firmware `nano_dlc_bridge_lab`. DLC Session повторно використовує
M2a encoder/parser/decoder, exact whitelist, partial model/recording.
Контракт має identity/version/policy, bounded buffers, власні MCU deadlines,
relative timing diagnostics та явний NewExperiment після неоднозначності.
ID оболонки не вставляється в ECU response. M1 firmware збережено окремо.

Фізичні Nano/USB: **NOT VERIFIED**. Virtual backend не має GPIO, D12 або
електричного DLC. Host/PTY/AVR tests не замінюють перевірку плати.
[Контракт і межі](NANO_DLC_BRIDGE_PROTOCOL.md),
[ручний PC/Nano чекліст](NANO_DLC_BRIDGE_TESTING.md).

## M2c — коректні polling/freshness і явна acceptance-перевірка

Bridge отримав окремий обмежений цикл опитування, per-channel політику
свіжості, довше вікно вимірювання частот і статистику latency. Причину
регулярного Stale ECT у M2b відтворено на production шляху протягом десяти
модельних хвилин; baseline/after зберігаються як машинні звіти.
`HondaDashBridgeCheck` явно перевіряє native або конкретний serial endpoint,
набори A/B, контрольований fault, старіння, drain та явний новий experiment.
GUI не запускає acceptance автоматично. Firmware/protocol/200ms guard
залишаються M2b. Деталі: [polling](POLLING_AND_FRESHNESS.md),
[acceptance](BRIDGE_ACCEPTANCE_CHECK.md).

Фізичний USB/Nano, reset/unplug і електричний DLC: **NOT VERIFIED**.
M2c сам не дозволяє фізичного DLC. M3a має окремий вузький bench backend.

## M3a — лабораторна фізична лінія між двома Nano

Дві окремі firmware, Timer1 open-collector driver, зовнішній responder із
спільними reference-derived fixtures, конкретна низьковольтна схема,
production GUI/acceptance та bit-level integration. Software/AVR перевіряються
окремо від обладнання. Фізичні USB обох Nano, обмін/рівні/фронти: **NOT VERIFIED**.
Критерій hardware completion — збережений A/B звіт через дві змонтовані Nano
та фактичні вимірювання за [checklist](TWO_NANO_ACCEPTANCE.md).

## M3b — окремий автомобільний електричний інтерфейс

Розробити й перевірити physical adapter: рівні напруг, захист, живлення,
земля, ізоляція/перетворення сигналів і поведінка при несправності.
USB-підключення Nano не доводить безпечності підключення до DLC/ECU.
Цей етап потребує іншої схеми та вимірювань захисту. Схема M3a не стає
автомобільною шляхом перейменування роз'єму. 12 V/VIN/заводський ECU для M3a заборонені.

## M4 — конкретний справжній ECU на стенді

Спочатку bench ECU з підтвердженим electrical adapter і профілем,
потім контрольована перевірка на конкретному автомобілі. Порівнювати
прийняті дані з незалежним засобом діагностики, документувати модель
ECU, firmware, частоту, latency й відмови. Не переносити обіцянку 10 Гц
із симуляції на обладнання.

Запис у ECU, прошивання, очищення DTC, Android і складний редактор
компонування не входять до M2a та не додаються побічно до цих робіт.
