# Honda DLC: доказова база M2a

Дата доступу до джерел: **2026-10-06**. Це дослідження відкритих реалізацій,
а не OEM-специфікація або звіт про фізичне випробування HondaDash.
Власних captures з ECU немає. `hardware_verified=false`, `live_enabled=false`.

## Джерела та походження

| Позначення | Джерело, зафіксований commit | Роль |
| --- | --- | --- |
| K-OBD | [kerpz/ArduinoHondaOBD, `8e990713628a28197eda2bdf094662ca0c86b98a`](https://github.com/kerpz/ArduinoHondaOBD/tree/8e990713628a28197eda2bdf094662ca0c86b98a) | Основний референс: `hobd_uni2/hobd_uni2.ino`, активна гілка `obd_select == 1` |
| K-UNI | [kerpz/ArduinoHondaUNI, `51898d0d4d6e6dd0cf77f41944a1b7789947591d`](https://github.com/kerpz/ArduinoHondaUNI/tree/51898d0d4d6e6dd0cf77f41944a1b7789947591d) | Наступник того самого автора; порівняння активного `app.cpp` із закоментованими функціями |
| MS | [mr-sneezy/ArduinoHondaOBD1_to_OBD2_BT, `234541594b3e2bf3cf898cafd309060525000f36`](https://github.com/mr-sneezy/ArduinoHondaOBD1_to_OBD2_BT/tree/234541594b3e2bf3cf898cafd309060525000f36) | Похідна реалізація; виявлення відмінних формул і слабших перевірок |

GitHub metadata позначає K-OBD і K-UNI як `fork=false`, але опис K-UNI
прямо називає його наступником ArduinoHondaOBD. Спільний автор, формули та
структура обміну роблять це однією лінією походження, а не двома незалежними
підтвердженнями. MS має `fork=true`, `parent` і `source` = K-OBD;
[його README](https://github.com/mr-sneezy/ArduinoHondaOBD1_to_OBD2_BT/blob/234541594b3e2bf3cf898cafd309060525000f36/README.md#L1-L6)
також пояснює походження. Metadata перевірено через GitHub API на дату доступу.

[K-OBD README, рядки 31–36](https://github.com/kerpz/ArduinoHondaOBD/blob/8e990713628a28197eda2bdf094662ca0c86b98a/README.md#L31-L36)
і [K-UNI README, рядки 4, 32–37](https://github.com/kerpz/ArduinoHondaUNI/blob/51898d0d4d6e6dd0cf77f41944a1b7789947591d/README.md#L32-L37)
прив'язують шлях до зовнішнього 3-pin DLC. Це підстава досліджувати саме
зовнішню DLC-гілку. Це не інструкція з електричного підключення.

**Повідомлення автора, не перевірка HondaDash:** обидва README називають
P2T OBD2 stock і P30 OBD1 chipped ([K-OBD:124](https://github.com/kerpz/ArduinoHondaOBD/blob/8e990713628a28197eda2bdf094662ca0c86b98a/README.md#L124),
[K-UNI:113](https://github.com/kerpz/ArduinoHondaUNI/blob/51898d0d4d6e6dd0cf77f41944a1b7789947591d/README.md#L113)).
Тому навіть заявлена автором OBD1-перевірка стосується зміненого P30, а не
будь-якого stock OBD1 ECU. Загальне твердження README про Honda до 2002 року
не прийняте як доказ універсальної сумісності. P07/P1G/P28 не верифіковані.

## Спостережуваний обмін і його межі

| Твердження | Тип доказу та конкретна ділянка | Обмеження |
| --- | --- | --- |
| Ініціалізація `68 6A F5 AF BF B3 B2 C1 DB B3 E9`, потім 300 ms | Активний K-OBD `dlcInit`, [137–151](https://github.com/kerpz/ArduinoHondaOBD/blob/8e990713628a28197eda2bdf094662ca0c86b98a/hobd_uni2/hobd_uni2.ino#L137-L151); виклик у `setup`, [1375](https://github.com/kerpz/ArduinoHondaOBD/blob/8e990713628a28197eda2bdf094662ca0c86b98a/hobd_uni2/hobd_uni2.ino#L1375) | Функція не читає ACK; відправлення не доводить присутність ECU |
| Конфігурація DLC на 9600 baud | Активний K-OBD `setup`, [1344–1347](https://github.com/kerpz/ArduinoHondaOBD/blob/8e990713628a28197eda2bdf094662ca0c86b98a/hobd_uni2/hobd_uni2.ino#L1344-L1347) | Не вимірювання сигналу або точності GPIO |
| Запит складається з command, довжини запиту, адреси, кількості даних, checksum | Активний K-OBD `dlcCommand`, [153–167](https://github.com/kerpz/ArduinoHondaOBD/blob/8e990713628a28197eda2bdf094662ca0c86b98a/hobd_uni2/hobd_uni2.ino#L153-L167) | M2a дозволяє тільки досліджені читання `20 05`, не сусідній reset `21` |
| Відповідь: `00`, `N+3`, `N` байтів даних, checksum | Коментар K-OBD [169](https://github.com/kerpz/ArduinoHondaOBD/blob/8e990713628a28197eda2bdf094662ca0c86b98a/hobd_uni2/hobd_uni2.ino#L169), активне читання `N+3` і checksum [170–201](https://github.com/kerpz/ArduinoHondaOBD/blob/8e990713628a28197eda2bdf094662ca0c86b98a/hobd_uni2/hobd_uni2.ino#L170-L201) | Перевірка заголовка в референсі закоментована; сувора перевірка HondaDash є додатковою політикою |
| Checksum = заперечена сума попередніх байтів modulo 256 | Активний K-OBD [155](https://github.com/kerpz/ArduinoHondaOBD/blob/8e990713628a28197eda2bdf094662ca0c86b98a/hobd_uni2/hobd_uni2.ino#L155), [189–196](https://github.com/kerpz/ArduinoHondaOBD/blob/8e990713628a28197eda2bdf094662ca0c86b98a/hobd_uni2/hobd_uni2.ino#L189-L196) | Назва змінної `crc` не робить алгоритм CRC; сума всього пакета дорівнює 0 modulo 256 |
| Загальний deadline 200 ms | K-OBD `dlcCommand` [157, 171](https://github.com/kerpz/ArduinoHondaOBD/blob/8e990713628a28197eda2bdf094662ca0c86b98a/hobd_uni2/hobd_uni2.ino#L157-L171) | Починається перед записом; міжбайтовий timeout окремо не реалізований |
| Пауза 1 ms між блоками | K-OBD `readEcuData` [283, 310, 330](https://github.com/kerpz/ArduinoHondaOBD/blob/8e990713628a28197eda2bdf094662ca0c86b98a/hobd_uni2/hobd_uni2.ino#L283-L330) | Це поведінка програми, не доведена мінімальна пауза ECU |
| Відповідь не повертає адресу чи transaction ID | Висновок із того самого розміру `N+3` і використання даних з offset 2 | Неможливо відрізнити пізні відповіді однакової довжини лише за цими байтами |

У K-UNI [app.cpp:70–84](https://github.com/kerpz/ArduinoHondaUNI/blob/51898d0d4d6e6dd0cf77f41944a1b7789947591d/app.cpp#L70-L84)
є така сама функція ініціалізації, але в перевірених `.ino/.cpp/.h` цього
commit її виклику немає; [appSetup:598–630](https://github.com/kerpz/ArduinoHondaUNI/blob/51898d0d4d6e6dd0cf77f41944a1b7789947591d/app.cpp#L598-L630)
відкриває serial без неї. Це суперечність шляхів запуску. M2a обирає поведінку
конкретного K-OBD `hobd_uni2`, не приписує всім ECU обов'язкову відповідь на init.

## Активні формули проти інших варіантів

Основне джерело перетворень — активний K-OBD `readEcuData`,
[257–305](https://github.com/kerpz/ArduinoHondaOBD/blob/8e990713628a28197eda2bdf094662ca0c86b98a/hobd_uni2/hobd_uni2.ino#L257-L305).
Його OBD1 RPM = `1875000 / (raw_be16 + 1)` з цілим результатом. Множник `4`
у [ELM-гілці:707–725](https://github.com/kerpz/ArduinoHondaOBD/blob/8e990713628a28197eda2bdf094662ca0c86b98a/hobd_uni2/hobd_uni2.ino#L707-L725)
готує значення для OBD-II PID, споживач якого ділить на 4; це не множник
фізичних RPM на панелі.

K-UNI має `readRPM/readECT/readTPS` усередині великого коментаря
[app.cpp:177–367](https://github.com/kerpz/ArduinoHondaUNI/blob/51898d0d4d6e6dd0cf77f41944a1b7789947591d/app.cpp#L177-L367).
Активна функція — [readEcuData:368–430](https://github.com/kerpz/ArduinoHondaUNI/blob/51898d0d4d6e6dd0cf77f41944a1b7789947591d/app.cpp#L368-L430),
яка повторює denominator RPM, ECT-поліном і TPS `(raw-24)/2`.

ECT у вибраному `hobd_uni2` присвоюється `int`, тобто відкидає дробову частину
до нуля. Старий [hobd_uni:584–591](https://github.com/kerpz/ArduinoHondaOBD/blob/8e990713628a28197eda2bdf094662ca0c86b98a/hobd_uni/hobd_uni.ino#L584-L591)
застосовує `round`; ELM-гілка також округлює і додає 40 тільки для PID-кодування.
Наприклад, raw 64 дає приблизно 61.6695 °C: вибраний шлях повертає 61,
старий — 62. Жодний варіант не містить документованої області калібрування
полінома або універсального sentinel.

На AVR `int` має 16 біт. Вираз `byte*256 + byte + 1` у референсі може
переповнювати signed int для старшого байта від `80`, а `FFFF + 1` у типовій
AVR-арифметиці може стати нулем. HondaDash обчислює unsigned word і знаменник
у 32 бітах; це свідоме усунення арифметичного дефекту, не новий фізичний доказ.
Негативний результат референс затирає нулем; HondaDash так не робить.

TPS у K-OBD ділиться як signed integer; дробова частина відкидається.
K-UNI зберігає TPS у `uint8_t`, тому від'ємний результат може ще й обернутись.
Число raw 23 при цілочисельному діленні дає 0; профіль окремо перевіряє
обрану політику діапазону raw, щоб не видавати це за підтверджений закритий дросель.

## Інший fork не підтверджує ту саму карту

MS передає RPM word майже прямо в ELM PID `41 0C`, а `FFFF` замінює нулем
з поясненням автора про конкретний тестовий ECU. Це інша арифметична гілка,
яка не підтверджує denominator-формулу K-OBD або універсальний engine-off
sentinel. Його основний TPS передає raw у PID `41 11`, а окремий relative TPS
масштабує raw 14–231. Ці правила не змішуються з профілем kerpz OBD1.
Джерела: MS `honda_obd_bt_compact.ino`,
[RPM:257–265](https://github.com/mr-sneezy/ArduinoHondaOBD1_to_OBD2_BT/blob/234541594b3e2bf3cf898cafd309060525000f36/honda_obd_bt_compact.ino#L257-L265),
[TPS:300–303](https://github.com/mr-sneezy/ArduinoHondaOBD1_to_OBD2_BT/blob/234541594b3e2bf3cf898cafd309060525000f36/honda_obd_bt_compact.ino#L300-L303),
[relative TPS:386–394](https://github.com/mr-sneezy/ArduinoHondaOBD1_to_OBD2_BT/blob/234541594b3e2bf3cf898cafd309060525000f36/honda_obd_bt_compact.ino#L386-L394)
і [межі TPS:45–46](https://github.com/mr-sneezy/ArduinoHondaOBD1_to_OBD2_BT/blob/234541594b3e2bf3cf898cafd309060525000f36/honda_obd_bt_compact.ino#L45-L46).

У MS загальний deadline становить 250 ms. `dlcCommand` читає `N+3`, але
не перевіряє checksum відповіді. Умова відхилення заголовка використовує `&&`,
тому одна неправильна складова заголовка може пройти перевірку. M2a не
відтворює ці дефекти. Джерело: MS
[dlcCommand:72–103](https://github.com/mr-sneezy/ArduinoHondaOBD1_to_OBD2_BT/blob/234541594b3e2bf3cf898cafd309060525000f36/honda_obd_bt_compact.ino#L72-L103).
Це не незалежне підтвердження коректності response parser.

MS [ECT:215–221](https://github.com/mr-sneezy/ArduinoHondaOBD1_to_OBD2_BT/blob/234541594b3e2bf3cf898cafd309060525000f36/honda_obd_bt_compact.ino#L215-L221)
використовує той самий поліном з `round` та ELM offset 40. MAP має
[інший коефіцієнт 0.7:247–251](https://github.com/mr-sneezy/ArduinoHondaOBD1_to_OBD2_BT/blob/234541594b3e2bf3cf898cafd309060525000f36/honda_obd_bt_compact.ino#L247-L251)
і коментар про bench test автора на S2000. Це авторське повідомлення для
іншої конфігурації, не калібрування нашого профілю.

## Документація Hondash: що саме підтверджує

| Сторінка, доступ 2026-10-06 | Підтверджує | Не підтверджує |
| --- | --- | --- |
| [FAQ](https://www.hondash.net/p/faq.html) | Розрізнення proprietary 3/5-pin шляху, оригінального scanner і 16-pin OBD-II/ELM у документації продукту | Карту байтів, формули, сумісність нашого профілю; різні частини сторінки неоднаково описують 16-pin підтримку |
| [Troubleshooting](https://www.hondash.net/p/troubleshooting.html) | З'єднання телефона зі scanner та scanner з ECU є різними станами; модифікації ECU і wiring мають значення | Байтовий handshake або верифікацію конкретної прошивки/ECU користувача |
| [Guidelines for HTS / eCtune / BMTune](https://www.hondash.net/2022/06/guidelines-for-hts-ectune-bmtune.html) | Автор продукту відрізняє внутрішній modified full-duplex шлях від заводського one-wire 9600 half-duplex | CN2 pinout для HondaDash, взаємозамінність форматів або дозвіл переносити внутрішній datalogging у зовнішній DLC |
| [Protocol configuration](https://www.hondash.net/2022/05/protocol-configuration.html) | Карти HOBD/HTS можуть відрізнятись; існують різні розміри raw та пріоритети опитування | Валідацію наших формул, формат frame або checksum |

## Ліцензія та правила використання доказів

На зафіксованих деревах трьох репозиторіїв не знайдено окремого `LICENSE`;
GitHub API повертає `license: null`. У переглянутих файлах є авторська
атрибуція, але не явний дозвіл повторно використовувати реалізацію.
Це результат огляду джерел, не висновок про їхній юридичний статус.
HondaDash зберігає посилання й опис спостережуваних байтів/формул; сторонні
`.ino/.cpp`, ресурси, OEM ROM та код Hondash до production-коду не копіюються.
Власні encoder/parser/session/responder мають самостійну реалізацію.

Контракт вибраного профілю, арифметичні рішення та межі перевірки:
[HONDA_DLC_REFERENCE_PROFILE.md](HONDA_DLC_REFERENCE_PROFILE.md).
