# gmpc-qt — План работ

**Цель:** Портировать gmpc (GTK2 MPD-клиент) на Qt6/C++20 с сохранением внешнего интерфейса и максимальной эффективностью кода.

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
| providers::LastFM | 🟡 SKELETON | Заглушка |
| providers::Discogs | 🟡 SKELETON | Заглушка |
| providers::LyrDB | 🟡 SKELETON | Заглушка |
| tools::AsyncDownload | ✅ DONE | QNetworkAccessManager-based HTTP |
| tools::Log | ✅ DONE | LOG_DEBUG/INFO/WARN/ERROR макросы + файловый вывод |
| Build (CMake + Nix flake) | ✅ DONE | Собирается, линкуется |

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
- [ ] CacheSqlite (DONE) → интеграция
- [ ] MetadataManager — менеджер метаданных
- [ ] CoverProvider — загрузка обложек
- [ ] LastFM провайдер
- [ ] Discogs провайдер
- [ ] LyrDB провайдер (тексты песен)

### Фаза 5 — Полировка
- [ ] Кэширование обложек
- [ ] Горячие клавиши
- [ ] Настройки (окно Preferences)
- [ ] DBus управление (MPRIS)
- [ ] Тесты
