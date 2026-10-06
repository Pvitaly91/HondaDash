# HondaDash — M3b.1: fail-safe revision B

Revision B додає [апаратний cutoff і re-arm](hardware/protected_dlc_interface/FAILSAFE.md)
та [production-driver/frontend integration](docs/PROTECTED_INTERFACE_DRIVER_INTEGRATION.md).
Після безперервного TX timer незалежно від Nano блокує власний sink;
відновлення вимагає стабільних живлень, кваліфікованого LOW та нового локального
натискання. Мінімально виправлено stop anchor production driver:17ticks запасу
на release електроніки; lateness/echo/200ms guard збережено. Revision A та її
історичні результати відтворюються окремо з b2cf4ea. Фізичні cutoff/обмін/фронти/
ізоляція — NOT VERIFIED; negative DC/off-state — OPEN, не protection PASS.

M3b додає [окремий hardware-пакет](hardware/protected_dlc_interface/README.md):
ізольований кандидат на ISO7721F з незалежною батареєю, open-drain TX,
захищеним RX, схемою/BOM, розрахунками, ngspice42 і планом вимірювань.
**GUI/USB identities/режими залишаються M3a; спільний bench driver має виправлення stop.**
Розрахунки/model checks не підтверджують апаратну безпечність. M3a physical,
protected-interface measurements, vehicle qualification і real ECU —
**NOT VERIFIED**. Подальші передумови: [M4 readiness](docs/M4_READINESS.md).
SPICE є тільки окремою build/test залежністю electrical workflow.

Незалежна настільна панель параметрів двигуна: C++20, Qt 6 Widgets,
власні прилади й графік через QPainter. Вбудована емуляція працює без
обладнання; USB Serial підтримує синтетичний endpoint класичної Nano.

Третє джерело «Honda DLC — лабораторна емуляція» використовує досліджені
байтові запити й відповіді та власний програмний відповідач ECU.
Четверте джерело «Honda DLC — тестовий міст» виконує той самий профіль
через C++11 bridge core: нативно на ПК або у новій Nano firmware з virtual ECU.
M3a додає в цьому ж джерелі «Стенд · дві Nano»: окремий bridge-bench передає
байти через низьковольтну лінію до зовнішнього responder-bench. Другий USB
керує лише тестовим пристроєм; вимірювання повертаються через сигнальну лінію.
**Усі джерела — simulation. Фізичні Nano/USB, електричний DLC і реальний ECU:
NOT VERIFIED. Реальні captures відсутні.**

`synthetic-demo-v1` — наш тестовий байтовий протокол. Він не є підтвердженим
Honda DLC, емуляцією процесора ECU або заводської прошивки. Усі діапазони,
формули, швидкість опитування та прискорений прогрів тут демонстраційні.
У вікні постійно видно «ЕМУЛЯЦІЯ — не підключено до автомобіля».

## Honda DLC offline

Профіль `honda-dlc-kerpz-obd1-reference-v1` прив'язаний до активного
`hobd_uni2` у kerpz ArduinoHondaOBD, pinned revision
`8e990713628a28197eda2bdf094662ca0c86b98a`. Він читає лише RPM `0x00/2`,
ECT `0x10/1`, TPS `0x14/1`. Дані reference-derived; калібрування та
застосовність до конкретного ECU не перевірені. Для решти чотирьох каналів
видно «Не визначено для цього профілю». Профілі P07/P1G/P28 не заявлені.

1. Виберіть «Honda DLC — лабораторна емуляція» й натисніть «Старт».
2. Базовий raw-набір дає 750 RPM, 61 °C і 32 % після окремих байтових читань.
3. Виберіть набір B: після відповідей з'являться 1500 RPM, 89 °C, 75 %.
4. Перегляньте TX/RX HEX, адресу, перевірку та походження формули. Час
   кожного каналу окремий; RPM/TPS опитуються частіше за ECT.
5. Пошкодьте checksum або обірвіть відповідь. Видима помилка зупинить
   опитування; старі числа стануть Stale та зникнуть. «Старт» створює
   новий **програмний** експеримент. Пауза не гарантує очищення реального ECU.
6. Запишіть журнал: format v3 зберігає часткові оновлення, давність,
   host-only transaction ID та всі RX, включно з пошкодженими.

Попередження «ЛАБОРАТОРНА ЕМУЛЯЦІЯ HONDA DLC — ECU НЕ ПІДКЛЮЧЕНО»
постійне. Саме M2a залишається offline; новий M2b допускає лише розпізнаний
virtual bridge через USB. Жоден Honda-шлях не надсилає DLC-команди в Nano M1.
Докази та суперечності: [evidence](docs/HONDA_DLC_EVIDENCE.md),
[профіль](docs/HONDA_DLC_REFERENCE_PROFILE.md),
[протокол/recovery](docs/HONDA_DLC_PROTOCOL.md),
[fixtures і перевірки](docs/HONDA_DLC_OFFLINE_TESTING.md).

## Honda DLC — тестовий міст

1. Виберіть це джерело та «Міст на ПК». «Старт» виконує лише handshake.
2. Натисніть «Новий експеримент»: створюється новий virtual ECU, виконується
   фіксований init, потім окремі RPM/ECT/TPS reads. Набори A/B збігаються з M2a.
3. Два HEX-рівні показують outer USB і повідомлені мостом inner DLC bytes.
   Спробуйте checksum, gap чи trailing fault: DLC polling зупиниться, пізні RX
   потраплять у журнал. Продовження потребує явного нового експерименту.
4. Для «Міст на Nano через USB» окремо складіть/завантажте `bridge-lab`,
   явно виберіть порт. M1 firmware не розпізнається як bridge; fallback відсутній.

Постійне позначення: **ТЕСТОВИЙ МІСТ — ВІРТУАЛЬНИЙ ECU — ФІЗИЧНИЙ DLC ВИМКНЕНО**.
Саме firmware bridge-lab не має фізичного DLC backend або GPIO init. DLC deadline контролює
embedded engine, USB watchdog — ПК. Для виявлення trailing bytes engine
спостерігає повне 200ms вікно. M2c задає цикл 320ms RPM→TPS→ECT→RPM→TPS:
цільові RPM/TPS по1,25Hz, ECT0,625Hz, разом3,125Hz. GUI окремо показує
досягнуті частоти, консервативний вік та статистику затримок. Журнал v3
зберігає host receipt time, request-start lower bound і незмінну політику сесії.

Stale/hide для bridge: RPM/TPS1400/4200ms, ECT2100/6300ms. Пороги виведено
з максимального проміжку між запитами, бюджету відповіді та запасу scheduler;
перевантаження не збільшує їх автоматично. M0/M1/M2a зберігають1000/3000ms.
Обґрунтування й десятихвилинний baseline/after: [polling/freshness](docs/POLLING_AND_FRESHNESS.md).

Явний acceptance runner використовує production Session/Client/Transport,
Model/Recorder та той самий embedded core. GUI не запускає його автоматично:

```powershell
.\HondaDashBridgeCheck.exe --backend native --report acceptance-native
.\HondaDashBridgeCheck.exe --backend serial --port COM7 --report acceptance-COM7
```

`COM7` — приклад: підставте свій конкретний порт лише для свідомої лабораторної
перевірки. Runner перевіряє identity перед NEW/CONFIG, виконує A/B, fault,
старіння та явне recovery. [Команди, exit codes і межі доказів](docs/BRIDGE_ACCEPTANCE_CHECK.md).
Serial/PTY PASS не підтверджує фізичну Nano або DLC.

## M3a — низьковольтний стенд двох Nano

Дві класичні Nano ATmega328P 5 V/16 MHz, кожна USB до того самого ПК;
спільна GND, **виходи 5V не з'єднувати**. Конкретна
[схема та BOM](hardware/two_nano_bench/README.md) використовують D3 для
open-collector TX, D8/ICP1 для RX, Timer1 і 9600 baud 8N1. D0/D1 залишаються USB.
Монтаж лише без живлення; заборонено автомобіль, 12 V, VIN та hot-plug сигнальних проводів.

1. Окремо складіть `bridge-bench` і `responder-bench`; upload тільки явно
   вибраного firmware на конкретний порт і варіант Nano. Програма нічого не прошиває.
2. Виконайте A–F [hardware-checklist](docs/TWO_NANO_ACCEPTANCE.md).
3. У джерелі тестового мосту оберіть «Стенд · дві Nano», два різні порти й «Старт / handshake».
   Обидві exact identity мають бути перевірені до будь-якого line TX.
4. «Новий експеримент» зупиняє bridge, отримує QUIESCE/generation від responder,
   дренує RX/перевіряє вільну лінію, виконує NEW → ARM → INIT. Bridge NEW/ABORT
   самі не очищують відкладену відповідь іншої плати.
5. Набори A/B, faults, parser/decoder, модель, Recorder та M2c freshness спільні.
   Без responder або після помилки немає virtual fallback чи автоматичного recovery.

```powershell
.\scripts\build-firmware.ps1 -Firmware all
.\build\windows\Release\HondaDashBridgeCheck.exe --backend two-nano-bench --bridge-port COM7 --responder-port COM8 --report acceptance-bench
```

Команди наведено з кореня репозиторію після desktop Release build.
У розпакованому Windows ZIP запускайте `.\HondaDashBridgeCheck.exe`.
Порти в прикладі треба замінити на власні. Upload-команди наведено в
[контракті firmware](docs/TWO_NANO_BENCH.md). Постійний напис:
**СТЕНД: ДВІ NANO — ЕМУЛЯТОР ECU — НЕ ПІДКЛЮЧАТИ ДО АВТОМОБІЛЯ**.
Metadata має `bench_schema_version=1`, `bench_io_enabled=true`,
`vehicle_connection_allowed=false`, обидві identity/версії/порти, `source=simulation`.
Identity не підтверджує електричну схему. Програмна бітова модель, AVR compilation,
фізичний USB кожної плати, обмін по лінії та вимірювання фронтів мають окремі
[статуси перевірки](docs/TESTING.md). Фізичні пункти **NOT VERIFIED**.

[USB-протокол](docs/NANO_DLC_BRIDGE_PROTOCOL.md),
[архітектура і clocks](docs/NANO_DLC_BRIDGE_ARCHITECTURE.md),
[збірка, перевірки, ручний PC/Nano чекліст](docs/NANO_DLC_BRIDGE_TESTING.md).

## Що можна перевірити

- Сім каналів: оберти, швидкість, температури охолоджувальної рідини та
  впускного повітря, дросель, абсолютний тиск у впуску й напруга.
- Запалювання без працюючого двигуна, холостий хід, детермінований
  демонстраційний цикл і ручні значення.
- Повний шлях запит → байти → емулятор → фрагменти відповіді → парсер →
  декодована вибірка → модель → панель і журнал.
- Відсутність відповідей, затримка, пошкоджена контрольна сума, обірваний
  пакет, Unsupported та Invalid для вибраного каналу.
- Переходи NoData, Valid, Stale, Unsupported, Invalid; нульові оберти
  залишаються коректним вимірюванням. Після 1 с число позначається
  застарілим, після 3 с поточне число замінюється на «—».
- CSV вимірювань і JSONL усіх TX/RX-фрагментів та подій. Папки з пробілами
  й українськими символами підтримуються; помилки запису видно на панелі.

## Windows: складання

Зафіксоване середовище: **Qt 6.8.3, MSVC 2022 x64, CMake 3.31.6**.
Потрібна Visual Studio 2022 з компонентом Desktop development with C++,
Windows SDK та Qt `msvc2022_64` з модулем SerialPort 6.8.3. Python використовується лише для
встановлення інструментів у CI; застосунок його не використовує.

Відкрийте PowerShell у корені репозиторію:

```powershell
$qtRoot = 'C:/Qt/6.8.3/msvc2022_64'
./scripts/build-windows.ps1 -QtPath $qtRoot
$env:PATH = "$qtRoot/bin;$env:PATH"
./build/windows/Release/HondaDash.exe
```

Шлях наведено як приклад: передайте власний `-QtPath` або задайте
`QT_ROOT_DIR`. Скрипт конфігурує preset `windows`, складає Debug і Release,
запускає CTest для обох конфігурацій та зупиняється на будь-якій помилці.
Запуск із build tree потребує Qt `bin` у PATH, як у прикладі вище;
розпакований ZIP уже містить DLL й цього налаштування не потребує.
Точну версію MSVC конкретної збірки видно в журналі CMake/CI.

Еквівалентні команди:

```powershell
cmake --preset windows -DCMAKE_PREFIX_PATH='C:/Qt/6.8.3/msvc2022_64'
cmake --build build/windows --config Release --parallel
ctest --test-dir build/windows -C Release --output-on-failure
```

Пакет Windows Release:

```powershell
./scripts/package-windows.ps1 -QtPath 'C:/Qt/6.8.3/msvc2022_64'
```

Результат — `dist/HondaDash-windows-x64.zip`. Скрипт запускає `windeployqt`,
перевіряє Qt DLL/platform plugin та окремо досліджує MSVC imports через
`dumpbin`. Потім розпаковує ZIP у нову папку й запускає **саме цю копію**
з PATH, який містить лише пакет і системні папки Windows, без Qt-змінних.
Звіт і фактичний знімок віджета з Windows platform plugin зберігаються
в `dist/reports/`. Це автоматична перевірка Windows GUI; вона не є
перевіркою людиною на фізичному дисплеї.

Пакет динамічний, EXE не можна переносити окремо від DLL і `platforms`.
Вимогу Visual C++ Redistributable 2015–2022 x64 або наявність runtime DLL
поруч з EXE скрипт явно записує у README пакета. Якщо потрібен
Redistributable, його версія має бути не старішою за використаний MSVC.

## Linux: те саме джерело

CI використовує Ubuntu 22.04, Qt 6.8.3 для Linux (`gcc_64`), CMake 3.31.6
та Ninja 1.11.1. Для локальної збірки потрібні GCC із підтримкою C++20,
Ninja, Qt 6.8.3 Widgets + SerialPort і системні залежності Qt.

На Ubuntu, окрім зафіксованих Qt/CMake, потрібні системні пакети:

```bash
sudo apt-get install build-essential ninja-build libgl1-mesa-dev libegl1-mesa-dev \
  libxkbcommon-dev libxkbcommon-x11-0 libxcb-cursor0 libxcb-xinerama0 \
  libxcb-xkb1 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-render-util0
```

```bash
export QT_ROOT_DIR=/path/to/Qt/6.8.3/gcc_64
cmake --preset linux -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR"
cmake --build build/linux --parallel 2
QT_QPA_PLATFORM=offscreen ctest --test-dir build/linux --output-on-failure
build/linux/HondaDash
```

Остання команда відкриває звичайне вікно за наявності графічної сесії.
В оточенні без дисплея можна перевірити справжній застосунок offscreen:

```bash
QT_QPA_PLATFORM=offscreen build/linux/HondaDash --smoke-test \
  --report build/linux/linux-smoke.json --screenshot build/linux/linux-offscreen.png
```

Offscreen-знімок підтверджує рендеринг віджета; він не є доказом запуску
у звичайному Linux або Windows desktop.

## Запуск і ручна перевірка

1. Запустіть `HondaDash.exe`, натисніть «Старт» і виберіть демонстраційний
   сценарій. Переконайтеся, що видно позначення емуляції та надходять дані.
2. У ручному режимі змініть оберти. Число має змінитися лише після
   наступної коректної байтової відповіді; стрілка плавно відображає її.
3. Вимкніть відповіді. Спостерігайте лічильник тайм-аутів, напис Stale
   після 1 с і «—» після 3 с; графік має розрив.
4. Відновіть відповіді: наступна прийнята вибірка поверне Valid.
5. Пошкодьте наступну контрольну суму й обірвіть наступний пакет:
   погані дані не повинні освіжити останнє коректне вимірювання.
6. Почніть запис, виберіть папку, наприклад `D:/Журнали Honda/тест`,
   змініть параметри, потім зупиніть запис. Усередині унікальної
   `recording-...` будуть `measurements.csv` і `raw.jsonl`.
7. Перевірте F11/Esc, розміри 1024×600 і 1280×720 та кілька Start/Stop.
   Закрийте вікно під час роботи — callbacks і запис мають завершитися.

Машинна перевірка під Windows:

```powershell
./HondaDash.exe --smoke-test --report smoke.json --screenshot dashboard.png
```

Успішний звіт містить `passed: true`, код завершення — 0. Smoke перевіряє
дані справжньої сесії, ручну зміну обертів через байти, обрив/відновлення
та закриття; просте очікування не вважається успіхом.

## CI, документація та межі

[GitHub Actions](https://github.com/Pvitaly91/HondaDash/actions) складає
Windows Debug/Release та Linux GUI/логіку. Успішний Windows job додає
окремі artifacts `HondaDash-windows-x64`, `HondaDash-windows-test-reports`,
`HondaDash-linux-test-reports`, `HondaDash-nano-firmware` (M1) та
`HondaDash-nano-dlc-bridge-lab-firmware` (M2b; HEX/ELF/map/size/stack).
Наявність workflow сама по собі не підтверджує, що конкретний запуск успішний;
перевіряйте статус відповідного commit у Actions.

- [Завантаження Nano та USB-чекліст](docs/NANO_USB_TESTING.md)
- [Розширення synthetic endpoint](docs/SYNTHETIC_DEVICE_EXTENSION.md)
- [Архітектура](docs/ARCHITECTURE.md)
- [Повний синтетичний протокол і golden fixtures](docs/DEMO_PROTOCOL.md)
- [Автоматичні та ручні перевірки](docs/TESTING.md)
- [Наступні етапи](docs/ROADMAP.md)
- [Third-party notices](docs/THIRD_PARTY_NOTICES.md)
- [Правила розвитку проєкту](AGENTS.md)

M1 не реалізує Honda DLC, записи в ECU,
скидання помилок, Android або відтворення журналів. Частота опитування
10 Гц є налаштуванням симуляції. Таймер перемальовування приблизно 16 мс
є ціллю плавності, а не заявою про виміряні 60 FPS.

Ліцензію всього проєкту власник поки не обрав. Ліцензійні тексти Qt та її
включених залежностей у `docs/licenses` стосуються цих сторонніх компонентів.

## Nano USB та simulation-only

USB-режим працює через той самий parser/Session/Model/Recorder. Виберіть
джерело USB, оновіть список портів і явно виберіть порт. Відкриття порту
ще не означає розпізнавання: GUI очікує запуск firmware, HELLO та INFO.
Постійний напис: «ЕМУЛЯЦІЯ НА ПРИСТРОЇ — ECU НЕ ПІДКЛЮЧЕНО».
Усі журнали мають source=simulation, окремий transport та firmware metadata.

```powershell
# Лише складання, без upload; завантажує точні CLI 1.2.2 / AVR Boards 1.8.6
./scripts/build-firmware.ps1 -Bootstrap
# Обидві окремі firmware, кожна для двох Nano FQBN; upload не виконується
./scripts/build-firmware.ps1 -Firmware all
```

Скрипт складає обидва `arduino:avr:nano:cpu=atmega328` / `atmega328old`.
Flash/SRAM, HEX/ELF, linker map та stack assessment — `build/firmware/`.
Upload виконується лише окремою командою з конкретним портом згідно
[інструкції Nano](docs/NANO_USB_TESTING.md); перед upload закрийте порт
у HondaDash і Serial Monitor. Потрібен тільки USB, без зовнішніх проводів.

GUI без залежності SerialPort:

```powershell
cmake -S . -B build/windows-simulation -G "Visual Studio 17 2022" -A x64 `
  -DHONDADASH_WITH_SERIAL=OFF -DCMAKE_PREFIX_PATH='C:/Qt/6.8.3/msvc2022_64'
cmake --build build/windows-simulation --config Release --parallel
ctest --test-dir build/windows-simulation -C Release --output-on-failure
```

На Linux аналогічно додайте `-DHONDADASH_WITH_SERIAL=OFF`; для логічних
бібліотек без Qt також `-DHONDADASH_BUILD_GUI=OFF`. Базові fixtures M0
залишено незмінними. Нові host тести збирають справжній embedded endpoint,
а Linux PTY тест використовує справжній QSerialPort. Ці перевірки та AVR
компіляція не підтверджують роботу USB-чипа або фізичної плати.
