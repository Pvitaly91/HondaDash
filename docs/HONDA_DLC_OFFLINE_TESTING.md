# Перевірка Honda DLC без обладнання

M2a перевіряє обробку байтів і станів програми. Фізичні Nano, ECU та
автомобіль у цих тестах не використовуються. Статус hardware —
**NOT VERIFIED**. `live_enabled=false`. Фактичні платформні результати
публікуються окремо в [TESTING.md](TESTING.md) та звітах конкретного CI run;
наявність тестового сценарію сама по собі не є доказом його запуску.

## Що проходить через систему

```text
scheduler → allowlisted Honda request bytes → OfflineLink
          → ScriptedHondaEcu → fragmented response bytes
          → header/length/additive checksum → reference decoder
          → partial model update → gauges/plot/CSV/JSONL
```

Цей шлях не використовує синтетичний frame `A5 5A` і не отримує готові
`Sample` від responder. Responder розпізнає власні статичні request literals
і повертає власні статичні response literals. Він не викликає production
encoder/parser/decoder для формування очікуваної відповіді.
Ініціалізація не повертає вигаданий ACK.

## Походження fixtures

Карта та арифметичні рішення зафіксовані у
[HONDA_DLC_REFERENCE_PROFILE.md](HONDA_DLC_REFERENCE_PROFILE.md), первинні
джерела з commit/рядками — у [HONDA_DLC_EVIDENCE.md](HONDA_DLC_EVIDENCE.md).

| Клас | Дані M2a | Що це означає |
| --- | --- | --- |
| `captured` | Немає | Не було власного запису обміну з фізичним ECU |
| `reference-derived` | Raw A/B/boundary і точні пакети нижче | Пакети побудовано вручну за дослідженим форматом; значення не є вимірюваннями автомобіля |
| `synthetic-fault` | Silent/delay/checksum/length/truncation/noise/late | Детерміновані штучні порушення того самого byte path |

| Поле | Запит hex | Набір A: відповідь / результат | Набір B: відповідь / результат |
| --- | --- | --- | --- |
| RPM | `20 05 00 02 D9` | `00 05 09 C3 2F` / 750 | `00 05 04 E1 16` / 1500 |
| ECT | `20 05 10 01 CA` | `00 04 40 BC` / 61 °C | `00 04 20 DC` / 89 °C |
| TPS | `20 05 14 01 C6` | `00 04 58 A4` / 32% | `00 04 AE 4E` / 75% |

Boundary: RPM `00 05 FF FF FD` → `Invalid` із причиною невизначеного raw;
ECT `00 04 FF FD` → −43 °C; TPS `00 04 18 E4` → коректні 0%.
Changing чергує набори між читаннями; він не є записом синхронного
фізичного стану всіх датчиків. Зміна набору під час сесії впливає на
наступні запити й не переписує модель напряму.

## Контракти перевірки

| Ділянка | Очікувана властивість |
| --- | --- |
| Encoder | Точні request golden bytes; тільки `(00,02)`, `(10,01)`, `(14,01)`; відхилення write/reset, інших адрес/довжин і переповнення адреси |
| Parser | Подача по одному байту та на всіх межах фрагментації; strict header/length/checksum; окремий incomplete; buffer ≤5 байтів |
| Арифметика | Big-endian RPM; 32-bit denominator; integer truncation; повний ECT raw domain; TPS domain policy; відсутність demo-clamp |
| Планувальник | Один активний запит; RPM/TPS швидше за ECT; тільки allowlist; bounded history/queue |
| Часткове оновлення | Нове RPM не змінює значення, якість, `lastValid` або provenance ECT/TPS; непідтверджені канали залишаються невизначеними |
| Freshness | Застарівання обчислюється окремо для кожного каналу; пошкоджений чи запізнілий пакет не освіжає останнє коректне значення |
| Recovery | Будь-яка неоднозначність зупиняє опитування до явного нового offline експерименту; всі пізні RX журналюються |
| Recording | Окремі profile/protocol/fixture metadata, `hardware_verified=false`, `live_enabled=false`; raw hex і причини якості; M0/M1 format v2 зберігається |
| Життєвий цикл | Stop/start, зміна джерела/профілю, callbacks; відсутність залишку старих вимірювань після нового експерименту |
| GUI | Постійне попередження, три визначені канали, чотири невизначені, реальні байти/формула/причини; відсутність live/COM маршруту |

Реалізовані перевірки в [honda_dlc_tests.cpp](../tests/honda_dlc_tests.cpp):

| Функція | Доказ у тесті |
| --- | --- |
| `goldenCodec` | Статичні init/request/response bytes, повний перебір 256 адрес × 4 довжини, wrong operation, поодинокі байти, кілька відповідей, 100000 noise bytes із перевіркою межі буфера |
| `goldenProfile` | Незалежні числові очікування, unknown sentinel, partial mask, відсутність demo-clamp у спільній Model |
| `independentResponderAndLink` | Responder зі статичними літералами, немає ACK, init wait, rejected request, overflow ≤64, пізні RX переживають локальний parser reset |
| `productionSession` | Три канали, різні частоти й timestamps, partial freshness, raw-набір B/boundary, заборонені операції без TX, невідомий профіль, повторні Start/Stop |
| `faultsAndAmbiguity` | Checksum/length/truncate/noise/silent; точна total/interbyte межа; A=ECT, B=TPS тієї самої довжини; late drain без оновлення; зміна профілю |
| `callbackCancellation` | Stop усередині TX та sample callbacks не продовжує скасований обмін |
| `endToEndRecording` | Production Session → responder → Model → Recorder, Unicode path, v3 metadata, partial sample, fault event, завершення/flush |

Спільні core/recording тести додатково перевіряють mask, per-channel age,
provenance й формат v2/v3; GUI smoke працює з фактичними QWidget.

Незалежні арифметичні точки для аудиту коефіцієнтів:

| Перетворення | Raw decimal / hex | Очікуваний результат |
| --- | --- | --- |
| RPM | `0 / 0000`, `100 / 0064`, `1249 / 04E1`, `2499 / 09C3`, `32768 / 8000` | 1875000, 18564, 1500, 750, 57 |
| ECT | 0, 16, 32, 64, 128, 192, 224, 255 | 155, 115, 89, 61, 33, 7, −9, −43 |
| TPS | 23, 24, 25, 88, 174, 224, 225 | Invalid, 0, 0, 32, 75, 100, Invalid |

RPM 1875000 і ECT −43 не обрізаються до шкали demo-приладу. Статус `Valid`
тут описує прийнятий лабораторний пакет і визначене reference-перетворення;
він не підтверджує фізичну правдоподібність чи калібрування.

## Модельний час і запізніла відповідь

Час передається явно; логічні тести не очікують ОС через `sleep`.
Init wait — 300 ms, total deadline — 200 ms, interbyte timeout — 50 ms.
OfflineLink має не більше 64 запланованих байтів з інтервалом due 2 ms;
байти отримують час фактичного виклику `tick`, а не вдаваний hardware timestamp.
Послідовність тестових `tick` монотонна. Загальний deadline перевіряється
навіть коли stream надходить потроху.

Критичний випадок: read A очікує однобайтовий ECT; його response затримано
за deadline. Після timeout parser/context очищено, але RX-події link
залишаються. Спроба read B тієї самої довжини відхиляється у fault state;
коли приходять байти A, вони потрапляють у raw log і не змінюють модель.
Це перевіряє неоднозначність без вигаданого response transaction ID.

Explicit start створює новий програмний ECU і позначає цю межу у журналі.
Лише ця явна заміна може відкинути чергу попереднього offline експерименту.
Reset одного parser такої дії не має. Жодна з цих лабораторних гарантій
не приписується справжньому ECU, USB-оболонці чи повторному фізичному init.

## Запуск

Після конфігурації й складання CMake 3.31.6 запустіть весь CTest набір:

```powershell
ctest --test-dir build/windows -C Debug --output-on-failure
ctest --test-dir build/windows -C Release --output-on-failure
```

```bash
QT_QPA_PLATFORM=offscreen ctest --test-dir build/linux --output-on-failure
```

Команди configure/build і simulation-only без SerialPort наведено в
[TESTING.md](TESTING.md). Повний набір потрібний для регресії M0/M1,
embedded Nano, recording і GUI; окремий успіх нового DLC тесту не замінює їх.
Пакет Windows перевіряється після розпакування з очищеними developer paths.

## Ручна перевірка Windows без Nano

1. Відкрийте розпакований `HondaDash.exe`, виберіть
   **Honda DLC — лабораторна емуляція** та reference-профіль.
2. Натисніть Старт. Залишається напис **ЛАБОРАТОРНА ЕМУЛЯЦІЯ HONDA DLC —
   ECU НЕ ПІДКЛЮЧЕНО**. До завершеного read init не означає відповідь ECU.
3. Для набору A перевірте RPM 750, ECT 61, TPS 32. Speed/IAT/MAP/Voltage
   мають причину **Не визначено для цього профілю**, а не нуль.
4. Перевірте hex request/response, checksum status, адресу та джерело формули.
   Виберіть набір B і boundary; TPS 0 лишається видимим, RPM FFFF має причину Invalid.
5. Увімкніть silent або затримку більше 200 ms. Видима помилка вимагає нового
   offline запуску; значення старіють окремо, графік має розриви. Вимкнення
   fault не має самовільно перезапускати читання.
6. Після нового запуску по черзі перевірте checksum, wrong length, truncation
   і noise. Старі delayed RX не повинні повертати показники до Valid.
7. Увімкніть запис у папку з українською назвою; перевірте CSV/JSONL metadata,
   byte events, raw-набори, причини якості та часові мітки окремих каналів.
8. Перейдіть до вбудованої M0 емуляції та M1 USB synthetic source, потім назад.
   Дані попереднього джерела не повинні залишатися новими показниками.
   Перевірте F11/Esc, 1024×600, 1280×720 і scale 100%/150%.

Цей чекліст не містить підключення DLC, upload firmware або команд до COM.
