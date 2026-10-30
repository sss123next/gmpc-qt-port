# gmpc → gmpc-qt: План синхронизации GUI

**Цель:** Достичь функционального паритета с оригинальным gmpc (GTK2) по UI/UX.
**Актуально на:** Phase 5l — Multimedia keys config (Preferences).
Build: 100% clean (0 warnings). No tests (GUI-only). RSS (playlist loaded): ~110 MB.

**DE-agnostic policy:** Не используем AppIndicator, DBus, GNOME-специфичные API.
Всё через `QSystemTrayIcon` — работает во всех X11-треях (tray2, waybar, polybar, stalonetray)
и минималистичных DE (awesome, hyprland, i3, sway, XFCE, LXQt).
GNOME без расширения AppIndicator — их проблемы.

---

## Приоритеты

| Уровень | Критерий |
|---------|----------|
| 🟥 P0 | Функционал полностью отсутствует — breakage UX |
| 🟧 P1 | Функционал есть, но урезан — regression |
| 🟡 P2 | Косметика / удобства |
| ⚰️ Dead | Написан, не используется |

---

## 1. 🟥 P0 — Критические пробелы

### 1.1 Hotkeys / Keyboard shortcuts ✅ DONE
Оригинал: ~30 акселераторов (Ctrl+Left/Right/Up/Down, F12, Ctrl+Space, Ctrl+R/S/T/Y/M/E…).
**Реализация:** `MainWindow::setupShortcuts()` — 12 QShortcut привязок:
- Ctrl+Left/Right/Up/Down → prev/next/togglePause/stop
- Ctrl+Plus/Minus → zoomIn/zoomOut (см. 1.9)
- Ctrl+R/S/T/Y → toggleRepeat/Random/Single/Consume
- Ctrl+M → muteToggle
- F12 → fullscreen toggle (см. 1.5)
**Файлы:** `src/gui/mainwindow.h/.cpp`
**Оценка:** ~50 строк (выполнено).

### 1.2 Context menus (Search + File browsers) ✅ DONE
Оригинал: правый клик → PopupMenu (Add, Play, Replace, Crop, Info).
**Реализация:** `MainWindow::setupBrowserContextMenu()` + `MpdCommands::addUri/playPos/clearQueue`.  
Активировано для:
- `SearchBrowser` — URI из `SearchResultItem::uri`
- `FileBrowser` — URI через `FileSystemModel::UriRole` (новая роль `Qt::UserRole+1`)
- `DatabaseBrowser` — отложено (модель не хранит URI)
- `CurrentPlaylist` — отложено (не критично, т.к. Queue уже показана)
**Файлы:** `src/core/mpd-commands.h/.cpp`, `src/gui/mainwindow.h/.cpp`, `src/browsers/file-system-model.h/.cpp`
**Зависимости:** новые методы `addUri`, `playPos`, `clearQueue` в MpdCommands.
**Оценка:** ~80 строк (выполнено).

### 1.3 Tray icon — полноценный ✅ DONE
Оригинал: gmpc-tray-icon2.c (~1200 строк, AppIndicator + StatusIcon, rich popup).
**Реализация:** `TrayIcon` через `QSystemTrayIcon`. Rich tooltip с названием трека.
Меню: Show/Hide, Play/Pause, Stop, Next, Previous, Quit.
Preference: enable/disable в Preferences.
`MainWindow`: `startHidden` и `trayEnabled` учитываются при старте.
**DE-agnostic:** без AppIndicator, работает во всех X11-треях.
**Файлы:** `src/gui/trayicon.h/.cpp`, `src/gui/preferences-dialog.h/.cpp`,
`src/core/config.h/.cpp`
**Оценка:** ~150 строк (выполнено).

### 1.4 Cover art + SongInfo + VolumeWidget в MainWindow ✅ DONE
Оригинал: `metaimage_album_art` в `hbox_playlist_player` (верх).
У нас: все три виджета написаны, не размещены.  
**Реализация:** Созданы в `MainWindow::setupUi()`, инжектированы в `ControlBar`.  
CoverWidget (128×128 слева), SongInfo (справа от cover), VolumeWidget (справа, с крутилкой).
**Файлы:** `src/gui/mainwindow.h/.cpp`, `src/gui/controlbar.h/.cpp`
**Оценка:** ~30 строк (выполнено).

### 1.5 Fullscreen overlay + auto-hide ✅ DONE
Оригинал: отдельное GtkWindow с полупрозрачным фоном, автопрятание 5s.
**Реализация:** `toggleFullscreenOverlay()` — F12 → showFullScreen/showNormal.
5s QTimer (overlayTimer_) → hide controlBar. mouseMoveEvent(Y<80) → show + reset timer.
**Файлы:** `src/gui/mainwindow.h/.cpp`
**Оценка:** ~35 строк (выполнено).

### 1.6 Multimedia keys
Оригинал: X11 GDK key grabbing, 15 actions (mm-keys.c).
**Файлы:** `src/gui/mainwindow.cpp` + `QAbstractNativeEventFilter`.
**Подзадачи:**
- [x] Перехват X11 ключей мультимедиа (XF86AudioPlay/Pause/Next/Prev/Stop)
- [x] Preference page: привязка клавиш к действиям
**Реализация:**
- MediaKeyFilter: X11 native event filter через xcb-keysyms, routing через Config::actionForKey()
- Config: `MediaKeyAction` enum, `MediaKeyBinding` struct, `mediaKeyBindings()` c 8 дефолтными XF86Audio keysym → action
- Preferences: "Multimedia Keys" QGroupBox с QComboBox (8 actions) для каждой из 8 XF86Audio-клавиш
- Сохранение через QSettings, синхронизация при accept()
**Зависимости:** xcb-keysyms, Config
**Оценка:** ~120 строк (выполнено).

### 1.7 Preferences: расширение
Оригинал: 10 .ui-страниц, plugin-based sidebar.
У нас: 6 QGroupBox (Connection, Playback, Window, **Proxy**, **Server**, **Tray**).
**Добавлено (Phase 5d):**
- [x] Proxy settings (HTTP host/port, enable toggle)
- [x] Server: music directory
- [x] Server: reconnect delay (500-30000 ms)
- [x] Server: connection timeout (1000-60000 ms)
**Остаётся:**
- [x] Playlist config (header markup, highlights)
- [x] Metadata appearance (font/color)
- [x] Multimedia keys config

### 1.8 Connection profiles ✅ DONE
Оригинал: GmpcProfiles (несколько host/port/password).
**Реализация:**
- `ConnectionProfile` struct в config.h с name/host/port/password/socketPath/autoConnect
- `Config::profiles()` / `Config::setProfiles()` / `Config::activeProfile()` — хранение через QSettings array
- Preferences: QListWidget + Add/Remove кнопки (редактирование через Connection group)
- MainWindow: File → Connection подменю со списком профилей + Disconnect
- Выбор профиля: разрыв соединения + переподключение с параметрами профиля
**Файлы:** `src/core/config.h/.cpp`, `src/gui/preferences-dialog.h/.cpp`, `src/gui/mainwindow.cpp`
**Оценка:** ~120 строк (выполнено).

### 1.9 Zoom levels (3 уровня) ✅ DONE
Оригинал: Ctrl+Plus/Minus → скрывает sidebar/breadcrumb/tabs.
**Реализация:** `setupZoomLevels()` — Ctrl+Plus/Qt::Key_Plus, Ctrl+Minus/Qt::Key_Minus.
`setZoomLevel(int)`: 0=full, 1=no sidebar, 2=compact (no sidebar, no statusbar).
Ctrl+Plus/Minus volume shortcuts удалены (теперь только VolumeWidget).
**Файлы:** `src/gui/mainwindow.h/.cpp`
**Оценка:** ~25 строк (выполнено).

---

## 2. 🟧 P1 — Существенные пробелы

### 2.1 Rich song header (title_header) — похоронен, заменён на SongInfo

### 2.2 Трей: rich tooltip (cover + song info) ✅ DONE
Оригинал: GtkWindow popup с cover 80px, title bold, artist, album/year, progress.
У нас: `QSystemTrayIcon::setToolTip("gmpc-qt — Artist — Title [Album]")` с полными
метаданными при PLAY. При PAUSE/STOP — краткий статус.
**Файлы:** `src/gui/tray-icon.h/.cpp`
**Оценка:** ~10 строк (выполнено).

### 2.3 SongInfo — подключён в MainWindow ✅ DONE

### 2.4 Search browser: field selector ✅ DONE
Оригинал: dropdown выбора поля (Artist/Album/Title/…/Query).
У нас: фиксированный мульти-поиск.
**Реализация:** QComboBox с 6 вариантами (All/Artist/Album/Title/Genre/Composer).
При "All" — мульти-поиск по 7 тегам, при конкретном — single-tag через
`mpd_search_add_tag_constraint`. Поле поиска перенесено в один HBoxLayout с комбо.
**Файлы:** `src/browsers/search-browser.h/.cpp`
**Оценка:** ~60 строк (выполнено).

### 2.5 File browser: 2-pane (tree + song list) ✅ DONE
Оригинал: GtkHPaned (левая панель = дерево, правая = треки).
У нас: один QTreeView + FileSystemModel.
**Реализация:** QSplitter с двумя QTreeView. Левый — дерево директорий.
Правый — FileContentProxy (QSortFilterProxyModel), показывает плоские списки
файлов/плейлистов в выбранной директории. FileSystemModel расширен до 3 колонок
(Name, Type, Duration), FileNode::duration заполняется из mpd_song_get_duration().
**Файлы:** `src/browsers/file-browser.h/.cpp`, `src/browsers/file-system-model.h/.cpp`
**Оценка:** ~140 строк (выполнено).

### 2.6 Database browser: multi-tag config ✅ DONE
Оригинал: несколько конфигурируемых tag-браузеров.
У нас: один Artist→Album→Track.
**Реализация:** QComboBox с 4 вариантами (Artist/Genre/Year/Composer).
MusicDatabaseModel::loadByTag() сбрасывает модель через beginResetModel()
и перезагружает корневые элементы через mpd_search_db_tags(MPD_TAG_...).
**Файлы:** `src/browsers/database-browser.h/.cpp`, `src/browsers/music-database-model.h/.cpp`
**Оценка:** ~50 строк (выполнено).

### 2.7 Memory: adaptive polling ✅ DONE
Connection::pollStatus() переключает интервал: 500ms при PLAY, 2000ms при STOP/PAUSE.
Снижает idle polling traffic в 4×. kDefaultPollMs = 500, адаптируется динамически.
**Файлы:** `src/core/connection.cpp`
**Оценка:** 5 строк (выполнено).

---

## 3. 🟡 P2 — Малые/косметические

### 3.1 Window position restore ✅ DONE
**Реализация:** `Config::windowX/Y/maximized()`, сохранение в `closeEvent()`,
восстановление в `setupUi()` через `move(wx,wy)` + `showMaximized()`.
**Файлы:** `src/core/config.h/.cpp`, `src/gui/mainwindow.cpp`
**Оценка:** ~15 строк (выполнено).

### 3.2 Breadcrumb / tab bar ✅ DONE
**Реализация:** QTabBar над QStackedWidget в правой панели (rightPanel).
Бидирекциональная синхронизация — tabBar_->currentChanged → centralStack_->setCurrentIndex
и обратно с blockSignals. Zoom-aware (скрывается на уровне 2).
**Файлы:** `src/gui/mainwindow.h/.cpp`
**Оценка:** ~20 строк (выполнено).

### 3.3 Статус repeat/random иконками ✅ DONE
**Реализация:** Кнопки "R"/"Z"/"1"/"C" заменены на иконки
QIcon::fromTheme с fallback на QStyle::standardIcon. Tooltip-подписи.
**Файлы:** `src/gui/controlbar.h/.cpp`
**Оценка:** ~15 строк (выполнено).

### 3.4 Hover tooltip на seek slider ✅ DONE
**Реализация:** `ProgressSlider::mouseMoveEvent` — QToolTip::showText с "MM:SS / MM:SS"
при drag. `setMouseTracking(true)`, `total_` поле для форматирования.
**Файлы:** `src/widgets/progress-slider.h/.cpp`
**Оценка:** ~15 строк (выполнено).

### 3.5 Countdown mode ✅ DONE
**Реализация:** QPushButton "C" (checkable) тумблер countdown_ в ControlBar.
`updateStatus()` форматирует `total - elapsed` при countdown_ == true.
**Файлы:** `src/gui/controlbar.h/.cpp`
**Оценка:** ~10 строк (выполнено).

### 3.6 Search-as-you-type в CurrentPlaylist ✅ DONE
**Реализация:** QLineEdit поверх QTreeView + QSortFilterProxyModel.
Фильтр по всем колонкам, case-insensitive.
**Файлы:** `src/browsers/current-playlist.h/.cpp`
**Оценка:** ~20 строк (выполнено).

### 3.7 Artist image в sidebar ✅ DONE
**Реализация:** QLabel (128px height, scaledContents, centred) над QTreeView
в sidebarPanel_ (QVBoxLayout-обёртка). zoomLevel скрывает/показывает sidebarPanel_
вместо sidebar_. Стиль: palette(window) фон, disabled(text) для "No cover".
**Файлы:** `src/gui/mainwindow.h/.cpp`
**Оценка:** 5 строк (выполнено).

---

## 4. ⚰️ Мёртвый код

| Файл | Статус | Решение |
|------|--------|---------|
| `src/widgets/song-info.cpp` | ✅ Revived | Подключён в MainWindow, инжектирован в ControlBar |
| `src/widgets/cover-widget.cpp` | ✅ Revived | Создан в MainWindow, инжектирован в ControlBar (128×128) |
| `src/widgets/volume-widget.cpp` | ✅ Revived | Инжектирован в ControlBar, дублирующий QSlider удалён |

---

## 5. Оценка общих усилий

| Категория | Пунктов | Выполнено | Примерное LOC |
|-----------|---------|-----------|---------------|
| 🟥 P0 | 9 | 9 (1.1–1.9) | ~800 (выполнено ~800) |
| 🟧 P1 | 7 | 5 (2.2–2.7) | ~250 (выполнено ~210) |
| 🟡 P2 | 7 | 7 (3.1–3.7) | ~80 (выполнено ~80) |
| ⚰️ Dead | 3 | 3 (✅ все) | ~10 (подключить) |
| **Итого** | **26** | **24** | **~1140 строк (выполнено ~1090)** |

Исходный gmpc: ~30000 строк C (GTK2 + Vala + .ui).
gmpc-qt: ~5500 строк C++ (рост +100 строк за Phase 5l).
Потенциальный паритет при 26 пунктах: ~5500 строк.

---

## 6. Рекомендуемый порядок

```
Phase 5a: Hotkeys + контекстные меню            (P0, ✅ DONE, ~120 строк)
Phase 5b: Мёртвый код — revive виджетов          (⚰️, ✅ DONE, ~30 строк)
Phase 5c: Трей (AppIndicator, rich popup)        (P0, ✅ DONE, ~150 строк)
Phase 5d: Preferences — расширение               (P0, ✅ DONE, ~120 строк)
Phase 5e: Connection profiles                    (P0, ✅ DONE, ~120 строк)
Phase 5f: Database browser URI + context menu      (P1, ✅ DONE, ~30 строк)
Phase 5g: Browser доработки + memory оптимизация  (P1, ✅ DONE, ~350 строк)
Phase 5h: Fullscreen overlay + Zoom               (P0+P0, ✅ DONE, ~60 строк)
Phase 5i: Multimedia keys native event filter      (P0, ✅ DONE, ~80 строк)
Phase 5j: Малые улучщения (tooltip, position, фильтр) (P2+P1, ✅ DONE, ~80 строк)
Phase 5k: P2 финал + Preferences доводка          (P2+P0, ✅ DONE, ~100 строк)
Phase 5l: Multimedia keys config — Preferences    (P0, ✅ DONE, ~120 строк)
```

**Итог:** 12 мини-фаз, ~1400 строк нового кода, 1 новая зависимость (xcb-keysyms).
24/26 пунктов выполнено (92%), ~1090 строк написано, gmpc-qt ~5500 строк C++.
