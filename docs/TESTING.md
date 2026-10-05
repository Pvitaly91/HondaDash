# Перевірки M0/M1 та M2a

M2a додає `honda_dlc` CTest і нові assertions до GUI smoke, зберігаючи всі
M0/M1 групи нижче. Незалежні байтові/math fixtures, recovery, partial ages,
наскрізний production session/responder/parser/model/recording та обмеження
доказів описано у [HONDA_DLC_OFFLINE_TESTING.md](HONDA_DLC_OFFLINE_TESTING.md).
Baseline M1 `856c6e8eda9dbfa4b33a1170b0b4192ffaf5bd93`: Windows Release
8/8 PASS до змін (`build/windows/m2a-baseline.xml`).

## Фактичний прогін M2a — 6 жовтня 2026

| Середовище | Результат |
| --- | --- |
| Windows x64, MSVC 19.34.31948.0, Debug | CTest 9/9 PASS, GUI 59/59, 40.68 с весь CTest |
| Windows x64, Release | CTest 9/9 PASS, GUI 59/59 |
| Windows Release без SerialPort | CTest 8/8 PASS, GUI 52/52 |
| WSL Ubuntu 24.04.3, GCC 13.3.0, Release | CTest 10/10 PASS, включно з Linux PTY; 4.59 с |
| Linux Release без SerialPort | CTest 8/8 PASS, GUI 52/52; 2.51 с; Qt6SerialPort відсутній у ldd |
| Розпакований Windows ZIP | Native platform=windows, offscreen=false; GUI 59/59 при 100% і 150%, без developer Qt paths |
| Nano atmega328 та atmega328old, Windows і Linux | Обидва PASS: Flash5736, .data108+.bss711=819≤1536; без upload |

Усі desktop-перевірки використовували Qt/SerialPort6.8.3 і CMake3.31.6;
Linux Ninja1.11.1. Firmware: CLI1.2.2, AVR Boards1.8.6,
avr-g++7.3.0-atmel3.6.1-arduino7. Перший необмежено паралельний Windows
Debug build отримав MSVC C1060 (out of heap); повтор із двома build jobs
пройшов. Скрипт тепер явно задає default Parallel=2, доступний параметр override.

Переглянуто фактичні PNG1024×600/1280×720, включно з масштабом150%:
попередження, три канали, чотири причини недоступності та повний HEX/джерело
формули видимі. Графік ECT допускає1250мс між точками як rendering tolerance;
RPM/TPS250мс. Це не змінює1000/3000мс model freshness.

Звіти: `build/windows/reports/ctest-{Debug,Release}.xml`,
`build/windows-simulation/m2a-test-results.xml`,
`build/linux-local/m2a-test-results.xml`,
`build/linux-simulation/m2a-test-results.xml`, `dist/reports/package-smoke.json`,
`dist/reports/package-scale-150.json`; PNG поруч. CI для опублікованого commit
потрібно перевіряти окремо; workflow не підміняє виконання.

M0/M1 regression — PASS. Honda DLC offline implementation — PASS у цьому
обсязі. Реальні captures — відсутні. **Фізична Nano/USB, електричний DLC,
реальний ECU — NOT VERIFIED.** Чиста незалежна Windows VM не тестувалася;
очищений PATH перевіряє пакет на локальній Windows, а не іншу ОС.

## Команди

```powershell
./scripts/build-windows.ps1 -QtPath '<Qt 6.8.3 msvc2022_64>'
./scripts/package-windows.ps1 -QtPath '<Qt 6.8.3 msvc2022_64>'
./scripts/build-firmware.ps1 -Bootstrap
```

```bash
cmake --preset linux -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR"
cmake --build build/linux --parallel 2
QT_QPA_PLATFORM=offscreen ctest --test-dir build/linux --output-on-failure
bash scripts/build-firmware.sh --bootstrap
```

GUI без SerialPort: `-DHONDADASH_WITH_SERIAL=OFF`; додатковий
`-DHONDADASH_BUILD_GUI=OFF` прибирає Qt цілком. CI перевіряє simulation-only
з `CMAKE_DISABLE_FIND_PACKAGE_Qt6SerialPort=TRUE`. У CI немає приховування
failures через continue-on-error. Тести не відкривають перераховані фізичні
порти: Windows перевіряє лише явно неіснуючий порт; Linux створює власний PTY.

## Що перевіряють тести

| Група | Доказ |
|---|---|
| core | Незмінні M0 golden fixtures; CRC/endian/scaled words усіх каналів, framing/noise/coalescing/truncation/resync, bounds, сценарії та freshness |
| session | Реальний byte pipeline з InMemoryTransport; ACK без вимірювання, coalescing controls, silence/Stale/hide/restore, CRC/truncate/late, IDs, boot phase, TX та загальні deadline включно з точною межею, старі callbacks і reentrancy, 200 start/stop та 10 модельних хвилин |
| in_memory | Контракт потоку й фрагментація без змішування кадрів; bounded queue та M1 protocol edge cases |
| recording | UTF-8/Unicode/space paths, CSV optional/zero/negative, locale, JSON escaping, точні corrupt RX, metadata, collision/restart/flush, disk та queue errors |
| embedded | Той самий endpoint.cpp, що для AVR: незалежні golden bytes, 79-байтовий кадр, всі фрагментації, склеювання, noise/CRC/length/version, до-HELLO error, duplicate/conflict, overflow, всі сценарії/controls, одноразові faults, 36000 reads, millis wrap |
| session_embedded | Production Session/encoder → embedded dispatcher → production parser/decoder/model/Recorder; сім відомих scaled values, reset/re-HELLO, control ACK, silence/restore, delayed snapshot Busy/restore, одноразові faults, 100 reconnect та 10 модельних хвилин |
| serial | Production Qt QIODevice pump: partial/zero write, buffers, timeout, errors, disconnect TX/RX, reconnect усередині callbacks, cancellation, відсутній QSerialPort |
| serial_pty (Linux) | Справжній QSerialPort ↔ системний openpty ↔ embedded Endpoint: HELLO/INFO, сім ручних каналів, CRC, silence/restore, firmware reset, reconnect та OS hangup |
| gui_smoke | Реальні QWidget/прилади/графік, джерела, порт/handshake, capability gating, USB warning, missing port, M0 сценарії/freshness/запис, fullscreen/resize/start-stop |

Логічні тести мають керований монотонний час. OS/GUI перевірки використовують
event loop та обмежені реальні deadlines. PTY тест не замінює Nano/USB-chip.
AVR compilation не доводить роботу фізичної плати.

## Пакет та візуальна перевірка

Windows package містить Qt6SerialPort.dll, інші Qt DLL, Windows plugin,
ліцензійні тексти та app-local VC143 CRT. Скрипт створює ZIP, розпаковує в
нову папку і запускає саме розпакований EXE з очищеними Qt variables та PATH,
що містить лише пакет і Windows. Перевіряються passed=true, platform=windows,
offscreen=false, exit code0; зберігаються фактичні screenshots1024×600/1280×720.

Windows offscreen smoke використовує встановлений Segoe UI для читабельної
української; шрифт не розповсюджується. Linux screenshots мають позначення
offscreen. Додаткова бажана незалежна перевірка — чиста Windows10/11 VM без
Qt/VS. Її не слід вважати виконаною лише через очищений PATH.

## Фактичне середовище та статус

Базовий M0 commit0ffe65335863b922a79ed21b9a34b7a502db0739 перед змінами:
Windows Release4/4 CTest PASS (`build/windows/m1-baseline.xml`).
Локальні інструменти M1: Qt/SerialPort6.8.3, CMake3.31.6,
MSVC19.34.31948.0; Linux WSL Ubuntu24.04.3, GCC13.3.0, Ninja1.11.1.
CI окремо використовує Windows2022 та Ubuntu22.04; compiler patch version
конкретного CI запуску міститься в його логах.

Arduino CLI1.2.2 (commit c11b9dd5), Arduino AVR Boards1.8.6,
avr-g++7.3.0 (`7.3.0-atmel3.6.1-arduino7`). Обидва Nano FQBN складено.
Фактичний Flash5736 байтів; .data108 + .bss711 =819 SRAM, включно з
HardwareSerial/default UART buffers. Залишок статичної SRAM1229 не є
доказом stack safety. Budget1536 перевіряється скриптами; окремі stack
reports та обмеження наведено в NANO_USB_TESTING.md.

Локальні докази зберігаються в build/windows/reports, build/linux-local,
build/windows-simulation, dist/reports та build/firmware. CI публікує
окремі Windows ZIP, desktop test reports/screenshots і firmware artifacts.
Workflow-файл сам по собі не доводить успіх; перевіряйте запуск для commit.
Build outputs не комітяться.

**Фізична Nano/USB: NOT VERIFIED. Windows COM end-to-end з платою:
NOT VERIFIED.** Host/PTY/build не перекласифіковуються як hardware test.
Ручні USB reset/disconnect/reconnect — окремий необов'язковий апаратний
чекліст у [NANO_USB_TESTING.md](NANO_USB_TESTING.md).

## Коротка ручна перевірка без плати

1. Розпакуйте ZIP, виберіть вбудовану емуляцію, натисніть Старт.
2. Перевірте сім каналів, чотири сценарії, ручні RPM та коректний нуль.
3. Вимкніть відповіді: Stale після1с, приховані числа після3с, розрив графіка.
4. Відновіть відповіді; окремо перевірте CRC, truncation, затримку, якості.
5. Запишіть CSV/JSONL у папку з українськими літерами, перевірте
   source=simulation, transport=in-memory і сирі байти.
6. Повторіть Start/Stop, зміну джерела, F11/Esc і зміну розміру вікна.

Підсумок локального M1 запуску 5 жовтня 2026: Windows Debug **8/8**,
Release **8/8**, simulation-only Release **7/7**; Linux Release **9/9**,
simulation-only Release **7/7** — PASS. Розпакований Windows ZIP пройшов
GUI smoke з `platform=windows`, `offscreen=false`, `passed=true` та
Qt6SerialPort.dll; фактичні PNG1024×600,1280×720 і USB-unconnected переглянуто.
Debug8/8:47,94с, Release8/8:3,09с; Linux9/9:7,47с. Ці локальні результати
не підміняють окремий статус GitHub Actions для опублікованого commit.
