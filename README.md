# HondaDash — M1

Незалежна настільна панель параметрів двигуна: C++20, Qt 6 Widgets,
власні прилади й графік через QPainter. Вбудована емуляція працює без
обладнання; USB Serial підтримує синтетичний endpoint класичної Nano.

**Обидва джерела генерують синтетичні дані. Фізичний Nano/USB: NOT VERIFIED.
Honda ECU/DLC сумісність не реалізована.**

`synthetic-demo-v1` — наш тестовий байтовий протокол. Він не є підтвердженим
Honda DLC, емуляцією процесора ECU або заводської прошивки. Усі діапазони,
формули, швидкість опитування та прискорений прогрів тут демонстраційні.
У вікні постійно видно «ЕМУЛЯЦІЯ — не підключено до автомобіля».

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
`HondaDash-linux-test-reports` та `HondaDash-nano-firmware` (HEX/ELF/map/size).
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
