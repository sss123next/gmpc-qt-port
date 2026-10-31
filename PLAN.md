# gmpc-qt — План работ

**Цель:** Портировать gmpc (GTK2 MPD-клиент) на Qt6/C++20 с сохранением внешнего
интерфейса и максимальной эффективностью кода.

## Статус (проверено 2026-10-31: проход сравнения с `gmpc-orig/` + живой MPD 0.25)

Легенда: ✅ работает · 🟡 частично/деградация · 🔴 макет или блокёр (экран неработоспособен).

| Component | Status | Проверено / чем именно плохо |
|-----------|--------|------------------------------|
| core::Connection | 🟡 | Только polling (500/2000 мс), `idle` нет. `songPos` читается (`connection.cpp:97`) и **нигде не используется**. Ошибка любой команды оставляет соединение в error-state → `isConnected()` лжёт, UI «отваливается» навсегда (Блокер B5) |
| core::MpdCommands | 🟡 | Транспорт/режимы/громкость работают, seek исправлен в 5r. Нет delete/move/clear для очереди, crossfade, outputs; `volumeUp/Down` делают лишний `fetchStatus()` |
| core::PlaylistModel | 🔴 | **`currentSong()` возвращает `songs_[0]`** (`playlist-model.cpp:114`) — не текущий трэк. Очередь грузится ровно один раз при коннекте (единственный вызов — `mainwindow.cpp:419`); на живой базе 65 279 позиций это **914,6 мс блокировки GUI-потока** |
| core::Config / MpdEnv | 🟡 | Конфиг и профили работают. В `MpdEnv` нет fallback'ов `$XDG_RUNTIME_DIR/mpd/socket` и `/run/mpd/socket`; `lastIndexOf('@')` ломает пароль, содержащий `@` |
| gui::MainWindow | 🟡 | Каркас есть. Браузеры **никогда не вызывают** `onActivate()`/`onConnectionChanged()` — в `src/` ноль вызовов → 4 экрана навсегда `setEnabled(false)`. `sidebarLabel_` мёртв (не обновляется). «Song Info» в контекстном меню = заглушка (`mainwindow.cpp:635`) |
| gui::ControlBar | 🔴 | **`seekSlider_` ни к чему не подключён** (`controlbar.cpp:40,112`) — перемотка из UI невозможна. Готовый `widgets/ProgressSlider` (сигнал seek + 6 тестов) в UI не используется |
| gui::StatusBar / TrayIcon | 🟡 | Работают, но тултип трея получает тот же неверный «текущий» трэк |
| gui::PreferencesDialog | ✅ | 12 групп (connection/proxy/server/tray/playlist/metadata/MM keys/profiles…) — самый цельный экран |
| browsers::CurrentPlaylist | 🔴 | Только фильтр + просмотр. Нет double-click, контекстного меню, DnD-переупорядочивания, удаления, clear/save. Модель `QAbstractListModel` → **одна колонка** «artist — title» вместо # / Artist / Title / Album / Time |
| browsers::DatabaseBrowser | 🔴 | Макет: у модели нет `canFetchMore()`/`fetchMore()`, а `loadAlbums()` (`music-database-model.cpp:97`) и `loadTracks()` (:147) **не вызываются нигде** → дереву неоткуда взять альбомы и трэки. Колонка «Year» всегда пуста |
| browsers::FileBrowser | 🔴 | Дерево останавливается на первом уровне: MPD отдаёт **полные относительные пути**, модель склеивает `parent + name` повторно (`file-system-model.cpp:177,186,196`) → «humor/humor/karcev» → ACK `No such directory`. Оригинал использует `g_path_get_basename()` (`playlist3-file-browser.c:462`) |
| browsers::SearchBrowser | 🔴 | «All fields» = 7 ограничений через AND (`search-browser.cpp:195-205`) → **0 результатов** (замер №4). Правильный any-tag даёт данные, но **2857 мс в GUI-потоке**. Поиск только по Enter; `fieldCombo_` ни к чему не подключён (`onFieldChanged` — мёртвый код) |
| browsers::PlaylistEditor | 🔴 | Никогда не включается. «New» = `mpd_run_save()` текущей очереди (не пустой плейлист). Нет удаления/перемещения трэков, DnD, контекстного меню, «load and play»; ошибки MPD проглатываются |
| widgets::SongInfo / CoverWidget | 🔴 | Код корректен, но кормится от `currentSong()` → показывают **не тот трэк** (замер №7); обложка ищется для неверной пары artist/album |
| widgets::VolumeWidget | ✅ | Работает |
| widgets::ProgressSlider | 🔴 | Мёртвый код: 6 тестов, ноль использований в UI |
| metadata::CacheSqlite / MetadataManager / CoverProvider | 🟡 | Каскад обложек работает; вход — неверная пара artist/album; lyrics-выходы менеджера не потребляются UI |
| providers::* | 🟡 | Провайдеры обложек подключены. **LrcLib и lyrics.ovh не имеют ни одного потребителя в UI** — показать текст песни негде |
| mpris::MprisPlayer | ✅ | 5r: имена интерфейсов, trackid, абсолютный/относительный seek подтверждены busctl. Минус: `Metadata` без title/artist/album → внешние плеер-клиенты показывают пусто |
| tools::AsyncDownload / Log | ✅ | Используются провайдерами |
| tests | 🟡 | **172 кейса / 18 сьютов — зелёные** (прогнано: `ctest` 100 %, 21,85 с). Но проверяют заголовки колонок и существование виджетов: ни одного теста на wiring браузеров, lazy-loading путей, seek, текущий трэк |
| Build (CMake + Nix flake) | ✅ | Сборка и тесты воспроизводятся только внутри `nix develop` |

## Проверено на живом MPD (2026-10-31)

Окружение: MPD **0.25.0** через unix-сокет (`MPD_HOST=/home/sss/.mpd/sock`), база по `countall`: 1827
артистов / 4462 альбома / 64 854 трэка (`search db artist` при этом отдаёт 1828 записи), очередь
**65 279** позиций. Проверяли временным зондом на libmpdclient
2.24 (те же вызовы, что делает наш код) и прямым протоколом через `nc -U`; зонд удалён из дерева.

| # | Что проверялось | Результат | Вывод |
|---|-----------------|-----------|-------|
| 1 | `mpd_send_list_queue_meta()` (наш `PlaylistModel`) | 65 279 сущностей за **914,6 мс** | Работает, но это ~1 с блокировки GUI при коннекте; этим же путём очередь грузится единственный раз |
| 2 | `mpd_search_db_tags(MPD_TAG_ARTIST)` (наш `loadArtists`) | 1828 артистов за **16,6 мс** | Модель БД живая: проблема не в MPD, а в отсутствии вызова и ленивой загрузки детей |
| 3a | `lsinfo ""` / `lsinfo "humor/karcev"` | 4 сущности / 39 сущностей | `lsinfo` в MPD 0.25 есть (`listinfo`/`listmeta` удалены, libmpdclient 2.24 уже шлёт `lsinfo`) — версия библиотеки не при чём |
| 3b | путь, который строит наша модель: `"humor/humor/karcev"` | **ACK `No such directory`** | Причина «грузится только корневой уровень» подтверждена: MPD возвращает полный путь (`directory: humor/karcev`), а мы к нему ещё раз приклеиваем имя |
| 4a | поиск как у нас: AND из 7 тегов, запрос `Radiohead` | **0 трэков** за 58 мс | «All fields» не может вернуть ничего осмысленного — MPD объединяет ограничения оператором И |
| 4b | то же через `mpd_search_add_any_tag_constraint()` | 1 трэк за **2857,5 мс** | Правильная форма найдена; вызов в GUI-потоке → подвеска на секунды, нужен рабочий поток |
| 5 | `mpd_send_list_playlists()` (наш `PlaylistEditor`) | 1 плейлист | Работает; экрана не видно только из-за отсутствующего wiring |
| 6 | error-state после одной неудачной команды (путь без `clear_error`) | `isConnected() == false`; следующий `mpd_run_status()` → NULL, «Cannot send a new command while receiving another response», **и все последующие тоже** | Любая ошибка в браузере клинит общее соединение: статус-бар «отключено», UI мёртв до переподключения |
| 7 | текущий трэк: `status`/`currentsong` против нашей `currentSong()` | MPD: `state=pause song=4522 songid=85565`, файл — соната C.P.E. Bach; `playlistinfo 0` → `humor/karcev/Karcev_Roman-Allo,_Grisha.opus` | UI показывает **Карцева вместо Баха**: SongInfo, обложка и тултип трея берут позицию 0 |

## Блокеры (P0) — без этого приложение не является клиентом MPD

* **B1. Жизненный цикл браузеров не подключён.** `BrowserWidget::onActivate()/onDeactivate()/
  onConnectionChanged()` объявлены и реализованы, но в `src/` нет ни одного вызова; все четыре экрана
  вызывают `setEnabled(false)` в конструкторе (`database-browser.cpp:56`, `file-browser.cpp:92`,
  `playlist-editor.cpp:79`, `search-browser.cpp:159`). Итог: Database / Files / Search / Playlist Editor
  недоступны всегда, включая после успешного коннекта.
  *Фикс:* связать `Connection::connected/disconnected` с `onConnectionChanged()` у всех браузеров и
  вызывать `onActivate()/onDeactivate()` при смене страницы стека (сейчас там только `setCurrentIndex`) + тест на wiring.

* **B2. «Текущий играющий трэк» — неверные данные.** `PlaylistModel::currentSong()` = `songs_[0]`
  (`playlist-model.cpp:108-115`); `Connection::Status::songPos` читается, но не используется; `idle` /
  `currentsong` не запрашиваются. Следствие — SongInfo, обложка и трей врут (замер №7); MPRIS-`Metadata`
  по содержательной части пуст → те же жалобы извне.
  *Фикс:* один источник истины — `mpd_run_current_song()` (или `songPos` + индекс модели), обновление по
  смене `songId`, одна и та же структура для SongInfo / CoverWidget / TrayIcon / MPRIS.

* **B3. Очередь не обновляется.** `loadFromConnection()` вызывается один раз (только `onConnected`), хотя
  `queueVersion` входит в условие изменения статуса. Добавление из контекстного меню, `OpenUri` из MPRIS,
  Next/Stop при single/consume — очередь в UI не меняется до переподключения. Плюс 914 мс на загрузке.
  *Фикс:* инкрементальные обновления (`mpd_send_pl_changes` по `queueVersion`) + события (`idle: PLAYER |
  MIXER | PLAYLIST`), дозагрузка вне GUI-потока, колонки очереди (# / Artist / Title / Album / Time).

* **B4. Files: дерево не спускается ниже корня.** Удвоение пути в `FileSystemModel::loadDirectory()`
  (замер №3b); побочный эффект — неудачная команда вешает общее соединение (B5).
  *Фикс:* имя = basename, путь = ровно то, что вернул MPD; тесты на двухуровневый `lsinfo` с реальными ответами.

* **B5. Нет обработки ошибок libmpdclient.** Ни один браузер/модель не делает `mpd_response_finish()` /
  `mpd_connection_clear_error()` после неудачи (аккуратен только `Connection::rawCommand()`). Замер №6:
  одна ошибка → все команды падают с «Cannot send a new command…», `isConnected()` лжёт, `pollStatus()`
  останавливает таймер и шлёт `disconnected()`.
  *Фикс:* RAII-обёртка «отправить–прочитать–завершить», явная очистка ошибки, состояние «ошибка команды»
  отдельно от «нет соединения», автопереподключение.

* **B6. Перемотка из интерфейса отсутствует.** `seekSlider_` не подключена, `ProgressSlider::seek` никуда
  не приходит; работает только seek через MPRIS (5r).
  *Фикс:* поставить `ProgressSlider`, подключить `sliderMoved → MpdCommands::seek`, подавить обратную связь
  с polling на время перетаскивания.

## Баги и деградации (P1)

* **Search «All fields» всегда пуст** — AND вместо any-tag (замеры №4). Плюс нет live-поиска по вводу
  (только Enter), `fieldCombo_` не подключён, нет счётчика результатов и состояния «нет совпадений».
* **Database: только один уровень.** Нет `canFetchMore()/fetchMore()`; колонка Year пуста; контекстное меню
  на артисте не показывается (`uri` пуст + родитель невалиден → ранний выход в `mainwindow.cpp`).
* **Current Playlist: взаимодействие отсутствует полностью** — ни double-click (в оригинале `row-activated`
  есть во всех браузерах: `playlist3-current-playlist-browser.c:430`, `…tag2-browser.c:1260`,
  `…find2-browser.c:193`, `…file-browser.c:209,220`, `…playlist-editor.c:1105`), ни контекстного меню,
  ни DnD/переупорядочивания, ни удаления строки очереди (`Delete`), ни clear/save.
* **Playlist Editor:** «New» создаёт плейлист из текущей очереди; содержимое — `QListWidget` без колонок,
  порядка и удаления строк; нет перетаскивания очереди в плейлист; `goto done` пропускает `response_finish`.
* **Блокирующие вызовы в GUI-потоке:** поиск (2,9 с), загрузка очереди (0,9 с), любые `mpd_run_*` из
  виджетов — нужен рабочий поток с очередью команд.
* **Только polling вместо `idle`:** до 2 с на смену состояния; single/consume/crossfade не отражаются
  мгновенно; лишняя нагрузка при 65-тысячной очереди.
* **MPRIS Metadata без title/artist/album** — часть жалобы «информация о трэке неверная» извне.
* **Мёртвый код UI:** `sidebarLabel_`, `ProgressSlider`, `SearchBrowser::onFieldChanged`,
  `PlaylistEditor::onActivate` (никто не зовёт).

## Расхождения UI с оригиналом (`gmpc-orig/`)

| Экран/механика оригинала | Где в оригинале | У нас |
|--------------------------|-----------------|-------|
| Current playlist: `row-activated`, контекстное меню, выбираемые колонки, live-обновление очереди | `browsers/playlist3-current-playlist-browser.c`, `gmpc-mpddata-model-playlist.gob` | 🔴 только фильтр+чтение, 1 колонка, обновлений нет |
| Database (tag browser): дерево тегов + список трэков справа, double-click, контекстное меню | `browsers/playlist3-tag2-browser.c:1029,1260` | 🔴 один уровень, ни одного действия |
| Files: дерево каталогов + список файлов, `row-activated` в обеих панелях, корректные basename | `browsers/playlist3-file-browser.c:209,220,462` | 🔴 удвоение путей, нет double-click |
| Find/Search: live-поиск, поле «any», таблица результатов, двойной клик | `browsers/playlist3-find2-browser.c:193`, `Tools/advanced-search.c` | 🔴 AND-баг, только Enter |
| Playlist editor: дерево плейлистов + список с операциями, double-click = load | `browsers/playlist3-playlist-editor.c:1105` | 🟡 кнопки CRUD, экран выключен |
| Now Playing (крупная обложка, теги) | `browsers/gmpc-nowplaying2.c` | 🔴 экрана нет |
| Metadata/Info: биография, похожие артисты/трэки, текст песни, фон-обложка | `browsers/gmpc-metadata-browser2.c`, `Widgets/gmpc-metadata-*` | 🔴 экрана нет; LrcLib/lyrics.ovh некуда отдавать текст |
| Server information (статистика MPD) | `browsers/server-information.c` | 🔴 нет |
| Crossfade / Stereo Effects в настройках и UI | `Tools/mpdinteraction.c:440-461`, `glade/preferences-esf-dialog.ui` | 🟡 `crossfade` читается в статус, UI и настроек нет (вместо них single/consume) |
| Выбор колонок таблицы | `egg/eggcolumnchooserdialog.c` | 🔴 нет |
| «Add URL», сохранение очереди как плейлиста | `glade/gmpc-add-url.ui`, `playlist-save-dialog.ui` | 🔴 нет (`addUri` вызывается только из контекстного меню и MPRIS OpenUri) |
| Тултип трея с кнопками, заголовок-хлебные крошки | `tray-icon2.c:685`, `GUI/title_header.c` | 🟡 трей есть (с неверным трэком), крошек нет — вкладки вместо них |
| IPC/CLI (`gmpc …`), плагины, мастер первоначальной настройки | `Tools/ipc.c`, `plugin.c`, `Tools/setup-assistant.c` | 🔴 нет |

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

### Phase 2 — Browsers (каркас готов; все четыре экрана неработоспособны — см. B1, B3, B4)
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

### Фаза 5 — Полировка (выполнено частично)
- [x] lyrics.ovh (резервный lyrics)
- [x] Горячие клавиши
- [x] DBus управление (MPRIS)
- [x] rawCommand() — нормальный парсинг ответа
- [ ] MPRIS `Metadata` без Title/Artist/Album → внешние провайдеры видят пустые поля (замер №7)
- [ ] Лирика: пайплайн собран, потребителя в UI нет (`LyricsProvider` нигде не инстанцируется) — фактически мёртвый код
- ⚠️ Обложка и `SongInfo` подключены к «текущему треку», но источник — `songs_[0]`, а не `songid` MPD (**B2**): UI честно показывает не то, что играет

---

## Фаза 6 — Разблокировка экранов (P0)

Порядок = порядок зависимостей: без 6.1 остальные пункты бессмысленны, потому что браузеры никогда не получают соединение и не активируются.

- [ ] **6.1 Lifecycle браузеров** (**B1**): хранить список `BrowserPage`, вызывать `setEnabled(connected)` + `onConnectionChanged(conn)` по `connectionStateChanged`, `onActivate()` — по смене активной страницы (`currentChanged`). Убрать `setEnabled(false)` из конструкторов. *Тест:* fake-соединение → после «подключено» все 4 страницы enabled, модели загружены; переключение вкладки ⇒ ровно один `onActivate`.
- [ ] **6.2 Единый источник истины о текущем треке** (**B2**): `Status::songPos`/`songid` → `PlaylistModel::currentSong()` по songid с fallback по pos; новый сигнал `currentSongChanged(const SongEntry*)`; все потребители (SongInfo, CoverWidget, заголовок окна, tray, sidebarLabel_, MPRIS Metadata) питаются только от него. *Тест:* очередь из 3 позиций при `song=1` → current == позиция 1; смена позиции ⇒ ровно один сигнал.
- [ ] **6.3 Пути в дереве файлов** (**B4**): хранить полный путь и показывать basename, дочерние запросы слать по полному пути (в оригинале — `g_path_get_basename()`). *Тест:* fake-ответ `directory: a/b/c` → отображается `c`, дочерний запрос идёт с `a/b/c`.
- [ ] **6.4 Выход из error-state** (**B5**): RAII-обёртка, вызывающая `mpd_response_finish()`/`mpd_connection_clear_error()` после каждой команды; ошибка → сигнал `commandFailed`, соединение остаётся подключённым; reconnect с экспоненциальной задержкой. *Тест:* команда с ACK ⇒ следующая команда успешна, состояние соединения не изменилось.
- [ ] **6.5 Поиск на any-tag** (**B3 по семантике**): «All fields» → `mpd_search_add_any_tag_constraint`; запрос из worker-потока с отменой предыдущего (замер: 2857 ms на живой базе — в GUI-потоке это неприемлемо). *Тест:* одно ограничение возвращает непустой результат.
- [ ] **6.6 Seek из GUI** (**B6**): `ProgressSlider::sliderMoved → seek(pos, seconds)`, убрать мёртвый дубль слайдера в ControlBar. *Тест:* drag ползунка ⇒ отправлен `seekid`.
- [ ] **6.7 idle-цикл событий**: отдельный поток с `mpd_run_idle` (`PLAYER|MIXER|PLAYLIST|MODIFIER|DATABASE|UPDATE`) вместо поллинга 500/2000 ms; polling оставить fallback для удалённых хостов.

## Фаза 7 — Интерактив (текущий плейлист и браузеры)

- [ ] **7.1 Current Playlist:** двойной клик → `play(pos)`; контекстное меню (Remove / Move up/down / Clear / Save as…); Delete/Backspace; drag&drop-перемещение (`InternalMove` + `moveSong`); колонки Title/Artist/Album/Time вместо единственной Path.
- [ ] **7.2 Обновление очереди:** перезагрузка по idle `playlist`/`player`, инкрементально через `plchanges <version>`; загрузку убрать из GUI-потока (замер: 914 ms на 65k позиций). *Тест:* изменение очереди другим клиентом видно без переключения вкладок.
- [ ] **7.3 Двойной клик во всех браузерах** (`row-activated` в оригинале): БД — добавить группу/альбом/трек; Файлы — добавить каталог/файл; Поиск — добавить трек; Редактор — добавить/удалить позицию.
- [ ] **7.4 Контекстные меню** для всех деревьев (добавить в очередь / заменить очередь / открыть папку / копировать путь), включая текущий плейлист, где их сейчас нет вовсе.
- [ ] **7.5 Playlist Editor:** настоящий редактор — список позиций, add/remove/move/clear из дерева БД и Файлов, сохранение через `mpd_save_playlist`, загрузка через `loadPlaylist`. *Тест:* fake-соединение: New → Load → Add → Save отправляет ожидаемую последовательность команд.
- [ ] **7.6 Живой поиск:** дебаунс 250–300 ms, Enter = добавить в очередь / «заменить очередь», история запросов (Ctrl+↑), счётчик найденного.

## Фаза 8 — Недостающие экраны и функции

- [ ] Now Playing отдельной страницей (крупная обложка + лирика), а не только control bar.
- [ ] Metadata/Info для выбранного трека + просмотр лирики (потребитель для `LyricsProvider`).
- [ ] Server information (`stats`), обновление по idle `database|update`.
- [ ] Настройки: crossfade, переключение выходов (ESF в оригинале), тема/иконки — сейчас только подключение и горячие клавиши.
- [ ] Настраиваемые колонки текущего плейлиста + форматная строка (аналог `mpc-format`).
- [ ] Add URL / Save queue as playlist / Clear queue из меню.
- [ ] IPC: управление уже запущенным экземпляром вместо второго GUI-процесса.

## Политика тестирования (что менять в подходе)

172 зелёных теста почти ничего не доказывают: они проверяют парсинг строк, наличие заголовков колонок и существование виджетов. Ни один баг из B1–B6 ими не поймать.

- [ ] **Wiring-тесты lifecycle:** после подключения все браузеры enabled и вызван `onConnectionChanged`; смена вкладки → `onActivate`. Поймал бы B1, отсутствие загрузки очереди, B4.
- [ ] **Поведенческие тесты на fake-соединении:** текущий трек = `songid` из status (B2); ACK-ошибка не оставляет соединение в error-state (B5).
- [ ] **Интеграционные тесты против реального MPD** (`MPD_HOST`, отдельная служебная база, пропуск без сервера): lsinfo/дерево, any-tag поиск, plchanges.
- [ ] **Запрет «пусто = зелёный»:** тест с ожидаемым непустым ответом обязан падать на пустом результате — иначе «0 песен» проходит как успех (именно так и произошло с поиском).

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

