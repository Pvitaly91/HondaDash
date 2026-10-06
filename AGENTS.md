# HondaDash — правила проєкту

M3a дозволяє лише окремий low-voltage bench backend між двома класичними
Nano 5V/16MHz: bridge-bench і зовнішній responder-bench, без автомобіля.
GPIO/Timer1 дозволено тільки новим bench firmware; старі synthetic і
bridge-lab зберігають свої backend/identity. Обидві identity перевіряються
до line TX. Quiesce/generation зовнішнього responder, drain/free-line bridge
та явний NEW/ARM/INIT потрібні для нового experiment; bridge NEW/ABORT
самостійно не очищує peer. M3a metadata: bench_io_enabled=true,
vehicle_connection_allowed=false, source=simulation; legacy physical DLC
disabled не можна показувати для активного GPIO стенда. Hardware залишається
NOT VERIFIED до фактичного запуску. Базова схема M3a: hardware/two_nano_bench;
жодних 12V/VIN/ECU, з'єднання USB5V рейок, auto-upload або пошуку портів.

M3b додає лише проект кандидата в hardware/protected_dlc_interface: схему,
BOM, розрахунки, окремі ngspice42 model checks та план ізольованої лабораторної
валідації. Дозволені тільки явно обмежені current-limited fault-стимули за
FAULT_MATRIX/VALIDATION на окремому інтерфейсі без Nano/responder/ПК/ECU.
Це вузький виняток для лабораторного проекту, не дозвіл автомобільного 12V/VIN,
реального ECU, нового live backend чи зміни whitelist/timing. Функціональний
стенд A використовує старі strict bridge-bench/responder-bench identities;
дві USB на одному ПК не доводять ізоляцію. Software, calculations, SPICE,
фізичний M3a, вимірювання protected interface, vehicle qualification і real ECU
мають окремі статуси; hardware NOT VERIFIED до фактичних вимірювань.

M2c bridge policy: cycle320ms RPM/TPS/ECT/RPM/TPS, desired800/800/1600ms;
stale/hide RPM/TPS1400/4200ms, ECT2100/6300ms. Policy фіксована для сесії,
GUI і Recorder використовують `Model::freshness()`. Stale/hide лише при
age > threshold; Sample.time — host receipt, freshnessSince — host request
start lower bound. Native і serial acceptance використовують один Runner;
serial лише з явним портом та strict identity до лабораторних NEW/CONFIG.
Не змінювати200ms observation window заради частоти. Документація:
`docs/POLLING_AND_FRESHNESS.md`, `docs/BRIDGE_ACCEPTANCE_CHECK.md`.

HondaDash — окремий C++20/Qt Widgets проєкт. M1 використовує тільки
синтетичний профіль `synthetic-demo-v1`: вбудований endpoint або Nano USB.
Фізичний USB не означає вимірювання автомобіля; hardware status окремий.
M2a додає окремий offline-only Honda DLC reference-профіль. Він не має
доступу до SerialTransport або Nano M1; hardware_verified=false і live_enabled=false.
M2b окремо дозволяє DLC Link через власний розпізнаний virtual bridge на ПК
або Nano USB. Лише identity `hondadash-dlc-bridge-lab-v1`, backend=virtual,
physical_dlc_enabled=false, exact policy1. Це не дозволяє фізичного DLC,
іншої firmware, raw tunnel чи передавання Honda bytes до Nano M1.
Не переносити сюди HondaEcu, WPF/.NET, ROM, код або ресурси Hondash.

- Перш ніж змінювати файли, перевірити git status та локальні інструкції;
  зберігати сторонні зміни. Не зливати робочу гілку в main і не force push.
- Core, protocol, emulator, application session і recording мають
  залишатися стандартним C++20 без Qt GUI та Windows API. Qt належить UI
  і необхідним обгорткам застосунку.
- Усі показники проходять через request → bytes → selected endpoint → fragmented
  bytes → parser → decoder → model. UI надсилає керування через Session і протокол;
  заборонено записувати значення слайдера прямо у модель чи прилад.
- Синтетичні формули, команди й діапазони не називати характеристиками
  Honda. Постійне позначення «ЕМУЛЯЦІЯ — не підключено до автомобіля»
  не можна приховувати у програмній симуляції.
- Honda DLC: лише whitelist профілю та власна реалізація за pinned evidence.
  Не змішувати ELM, CN2 і DLC; не копіювати код без ліцензійного дозволу.
  Постійно показувати «ЛАБОРАТОРНА ЕМУЛЯЦІЯ HONDA DLC — ECU НЕ ПІДКЛЮЧЕНО».
  Valid/checksum не підтверджують калібрування чи сумісність із ECU.
- Часткове читання змінює тільки позначені канали; не освіжати старі значення
  з інших блоків. Діапазони demo-віджетів не є критеріями Honda validity.
- Відповідь reference DLC не має адреси/ID: після timeout/error зупинити
  опитування, зберегти пізні RX у журналі. Parser reset не очищує offline link.
  Новий offline experiment не є доказом recovery фізичного ECU.
- Qt SerialPort 6.8.3 дозволений лише в транспортній Qt-обгортці M1,
  розпізнаного M2b virtual bridge та двох strict M3a bench endpoints.
  USB/DLC deadlines незалежні;
  DLC 300/200/50ms належать embedded engine після DLC TX-complete.
  ABORT/HELLO/parser reset не очищують queued ECU RX; лише явний новий
  лабораторний експеримент. Пізні RX журналюються без прив'язки до read.
  Session/model/protocol залишаються стандартним C++20; simulation-only
  збірка повинна працювати без SerialPort.
- Не додавати QML, Qt Charts, WebEngine,
  JavaScript/Python до застосунку, базу даних або хмарний runtime.
- Використовувати монотонний керований час у логіці. У тестах не залежати
  від точності пробудження ОС; перевіряти deadline модельним часом.
- Один активний запит, обмежені parser/transport/plot/recording буфери.
  GUI не очікує відповіді через sleep, busy-wait або processEvents.
- Нуль — коректне число; відсутність, Unsupported та Invalid не замінювати
  нулем. Поганий/пізній/чужий пакет не освіжує останній коректний стан.
- Журнал отримує декодовані вибірки та всі сирі RX, включно з поганими.
  Використовувати locale-independent числа й filesystem Unicode paths;
  помилки диска та переповнення черги робити видимими.
- Динамічно використовувати зафіксовану Qt 6.8.3. Зміна залежностей
  потребує оновлення CMake, CI, документації та license/notice bundle.
- Перед завершенням запускати відповідні CTest і GUI smoke. Вказувати
  фактично перевірені платформи та версії; workflow не є доказом запуску.
- Windows пакет перевіряти після розпакування, без developer Qt paths,
  з перевіркою runtime. Не комітити build, EXE/DLL/ZIP, кеші й журнали.
- Не обирати ліцензію HondaDash від імені власника. Зберігати copyright
  та оригінальні license texts сторонніх компонентів.
- Nano firmware: Arduino AVR Boards 1.8.6, фіксований Arduino CLI;
  без heap/String/exceptions, з потоковими RX/TX та .data+.bss <=1536.
  Нативні тести компілюють той самий embedded parser/dispatcher.
- Жодного auto-upload, сканування портів командами, автомобільного Honda DLC,
  SoftwareSerial, EEPROM/fuses/bootloader змін. GPIO дозволено тільки у
  визначеному вище M3a low-voltage bench (або функціональному M3b стенді A), за документованою схемою. Upload тільки явною
  командою користувача з конкретним портом і варіантом Nano.
- PTY, host firmware tests і AVR compilation не є фізичним USB тестом.

Протокол описано в `docs/DEMO_PROTOCOL.md`, межі наступних етапів —
в `docs/ROADMAP.md`. Новий реальний транспорт не повинен потребувати
переписування приладів, моделі та журналювання.
Evidence і межі M2a: `docs/HONDA_DLC_EVIDENCE.md`,
`docs/HONDA_DLC_PROTOCOL.md`, `docs/HONDA_DLC_REFERENCE_PROFILE.md`.
Контракт M2b: `docs/NANO_DLC_BRIDGE_PROTOCOL.md`,
`docs/NANO_DLC_BRIDGE_ARCHITECTURE.md`, `docs/NANO_DLC_BRIDGE_TESTING.md`.
M2b постійно показує «ТЕСТОВИЙ МІСТ — ВІРТУАЛЬНИЙ ECU — ФІЗИЧНИЙ DLC ВИМКНЕНО».
M3a постійно показує «СТЕНД: ДВІ NANO — ЕМУЛЯТОР ECU — НЕ ПІДКЛЮЧАТИ ДО АВТОМОБІЛЯ».
Контракт/перевірки: `docs/TWO_NANO_BENCH.md`, `docs/ONE_WIRE_DRIVER.md`,
`docs/TWO_NANO_ACCEPTANCE.md`. Build/upload завжди розрізняють `synthetic`,
`bridge-lab`, `bridge-bench`, `responder-bench`; auto-upload заборонений.
