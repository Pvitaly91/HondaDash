# Nano USB: synthetic endpoint M1

Класична Arduino Nano **ATmega328P, 16 MHz**, одна плата та її USB-кабель.
Тут немає Honda DLC, ECU, другої Arduino, датчиків, D12 або зовнішніх
проводів. Панель показує «ЕМУЛЯЦІЯ НА ПРИСТРОЇ — ECU НЕ ПІДКЛЮЧЕНО».
Фізичний Nano/USB тест у цій реалізації має статус **NOT VERIFIED**.

## Зафіксовані інструменти

- [Arduino CLI 1.2.2](https://github.com/arduino/arduino-cli/releases/tag/v1.2.2),
  commit `c11b9dd5`. Скрипти перевіряють точну версію; bootstrap перевіряє
  SHA256 офіційного архіву.
- [Arduino AVR Boards 1.8.6](https://github.com/arduino/ArduinoCore-avr/releases/tag/1.8.6).
- Пакет компілятора `avr-gcc 7.3.0-atmel3.6.1-arduino7`, GCC 7.3.0;
  embedded модуль — C++11, desktop — C++20, Qt 6.8.3.
- FQBN `arduino:avr:nano:cpu=atmega328` та
  `arduino:avr:nano:cpu=atmega328old`.

## Складання без плати

Windows, PowerShell у корені репозиторію:

```powershell
./scripts/build-firmware.ps1 -Bootstrap
# Наступні офлайн-збірки після встановлення інструментів:
./scripts/build-firmware.ps1
```

Linux x86_64:

```bash
bash scripts/build-firmware.sh --bootstrap
# Наступні офлайн-збірки:
bash scripts/build-firmware.sh
```

Bootstrap завантажує лише інструменти з офіційних Arduino джерел у
ігноровану `.tools/arduino` (Windows) або `.tools/arduino-linux` (Linux).
Не встановлює COM-драйверів, не відкриває порти й не виконує upload.
Linux може задати `ARDUINO_CLI` і `ARDUINO_TOOLS_DIR`; Windows має
параметр `-ArduinoCli`. Версії однаково перевіряються.

Обидва варіанти збираються в `build/firmware/atmega328` і
`build/firmware/atmega328old`: `.ino.hex`, `.ino.elf`, `nano_synthetic.map`,
`size.txt`, `size-report.json`, `symbols.txt`, `compile.txt`,
`stack/endpoint.su`. Скрипт повертає помилку при невдалому compile,
відсутньому artifact, linked allocator, SRAM >1536 або Flash >30720.
Build outputs не комітяться. CI публікує firmware окремим artifact.

## Upload — лише окрема команда користувача

Спочатку від’єднайте сесію HondaDash, щоб вона **закрила COM-порт**.
Закрийте Serial Monitor та інші програми, що використовують цей порт.
Нижче `COM7` та `/dev/ttyUSB0` — приклади: замініть на явно вибраний
порт своєї плати. Варіант bootloader також виберіть явно.

```powershell
# ATmega328P / новий bootloader:
& ./.tools/arduino/cli-1.2.2/arduino-cli.exe `
  --config-file ./.tools/arduino/arduino-cli.yaml upload `
  --fqbn arduino:avr:nano:cpu=atmega328 --port COM7 `
  --input-file ./build/firmware/atmega328/nano_synthetic.ino.hex

# Лише для плати з ATmega328P (Old Bootloader):
& ./.tools/arduino/cli-1.2.2/arduino-cli.exe `
  --config-file ./.tools/arduino/arduino-cli.yaml upload `
  --fqbn arduino:avr:nano:cpu=atmega328old --port COM7 `
  --input-file ./build/firmware/atmega328old/nano_synthetic.ino.hex
```

```bash
./.tools/arduino-linux/cli-1.2.2/arduino-cli \
  --config-file ./.tools/arduino-linux/arduino-cli.yaml upload \
  --fqbn arduino:avr:nano:cpu=atmega328 --port /dev/ttyUSB0 \
  --input-file ./build/firmware/atmega328/nano_synthetic.ino.hex
```

В Arduino IDE встановіть **Arduino AVR Boards 1.8.6** у Boards Manager,
відкрийте `firmware/nano_synthetic/nano_synthetic.ino`, виберіть Arduino
Nano, ATmega328P або ATmega328P (Old Bootloader), потім свій порт і Upload.
Версія IDE не є частиною перевіреного CLI build; зафіксовані CLI-артефакти
мають відтворюватися наведеними скриптами. Не виконуйте Burn Bootloader:
bootloader, fuse bits та EEPROM цей етап не змінює.

Новий bootloader має upload speed 115200, старий — 57600; це властивість
зафіксованих Nano board recipes. **Швидкість бінарного протоколу після
запуску firmware завжди 115200 baud**, 8N1, no flow control. Serial Monitor
не потрібно відкривати; текстових Serial.print у прошивці немає.

## Підключення й reset

Виберіть «USB / Serial — тестова прошивка Nano», оновіть список портів,
явно виберіть порт і натисніть підключення. Перелік використовує метадані
без відкриття кожного порту. VID/PID або serial number не є обов’язковими
фільтрами; сумісність визначається HELLO та device info.

Стани: від’єднано → відкриття → очікування запуску → перевірка протоколу
→ робота; помилка або disconnect завершує сесію. Окремо видно порт,
firmware та факт отримання свіжого snapshot. Відкритий порт не означає
готовності Nano, а ACK керування не означає отримання нового вимірювання.

Початкові USB-настройки UI: boot wait 2000 мс, загальний handshake budget
5000 мс, TX deadline 500 мс та response timeout 300 мс. Boot wait і загальний
handshake budget налаштовуються окремо. Короткий READ timeout не витрачає
весь бюджет запуску плати. HELLO повторюється в уже відкритому порту.

Після відкриття SerialTransport один раз встановлює DTR=true, RTS=false;
під час повторів HELLO не перемикає лінії й не перевідкриває порт. Драйвер
або ОС можуть змінити DTR уже під час open; це **не гарантує відсутності
reset** для будь-якої Nano/клону. Підтвердження плати потребує її тесту.
Непідтримуваний modem-line ioctl у Linux PTY повідомляється як notice;
помилки справжнього I/O та налаштування 115200/8N1 завершують сесію.

TX deadline починається при прийнятті запиту транспортом. Response
deadline починається після дренування байтів QSerialPort у драйвер ОС.
Це не підтвердження приймання Nano; його дає лише відповідь. У журналі
TX означає фактично прийняті transport write байти, RX — фактичні
отримані фрагменти, включно з поганими CRC та обривами.

Після reset firmware відповідає `NotInitialized` на READ; новий HELLO
відновлює протокол. Якщо сесію вже завершено з помилкою, від’єднайте й
вручну підключіться до того самого явно вибраного порту. USB disconnect
не перемикає програму на вбудовану емуляцію. Нове підключення має нову
session identity, очищені parser/pending/model і не відновлює старі числа
як свіжі до нового коректного snapshot.

## Пам’ять і межі доказів

Результат фактичного складання на Windows 5 жовтня 2026 для обох FQBN:
Flash **5736/30720**, `.data` **108**, `.bss` **711**, разом статична SRAM
**819/1536**, із фізичних 2048 байтів залишається **1229** для стека.
Звіт включає Serial: символ HardwareSerial має **157 байтів**, зокрема
стандартні RX 64 та TX 64; UART-буфери не збільшено. Сам endpoint — 545
байтів на AVR (552 у нативній x64-збірці через alignment).

Статичні буфери endpoint: RX79, TX158, cached request79/response79,
delayed79. Arduino loop обробляє не більше 32 RX та 32 TX байтів за один
прохід, використовує availableForWrite, не очікує цілого 79-байтового
кадру в 64-байтовому UART-буфері та не викликає delay. Embedded-модуль
не містить String, контейнерів, heap allocation чи exceptions. Linker
symbols перевіряються на malloc/new; компіляція core new.cpp у build log
сама по собі не означає його включення до фінального ELF.

Окрема non-LTO діагностична компіляція `-fstack-usage` має найбільший
окремий frame dispatch 81 байт, snapshot/error по 32; ланцюг
receive→drain→dispatch→snapshot і допоміжні виклики дає порядок 150–200
байтів до Arduino loop/ISR. Буфер loop — 32 байти. Рекурсії немає.
Це оцінка, а не формальний worst-case аналіз LTO-бінарника. Вплив
interrupt nesting, compiler inlining, реальний stack high-water, USB
latency та втрати UART на фізичній платі **не виміряні**. Статична вільна
SRAM сама по собі не доводить відсутності stack overflow.

Той самий `endpoint.cpp` компілюється в CTest на ПК. Перевірки охоплюють
незалежні golden bytes, signed scaled words, сценарії, парсер 79 байтів,
шум/CRC/version/length/обриви, bounded storage, faults/control ACK,
дублікати та wrap-around millis. Production Session→embedded→Model
перевіряється окремим наскрізним тестом. Linux PTY використовує справжній
QSerialPort та host firmware. Це перевірка serial-шляху ОС, **не тест
USB-перетворювача або фізичної Nano**. Статус кожного запуску дивіться в
JUnit/CI; існування workflow не є результатом виконання.

## Ручний чекліст

Без плати: запустити «Вбудована емуляція», отримати всі сім каналів,
змінити ручні оберти, перевірити silence→Stale→відсутні числа→відновлення,
графік, F11/Esc, журнал CSV/JSONL та розміри 1024×600 і 1280×720.

З однією фізичною Nano:

1. Окремою командою завантажити firmware у свій порт і варіант Nano.
2. Залишити лише USB-кабель; не підключати ECU, 12 В або датчики.
3. Вибрати порт у HondaDash і дочекатися HELLO/device info.
4. Побачити сім синтетичних каналів та постійне позначення емуляції.
5. Вибрати manual, змінити оберти; дочекатися ACK та наступного snapshot.
6. Увімкнути «Не відповідати»: Stale після >1 с, числа зникають після >3 с.
   Відновити відповіді тим самим control channel; перевірити Valid.
7. Пошкодити наступний snapshot; ACK залишається справним, поганий RX
   записується в журнал і не освіжає модель.
8. Натиснути Reset на Nano: переконатися в повторному HELLO; за потреби
   вручну від’єднати/підключити сесію та отримати новий snapshot.
9. Від’єднати USB, під’єднати знову, явно вибрати порт і підключитися.
10. Записати CSV/JSONL; перевірити synthetic source, serial transport,
    endpoint/version, порт/baud, reset/reconnect та причини помилок.

Для апаратного підтвердження зафіксуйте модель Nano/USB-чипа, ОС,
bootloader variant, SHA desktop/firmware, порт, версії tooling, журнали,
результати reset/disconnect/reconnect. До фактичного виконання цього
чекліста hardware status залишається **NOT VERIFIED**.
