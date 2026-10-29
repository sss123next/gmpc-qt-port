# gmpc-qt — План работ

**Цель:** Портировать gmpc (GTK2 MPD-клиент) на Qt6/C++20 с сохранением внешнего
интерфейса и максимальной эффективностью кода.

## Статус

| Component | Status | Note |
|-----------|--------|------|
| core::Connection | ✅ DONE | libmpdclient wrapper, QTimer polling (500ms) |
| core::MpdCommands | ✅ DONE | 20+ player commands, MPD macro for conn_->raw() |
| core::PlaylistModel | ✅ DONE | QAbstractListModel, loads queue via mpd_send_list_queue_meta |
| core::Config | ✅ DONE | QSettings wrapper, basic implementation |
| gui::MainWindow | ✅ DONE | QMainWindow, sidebar, stacked browser views, tray |
| gui::ControlBar | ✅ DONE | Transport, seek/volume, repeat/random/single/consume |
| gui::StatusBar | ✅ DONE | Connection state + info label |
| gui::TrayIcon | ✅ DONE | QSystemTrayIcon, play/pause/next/prev/quit |
| browsers::CurrentPlaylist | ✅ DONE | QTreeView + PlaylistModel |
| browsers::DatabaseBrowser | 🟡 PARTIAL | QTreeView + MusicDatabaseModel skeleton, 4 trompeloeil tests |
| browsers::FileBrowser | ✅ DONE | QTreeView + FileSystemModel (lazy loading via listmeta), 8 trompeloeil tests |
| browsers::SearchBrowser | ✅ DONE | QLineEdit + QTreeView + SearchResultModel, 12 trompeloeil tests |
| browsers::PlaylistEditor | ✅ DONE | Dual QListWidget + CRUD operations, 6 trompeloeil tests |
| widgets::VolumeWidget | 🟡 SKELETON | QWidget only declared |
| widgets::ProgressSlider | 🟡 SKELETON | QWidget only declared |
| widgets::SongInfo | 🟡 SKELETON | QWidget only declared |
| widgets::CoverWidget | 🟡 SKELETON | QWidget only declared |
| metadata::CacheSqlite | ✅ DONE | SQLite key-value cache |
| metadata::MetadataManager | 🟡 SKELETON | Stub |
| metadata::CoverProvider | 🟡 SKELETON | Stub |
| providers::LastFM | ✅ DONE | API album.getInfo, extracts cover URL |
| providers::MusicBrainz | ✅ DONE | MusicBrainz → Cover Art Archive, keyless |
| providers::Discogs | 🟡 SKELETON | Live API, needs OAuth — postponed |
| providers::LrcLib | ✅ DONE | LRCLIB, keyless, plain + synced lyrics |
| tools::AsyncDownload | ✅ DONE | QNetworkAccessManager-based HTTP, get(QUrl) + get(QNetworkRequest) |
| tools::Log | ✅ DONE | LOG_DEBUG/INFO/WARN/ERROR macros + file output |
| tests | ✅ DONE | doctest + trompeloeil, 55 test cases (7 suites, 100% passing) |
| Build (CMake + Nix flake) | ✅ DONE | Compiles, links, tests pass |

## Cover-art pipeline

```
MusicBrainz (CAA) ─── первичный, без ключа, бесплатно
    ↓ (не найдено)
Last.FM ───────────── резерв, требует бесплатный API-ключ (album.getInfo)
    ↓ (не найдено)
Deezer ────────────── резерв, без ключа, поиск по артисту/альбому (TODO)
```

- MusicBrainz: 2-фазный — поиск release MBID → Cover Art Archive `/release/{mbid}/front`
- Last.FM: `album.getInfo`, выбирает самое большое изображение из массива `image`
- Deezer: пока не реализован (API жив, без ключа, JSON с `album.cover_big`)

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

### Фаза 3 — Виджеты
- [ ] VolumeWidget — кастомный контрол громкости
- [ ] ProgressSlider — кастомный слайдер прогресса
- [ ] SongInfo — отображение информации о треке
- [ ] CoverWidget — отображение обложки

### Фаза 4 — Метаданные и провайдеры
- [ ] CacheSqlite — интеграция с провайдерами
- [ ] MetadataManager — менеджер метаданных
- [ ] CoverProvider — загрузка обложек (MusicBrainz → Last.FM → Deezer)
- [ ] Discogs провайдер — OAuth (отложен)

### Фаза 5 — Полировка
- [ ] Deezer cover provider (резервный)
- [ ] lyrics.ovh (резервный lyrics)
- [ ] Кэширование обложек
- [ ] Горячие клавиши
- [ ] Настройки (окно Preferences)
- [ ] DBus управление (MPRIS)
