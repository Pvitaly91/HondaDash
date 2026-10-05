# Перевірки M0

## Автоматичний набір

```powershell
./scripts/build-windows.ps1 -QtPath '<Qt 6.8.3 msvc2022_64>'
```

```bash
cmake --preset linux -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR"
cmake --build build/linux --parallel 2
QT_QPA_PLATFORM=offscreen ctest --test-dir build/linux --output-on-failure
```

CTest запускає protocol/model/emulator, session integration, recording
та GUI smoke. Логічні перевірки не потребують GUI, мережі або пристроїв.
Для headless збірки доступне `-DHONDADASH_BUILD_GUI=OFF`.

| Група | Що підтверджує |
|---|---|
| Protocol/model/emulator | Незалежні golden bytes, CRC, масштабування семи каналів, межі та від’ємні температури, zero/Unsupported/Invalid, версія/довжина, fragmentation/coalescing/noise/truncation/resync, bounded parser, детермінізм сценаріїв, freshness |
| Session integration | HELLO та snapshot через повний байтовий шлях, timeout, late/duplicate/old session, обрив/відновлення, ручний стан після відповіді, bounded deliveries, повторні Start/Stop |
| Recording | UTF-8 Unicode/space paths, CSV optional/zero/negative числа, locale-independent decimal point, JSON escaping і точні corrupt RX bytes, метадані, collision avoidance, flush/restart, відкриття/запис/queue overflow |
| GUI smoke | Справжній QWidget застосунок, прийняті дані, ручна зміна обертів через session, втрата/відновлення, штатне завершення та screenshot |

Логіка deadline перевіряється керованим монотонним часом. Recording
overflow test блокує worker явним test gate, а не припущенням про
планування ОС; п’ятисекундний guard лише не дозволяє завислому тесту
тримати CI необмежено. WriteHook детерміновано відтворює помилку диска.

GUI smoke виконує модельні переходи справжньої сесії та перевіряє
результат; він не просто чекає кілька секунд. Звіт має `passed`,
`result`, список checks та ознаку platform/offscreen. При порушенні
перевірки застосунок повертає ненульовий exit code.

## Windows пакет

```powershell
./scripts/package-windows.ps1 -QtPath '<Qt 6.8.3 msvc2022_64>'
```

Перевірка виконується після `Expand-Archive` у новій папці, з очищеними
Qt environment variables й PATH лише package/Windows. Скрипт перевіряє
код завершення, `passed=true` в JSON та наявність screenshot; відсутність
Qt DLL/plugin, MSVC dependency inspection tool чи license bundle є
помилкою, а не skip. Прямі MSVC imports EXE та всіх DLL записуються
у `dist/reports/windows-dependencies.txt`. README пакета оголошує
Redistributable prerequisite, якщо runtime DLL не app-local.

Ця перевірка на машині збірки не доводить відсутності потрібного
Redistributable на чистій Windows. Остаточна ручна перевірка для
розповсюдження: VM Windows 10/11 x64 без Qt/VS, встановлення оголошеного
runtime за потреби, розпакування всього ZIP і запуск.

## Ручний чекліст

1. Розпакувати Windows ZIP, запустити HondaDash.exe. Видно постійний
   напис «ЕМУЛЯЦІЯ — не підключено до автомобіля».
2. Start у сценарії демонстрації: після HELLO є вимірювання всіх каналів,
   видно стан сесії, прийняту частоту відповідей, давність і counters.
   Прискорений прогрів позначено демонстраційним.
3. Manual: змінити оберти та інші поля; значення приймаються тільки
   після наступної коректної відповіді. Запалювання зупиненого двигуна
   показує 0 об/хв як Valid.
4. Вимкнути відповіді. Після >1 с побачити Stale текст/колір; після >3 с
   числа сховані, графік має розрив, timeout counter збільшується.
5. Відновити відповіді: нова вибірка повертає Valid. Затримати відповідь
   понад deadline, потім прибрати затримку: пізня відповідь не освіжає
   модель, наступна актуальна працює.
6. Зіпсувати CRC наступної відповіді та обірвати наступний пакет.
   Переконатися, що поганий пакет не підставляє нове число й не змінює
   час останнього Valid; наступний правильний пакет відновлює парсер.
7. Позначити окремий канал Unsupported, потім Invalid: видно стан цього
   каналу, він не перетворюється на нуль і не псує решту каналів.
8. Записати до папки з пробілами й українськими літерами. Stop запису,
   відкрити CSV і JSONL: decoded числа, відсутні порожні values,
   identities/time/metadata, сирі fragments включно з corrupt RX.
9. Повторити Start/Stop, перемкнути сценарій і seed, перевірити 1024×600,
   1280×720, масштаб 100%/150%, F11/Esc. Закрити під час сесії/запису.

## Облік доказів

Windows offscreen-тест завантажує встановлені в ОС Segoe UI шрифти,
щоб читабельно намалювати український текст без native font database.
Шрифти не входять до репозиторію чи ZIP. JSON перевіряє підтримку
ASCII та українських символів; `platform=windows, offscreen=false`
окремо позначає перевірку розпакованого пакета з Windows plugin.

GitHub workflow складає Windows Debug і Release на `windows-2022`,
Linux на `ubuntu-22.04`; Qt 6.8.3 та CMake 3.31.6 зафіксовані.
Windows artifacts містять ZIP, JUnit, smoke JSON, dependency report і
знімок Windows platform plugin. Linux screenshot позначено offscreen.

Фактичні local/CI результати, compiler patch versions та commit SHA
повідомляються у звіті конкретного запуску. Не вважати цей опис або
workflow-файл доказом пройдених перевірок. У M0 не перевірено реальні
ECU, електричний інтерфейс, Nano/USB або продуктивність 60 FPS.

Підтвердження локальної робочої сесії 5 жовтня 2026: Windows x64,
MSVC **19.34.31948.0** (Visual Studio 2022), Qt **6.8.3**, CMake **3.31.6**.
Фінальний `scripts/build-windows.ps1` зібрав Debug і Release: обидва
набори **4/4 CTest** пройшли (core, session, recording, GUI offscreen).
Debug: 21,23 с; Release: 2,49 с. Фінальний ZIP пройшов усі 21 GUI
перевірку після розпакування: `platform=windows`, `offscreen=false`,
очищені Qt environment і PATH. У пакет додано VC143 CRT DLL версії
14.42.34433; системні Universal CRT DLL залишаються частиною Windows.
Перевірено й переглянуто знімки 1024×600 та 1280×720.

Linux: реальна збірка тих самих бібліотек і GUI у WSL Ubuntu **24.04.3**,
GCC **13.3.0**, Qt **6.8.3**, CMake **3.31.6**, Ninja **1.11.1**.
Release **4/4 CTest** пройшли, загальний час 8,11 с; GUI виконувався
offscreen. Залежності встановлено локально; CI окремо використовує
Ubuntu 22.04. Linux desktop із фізичним дисплеєм не перевірено.

Локальні докази: `build/windows/reports/ctest-*.xml`,
`build/windows/smoke-*.json/png`, `dist/reports/package-smoke.json`,
`dist/reports/package-windows*.png`, `dist/reports/windows-dependencies.txt`,
`build/linux-local/junit-linux.xml`, `build/linux-local/smoke-Release.json`.
Build/ZIP/журнали не входять до Git. Ці фактичні локальні результати
не підміняють окремий статус запуску GitHub Actions.
