# HondaDash — правила проєкту

HondaDash — окремий C++20/Qt Widgets проєкт. M1 використовує тільки
синтетичний профіль `synthetic-demo-v1`: вбудований endpoint або Nano USB.
Фізичний USB не означає вимірювання автомобіля; hardware status окремий.
Не переносити сюди HondaEcu, WPF/.NET, ROM, код або ресурси Hondash.

- Перш ніж змінювати файли, перевірити git status та локальні інструкції;
  зберігати сторонні зміни. Не зливати робочу гілку в main і не force push.
- Core, protocol, emulator, application session і recording мають
  залишатися стандартним C++20 без Qt GUI та Windows API. Qt належить UI
  і необхідним обгорткам застосунку.
- Усі показники проходять через request → bytes → selected endpoint → fragmented
  bytes → parser → decoder → model. UI надсилає керування через Session і протокол;
  заборонено записувати значення слайдера прямо у модель чи прилад.
- Синтетичні формули, команди й діапазони не називати характеристиками
  Honda. Постійне позначення «ЕМУЛЯЦІЯ — не підключено до автомобіля»
  не можна приховувати у програмній симуляції.
- Qt SerialPort 6.8.3 дозволений лише в транспортній Qt-обгортці M1.
  Session/model/protocol залишаються стандартним C++20; simulation-only
  збірка повинна працювати без SerialPort.
- Не додавати QML, Qt Charts, WebEngine,
  JavaScript/Python до застосунку, базу даних або хмарний runtime.
- Використовувати монотонний керований час у логіці. У тестах не залежати
  від точності пробудження ОС; перевіряти deadline модельним часом.
- Один активний запит, обмежені parser/transport/plot/recording буфери.
  GUI не очікує відповіді через sleep, busy-wait або processEvents.
- Нуль — коректне число; відсутність, Unsupported та Invalid не замінювати
  нулем. Поганий/пізній/чужий пакет не освіжує останній коректний стан.
- Журнал отримує декодовані вибірки та всі сирі RX, включно з поганими.
  Використовувати locale-independent числа й filesystem Unicode paths;
  помилки диска та переповнення черги робити видимими.
- Динамічно використовувати зафіксовану Qt 6.8.3. Зміна залежностей
  потребує оновлення CMake, CI, документації та license/notice bundle.
- Перед завершенням запускати відповідні CTest і GUI smoke. Вказувати
  фактично перевірені платформи та версії; workflow не є доказом запуску.
- Windows пакет перевіряти після розпакування, без developer Qt paths,
  з перевіркою runtime. Не комітити build, EXE/DLL/ZIP, кеші й журнали.
- Не обирати ліцензію HondaDash від імені власника. Зберігати copyright
  та оригінальні license texts сторонніх компонентів.
- Nano firmware: Arduino AVR Boards 1.8.6, фіксований Arduino CLI;
  без heap/String/exceptions, з потоковими RX/TX та .data+.bss <=1536.
  Нативні тести компілюють той самий embedded parser/dispatcher.
- Жодного auto-upload, сканування портів командами, Honda DLC, D12,
  SoftwareSerial, EEPROM/fuses/bootloader змін. Upload тільки явною
  командою користувача з конкретним портом і варіантом Nano.
- PTY, host firmware tests і AVR compilation не є фізичним USB тестом.

Протокол описано в `docs/DEMO_PROTOCOL.md`, межі наступних етапів —
в `docs/ROADMAP.md`. Новий реальний транспорт не повинен потребувати
переписування приладів, моделі та журналювання.
