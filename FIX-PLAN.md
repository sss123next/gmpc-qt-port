# gmpc-qt — План исправлений по результатам аудита

**Дата:** 2026-11-17
**Основание:** Полный аудит кода и GUI.
**Принцип:** Каждый пункт = конкретный файл + строка + что менять. Без проектирования «на потом».

---

## P0 — Критические: блокируют нормальную работу GUI

### P0.1 FileBrowser — дерево не работает / правая панель пуста

**Файлы:** `src/browsers/file-browser.h`, `src/browsers/file-browser.cpp`, `src/browsers/file-system-model.cpp`

**Диагноз:** Две независимых проблемы.

**Проблема A — левое дерево не грузится при коннекте:**
`FileBrowser::onConnectionChanged(true)` вызывает `model_->refresh()` + `model_->fetchMore({})`.
`fetchMore({})` → `loadDirectory(root_)` → `mpd_send_list_meta(conn, "")`.
- Если `loadDirectory` падает из-за `!conn_->hasHandle()` (мок без handle) — молча возвращается,
  `node->loaded` не ставится, при следующем `canFetchMore` снова попытка — и снова отказ.
- На живом MPD должно работать, но если сервер не отвечает или соединение не установлено,
  `onConnectionChanged(true)` не вызывается вовсе, а `onActivate()` проверяет `conn_->isConnected()`.
- **Дополнительно:** `FileSystemModel::canFetchMore` для корня проверяет `root_->children.isEmpty()`:
  если корень загружен и пуст (нет файлов в music_directory), то `canFetchMore({})` → `false`,
  что верно — нечего грузить. Но если `loadDirectory` упал с ошибкой, `children` пуст, а `loaded` = `false`,
  то повторный `fetchMore` возможен. Проблема: после отказа команды `ResponseScope` чистит ошибку в деструкторе,
  и `loadDirectory` при повторном вызове снова попытается отправить ту же команду. Это цикл, но не бесконечный:
  QTreeView дёргает `fetchMore` только при раскрытии узла.

**Проблема B — правая панель (FileContentProxy) никогда не показывает файлы:**
Коренная причина: `FileContentProxy` — `QSortFilterProxyModel`, применённый к `FileSystemModel` (иерархическому дереву).
Правая панель (`rightView_`) — `QTreeView` с `setRootIsDecorated(false)` (плоский список). Прокси фильтрует
строки на каждом уровне дерева по отдельности, но:
1. Файл `Music/subdir/song.opus` лежит на глубине 2 от корня, а `QTreeView` с `rootIsDecorated(false)`
   показывает только корневой уровень → файл никогда не попадёт в отфильтрованный результат.
2. Логика `filterAcceptsRow` в принципе неверна: она проверяет `parentPath == rootPath_`, где `parentPath` —
   путь пра-родителя (дед), а не прямого родителя.
3. Итог: правая панель всегда пуста.

**Исправление (A — гарантировать загрузку левого дерева):**
1. `FileBrowser::onConnectionChanged()` — проверять, что `model_->canFetchMore({})` после `refresh()` ложь
   (т.е. корень загружен). Если после `fetchMore` корень всё ещё пуст и loaded = false — логировать
   ошибку в `qWarning`.
2. Добавить простой индикатор состояния в `FileBrowser` (QLabel «Loading...» / «Not connected»).

**Исправление (B — починить правую панель):**
1. **Удалить `FileContentProxy` целиком** — класс `file-browser.h` и реализация `file-browser.cpp`.
2. Правая панель должна использовать **ту же модель**, что и левая (`FileSystemModel`), но показывать
   поддерево выбранного каталога через `QTreeView::setRootIndex()`:
   ```cpp
   // Вместо contentProxy_->setSourceModel(model_):
   rightView_->setModel(model_);
   // В onLeftSelectionChanged():
   void FileBrowser::onLeftSelectionChanged() {
       QModelIndex idx = leftView_->currentIndex();
       rightView_->setRootIndex(idx);
       // Если idx невалиден (ничего не выбрано) — rightView_ пуст
   }
   ```
3. `rightView_->setRootIsDecorated(false)` — оставить: дети выбранного каталога показываются как плоский
   список (файлы + подкаталоги одной строкой).
4. Двойной клик по строке правой панели: `setupBrowserActivation` уже подключён в MainWindow к
   `fileBrowser_->contentView()` и различает Directory / Song / Playlist по `TypeRole` — работает.

**Тест:** MPD с music_directory → левое дерево показывает каталоги → клик на каталог → правая панель
показывает его содержимое (файлы, подкаталоги, .m3u). Двойной клик по каталогу справа открывает его,
по трэку — играет.

---

### P0.2 DatabaseBrowser — ни одного действия, меню нет, сортировка не кликабельна

**Файлы:** `src/browsers/database-browser.cpp`

**Диагноз:** Четыре независимых проблемы.

**Проблема A — модель не грузится, если MPD не отвечает:**
`onConnectionChanged(true) → loadByTag(currentTag_)`. Если `!conn_ || !conn_->isConnected() || !conn_->hasHandle()`
— молча ничего не делает. На живом MPD работает, но без индикации ошибки.

**Проблема B — контекстное меню есть, но никогда не появляется:**
`setupBrowserContextMenu(databaseBrowser_->treeView(), ...)` корректно подключается в `MainWindow::setupCentralArea()`.
Меню появляется при правом клике на строке дерева. Если дерево пусто (нет данных) — кликать не по чему.
Фикс: сначала починить загрузку данных, затем проверить контекстное меню.

**Проблема C — сортировка по колонкам не работает:**
`view_->setSortingEnabled(true)` **не вызван** в конструкторе `DatabaseBrowser`.
`QTreeView` по умолчанию не сортирует — заголовки колонок не кликабельны и не показывают стрелку сортировки.

**Проблема D — активация (двойной клик) работает, но без фидбэка:**
`setupBrowserActivation(databaseBrowser_->treeView(), ...)` подключён. `activationFor(idx)` различает
трэк (→ `activateUri`) и группу/альбом (→ `findAddSongs`). Если MPD не на связи — команды уходят
в никуда, пользователь не видит результата.

**Исправление:**
1. `view_->setSortingEnabled(true)` — добавить после `setExpandsOnDoubleClick(false)`.
2. В `onActivate()` — проверять, что данные загружены (`model_->rowCount({}) > 0`), иначе показывать
   `qWarning` / индикатор.
3. (Опционально) `QHeaderView::setSectionsClickable(true)` — в Qt6 включено по умолчанию для сортировки.
4. Проверить, что `MusicDatabaseModel::loadByTag` не блокируется ошибкой `mpd_search_commit()`:
   после неудачи `ResponseScope` в деструкторе чистит ошибку, но модель остаётся пустой. Добавить
   проверку `mpd_connection_get_error()` после `commit` и логирование.

**Тест:** Подключение к MPD → Database показывает дерево (Artist → Album → Track).
Клик на колонку «Artist / Album» сортирует A–Z / Z–A.
Правый клик — контекстное меню «Play» / «Add» / «Replace».
Двойной клик по артисту — `findadd`, по трэку — играет.

---

### P0.3 Неверные иконки на кнопках repeat/random/single/consume

**Файл:** `src/gui/controlbar.cpp`, строки 75–96

**Что не так:**
- `randomBtn_`: fallback `QStyle::SP_MediaPlay` — **это иконка Play, не shuffle**
- `singleBtn_`: fallback `QStyle::SP_MediaPlay` — **тоже Play**
- `consumeBtn_`: fallback `QStyle::SP_TrashIcon` — consume = потребление, не удаление
- `repeatBtn_`: fallback `QStyle::SP_BrowserReload` — приемлемо

**Исправление:**
1. `randomBtn_`: `QIcon::fromTheme("media-shuffle", style()->standardIcon(QStyle::SP_MediaShuffle))`
   — `SP_MediaShuffle` существует в Qt6
2. `singleBtn_`: fallback `QStyle::SP_MediaStop` (квадрат с единицей — лучшее из встроенных)
3. `consumeBtn_`: fallback `QStyle::SP_DialogApplyButton` (галочка) или кастомный SVG
4. `repeatBtn_`: оставить как есть

**Тест:** Визуально — четыре разные иконки, ни одна не дублирует Play/Stop.

---

### P0.4 Дублирование навигации: sidebar + tabBar

**Файлы:** `src/gui/mainwindow.cpp`, строки 186–199 (setupSidebar), 313–327 (setupBrowserTabs)

**Что не так:** QTreeView sidebar и QTabBar — 6 идентичных пунктов, переключающих один QStackedWidget.

**Исправление:** Убрать sidebar (стратегия A). Причины: tabBar компактнее, sidebar занимает ~200px
по горизонтали и дублирует навигацию один-в-один. Вкладки — стандарт для Qt-приложений.
1. `setupSidebar()` — удалить целиком (строки 186–199)
2. Из .h: удалить `sidebar_`, `sidebarPanel_`, `setupSidebar()`, `setupSidebarLabel()`
3. `setupSidebarLabel()` и `sidebarLabel_` — удалить (обложка в sidebar мёртва, см. P0.5)
4. `setupUi()`:
   - Убрать `setupSidebar()`, `setupSidebarLabel()`
   - Убрать `splitter` — правый panel идёт на всю ширину
   - `splitter->addWidget(sidebarPanel_)` / `splitter->addWidget(rightPanel)` → только `layout->addWidget(rightPanel, 1)`
5. Сигнал `sidebar_->selectionModel() → currentChanged` — удалить
6. Zoom levels: `sidebarPanel_->hide()` → удалить
7. `sidebar_`-зависимости в `setupCentralArea()` — проверить, что нет скрытых ссылок

**Тест:** После изменения — 6 вкладок в tabBar, ни одна не выпала, все браузеры переключаются.
Окно не содержит пустой области слева.

---

### P0.5 CoverWidget в ControlBar + sidebarLabel_ мёртв

**Файлы:** `src/widgets/cover-widget.cpp` (min 128×128), `src/gui/controlbar.cpp:112` (max 48),
          `src/gui/mainwindow.cpp:210-216` (sidebarLabel_), `src/browsers/now-playing.cpp` (256×256)

**Что не так:**
- CoverWidget (128×128 min) втиснут в ControlBar (48px max) — обложка расплющена
- `sidebarLabel_` показывает «No cover», никогда не заполняется
- Единственное место, где обложка нормально показывается — NowPlaying (256px)

**Исправление:**
1. Убрать `coverWidget_` из `ControlBar::addWidget()` — `controlbar.cpp:112`, строка `lay->addWidget(cover);`
2. `sidebarLabel_` уже удалён в P0.4 (стратегия A)
3. В `MainWindow::statusBar()` или SongInfo добавить миниатюру обложки 48×48 (опционально)
4. ControlBar: убрать `setMaximumHeight(48)` — без Cover и SongInfo он станет ~32px

**Тест:** Обложка показывается только на странице Now Playing (и опционально миниатюра в статус-баре).
Никаких «No cover» на пустых местах.

---

### P0.6 Preferences — всё кучей, нет страниц и скролла

**Файл:** `src/gui/preferences-dialog.cpp` (весь файл)

**Что не так:** 11 групп `QGroupBox` в `QVBoxLayout` без `QTabWidget` и `QScrollArea`.

**Исправление (QScrollArea — минимум):**
```cpp
auto *scrollArea = new QScrollArea(this);
scrollArea->setWidgetResizable(true);
auto *inner = new QWidget;
auto *innerLay = new QVBoxLayout(inner);
// Перенести сюда все addWidget/addLayout из root (кроме кнопок)
// ... (копия существующего кода с заменой root→innerLay)
inner->setLayout(innerLay);
scrollArea->setWidget(inner);
root->addWidget(scrollArea);
// QDialogButtonBox отдельно, вне scroll
root->addWidget(buttons_);
```

**Исправление (QTabWidget —推荐的):**
Страницы:
- *Connection*: Mode, Host/Port/Socket, Password, Auto-connect, Proxy
- *Playback*: Stop on exit, Crossfade, Replay Gain, Outputs
- *Interface*: Tray, Window (Start hidden)
- *Playlist*: Header markup, Highlight color, Font, Colors
- *Metadata*: Discogs token, Metadata font/color, Music directory
- *Keys*: Multimedia key bindings (8 × XF86Audio)
- *Profiles*: (отдельная страница)

**Тест:** Окно Preferences открывается целиком на 800×600, все поля видны без вертикального скролла
или с ним, но без «окно уходит за экран».

---

## P1 — Важные: ломают UX, но не рендерят приложение бесполезным

### P1.1 Tooltip-нотификация при смене трэка

**Файлы:** `src/gui/mainwindow.cpp` (onStatusChanged, ~строка 270), `src/gui/trayicon.h/.cpp`

**Что не так:** При смене `songId` ничего не показывает. `handleIpcShowNotification()` — заглушка
`showMessage("gmpc-qt", "Playing...")` без информации о трэке.

**Исправление:**
В `MainWindow::onStatusChanged()`, сразу после `if (songId == lastSongId_) return;`:
```cpp
if (trayIcon_ && trayIcon_->isVisible() && song) {
    QString msg = song->artist;
    if (!song->title.isEmpty())
        msg += QStringLiteral(" — ") + song->title;
    if (!song->album.isEmpty())
        msg += QStringLiteral(" [%1]").arg(song->album);
    trayIcon_->showMessage(tr("Now Playing"), msg,
                           QSystemTrayIcon::Information, 5000);
}
```
А также обновить `handleIpcShowNotification()` — искать текущий трэк по `lastSongId_` в модели.

**Тест:** Next/Previous — всплывает balloon «Artist — Title [Album]» на 5 секунд.

### P1.2 Playlist Editor — нет обратной связи при addUri/load

**Файл:** `src/browsers/playlist-editor.cpp` (showContentsMenu, ~строка 227)

**Что не так:** `cmds_->addUri(intent.uri)` без результата. Пользователь не знает, сработало ли.

**Исправление:** После вызова `cmds_->addUri(...)` в `showContentsMenu` — временное сообщение в статус-бар.
Без upcast'а: добавить сигнал `MpdCommands::uriQueued(QString uri)` и подписать статус-бар на него.
Либо, минимально: вставить после цикла `for (const BrowserIntent &intent : intents) cmds_->addUri(intent.uri)`:
```cpp
if (auto *mw = qobject_cast<MainWindow *>(window()))
    mw->statusBar()->showMessage(tr("Added to queue"), 2000);
```

**Тест:** «Add to Queue» в редакторе плейлистов → статус-бар: «Added to queue».

### P1.3 QueueSync не синхронизируется после clearQueue

**Файл:** `src/gui/mainwindow.cpp` (clearCurrentQueue, ~строка 390)

**Что не так:** `cmds_->clearQueue()` → серверная очередь пуста, но модель не обновляется.

**Исправление:**
```cpp
void MainWindow::clearCurrentQueue()
{
    currentPlaylist_->clearQueueWithConfirm();
    queueSync_->reload();
}
```

**Тест:** Clear Queue → Current Playlist пуст в течение <1 секунды.

### P1.4 StatusBar — временные сообщения перебиваются updateStatus

**Файл:** `src/gui/statusbar.cpp`

**Что не так:** `infoLabel_->setText(msg)` из `showMessage()` через 2 секунды заменяется `updateStatus()`.

**Исправление:** Добавить `QTimer *messageTimer_`. В `showMessage()` — запустить таймер, флаг `showingMessage_ = true`.
В `updateStatus()` — не трогать `infoLabel_`, если `showingMessage_` (таймер не тикнул). В слот таймера —
очистить флаг.

**Тест:** Временное сообщение висит положенное время, не сбиваясь poll'ом.

---

## P2 — Косметические и улучшения

### P2.1 Sidebar без иконок (если P0.4 не выполнен — sidebar не удалён)

**Файл:** `src/gui/mainwindow.cpp`, setupSidebar

**Исправление:** Добавить иконки через `QIcon::fromTheme` для каждого пункта.

### P2.2 VolumeWidget — scroll вне диапазона

**Файл:** `src/widgets/volume-widget.cpp`

**Исправление:** В `setVolume(int vol)` — `qBound(0, vol, 100)`.

### P2.3 Config — zoom-уровень не сохраняется

**Файлы:** `src/core/config.h`, `config.cpp`, `src/gui/mainwindow.cpp`

**Исправление:** Сохранять `zoomLevel_` при `closeEvent()`, читать в `setupUi()`.

### P2.4 `lastSongId_` guard после реконнекта

**Файл:** `src/gui/mainwindow.cpp`, строка 268

**Что не так:** `if (songId == lastSongId_) return` может пропустить обновление,
если после реконнекта новый трэк имеет songId = −1 (то же значение, что в `onDisconnected()`).

**Исправление:** Сбросить `lastSongId_` в −2 при `onDisconnected()`, чтобы первый `onStatusChanged`
после коннекта всегда обновил UI.

---

## Критерий готовности (Definition of Done)

Каждый пункт:
1. Исходный код изменён, указаны конкретные строки
2. Приложение собирается без предупреждений (`cmake --build build`)
3. CTest — 34/34 зелёных (`cd build && ctest`)
4. (Где применимо) Живой запуск против MPD: функция работает как описано в «Тест»

---

## Порядок выполнения

Очерёдность строгая: P0 → P1 → P2. Внутри P0 — сначала браузеры (P0.1, P0.2), потом косметика
(P0.3–P0.6), потому что первые блокируют функциональность, вторые — только вид.

1. **P0.1** FileBrowser — 45 мин (удалить FileContentProxy, переключить rightView_ на модель)
2. **P0.2** DatabaseBrowser — 15 мин (setSortingEnabled, проверка загрузки)
3. **P0.3** Иконки кнопок — 15 мин
4. **P0.4** Дублирование навигации — 30 мин
5. **P0.5** CoverWidget / sidebarLabel — 15 мин
6. **P0.6** Preferences scroll|tabs — 30–60 мин

7. **P1.1** Tooltip-нотификация — 15 мин
8. **P1.2** Playlist feedback — 15 мин
9. **P1.3** QueueSync clear — 5 мин
10. **P1.4** StatusBar message — 15 мин

11. **P2.1–P2.4** — по желанию, по 10 мин каждый
