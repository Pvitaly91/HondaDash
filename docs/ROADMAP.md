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

## M3b — design readiness

Є [одна схема кандидата](../hardware/protected_dlc_interface/README.md):
ISO7721F, окрема батарея/TPS709, protected open-drain TX і comparator RX.
Pin-netlist/BOM/SVG звіряються автоматично; арифметика й ngspice42 мають
позитивні та навмисно негативні чисельні перевірки. Межі лінії — проектні
припущення, не специфікація невідомого Honda ECU. Дизайн готовий до review
та підготовки стенда; затримка компаратора, витоки, паразитні ємності,
температура й off-state залишаються вимірюваними умовами приймання.
Застосунок/чотири firmware/whitelist/200ms guard не змінюються.

## M3b — physical electrical validation

**NOT VERIFIED.** Потрібні виготовлена/перевірена плата, реальні рівні,
струми, фронти, затримки, нагрівання, off/reset/power-sequence, ізоляція
та визначені fault cases з post-check. Функціональний стенд A з двома
Nano і стенд B без responder/ПК мають різні цілі. Дві USB на одному ПК
не доводять ізоляцію. Vehicle qualification не випливає з лабораторного
PASS. Arbitrary battery faults, load dump, EMC/ESD — поза поточним envelope.
12V/VIN/ECU у базовому M3a залишаються забороненими.

## M3b.1 — fail-safe revision B / production driver

Кандидат B містить незалежний timer, arm/fault latch, release qualification,
debounced кнопку, field supervisor та окремий ізольований USB power-good.
Поява живлення/D3 HIGH/короткий LOW/утримана кнопка не є re-arm. Виправлення
17ticks stop-settle має baseline контрприклад і production state-machine tests;
електричні traces та closed-loop feedback перевіряються окремо від AVR static
instruction paths. Firmware/sketches залишаються чотири, identities/whitelist/
polling/freshness/200ms unchanged. Ця ревізія готовиться до фізичної перевірки,
а не PCB виробництва чи ECU session. **Cutoff, реальний обмін, фронти та ізоляція
NOT VERIFIED. Negative DC/off-state OPEN**, жодного загального protection PASS.

## M4 — конкретний справжній ECU на стенді

**NOT VERIFIED; вхідні дані ECU невідомі.** [M4_READINESS](M4_READINESS.md)
фіксує відсутні номер/ревізію, живлення/маси, діагностичний pin, електричні
характеристики, незалежне порівняння й політику stop без auto-recovery.
P07/P1G не припускаються; повне відновлення ECU firmware не є передумовою.

Спочатку bench ECU з підтвердженим electrical adapter і профілем,
потім контрольована перевірка на конкретному автомобілі. Порівнювати
прийняті дані з незалежним засобом діагностики, документувати модель
ECU, firmware, частоту, latency й відмови. Не переносити обіцянку 10 Гц
із симуляції на обладнання.

Запис у ECU, прошивання, очищення DTC, Android і складний редактор
компонування не входять до M2a та не додаються побічно до цих робіт.
