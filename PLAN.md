# gmpc-qt — План работ

**Цель:** Портировать gmpc (GTK2 MPD-клиент) на Qt6/C++20 с сохранением внешнего
интерфейса и максимальной эффективностью кода.

## Статус

| Component | Status | Note |
|-----------|--------|------|
| core::Connection | ✅ DONE | libmpdclient wrapper, QTimer polling (500ms); rawCommand() FIXME |
| core::MpdCommands | ✅ DONE | 20+ player commands, MPD macro for conn_->raw() |
| core::PlaylistModel | ✅ DONE | QAbstractListModel, loads queue via mpd_send_list_queue_meta |
| core::Config | ✅ DONE | QSettings singleton, 14 props, socket path support |
| gui::MainWindow | ✅ DONE | QMainWindow, sidebar, stacked browser views, tray, Preferences wired |
| gui::ControlBar | ✅ DONE | Transport, seek/volume, repeat/random/single/consume |
| gui::StatusBar | ✅ DONE | Connection state + info label |
| gui::TrayIcon | ✅ DONE | QSystemTrayIcon, play/pause/next/prev/quit |
| gui::PreferencesDialog | ✅ DONE | TCP/Unix socket, password, auto-connect, playback, window groups; 9 tests |
| browsers::CurrentPlaylist | ✅ DONE | QTreeView + PlaylistModel |
| browsers::DatabaseBrowser | ✅ DONE | QTreeView + MusicDatabaseModel (3-level Artist/Album/Track); 14 trompeloeil tests |
| browsers::FileBrowser | ✅ DONE | QTreeView + FileSystemModel (lazy loading via listmeta), 8 trompeloeil tests |
| browsers::SearchBrowser | ✅ DONE | QLineEdit + QTreeView + SearchResultModel, 12 trompeloeil tests |
| browsers::PlaylistEditor | ✅ DONE | Dual QListWidget + CRUD operations, 6 trompeloeil tests |
| widgets::VolumeWidget | ✅ DONE | QSlider + QLabel, 6 tests |
| widgets::ProgressSlider | ✅ DONE | Promille range 0-1000, seek signal, 6 tests |
| widgets::SongInfo | ✅ DONE | Artist/Title/Album labels, 5 tests |
| widgets::CoverWidget | ✅ DONE | setCover/clear/fetchCover via MetadataManager; 4 tests |
| metadata::CacheSqlite | ✅ DONE | SQLite key-value cache with upsert + cleanup |
| metadata::MetadataManager | ✅ DONE | Orchestrator: CoverProvider + LrcLibProvider + CacheSqlite; 2 tests |
| metadata::CoverProvider | ✅ DONE | Cascading pipeline MB → Last.FM → Deezer → Discogs; injectable AsyncDownload; 3 tests |
| providers::LastFM | ✅ DONE | API album.getInfo, extracts largest cover URL |
| providers::MusicBrainz | ✅ DONE | MusicBrainz → Cover Art Archive, keyless |
| providers::Discogs | ✅ DONE | Search API + token (опция); 4-й каскад; Config+Prefs |
| providers::LrcLib | ✅ DONE | LRCLIB, keyless, plain + synced lyrics |
| tools::AsyncDownload | ✅ DONE | QNetworkAccessManager-based HTTP, get(QUrl) + get(QNetworkRequest) |
| tools::Log | ✅ DONE | LOG_DEBUG/INFO/WARN/ERROR macros + file output |
| tests | ✅ DONE | doctest + trompeloeil, **109** test cases (**17** suites, 100% passing) |
| Build (CMake + Nix flake) | ✅ DONE | Compiles, links, tests pass |

## Cover-art pipeline

```
MusicBrainz (CAA) ─── первичный, без ключа, бесплатно
    ↓ (не найдено)
Last.FM ───────────── резерв, требует бесплатный API-ключ (album.getInfo)
    ↓ (не найдено)
Deezer ────────────── резерв, без ключа, поиск по артисту/альбому
    ↓ (не найдено)
Discogs ───────────── 4-й резерв, опциональный Personal Access Token
```

- MusicBrainz: 2-фазный — поиск release MBID → Cover Art Archive `/release/{mbid}/front`
- Last.FM: `album.getInfo`, выбирает самое большое изображение из массива `image`
- Deezer: безключевой `search/album`, берёт `cover_big` из первого совпадения
- Discogs: `database/search?q=ARTIST+ALBUM&type=release`, берёт `cover_image` из первого результата; без токена лимит 25 req/min, с токеном 60 req/min

**Мёртвые/исключённые:** LyrDB, ChartLyrics — оба мертвы. Гугл Images убрать — нелегально.

## Lyrics pipeline

```
LRCLIB ─── первичный, без ключа, plain + synced LRC
    ↓ (не найдено)
lyrics.ovh ────────── резерв, без ключа, только plain (TODO)
```

- LRCLIB: `GET /api/get?artist_name=X&track_name=Y` → JSON с `plainLyrics` и `syncedLyrics`
- lyrics.ovh: `https://api.lyrics.ovh/v1/artist/title` — простой JSON (TODO)

## Очередь

### Фаза 1 — Фундамент (DONE)
- [x] flake.nix + flake.lock (nixpkgs-unstable)
- [x] CMakeLists.txt (Qt6 + libmpdclient + SQLite)
- [x] core::Connection — обёртка libmpdclient
- [x] core::MpdCommands — команды плеера
- [x] core::PlaylistModel — модель очереди
- [x] core::Config — конфиг
- [x] gui::MainWindow — главное окно
- [x] gui::ControlBar — панель управления
- [x] gui::StatusBar — строка состояния
- [x] gui::TrayIcon — системный трей
- [x] providers::LastFM — полноценная имплементация
- [x] providers::MusicBrainz — MusicBrainz + CAA
- [x] providers::LrcLib — LRCLIB lyrics (ex-LyrDB)
- [x] Provider research — валидация живых сервисов
- [x] tests — doctest + trompeloeil (Config, Log, PlaylistModel)

### Phase 2 — Browsers (DONE)
- [x] DatabaseBrowser — database navigation (MusicDatabaseModel skeleton + QTreeView)
- [x] FileBrowser — filesystem navigation (FileSystemModel lazy loading + QTreeView)
- [x] SearchBrowser — full-text search (QLineEdit + SearchResultModel + QTreeView)
- [x] PlaylistEditor — playlist CRUD (dual QListWidget + button bar)

### Фаза 3 — Виджеты (DONE)
- [x] VolumeWidget — кастомный контрол громкости (6 tests)
- [x] ProgressSlider — кастомный слайдер прогресса (6 tests)
- [x] SongInfo — отображение информации о треке (5 tests)
- [x] CoverWidget — отображение обложки (4 tests; fetchCover() пока TODO)

### Фаза 4 — Метаданные и провайдеры (DONE)
- [x] CoverProvider — загрузка обложек (MusicBrainz → Last.FM → Deezer; injectable AsyncDownload)
- [x] MetadataManager — оркестратор: CoverProvider + LrcLibProvider + CacheSqlite
- [x] CoverWidget::fetchCover() — прикрутить MetadataManager, сигналы coverReady/coverNotFound
- [x] DeezerProvider — безключевой REST API (альбомный поиск по артисту/названию)
- [x] CacheSqlite — SQLite key-value кэш (upsert)
- [x] Тесты: CacheSqlite (6 unit), CoverProvider (3 skipped*), DeezerProvider (2 smoke), MetadataManager (2 integration), CoverWidget (4 unit)
- [x] Discogs провайдер — 4-й каскад обложек; опциональный Personal Access Token (Config + Preferences)

### Фаза 5 — Полировка
- [x] lyrics.ovh (резервный lyrics)
- [x] Горячие клавиши
- [x] DBus управление (MPRIS)
- [x] rawCommand() — нормальный парсинг ответа

## Практики и рекомендации

*Синтез лучших практик C++20, Qt6, doctest + trompeloeil. Сохранён в KB:*
*- `research/cpp20-best-practices` (12 сущностей)*
*- `research/qt6-best-practices` (11 сущностей)*
*- `research/doctest-trompeloeil` (7 сущностей)*

---

### 🎯 C++20 — что используем, что нет

**Активно (без оговорок):**

| Фича | Как используем | Важно |
|------|----------------|-------|
| `std::span` | Параметры невладеющих буферов | Передавать **по значению**, **не хранить** как член класса (dangling) |
| `std::string_view` | Константные строковые параметры (не-for-QString) | **Не гарантирует `\0`** — `sv.data()` в C-API → UB. Не возвращать из функции |
| `std::source_location` | Логи — типобезопасная замена `__FILE__ __LINE__` | Паттерн: параметр со значением по умолчанию |
| `[[likely]]/[[unlikely]]` | В горячих путях (без профилирования — гадание) | Только после профилирования |
| `consteval`/`constexpr` | Валидация времени компиляции | `constexpr vector/string` — transient allocation (освобождается в том же контексте) |
| Concepts | Замена SFINAE, улучшение error messages | Стандартные концепты предпочтительнее своих. Избегать вложенных `requires` |
| `= default` comparisons | Тривиальные value-типы | `auto operator<=>(const X&) = default;` |

**Ограниченно (с оговорками):**

| Фича | Условие | Риски |
|------|---------|-------|
| Coroutines | Только идиоматический async-конвейер (async download chain) | Аллокация фрейма, 20-30 строк boilerplate для генератора, ~3-4 уровня косвенности на `co_await` |
| `std::ranges` | filter/transform для однопроходного чтения | На **10-30% медленнее** ручного цикла с `reserve`+`emplace_back`. View **не кэшируются** — каждый проход перевычисляет |
| `std::optional` как возврат | Когда результат может отсутствовать | Не злоупотреблять — `std::variant` предпочтительнее для >2 состояний |

**Не используем:**

| Фича | Причина |
|------|---------|
| Modules | CMake 3.28+ поддержка есть, но IntelliSense/редакторы экспериментальные. `import std;` стабильно только MSVC |
| `std::jthread` | QThread другая модель. Stop_token **кооперативный** — не прерывает блокирующие syscall'ы (read/write) |
| `std::bind` | Лямбды лучше во всех случаях |

**Breaking changes C++17→C++20 (проверить при апгрейде):**
- Aggregate init через `()` (P0960R3) — раньше не работало, теперь работает
- ADL с `==` (P2468R2) — может дать ambiguous
- **Удалены:** `throw()`, `std::is_literal_type`, `std::result_of`, `std::shared_ptr::unique()`, `std::get_temporary_buffer`
- **Deprecated:** `std::is_pod`, `std::aligned_storage`

**Типичные ошибки:**
1. `string_view` не null-terminated → передача `sv.data()` в C-API → UB
2. ranges view — каждый проход перевычисляет transform/filter
3. `span` как член класса — dangling reference
4. `constexpr vector` — transient allocation, нельзя передавать в runtime-функцию

---

### 🏗 Qt6 — наши правила

**Сборка и CMake:**
- `qt_add_executable` вместо `add_executable` (AUTOMOC-friendly)
- `set(CMAKE_AUTOMOC ON)` — обязателен
- `qt_standard_project_setup()` — CMake 3.28+, унифицирует настройку Qt

**Сигналы/слоты:**
- PMF-style `connect(obj, &Class::signal, ...)` — **никаких** `SIGNAL()`/`SLOT()` макросов
- Лямбды в connect — норма, если слот одноразовый или тривиальный
- Для асинхронных тестов: `QSignalSpy::wait()` вместо ручных `QEventLoop`

**Типы:**
- `QStringView` — константные строковые параметры функций
- `QString` — только когда нужно владение, модификацию или передачу в Qt API
- `QRegularExpression` — **никогда** `QRegExp` (удалён в Qt6)
- `QList<T>` = `QVector` — **нет COW с Qt6**, копирование честное
- `QLatin1StringView` (бывший `QLatin1String`) — эффективное сравнение

**Виджеты и графики:**
- `QLabel::pixmap()` возвращает `QPixmap` **по значению** (не `const QPixmap*`)
- QStackedWidget — предпочтительный паттерн для переключения модов (TCP/Unix)
- `QWidget::enterEvent(QEvent*)` → `enterEvent(QEnterEvent*)` — изменилась сигнатура
- `QDesktopWidget` удалён → `QScreen` для информации об экранах

**Сеть:**
- `QNetworkAccessManager::get(QNetworkRequest)` — не принимает `QUrl` напрямую
- `QNetworkRequest` обязателен, даже для простых GET

**Инструментарий (Phase 5):**
- Qt Linguist: `lupdate`/`lrelease` + `tr()` — официальный путь интернационализации
- `QXmlStreamReader` — предпочтительнее DOM/SAX для XML

**Миграция Qt5→Qt6 (чеклист):**
- [ ] `QRegExp` → `QRegularExpression` везде
- [ ] `QStringRef` → `QStringView`
- [ ] `qSort`/`qStableSort` → `std::sort`
- [ ] `QTextCodec` → `QStringConverter`
- [ ] `enterEvent(QEvent*)` → `enterEvent(QEnterEvent*)`
- [ ] `QDesktopWidget` → `QScreen`
- [ ] `QLabel::pixmap()` — возврат по значению
- [ ] `QList<T>` перестал быть COW — проверять копирования в hot path

---

### 🧪 Тестирование (doctest + trompeloeil)

**doctest (v2.x):**

| Принцип | Правило |
|---------|---------|
| Режим main | `DOCTEST_CONFIG_IMPLEMENT` + явный `main()` — **обязательно** для Qt-тестов (иначе segfault при статическом QApplication) |
| `DOCTEST_CONFIG_SUPER_FAST_ASSERTS` | Даёт 31-91% ускорения компиляции, но отладчик ломается на внутренней функции |
| Выбор ассерта | `REQUIRE` — немедленный выход (fatal); `CHECK` — продолжение после фейла |
| Binary asserts | `CHECK_EQ`, `CHECK_GT` — на 57-68% **быстрее** expression-decomposing asserts |
| Иерархия | `SUBCASE` — каждая ветка запускается с чистого листа |
| Логирование при фейлах | `INFO`/`CAPTURE` — ленивое логирование |
| Типизированные тесты | `TEST_CASE_TEMPLATE` — для parameterized по типам |
| QFlags | `CHECK(x.testFlag(Ok))` — вместо `CHECK(x & Ok)` (ExpressionTooComplex) |
| Конфликты имён | `DOCTEST_CONFIG_NO_SHORT_MACRO_NAMES` при конфликте с Boost |
| Char* | `DOCTEST_CONFIG_TREAT_CHAR_STAR_AS_STRING` — включить глобально |

**trompeloeil (v4x+):**

| Принцип | Правило |
|---------|---------|
| Объявление mock | `MAKE_MOCK(n, auto(...) -> ret_type)` — предпочтительный variadic синтаксис |
| Ожидания | `ALLOW_CALL` — опциональные вызовы; `REQUIRE_CALL` — обязательные; `NAMED_REQUIRE_CALL` — ручное управление временем жизни |
| Параметры | `WITH(expr)` — копия значений; `LR_WITH(expr)` — захват по ссылке |
| Порядок вызовов | `trompeloeil::sequence` — для строгой последовательности |
| Трассировка | `trompeloeil::stream_tracer tracer(std::cerr)` — печать всех вызовов mock-функций |
| Адаптер | `#include <doctest/trompeloeil.hpp>` — после `<doctest/doctest.h>` |

**Trompeloeil + Qt:**
1. **Q_OBJECT несовместим** — mock-интерфейс без Q_OBJECT, реализация с Q_OBJECT
2. **Невиртуальные методы не мокаются** — нужен интерфейс (чисто виртуальный базовый класс)
3. **QSignalSpy** — родная альтернатива trompeloeil для проверки сигналов
4. **DOCTEST_CONFIG_DISABLE** — убирает ВСЁ тестовое из бинарника; не оставлять mock-вызовы

**Грабли (личный опыт):**
1. **Static QApplication** → segfault на выходе из `IMPLEMENT_WITH_MAIN`. **Фикс:** явный `main()` с QApplication на стеке
2. **AsyncDownload::get() не virtual** → mock через `setDownloader()`, не через `override`
3. **QFlags + doctest** → auto var на стек, потом `CHECK(var.testFlag(Ok))`
4. **Trompeloeil + Q_OBJECT** → ошибка компиляции (макросы moc конфликтуют с макросами mock)

