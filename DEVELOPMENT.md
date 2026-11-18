# gmpc-qt — разработка

**Обзор:** Порт gmpc (GTK2 MPD-клиент) на Qt6/C++20 с сохранением внешнего
интерфейса и максимальной эффективностью кода. Проект переведён на Qt6, вся
функциональность оригинального gmpc восстановлена.

Документ описывает соглашения, стандарты и практики, принятые в проекте.

---

## Содержание

- [C++20 — что используем, что нет](#c20--что-используем-что-нет)
- [Qt6 — правила](#qt6--правила)
- [Тестирование (doctest + trompeloeil)](#тестирование-doctest--trompeloeil)
- [Сборка (CMake + Nix)](#сборка-cmake--nix)
- [Архитектура проекта](#архитектура-проекта)
- [Интеграция с окружением](#интеграция-с-окружением)

---

## C++20 — что используем, что нет

### Активно (без оговорок)

| Фича | Как используем | Важно |
|------|----------------|-------|
| `std::span` | Параметры невладеющих буферов | Передавать **по значению**, **не хранить** как член класса (dangling) |
| `std::string_view` | Константные строковые параметры (не для QString) | **Не гарантирует `\0`** — `sv.data()` в C-API → UB. Не возвращать из функции |
| `std::source_location` | Логи — типобезопасная замена `__FILE__ __LINE__` | Паттерн: параметр со значением по умолчанию |
| `[[likely]]/[[unlikely]]` | В горячих путях (без профилирования — гадание) | Только после профилирования |
| `consteval`/`constexpr` | Валидация времени компиляции | `constexpr vector/string` — transient allocation (освобождается в том же контексте) |
| Concepts | Замена SFINAE, улучшение error messages | Стандартные концепты предпочтительнее своих. Избегать вложенных `requires` |
| `= default` comparisons | Тривиальные value-типы | `auto operator<=>(const X&) = default;` |

### Ограниченно (с оговорками)

| Фича | Условие | Риски |
|------|---------|-------|
| Coroutines | Только идиоматический async-конвейер (async download chain) | Аллокация фрейма, 20–30 строк boilerplate для генератора, ~3–4 уровня косвенности на `co_await` |
| `std::ranges` | filter/transform для однопроходного чтения | На **10–30% медленнее** ручного цикла с `reserve`+`emplace_back`. View **не кэшируются** — каждый проход перевычисляет |
| `std::optional` как возврат | Когда результат может отсутствовать | Не злоупотреблять — `std::variant` предпочтительнее для >2 состояний |

### Не используем

| Фича | Причина |
|------|---------|
| Modules | CMake 3.28+ поддержка есть, но IntelliSense/редакторы экспериментальные. `import std;` стабильно только MSVC |
| `std::jthread` | QThread — другая модель. Stop_token **кооперативный** — не прерывает блокирующие syscall'ы (read/write) |
| `std::bind` | Лямбды лучше во всех случаях |

### Breaking changes C++17→C++20 (проверить при апгрейде)

- Aggregate init через `()` (P0960R3) — раньше не работало, теперь работает.
- ADL с `==` (P2468R2) — может дать ambiguous.
- **Удалены:** `throw()`, `std::is_literal_type`, `std::result_of`, `std::shared_ptr::unique()`, `std::get_temporary_buffer`.
- **Deprecated:** `std::is_pod`, `std::aligned_storage`.

### Типичные ошибки

1. `string_view` не null-terminated → передача `sv.data()` в C-API → UB.
2. ranges view — каждый проход перевычисляет transform/filter.
3. `span` как член класса — dangling reference.
4. `constexpr vector` — transient allocation, нельзя передавать в runtime-функцию.

---

## Qt6 — правила

### Сборка и CMake

- `qt_add_executable` вместо `add_executable` (AUTOMOC-friendly).
- `set(CMAKE_AUTOMOC ON)` — обязателен.
- `qt_standard_project_setup()` — CMake 3.28+, унифицирует настройку Qt.

### Сигналы/слоты

- PMF-style `connect(obj, &Class::signal, ...)` — **никаких** `SIGNAL()`/`SLOT()` макросов.
- Лямбды в connect — норма, если слот одноразовый или тривиальный.
- Для асинхронных тестов: `QSignalSpy::wait()` вместо ручных `QEventLoop`.

### Типы

- `QStringView` — константные строковые параметры функций.
- `QString` — только когда нужно владение, модификацию или передачу в Qt API.
- `QRegularExpression` — **никогда** `QRegExp` (удалён в Qt6).
- `QList<T>` = `QVector` — **нет COW с Qt6**, копирование честное.
- `QLatin1StringView` (бывший `QLatin1String`) — эффективное сравнение.

### Виджеты и графика

- `QLabel::pixmap()` возвращает `QPixmap` **по значению** (не `const QPixmap*`).
- `QStackedWidget` — предпочтительный паттерн для переключения страниц.
- `QWidget::enterEvent(QEvent*)` → `enterEvent(QEnterEvent*)` — изменилась сигнатура.
- `QDesktopWidget` удалён → `QScreen` для информации об экранах.

### Сеть

- `QNetworkAccessManager::get(QNetworkRequest)` — не принимает `QUrl` напрямую.
- `QNetworkRequest` обязателен, даже для простых GET.

### Инструментарий

- Qt Linguist: `lupdate`/`lrelease` + `tr()` — официальный путь интернационализации.
- `QXmlStreamReader` — предпочтительнее DOM/SAX для XML.

### Миграция Qt5→Qt6 (чеклист)

Аудит проекта показал: ни одного из перечисленных старых имён в `src/` не осталось,
а некоторые пункты неприменимы — проект эти вызовы вовсе не делает.

- `QRegExp` → `QRegularExpression` везде — ✅
- `QStringRef` → `QStringView` — ✅
- `qSort`/`qStableSort` → `std::sort` — ✅
- `QTextCodec` → `QStringConverter` — ✅
- `enterEvent(QEvent*)` → `enterEvent(QEnterEvent*)` — **неприменимо:** проект не переопределяет `enterEvent`, а держит свой событийный фильтр (`eventFilter()` / `nativeEventFilter()`, 11 мест)
- `QDesktopWidget` → `QScreen` — ✅
- `QLabel::pixmap()` — возврат по значению — **неприменимо:** геттер нигде не вызывается, есть только `setPixmap(QPixmap())`
- `QList<T>` перестал быть COW — проверять копирования в hot path — ✅ (контейнеры в параметрах только по ссылке)

---

## Тестирование (doctest + trompeloeil)

### doctest (v2.x)

| Принцип | Правило |
|---------|---------|
| Режим main | `DOCTEST_CONFIG_IMPLEMENT` + явный `main()` — **обязательно** для Qt-тестов (иначе segfault при статическом QApplication) |
| `DOCTEST_CONFIG_SUPER_FAST_ASSERTS` | Даёт 31–91% ускорения компиляции, но отладчик ломается на внутренней функции |
| Выбор ассерта | `REQUIRE` — немедленный выход (fatal); `CHECK` — продолжение после фейла |
| Binary asserts | `CHECK_EQ`, `CHECK_GT` — на 57–68% **быстрее** expression-decomposing asserts |
| Иерархия | `SUBCASE` — каждая ветка запускается с чистого листа |
| Логирование при фейлах | `INFO`/`CAPTURE` — ленивое логирование |
| Типизированные тесты | `TEST_CASE_TEMPLATE` — для parameterized по типам |
| QFlags | `CHECK(x.testFlag(Ok))` — вместо `CHECK(x & Ok)` (ExpressionTooComplex) |
| Конфликты имён | `DOCTEST_CONFIG_NO_SHORT_MACRO_NAMES` при конфликте с Boost |
| Char\* | `DOCTEST_CONFIG_TREAT_CHAR_STAR_AS_STRING` — включить глобально |

### Trompeloeil (v4x+)

| Принцип | Правило |
|---------|---------|
| Объявление mock | `MAKE_MOCK(n, auto(...) -> ret_type)` — предпочтительный variadic синтаксис |
| Ожидания | `ALLOW_CALL` — опциональные вызовы; `REQUIRE_CALL` — обязательные; `NAMED_REQUIRE_CALL` — ручное управление временем жизни |
| Параметры | `WITH(expr)` — копия значений; `LR_WITH(expr)` — захват по ссылке |
| Порядок вызовов | `trompeloeil::sequence` — для строгой последовательности |
| Трассировка | `trompeloeil::stream_tracer tracer(std::cerr)` — печать всех вызовов mock-функций |
| Адаптер | `#include <doctest/trompeloeil.hpp>` — после `<doctest/doctest.h>` |

### Trompeloeil + Qt — особые случаи

1. **Q_OBJECT несовместим** — mock-интерфейс без Q_OBJECT, реализация с Q_OBJECT.
2. **Невиртуальные методы не мокаются** — нужен интерфейс (чисто виртуальный базовый класс).
3. **QSignalSpy** — родная альтернатива trompeloeil для проверки сигналов.
4. **DOCTEST_CONFIG_DISABLE** — убирает ВСЁ тестовое из бинарника; не оставлять mock-вызовы.

### Грабли (личный опыт)

1. **Static QApplication** → segfault на выходе из `IMPLEMENT_WITH_MAIN`. **Фикс:** явный `main()` с QApplication на стеке.
2. **AsyncDownload::get() не virtual** → mock через `setDownloader()`, не через `override`.
3. **QFlags + doctest** → auto var на стек, потом `CHECK(var.testFlag(Ok))`.
4. **Trompeloeil + Q_OBJECT** → ошибка компиляции (макросы moc конфликтуют с макросами mock).

### Параллельный прогон тестов

Разные сьюты могут писать друг другу в общий INI-файл через `QSettings`. Для
тестов, работающих с конфигом (`test-config`, `test-playlist-model`,
`test-current-playlist-columns`), выставляется приватный `XDG_CONFIG_HOME`.
При добавлении нового такого сьюта — не забудьте сделать то же самое.

---

## Сборка (CMake + Nix)

### CMake

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```

Ключи:
- `-DBUILD_TESTS=ON` — сборка тестов (без него `ctest` ответит «No tests were found»).
- Сборка ведётся вне дерева исходников (`-B <path>`). Домашний раздел (`/home`)
  смонтирован `noexec`, поэтому бинарники тестов должны лежать за его пределами
  (обычно `/tmp/gmpc-build`).

### Nix

Сборочное окружение описывается `flake.nix`. Все зависимости (Qt6, libmpdclient,
trompeloeil, doctest) подтягиваются через `nix develop`. Сборка и тесты
воспроизводятся **только** внутри этого окружения.

```sh
nix develop
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```

### Санитайзеры

Для сборки с санитайзерами есть отдельный `build-asan/`:

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DSANITIZE=address,undefined
cmake --build build-asan
```

### Интеграционные тесты (живой MPD)

Интеграционные тесты с реальным MPD-сервером работают только при установленной
переменной окружения `GMPC_QT_TEST_MPD` (путь к сокету). Для мутационных тестов
(добавление/удаление трэков) нужен одноразовый сервер и
`GMPC_QT_TEST_MPD_MUTATE=1`.

```sh
export GMPC_QT_TEST_MPD=/tmp/gmpc-scratch/mpd/sock
export GMPC_QT_TEST_MPD_MUTATE=1
ctest --test-dir build
```

---

## Архитектура проекта

### Основные слои

```
src/
├── core/           # Транспорт, модели, синхронизация (без GUI)
│   ├── connection.h/cpp         # Соединение с MPD (переподключение, error-state)
│   ├── idle-watcher.h/cpp       # Событийный цикл idle (свой поток + QSocketNotifier)
│   ├── mpd-commands.h/cpp       # Все команды MPD (виртуальный интерфейс для моков)
│   ├── mpd-endpoint.h/cpp       # Хелпер открытия соединения (TCP/Unix)
│   ├── mpd-error.h/cpp          # Классификация ошибок libmpdclient
│   ├── playlist-model.h/cpp     # QAbstractTableModel для очереди
│   ├── queue-sync.h/cpp         # Зеркало очереди вне GUI-потока
│   ├── queue-diff.h/cpp         # Чистое слияние диффа plchanges
│   ├── search-engine.h/cpp      # Поиск по базе (свой поток)
│   └── config.h/cpp             # Конфиг и профили QSettings
├── gui/            # Окна и виджеты главного интерфейса
│   ├── main-window.h/cpp        # Главное окно, меню, статус-бар
│   ├── control-bar.h/cpp        # Панель управления (play/stop/next/prev/volume/seek)
│   ├── preferences-dialog.h/cpp # Окно настроек (12 групп)
│   ├── tray-icon.h/cpp          # Системный трей
│   ├── browser-stack.h/cpp      # Стек браузеров (QStackedWidget + lifecycle)
│   ├── column-chooser-dialog.h/cpp  # Настройка колонок очереди
│   ├── add-url-dialog.h/cpp     # Диалог добавления URL
│   ├── song-info-dialog.h/cpp   # Окно метаданных трека
│   └── server-info-dialog.h/cpp # Статистика MPD-сервера
├── browsers/       # Страницы-браузеры (наследники BrowserWidget)
│   ├── current-playlist.h/cpp   # Текущая очередь
│   ├── database-browser.h/cpp   # Дерево базы (Artist/Album/Track)
│   ├── file-browser.h/cpp       # Файловое дерево
│   ├── search-browser.h/cpp     # Поиск
│   ├── playlist-editor.h/cpp    # Редактор сохранённых плейлистов
│   └── now-playing.h/cpp        # Страница «Сейчас играет» (обложка + лирика)
├── widgets/        # Переиспользуемые виджеты
│   ├── progress-slider.h/cpp    # Ползунок перемотки (промилле → секунды)
│   ├── volume-widget.h/cpp      # Регулятор громкости
│   ├── cover-widget.h/cpp       # Виджет обложки
│   └── song-info.h/cpp          # Отображение тегов трека
├── metadata/       # Кэш обложек и лирики
│   ├── cache-sqlite.h/cpp       # SQLite-кэш
│   ├── metadata-manager.h/cpp   # Менеджер метаданных (каскад провайдеров)
│   └── cover-provider.h/cpp     # Провайдер обложек
├── providers/      # Внешние провайдеры (Last.fm, LrcLib, lyrics.ovh и т.д.)
├── mpris/          # MPRIS2 (DBus)
│   └── mpris-player.h/cpp       # MPRIS2-интерфейс
├── tools/          # Утилиты
│   ├── async-download.h/cpp     # Асинхронная загрузка
│   └── log.h/cpp                # Логирование
└── ipc/            # IPC между экземплярами
    └── ipc-server.h/cpp         # Unix-socket IPC (один экземпляр, эмиссия сигналов)
```

### Ключевые решения

- **Единый источник истины о текущем треке:** `PlaylistModel::setCurrentSong(songId, songPos)` —
  разрешение по `songid` с fallback на позицию, сигнал `currentSongChanged(int)`.
- **Очередь вне GUI-потока:** `QueueSync` — своё соединение + свой поток + дифф через `plchanges`.
  Полный снимок очереди только при коннекте (914 мс для 65k треков), далее инкрементально.
- **События MPD:** `IdleWatcher` — своё соединение + свой поток + `QSocketNotifier` на fd.
  Маска: `PLAYER|OPTIONS|MIXER|QUEUE|STORED_PLAYLIST|DATABASE|UPDATE|OUTPUT`.
- **Поиск:** `SearchEngine` — worker-поток со своим соединением, дебаунс 300 мс,
  any-tag (один `mpd_search_add_any_tag_constraint`), окно + одна сортировка `ArtistSort`.
- **Контекстные меню:** Собираются из намерения строки (`BrowserIntent`), а не дублируются
  по браузерам. Состав проверяется юнит-тестами без сервера.
- **MPRIS:** Отдельный слой без зависимостей от GUI. `Metadata` собирается из `currentsong`,
  кэшируется по `songId`.

### IPC (межпроцессное взаимодействие)

Спецификация протокола — [`IPC.md`](IPC.md). Используется для:
- Запрета второго GUI-экземпляра (дубликат отправляет команду первому и завершается).
- Передачи команд из командной строки (play/pause/next/prev/toggle и т.д.).
- Эмиссии сигналов всем подключённым клиентам (например, при смене трека).

---

## Интеграция с окружением

### Трей

Только `QSystemTrayIcon` — никаких AppIndicator и GNOME-специфичных API.
Работает в X11-треях (tray2, waybar, polybar, stalonetray) и в минималистичных
WM/DE (awesome, hyprland, i3, sway, XFCE, LXQt). GNOME без расширения
AppIndicator — их проблемы.

### DBus

Используется ровно в одном месте — MPRIS (`src/mpris/mpris-player.*`), и это
внешний интерфейс приложения, а не зависимость от окружения; трей от него не
зависит.

### Мультимедийные клавиши

X11 native event filter на `xcb-keysyms` (единственная дополнительная зависимость
сборки). Привязки живут в `Config`, правятся в Preferences.
