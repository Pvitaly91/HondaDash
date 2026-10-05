# Етапи HondaDash

## M0 — програмна емуляція

Наскрізний request/byte-stream/session/model/UI/recording шлях без
обладнання. Synthetic-demo-v1 є власним тестовим форматом. Досягнутий
обсяг не підтверджує Honda сумісність. Replay CSV/JSONL, розширені
оцінки продуктивності та складний редактор панелі залишаються окремими
можливими програмними завданнями.

## M1 — USB обмін із класичною Nano

Реалізовано незалежний Session, InMemoryTransport, асинхронний Qt
SerialPort та синтетичну firmware класичної Nano. Host firmware,
наскрізний Session і Linux PTY перевіряються окремо від AVR compilation.
Додано керування сценаріями/seed, ручними значеннями і faults через bytes.
Обидві Nano-конфігурації складаються; static SRAM budget1536 перевіряється.

Фізичний hardware status: **NOT VERIFIED**. Наступна апаратна перевірка —
одна Nano, тільки USB, за docs/NANO_USB_TESTING.md: manual values,
reset/disconnect/reconnect та журнал. Лише фактичний тест тим самим
Windows пакетом може підтвердити ПК ↔ USB ↔ Nano. Honda сумісності M1
не заявляє. Windows COM end-to-end без обладнання не підтверджено.
## M2a — досліджений протокол і offline implementation

Реалізовано незалежний C++20 Honda DLC encoder/parser/profile/session та
ScriptedHondaEcu. Whitelist трьох читань, RPM/ECT/TPS, per-channel freshness,
лабораторний GUI і recording v3 перевіряються незалежними reference-derived
fixtures. Evidence містить pinned revisions, походження форків і суперечності.
Hardware_verified=false, live_enabled=false; captures відсутні. Це не статус
«підтверджений Honda» і не перевірка P07/P1G/P28. M0/M1 збережено окремо.

## M2b — інтеграція з firmware-мостом Nano та програмні перевірки

Майбутня межа: ПК ↔ власна USB-оболонка ↔ Nano bridge ↔ Honda DLC.
Потрібен окремий контракт bridge, bounded buffers, transport diagnostics і
модель recovery без вигаданого ID в ECU-відповіді. M1 firmware залишається
синтетичним endpoint; передавання сирих Honda-команд у неї заборонене.
Host tests/AVR compilation не замінюють перевірку фізичної плати.

## M3 — електричний інтерфейс

Розробити й перевірити physical adapter: рівні напруг, захист, живлення,
земля, ізоляція/перетворення сигналів і поведінка при несправності.
USB-підключення Nano не доводить безпечності підключення до DLC/ECU.
Цей етап потребує реальної схеми та вимірювань; M2a їх не реалізує.

## M4 — справжній ECU

Спочатку bench ECU з підтвердженим electrical adapter і профілем,
потім контрольована перевірка на конкретному автомобілі. Порівнювати
прийняті дані з незалежним засобом діагностики, документувати модель
ECU, firmware, частоту, latency й відмови. Не переносити обіцянку 10 Гц
із симуляції на обладнання.

Запис у ECU, прошивання, очищення DTC, Android і складний редактор
компонування не входять до M2a та не додаються побічно до цих робіт.
