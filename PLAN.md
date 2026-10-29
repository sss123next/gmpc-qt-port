# gmpc-qt — План работ

**Цель:** Портировать gmpc (GTK2 MPD-клиент) на Qt6/C++20 с сохранением внешнего
интерфейса и максимальной эффективностью кода.

## Статус

| Компонент | Статус | Примечание |
|-----------|--------|------------|
| core::Connection | ✅ DONE | libmpdclient обёртка, QTimer polling (500ms) |
| core::MpdCommands | ✅ DONE | Все команды плеера (20+), макрос MPD для conn_->raw() |
| core::PlaylistModel | ✅ DONE | QAbstractListModel, загрузка очереди через mpd_send_list_queue_meta |
| core::Config | 🟡 DONE | QSettings обёртка, базовая реализация |
| gui::MainWindow | ✅ DONE | QMainWindow, sidebar, stacked browser views, tray |
| gui::ControlBar | ✅ DONE | Transport, seek/volume, repeat/random/single/consume |
| gui::StatusBar | ✅ DONE | Connection state + info label |
| gui::TrayIcon | ✅ DONE | QSystemTrayIcon, play/pause/next/prev/quit |
| browsers::CurrentPlaylist | ✅ DONE | QTreeView + PlaylistModel |
| browsers::DatabaseBrowser | 🟡 SKELETON | QWidget только объявлен |
| browsers::FileBrowser | 🟡 SKELETON | QWidget только объявлен |
| browsers::SearchBrowser | 🟡 SKELETON | QWidget только объявлен |
| browsers::PlaylistEditor | 🟡 SKELETON | QWidget только объявлен |
| widgets::VolumeWidget | 🟡 SKELETON | QWidget только объявлен |
| widgets::ProgressSlider | 🟡 SKELETON | QWidget только объявлен |
| widgets::SongInfo | 🟡 SKELETON | QWidget только объявлен |
| widgets::CoverWidget | 🟡 SKELETON | QWidget только объявлен |
| metadata::CacheSqlite | ✅ DONE | SQLite key-value cache |
| metadata::MetadataManager | 🟡 SKELETON | Заглушка |
| metadata::CoverProvider | 🟡 SKELETON | Заглушка |
| providers::LastFM | ✅ DONE | API album.getInfo, извлекает URL обложки |
| providers::MusicBrainz | ✅ DONE | MusicBrainz → Cover Art Archive, без ключа |
| providers::Discogs | 🟡 SKELETON | Живой API, нужен OAuth — отложен |
| providers::LrcLib (ex-LyrDB) | ✅ DONE | LRCLIB, без ключа, plain + synced lyrics |
| tools::AsyncDownload | ✅ DONE | QNetworkAccessManager-based HTTP |
| tools::Log | ✅ DONE | LOG_DEBUG/INFO/WARN/ERROR макросы + файловый вывод |
| Build (CMake + Nix flake) | ✅ DONE | Собирается, линкуется |

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

### Фаза 2 — Браузеры (NEXT)
- [ ] DatabaseBrowser — навигация по медиатеке
- [ ] FileBrowser — файловая навигация
- [ ] SearchBrowser — поиск
- [ ] PlaylistEditor — редактирование плейлистов

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
- [ ] Тесты
