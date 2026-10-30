# gmpc → gmpc-qt: План синхронизации GUI

**Цель:** Достичь функционального паритета с оригинальным gmpc (GTK2) по UI/UX.
**Актуально на:** Phase 5d — Preferences: Proxy, Server, Reconnect.
Build: 100% clean. No tests (GUI-only).

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
**Реализация:** `MainWindow::setupShortcuts()` — 11 QShortcut привязок:
- Ctrl+Left/Right/Up/Down → prev/next/togglePause/stop
- Ctrl+Plus/Minus → volumeUp/volumeDown
- Ctrl+R/S/T/Y → toggleRepeat/Random/Single/Consume
- Ctrl+M → muteToggle
- F12 → fullscreen toggle
**Файлы:** `src/gui/mainwindow.h/.cpp`
**Оценка:** ~40 строк (выполнено).

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

### 1.5 Fullscreen (basic) ✅ DONE
Оригинал: отдельное GtkWindow с полупрозрачным фоном, автопрятание 5s.
**Реализация:** `bool fullscreen_` + QShortcut(F12) → showFullScreen/showNormal.
Overlay и auto-hide отложены (P1).
**Файлы:** `src/gui/mainwindow.h/.cpp`
**Оценка:** ~10 строк (выполнено, overlay отложен).

### 1.6 Multimedia keys
Оригинал: X11 GDK key grabbing, 15 actions (mm-keys.c).
**Файлы:** `src/gui/main-window.cpp` + возможно `QAbstractNativeEventFilter`.
**Подзадачи:**
- [ ] Перехват X11 ключей мультимедиа (XF86AudioPlay/Pause/Next/Prev/Stop)
- [ ] Preference page: привязка клавиш к действиям
**Зависимости:** Preferences plugin (2.4).
**Оценка:** ~80 строк.

### 1.7 Preferences: расширение
Оригинал: 10 .ui-страниц, plugin-based sidebar.
У нас: 6 QGroupBox (Connection, Playback, Window, **Proxy**, **Server**, **Tray**).
**Добавлено (Phase 5d):**
- [x] Proxy settings (HTTP host/port, enable toggle)
- [x] Server: music directory
- [x] Server: reconnect delay (500-30000 ms)
- [x] Server: connection timeout (1000-60000 ms)
**Остаётся:**
- [ ] Playlist config (header markup, highlights)
- [ ] Metadata appearance (font/color)
- [ ] Multimedia keys config

### 1.8 Connection profiles
Оригинал: GmpcProfiles (несколько host/port/password).
**Задача:** Хранить список профилей в Config, выбор из меню/Preferences.
**Файлы:** `src/core/config.h/.cpp`
**Оценка:** ~50 строк.

### 1.9 Zoom levels (3 уровня)
Оригинал: Ctrl+Plus/Minus → скрывает sidebar/breadcrumb/tabs.
**Задача:** `setZoomLevel(int level)` — show/hide виджетов.
**Оценка:** ~20 строк.

---

## 2. 🟧 P1 — Существенные пробелы

### 2.1 Rich song header (title_header) — похоронен, заменён на SongInfo

### 2.2 Трей: rich popup (cover + song info)
Оригинал: GtkWindow popup с cover 80px, title bold, artist, album/year, progress.
У нас: QSystemTrayIcon::setToolTip("gmpc-qt - Playing…").
**Задача:** QWidget popup (Qt::ToolTip или собственное окно).
**Файлы:** `src/gui/tray-icon.h/.cpp`
**Зависимости:** CoverWidget integration (1.4).
**Оценка:** ~60 строк.

### 2.3 SongInfo — подключён в MainWindow ✅ DONE

### 2.4 Search browser: field selector
Оригинал: dropdown выбора поля (Artist/Album/Title/…/Query).
У нас: фиксированный мульти-поиск.
**Задача:** QComboBox + переключение режима поиска.
**Файлы:** `src/browsers/search-browser.h/.cpp`
**Оценка:** ~30 строк.

### 2.5 File browser: 2-pane (tree + song list)
Оригинал: GtkHPaned (левая панель = дерево, правая = треки).
У нас: один QTreeView + FileSystemModel.
**Задача:** QSplitter (слева directory tree, справа song list).
**Файлы:** `src/browsers/file-browser.h/.cpp`
**Оценка:** ~40 строк.

### 2.6 Database browser: multi-tag config
Оригинал: несколько конфигурируемых tag-браузеров.
У нас: один Artist→Album→Track.
**Задача:** Allow выбор tag-группы (Artist/Genre/Year/Composer).
**Файлы:** `src/browsers/database-browser.h/.cpp`
**Оценка:** ~30 строк.

---

## 3. 🟡 P2 — Малые/косметические

### 3.1 Window position restore
**Файлы:** `src/core/config.h/.cpp`, `src/gui/main-window.cpp`
**Задача:** Сохранять/восстанавливать `window/x`, `window/y`, `window/maximized`.
**Оценка:** ~10 строк.

### 3.2 Breadcrumb / tab bar
**Задача:** QComboBox или QTabBar над QStackedWidget в browser area.
**Оценка:** ~20 строк.

### 3.3 Статус repeat/random в отдельной панели
Оригинал: внизу окна, иконки.
У нас: в ControlBar, текст "R/Z/1/C".
**Задача:** Перенести в отдельную строку или заменить на иконки.
**Оценка:** ~15 строк.

### 3.4 Hover tooltip на seek slider
**Задача:** `QSlider::setToolTip()` при движении ползунка — "MM:SS / MM:SS".
**Файлы:** `src/widgets/progress-slider.cpp` или `src/gui/control-bar.cpp`
**Оценка:** 5 строк.

### 3.5 Countdown mode
**Задача:** Тумблер в ControlBar: elapsed / remaining.
**Файлы:** `src/gui/control-bar.h/.cpp`
**Оценка:** ~10 строк.

### 3.6 Search-as-you-type в CurrentPlaylist
**Задача:** QLineEdit поверх QTreeView + фильтр по модели.
**Файлы:** `src/browsers/current-playlist.h/.cpp`
**Оценка:** ~15 строк.

### 3.7 Artist image в sidebar
**Задача:** QLabel над QTreeView в sidebar.
**Файлы:** `src/gui/main-window.cpp`
**Оценка:** 5 строк.

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
| 🟥 P0 | 9 | 5 (1.1, 1.2, 1.4, 1.5, 1.7) | ~680 (выполнено ~280) |
| 🟧 P1 | 6 | 1 (2.3) | ~210 (выполнено ~30) |
| 🟡 P2 | 7 | 0 | ~80 |
| ⚰️ Dead | 3 | 3 (✅ все) | ~10 (подключить) |
| **Итого** | **25** | **9** | **~980 строк (выполнено ~320)** |

Исходный gmpc: ~30000 строк C (GTK2 + Vala + .ui).
gmpc-qt: ~4650 строк C++ (рост +650 строк за Phase 4-5).
Потенциальный паритет при 25 пунктах: ~5000 строк.

---

## 6. Рекомендуемый порядок

```
Phase 5a: Hotkeys + контекстные меню            (P0, ✅ DONE, ~120 строк)
Phase 5b: Мёртвый код — revive виджетов          (⚰️, ✅ DONE, ~30 строк)
Phase 5c: Трей (AppIndicator, rich popup)        (P0, ✅ DONE, ~150 строк)
Phase 5d: Preferences — расширение               (P0, ✅ DONE, ~120 строк)
Phase 5e: Connection profiles                     (P0, ~50 строк)
Phase 5f: Database browser URI + context menu      (P1, ~30 строк)
Phase 5g: Browser доработки                       (P1, ~120 строк)
Phase 5h: Fullscreen overlay + Zoom               (P0+P0, ~120 строк)
Phase 5i: Multimedia keys                         (P0, ~80 строк)
Phase 5j: Малые улучшения                         (P2, ~80 строк)
```

**Итог:** 10 мини-фаз, ~660 строк нового кода, 0 новых зависимостей.
8/25 пунктов выполнено (32%), ~200 строк написано, CLI target ~4500 строк C++.
