# M2b — перевірки тестового мосту

Усі дані simulation/reference-derived. Реальні Honda captures відсутні.
Фізична Nano/USB, Windows COM end-to-end з платою, електричний DLC,
реальний ECU та stack high-water на MCU: **NOT VERIFIED**.
Нативні тести, Linux PTY та AVR compilation не змінюють цей статус.

## Збірка і автоматичні перевірки

Pinned desktop: C++20, Qt Widgets/SerialPort6.8.3, CMake3.31.6; embedded C++11,
Arduino CLI1.2.2, Arduino AVR Boards1.8.6,
avr-g++7.3.0 (`7.3.0-atmel3.6.1-arduino7`). Залежності M0/M1/M2a не оновлювалися.

```powershell
./scripts/build-windows.ps1 -QtPath 'C:/Qt/6.8.3/msvc2022_64'
./scripts/package-windows.ps1 -QtPath 'C:/Qt/6.8.3/msvc2022_64'
# Компіляція двох окремих firmware для двох bootloader FQBN, без upload
./scripts/build-firmware.ps1 -Bootstrap -Firmware all
# Або тільки M2b
./scripts/build-firmware.ps1 -Firmware bridge-lab
```

```bash
cmake --preset linux -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR"
cmake --build build/linux --parallel 2
QT_QPA_PLATFORM=offscreen ctest --test-dir build/linux --output-on-failure
bash scripts/build-firmware.sh --bootstrap --firmware all
```

Без SerialPort: `-DHONDADASH_WITH_SERIAL=OFF
-DCMAKE_DISABLE_FIND_PACKAGE_Qt6SerialPort=TRUE`; native bridge залишається.
`-DHONDADASH_BUILD_GUI=OFF` додатково дозволяє core-only build без Qt.
PowerShell/Linux scripts за замовчуванням складають `synthetic`; явний
`all` складає обидві firmware. Output M1 `build/firmware`, M2b
`build/firmware-bridge-lab`, кожен з `atmega328` та `atmega328old`.
`FIRMWARE_BUILD_DIR` для shell змінює базу; M2b додає суфікс `-bridge-lab`.

Build scripts не відкривають порт, не сканують плату, не виконують upload.
Вони перевіряють `.data+.bss<=1536`, Flash<=30720, відсутність allocator symbols
у кінцевому ELF; зберігають HEX/ELF/map, sections/symbols, size-report.json,
compiler.txt, compile.txt та diagnostic non-LTO `.su`/`.o` для stack analysis.
UART RX64/TX64 та HardwareSerial входять до статичних SRAM цифр.

| Перевірка | Зміст |
| --- | --- |
| bridge_embedded | Frozen outer request/result goldens, CRC16, exact inner M2a bytes, A/B/Boundary, whitelist і no-TX rejection, fragment/coalesced outer stream, duplicates/cache, bounds, faults, tails, late ECT→TPS block, ABORT, millis wrap, partial/zero DLC TX, deadlines, slow USB reader, тисячі transactions та new/abort |
| bridge | Production Session→Client→NativeTransport→AVR sources→VirtualECU→parser→M2a decoder→Model/Recorder, порівняння values/quality/mask/reasons з M2a, two-layer trace, per-channel ages, independent clocks, USB delay/fragment pause проти DLC gap, result age limit, identity/version/M1 rejection, outer/inner checksum, duplicate/lost/stale/reset/cancel, host watchdogs, bounded sustained run |
| bridge_pty (Linux) | Справжній QSerialPort через власний kernel PTY до того самого embedded BridgeEndpoint, handshake-only, A/B, fault/freshness, новий experiment, reset і reconnect |
| gui_smoke | Старі M0/M1/M2a assertions плюс bridge backend/state/capability/explicit new experiment, A/B bytes, faults/gap/trailing, recording, two HEX layers, resize1024×600/1280×720, no fallback, source changes/close |
| core/recording | Нижня межа freshness не змінює receipt time, відразу Stale при затримці, partial channels, typed trace без довірених facts із bad outer CRC, v2 rejection, Unicode/locale та bounded recording |

Старі `core`, `session`, `recording`, `embedded`, `in_memory`, `session_embedded`,
`serial`, `serial_pty`, `honda_dlc` залишаються окремими regression tests.
Model-time assertions не залежать від точності OS wakeup; PTY/GUI використовують
event loop і загальний bounded watchdog, без тверджень про мілісекундну точність ОС.

## Ручна перевірка на ПК

1. Розпакуйте Windows ZIP повністю й запустіть EXE. Виберіть «Honda DLC —
   тестовий міст», «Міст на ПК». Перевірте постійне попередження про virtual ECU.
2. «Старт / handshake»: має з'явитися identity `hondadash-dlc-bridge-lab-v1`,
   backend virtual, physical DLC disabled. Сам handshake не створює вимірювань.
3. «Новий лабораторний експеримент»: після init з'являться750RPM/61°C/32%.
   Виберіть B й дочекайтеся1500/89/75. Інші чотири канали Unsupported.
4. HEX USB показує A55A/CRC16, HEX DLC — init і whitelist request/response
   без ID. Перевірте generation, operation, durations та фактичні частоти.
5. Почніть запис у папку з пробілами/українським іменем. Перевірте source=simulation,
   bridge metadata, rawUSB та bridge_reported inner facts, partial updates і ages.
6. Задайте delay>200ms або gap>=50ms, checksum, truncation чи trailing fault.
   Очікуйте visible fault, зупинку polling, згодом Stale/приховані числа.
   ECT fault не може перетворитися на TPS тієї самої довжини. Late RX журналюється.
7. ABORT/Стоп не є очищенням ECU. Для продовження вимкніть continuous fault,
   явно створіть новий experiment. Boundary у JSONL має відповідати цьому натисканню.
8. Перевірте F11/Esc,1024×600/1280×720,100%/150%, перемикання M0/M1/M2a/M2b,
   кілька start/stop, закриття під час роботи й окремі помилки диска/черги.

Default freshness1000/3000ms; за 200ms observation window і приблизно1Hz ECT
може коротко бути Stale без fault. Host request start є lower bound, а не точним
часом ECU. USB delay не робить значення знову свіжим.
У Faulted з відкритим transport програма продовжує дренувати й записувати late RX.
Кнопка «Стоп» також закриває USB: після закриття ПК не може отримувати наступні
events. У native backend queued virtual RX зберігаються до повторного відкриття
і drain або до явного NEW/reset; новий experiment позначає відкидання старої черги.

## Окрема перевірка однієї Nano через USB

Цей checklist **не виконано**. Для нього достатньо однієї класичної Nano та USB.
Не підключайте автомобіль, DLC або зовнішні GPIO-проводи. Закрийте порт в GUI
та Serial Monitor. Власник має явно визначити порт і варіант bootloader.

Приклади команд для ручного виконання після підстановки власного порту:

```powershell
# Спершу перевірка параметрів без upload
./scripts/upload-firmware.ps1 -Firmware bridge-lab -Port '<ВАШ_COM>' `
  -Fqbn 'arduino:avr:nano:cpu=atmega328old' -WhatIf
# Upload виконуйте лише за власною явною командою з конкретним портом:
./scripts/upload-firmware.ps1 -Firmware bridge-lab -Port '<ВАШ_COM>' `
  -Fqbn 'arduino:avr:nano:cpu=atmega328old'
```

```bash
bash scripts/upload-firmware.sh --firmware bridge-lab --port '<ВАШ_PORT>' \
  --fqbn arduino:avr:nano:cpu=atmega328 --dry-run
# Після явного вибору правильних параметрів приберіть --dry-run вручну.
```

Helpers вимагають firmware, port і exact FQBN; не змінюють EEPROM/fuses/bootloader.
Для M1 явно виберіть `synthetic`; M1 і M2b образи не взаємозамінні.

У GUI виберіть «Міст на Nano через USB» та конкретний порт. Перевірте окремо
open→boot→recognized→new experiment→init→reads. Повторіть A/B, faults і запис.
Перезавантаження Nano, висмикування/повторне підключення USB, wrong firmware та
занятий порт повинні давати visible fault, без fallback і без автоматичного polling.
Новий handshake та явний experiment потрібні знову. Зафіксуйте плату/USB-chip,
порт, FQBN, SHA firmware/desktop, OS, journal та фактичні результати.

## Докази виконання і artifacts

Baseline M2a `1d7a8ede50c7d114a1a406511b470f8efc817e81`: Windows Release9/9 PASS.
Локальне середовище: Windows10.0.26300, MSVC19.34.31948.0; Linux WSL
Ubuntu24.04.3, GCC13.3.0/Ninja1.11.1; Qt6.8.3/CMake3.31.6 на обох.
Фінальні результати запуску та розміри ELF наведено в docs/TESTING.md.

Локальні outputs: build/windows/reports, build/windows-simulation,
build/linux-local, build/linux-simulation, dist/reports, build/firmware*.
Windows package script розпаковує ZIP у нову папку, очищує developer Qt paths,
перевіряє imports/runtime та запускає native `platform=windows`,
`offscreen=false` smoke100%/150%. PNG — фактичні QWidget captures; це не тест
людиною на окремому фізичному екрані й не чиста незалежна Windows VM.

CI artifacts: `HondaDash-windows-x64`, `HondaDash-windows-test-reports`,
`HondaDash-linux-test-reports`, `HondaDash-nano-firmware` (M1),
`HondaDash-nano-dlc-bridge-lab-firmware` (M2b). Firmware artifacts містять
original license bundle. Перевіряйте jobs/artifacts саме потрібного commit;
наявність workflow не є доказом виконання. Build/ZIP/EXE/HEX/logs не комітяться.
