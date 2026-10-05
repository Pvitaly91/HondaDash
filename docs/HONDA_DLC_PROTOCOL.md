# Honda DLC: вузький offline-контракт M2a

Evidence: [HONDA_DLC_EVIDENCE.md](HONDA_DLC_EVIDENCE.md), профіль:
[HONDA_DLC_REFERENCE_PROFILE.md](HONDA_DLC_REFERENCE_PROFILE.md).
Це власна реалізація спостережуваного формату, не OEM-специфікація.
Немає physical captures, hardware_verified=false, live_enabled=false.

## Шари та ініціалізація

`hondadash_dlc` — C++20 без Qt/Windows API. M2a constructor `hd::dlc::Session`
володіє `OfflineLink` і не приймає `Transport`/COM. M2b додає ін'єкцію вузького
`dlc::Link` до окремого розпізнаного virtual bridge; inner protocol незмінний.
M1 `hd::Session`, SerialTransport,
synthetic-demo-v1 і firmware Nano лишаються окремими. Синтетичні A5 5A,
CRC16 та session/request IDs не додаються до Honda-пакетів.

Обраний старіший активний шлях `hobd_uni2` надсилає
`68 6A F5 AF BF B3 B2 C1 DB B3 E9`, потім чекає 300 мс. M2a відтворює
цю послідовність. Немає вигаданих HELLO/ACK, перевірки ідентичності ECU чи
твердження, що він відповів. У successor UNI функція init визначена,
але не викликана; універсальна необхідність такої ініціалізації невідома.

## Encoder та parser

Запит: `20 05 address length checksum`, рівно п'ять байтів.
Дозволено тільки `(00,02)`, `(10,01)`, `(14,01)`. Адреса/довжина спочатку
перевіряються як uint16 без звуження; zero/overflow/інший діапазон,
Write/Reset відхиляються без TX. Arbitrary command editor відсутній.

Відповідь: `00 total_length payload checksum`, total_length=payload+3.
Адреси запиту та transaction ID немає. Checksum — адитивне двійкове
доповнення modulo256: сума всіх байтів кадру дорівнює 0 modulo256.
Це не CRC. У референсах перевірки заголовка слабкі/закоментовані; наша
строга перевірка `00` і очікуваної довжини — обмеження профілю за
коментарем референсу, не висновок із власного фізичного trace.

Parser приймає по одному байту, buffer≤5, очікує payload1 або2. Відрізняє
Incomplete, HeaderError, LengthError, ChecksumError, Complete. Завершена
відповідь декодується тільки під одним активним дозволеним читанням.
Standalone parser може reset і розбирати наступний кадр; Session після
будь-якої framing/checksum помилки втрачає контекст і зупиняє опитування.
Не шукає A5 5A, не вважає наступний zero гарантованою межею реального кадру.

## Керований час і scheduler

Усі часові рішення приймаються за переданим монотонним `hd::Time`.
200 мс — загальний deadline з обраного референсу, від моменту локального
TX. 50 мс між отриманими байтами — власна консервативна політика HondaDash.
Окремо 300 мс init wait. Рівність deadline означає timeout. Якщо GUI tick
запізнився за deadline, queued дані не стають своєчасними заднім числом.

RPM/TPS мають лабораторний інтервал100 мс, ECT1000 мс. Round-robin серед
належних читань запобігає витісненню ECT. Один pending, без черги запитів;
не наздоганяємо пропущені ticks пачкою. Це бажані інтервали, не обіцянка
реального ECU. Фактичний rate = кількість прийнятих блоків за останні
1000 мс; до64 timestamps на канал. Packet accepted і Valid measurement
окремі: коректний кадр FFFF RPM може містити невизначене перетворення.

OfflineLink тримає до64 байтових подій. Модельна доставка фрагментами по
одному байту з due-spacing2 мс; час вимірювання — час приймального tick,
не GPIO timestamp. Затримка обмежена10 с; переповнення видиме. Жодної
електричної перевірки, bit-banging, OS sleep або busy-wait немає.

## Пізні відповіді та recovery

`Stopped → Initializing → Polling → Faulted`. Timeout/header/length/checksum/
noise/queue overflow очищують pending і parser, залишають видимий Faulted,
зупиняють TX. OfflineLink зберігає заплановані старі байти; наступні ticks
логують їх як unassociated/late з host_transaction_id=0, без Model.apply.

Критичний приклад: ECT A прострочений, TPS B має таку саму довжину відповіді.
Неможливо відрізнити A від B за їхніми пакетами. Тому B після timeout **не
починається**. Тест доставляє справжні відкладені байти A після parser reset,
перевіряє незмінені timestamps ECT/TPS і raw RX журнал. Номер host transaction
лише описує локальний контекст; не приписується ECU й не розв'язує цю проблему.

Stop/profile change також скасовують контекст і зберігають drain. Явний Start
створює новий **програмний ECU instance**, відкидає чергу минулого offline
експерименту та записує `offline_boundary`. Це допустима лабораторна межа,
а не твердження про recovery фізичного ECU. Мовчання, повторний init або ID
зовнішньої USB-оболонки не гарантують очищення невідомої внутрішньої черги ECU.
Для live режиму необхідні виміряні traces, upper bounds запізнення й окремий
доказ відновлення. M2a live-шляху не має.

## Модель та recording v3

`Sample.updatedMask` за замовчуванням містить усі сім каналів для M0/M1.
Honda decoder виставляє рівно один bit, source=profile ID, quality і reason.
`Model.apply` не торкається решти каналів; вони зберігають власні lastValid,
host session/request, source і reason. Zero є Valid; невідоме/Invalid не
підміняється нулем. DecoderValidated допускає скінченні значення поза
demo-шкалою приладу. Шкала визначає лише малювання, не validity.

Recording v2 зберігає формат повного synthetic snapshot. Partial/non-synthetic
sample для v2 відхиляється видимою помилкою. M2a обирає v3 явно. Метадані:
source=simulation, wire_protocol=honda-dlc, profile/version, evidence_status,
fixture_class/id, scenario, transport=offline-in-memory, endpoint=scripted-honda-ecu,
hardware_verified=false, live_enabled=false, transaction_id_origin=host_only_not_ecu_wire.
Системний created_unix_ms тільки описує файл; вимірювання використовують
monotonic_ms. Зміни сценарію й faults записуються як окремі події GUI.

CSV `measurements.csv`: час, host IDs, updated_mask, потім для кожного каналу
value/state/updated/last_valid_ms/age_ms/source/host IDs/reason. Для незміненого
каналу value порожнє, але давність і попередній стан доступні. JSONL `raw.jsonl`
додає `partial_sample` з тими самими фактами (`value:null` для незміненого)
та зберігає **точні** TX/RX-фрагменти й transaction events, навіть bad/late RX.
`tx_queued` означає намір передавання; `tx` записується після синхронної
доставки запиту в OfflineLink/ScriptedHondaEcu. Скасування з callback між
цими подіями не створює вигаданого `tx`. Це не ACK ECU.
Усі числа locale-independent, paths Unicode, черга256; помилки диска/черги видимі.

Якщо запис почато посеред сесії, availability-bootstrap позначає відомі
та невизначені канали без історичних чисел; записуються тільки подальші
прийняті читання. Давність до початку запису не реконструюється.

## Межа з M2b та наступними етапами

M2b: ПК ↔ власна USB-оболонка ↔ bridge core ↔ virtual ECU з DLC bytes.
Це окрема firmware і native backend, описані в
[NANO_DLC_BRIDGE_PROTOCOL.md](NANO_DLC_BRIDGE_PROTOCOL.md);
M1 не є прозорим bridge. Фізичний DLC backend відсутній. M3 — перевірений
електричний інтерфейс, M4 — конкретний ECU. M2a не містить wiring/upload,
довільного читання пам'яті, reset/write операцій або ECU compatibility claims.
