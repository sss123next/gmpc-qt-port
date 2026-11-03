# gmpc-qt — План работ

**Цель:** Портировать gmpc (GTK2 MPD-клиент) на Qt6/C++20 с сохранением внешнего
интерфейса и максимальной эффективностью кода.

## Статус (проверено 2026-10-31: проход сравнения с `gmpc-orig/` + живой MPD 0.25)

Легенда: ✅ работает · 🟡 частично/деградация · 🔴 макет или блокёр (экран неработоспособен).

| Component | Status | Проверено / чем именно плохо |
|-----------|--------|------------------------------|
| core::Connection | 🟡 | Polling + события (6.7): статус приходит по idle через `IdleWatcher` (`refreshNow()`), интервал в простое — 10 с вместо 2 с, пока слушатель жив (`setIdleWatched()`); при его отвале опрос возвращается к 2 с и делается немедленный добор. Ошибки команд разбираются (`consumeError()` + RAII `ResponseScope`), reconnect с экспоненциальной задержкой, `commandFailed` отделён от `disconnected` — B5 закрыт. С 6.5 отдаёт параметры последнего подключения (`host()/port()/password()/timeoutMs()`) — второму соединению (поиск, idle) их больше неоткуда взять |
| core::IdleWatcher / MpdEndpoint | ✅ | Новый слушатель событий (6.7): своё соединение + свой поток + `QSocketNotifier` на `mpd_connection_get_fd()`, маска `PLAYER\|OPTIONS\|MIXER\|QUEUE\|STORED_PLAYLIST\|DATABASE\|UPDATE`, переподключение 1→30 с, сигнал `watchingChanged(bool)` как разрешение вернуться к polling. Задержка доставки события от другого клиента — **< 1 мс** (живой MPD). `MpdEndpoint` + `openMpdConnection()` — общее открытие соединения (password, keepalive для TCP) для поиска и слушателя |
| core::MpdCommands | 🟡 | Транспорт/режимы/громкость работают, seek исправлен в 5r. Очередь обслуживается с 7.1: `removeSongIds()` (`deleteid` по одному id — формы со списком идентификаторов в libmpdclient 2.24 нет), `moveQueueBlock()` (`move_range`), `saveQueueAs()` (`save`), `clearQueue()`. Нет crossfade, outputs; `volumeUp/Down` делают лишний `fetchStatus()` |
| core::PlaylistModel | 🟡 | С 7.1 — `QAbstractTableModel`: колонки Title/Artist/Album/Time, `SortRole` (длительность сравнивается численно), MIME строки очереди для DnD, локальные `moveRowsWithin()`/`removeRowsAt()` с перенумерацией позиций. Текущий трэк разрешается по `songid` с fallback на позицию (6.2), сигнал `currentSongChanged`. Очередь грузится ровно один раз при коннекте; на живой базе 65 279 позиций это **914,6 мс блокировки GUI-потока** — инкрементальные обновления ещё не сделаны |
| core::Config / MpdEnv | 🟡 | Конфиг и профили работают. В `MpdEnv` нет fallback'ов `$XDG_RUNTIME_DIR/mpd/socket` и `/run/mpd/socket`; `lastIndexOf('@')` ломает пароль, содержащий `@` |
| core::SearchEngine | ✅ | Новый движок поиска (6.5): чистая сборка шагов протокола (`makeSearchSteps()` / `makeCountSteps()`), worker-поток с собственным `mpd_connection`, отмена по номеру запроса, бросок чтения на границе трэка. 13 юнит-кейсов + интеграционный прогон при заданном `GMPC_QT_TEST_MPD` |
| gui::MainWindow | 🟡 | Каркас есть, lifecycle браузеров подключён через `BrowserStack` (6.1). Ошибки команд (`Connection::commandFailed`) идут в статус-бар временным сообщением, индикатор подключения не трогая. `sidebarLabel_` мёртв (не обновляется). «Song Info» в контекстном меню = заглушка (`mainwindow.cpp:635`) |
| gui::ControlBar | 🟡 | Перемотка подключена (6.6): `ProgressSlider` вместо голого `QSlider`, `seekRequested(секунды) → MpdCommands::seek` (`seekcur`). Прогресс задаётся парой `setPosition(elapsed, total)`, на время drag polling его не перебивает. Остаток: нет keyboard-перемотки и отображения буферизации |
| gui::StatusBar / TrayIcon | 🟡 | Работают, но тултип трея получает тот же неверный «текущий» трэк |
| gui::PreferencesDialog | ✅ | 12 групп (connection/proxy/server/tray/playlist/metadata/MM keys/profiles…) — самый цельный экран |
| browsers::CurrentPlaylist | 🟡 | С 7.1: double-click/Enter → `playid`, контекстное меню (Play / Remove / Move up / Move down / Clear / Save as…), Delete/Backspace, перестановка перетаскиванием (`InternalMove` + `move_range`). Модель табличная — Title/Artist/Album/Time вместо одной «artist — title». Остаток: живого обновления очереди нет (7.2), непоследовательное выделение не переносится (одна `move_range` = один смежный блок) |
| browsers::DatabaseBrowser | 🔴 | Макет: у модели нет `canFetchMore()`/`fetchMore()`, а `loadAlbums()` (`music-database-model.cpp:97`) и `loadTracks()` (:147) **не вызываются нигде** → дереву неоткуда взять альбомы и трэки. Колонка «Year» всегда пуста |
| browsers::FileBrowser | 🟡 | Дерево спускается: путь хранится как его вернул MPD, показывается `mpdBasename()` (6.3). Осталось: ленивая загрузка только по раскрытию (`fetchMore` есть, но не тестировалась на реальной базе), колонки/иконки, double-click «воспроизвести» |
| browsers::SearchBrowser | 🟡 | 6.5: поиск идёт в `core::SearchEngine` (worker-поток, своё соединение), «All fields» = один any-tag, окно + сортировка, счётчик «N из M совпадений», дебаунс 300 мс, `fieldCombo_` подключён (`onFieldChanged`). Остаток: нет double-click «добавить в очередь» (7.3) и пагинации окна (движок её поддерживает через `SearchQuery::offset`) |
| browsers::PlaylistEditor | 🟡 | Включается (6.1), список и содержимое загружаются, ошибки MPD больше не проглатываются и не вешают соединение (6.4): `save` показывает текст ACK в `QMessageBox`, `rename/rm/load` разбираются через `consumeError()`. «New» по-прежнему = `mpd_run_save()` текущей очереди (не пустой плейлист). Нет удаления/перемещения трэков, DnD, контекстного меню, «load and play» |
| widgets::SongInfo / CoverWidget | 🟡 | Кормятся от `currentSongChanged` (6.2), то есть от `songid` MPD, а не от `songs_[0]`. Целостные метаданные всё ещё берутся строкой из очереди, а не `mpd_run_current_song()` |
| widgets::VolumeWidget | ✅ | Работает |
| widgets::ProgressSlider | ✅ | Используется в `ControlBar` (6.6): шкала в промилле, tooltip с временем, `seekRequested` в секундах; 10 тестов, включая «drag ⇒ секунды» и «polling не перебивает drag» |
| metadata::CacheSqlite / MetadataManager / CoverProvider | 🟡 | Каскад обложек работает; вход — неверная пара artist/album; lyrics-выходы менеджера не потребляются UI |
| providers::* | 🟡 | Провайдеры обложек подключены. **LrcLib и lyrics.ovh не имеют ни одного потребителя в UI** — показать текст песни негде |
| mpris::MprisPlayer | ✅ | 5r: имена интерфейсов, trackid, абсолютный/относительный seek подтверждены busctl. Минус: `Metadata` без title/artist/album → внешние плеер-клиенты показывают пусто |
| tools::AsyncDownload / Log | ✅ | Используются провайдерами |
| tests | 🟡 | **22 сьюта — зелёные** (`ctest` 100 %). Добавлены `test-browser-stack` (wiring жизненного цикла), `test-mpd-error` (классификация ошибок libmpdclient, `mpdBasename()`, инертность `ResponseScope`), `test-search-engine` (сборка поискового запроса + отмена; 13 кейсов / 71 ассерт) и `test-idle-watcher` (разбор протокольной маски idle, поведение при пустом/мёртвом адресе, живое событие от второго клиента). `test-playlist-model` с 7.1 — **34 кейса / 224 ассерта**: колонки и их значения, откат на имя файла без Title, `--:--`, сортировка по длительности через `SortRole` (проверено на паре 10:00/0:09), MIME-круговой проход `mimeData → songIdsFromMimeData`, локальные перемещения/удаления и сверка `targetPosForMove` с семантикой `move_range` («вынуть блок, вставить по TO») на всех комбинациях очереди из пяти строк. Интеграционные прогоны против живого MPD включаются переменной `GMPC_QT_TEST_MPD=/home/sss/.mpd/sock`. Не хватает: fake-соединение для поведенческих тестов браузеров, ленивая загрузка дерева, lazy-модель БД; wiring `IdleWatcher → Connection` проверен только компиляцией; `CurrentPlaylist` как виджет (DnD, контекстное меню) — сборкой и дымовым запуском об offscreen-платформе |
| Build (CMake + Nix flake) | ✅ | Сборка и тесты воспроизводятся только внутри `nix develop` |

## Проверено на живом MPD (2026-10-31, дополнено 2026-11-01)

Окружение: MPD **0.25.0** через unix-сокет (`MPD_HOST=/home/sss/.mpd/sock`), база по `countall`: 1827
артистов / 4462 альбома / 64 854 трэка (`search db artist` при этом отдаёт 1828 записи), очередь
**65 279** позиций. Проверяли временным зондом на libmpdclient
2.24 (те же вызовы, что делает наш код) и прямым протоколом через `nc -U`. Зонды Phase 6 (`build/b3_probe.c`
— окно/сортировки/цена пустого запроса, `build/b3_count_probe.c` — ограничения `searchcount`) лежат в
gitignored `build/` и служат источником замеров №8–№9.

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
| 8 | цена any-tag поиска: окно и сортировка (`build/b3_probe.c`, 2026-11-01) | без окна — вся база: 63 165 трэков за **752 мс**; `window 0:200` — ровно 200 записей за **158 мс**; то же + `sort ArtistSort` — **250 мс**; второй `sort_tag()` → ACK «Unknown filter type»; запрос **без единого совпадения** — **~2,05 с**, повторы того же термина не быстрее | окно обязательно (иначе вся база), сортировка ровно одна, пустой результат дороже полного ⇒ дебаунс в UI и поиск вне GUI-потока; пустые `ArtistSort` сортируются первыми |
| 9 | что принимает `searchcount` (`build/b3_count_probe.c`, 2026-11-01) | только тег-ограничители: `songs=63165`, `playtime=18368566`, `finish=1`. С `sort` **или** `window` → **ошибка 9 / ACK 2 «Unknown filter type»**, при этом соединение выживает (`status` после счётчика успешен, ошибка 0) | для счётчика нужна отдельная сборка шагов без sort/window — `makeCountSteps()`; error-state лечится `clear_error()`, переподключение не нужно |

## Документация: libmpdclient 2.24 и протокол MPD 0.25 (загружено 2026-10-31)

Источники: заголовки установленной библиотеки (`/nix/store/*-libmpdclient-2.24/include/mpd`, первоисточник
для нашего API), `musicpd.org/doc/libmpdclient` (doxygen, уровень 2 — exa), `mpd.readthedocs.io/en/latest/protocol.html`
и `client.html` (уровень 1 — SearXNG + чтение страниц), переписка `mpd-devel` с формулировками мейнтейнера,
`MPD src/protocol/Ack.hxx`. Shell наружу не ходит (DNS недоступен) → вся загрузка шла через MCP-инструменты.
Всё это лежит в KB проекта: **`research/libmpdclient-2.24`** (+ `::errors`, `::idle-loop`, `::search-filters`,
`::search-count`, `::queue-plchanges`, `::binary-artwork`, `::connection-setup`) и **`research/mpd-client-best-practices`**
(+ `::db-discipline`, `::state-sync`).

Поправки к собственным утверждениям плана:

* Библиотека — **libmpdclient 2.24**, а не «0.24»; `OK MPD 0.25.0` в приветствии — версия **протокола**,
  номер демона по соединению узнать нельзя (только `commands`, `tagtypes available`, `protocol available`).
* **`mpd_error_is_fatal()` в libmpdclient не существует** (проверено grep по заголовкам 2.24). Фатальность
  определяет возвращаемое значение `mpd_connection_clear_error()`: `false` = восстановить нельзя
  (`CLOSED`, `OOM`, `TIMEOUT`, `SYSTEM`, `RESOLVER`, `MALFORMED`) → переподключение; `true` = ошибка
  командная (`MPD_SERVER_*`, `ARGUMENT`, `STATE`) → соединение живое, чиним и работаем дальше. Это ровно то,
  что нужно B5 вместо выдуманной функции.
* Коды серверных ошибок берём из `enum mpd_server_error` (`mpd/protocol.h`): `NOT_LIST=1`, `ARG=2`,
  `PASSWORD=3`, `PERMISSION=4`, `UNKNOWN_CMD=5`, `NO_EXIST=50`, `PLAYLIST_MAX=51`, `SYSTEM=52`,
  `PLAYLIST_LOAD=53`, `UPDATE_ALREADY=54`, `PLAYER_SYNC=55`, `EXIST=56` (наш «No such directory» = 50).
  Формат серверного ответа: `ACK [err@listNum] {cmd} text`; дополнительно доступны
  `mpd_connection_get_server_error()`, `_server_error_location()`, `get_system_error()` (errno).
* Инкрементальное обновление очереди — **`mpd_send_queue_changes_meta(conn, queueVersion)`** (`plchanges`) и
  `mpd_send_queue_changes_brief()` + `mpd_recv_queue_change_brief()` (`plchangesposid`, только pos+id);
  имени `mpd_send_pl_changes` в API нет. Удалённые **в хвосте** очереди обнаруживаются сравнением
  `playlistlength` из `status` — доки подчёркивают это отдельно, иначе «исчезнувшие» треки не видны.

Правила, которые меняют архитектуру (привязка к блокерам):

* **B5 / error-state.** Любая неудачная команда оставляет соединение в error-state, и все последующие падают
  с «Cannot send a new command while receiving another response», пока не вызваны `mpd_response_finish()` или
  `mpd_connection_clear_error()`. Значит: RAII-обёртка на каждую отправку + разбор `clear_error()` (см. выше) →
  при `true` только сигнал «ошибка команды», при `false` — разрыв и реконнект. `isConnected()` обязана
  перестать быть `error == SUCCESS`.
* **6.7 / idle.** Канонический паттерн от мейнтейнера: `mpd_send_idle()` → зарегистрировать fd из
  `mpd_connection_get_fd()` в событийном цикле (в Qt — `QSocketNotifier`) → по готовности `mpd_recv_idle()`,
  обработать, снова `idle`; перед своей командой — `mpd_run_noidle()`, разобрать накопленные события, затем
  команды и возврат в idle. События накапливаются, пока клиент не в idle, — потерянных нет; если
  событие уже произошло, следующий `idle` вернётся немедленно. `mpd_recv_idle()` возвращает 0 и при ошибке,
  и когда событий нет → различать только по `mpd_connection_get_error()`.
  **Как сделано (отклонение от первоначального замысла):** слушатель живёт на **втором** соединении и в
  **своём** потоке. План обещал «отдельный поток не нужен», но общее соединение браузеры используют через
  `raw()` синхронными `mpd_run_*` из GUI-потока: вписать idle в него значит поставить `noidle` перед каждым
  вызовом во всех моделях, а пропуск одного пути оставляет транспорт в `MPD_ERROR_STATE` либо в зависшем
  ответе. Цена — одно лишнее соединение (у поиска их уже два), выгода — ни одной гонки с GUI-потоком и
  ноль переделок. Второе отклонение: `mpd_recv_idle(conn, true)` вместо `false` — при настроенном
  `MPD_TIMEOUT` короткий таймаут рвёт ответ idle на середине (а вызов происходит только когда fd уже
  готов, так что ждать нечего). Третье: worker удаляется через `QThread::finished → deleteLater` — у него
  дети-таймеры, а Qt не позволяет останавливать таймеры из чужого потока (`QObject::killTimer` предупреждал
  при `delete worker_` из GUI); проверено зондом на qtbase 6.8.2: объект умирает в потоке worker'а **до**
  возврата `QThread::wait()`, утечки нет (замер — `build/shutdown_probe.cpp`).
* **B3 / поиск.** Фильтр 0.21+: `(TAG == 'V')`, `!=`, `contains`, `starts_with`, `=~` (PCRE), `file`,
  `base 'DIR'`, `modified-since`, `added-since` (0.24), `(!E)`, `(A AND B)`; спец-тег `any` = все теги →
  `mpd_search_add_any_tag_constraint()`. Обязательные модификаторы для нашей базы в 65k трэков:
  **`window START:END`** (страницы), `sort` (`ArtistSort`/`AlbumSort`, `-` = убыв.), `group`; «N найдено» —
  `count`/`searchcount` вместо выгрузки. `find` регистрозависимый, `search` нет (+ICU-нормализация и
  `strip_diacritics`). Экранирование кавычек на протоколе двойное → фильтры только через libmpdclient, никогда
  через `rawCommand`.
* **B2 / текущий трэк.** Целостный текущий трэк берётся из `mpd_run_current_song()`, а не сканированием очереди;
  в `status` есть `songid`/`nextsongid`, `elapsed` (высокая точность) и `duration` (0.20), поле **`error`** —
  текст ошибки воспроизведения от сервера (сейчас игнорируется полностью). MPD вправе **опускать** строки без
  значения → дефолты «неизвестно» в парсере обязательны. Прогресс интерполируем локально таймером между
  событиями, а не поллингом `status`.
* **B6 / перемотка.** `mpd_send_seek_current(conn, seconds, relative)` → `seekcur` (или `mpd_run_seek_id_float`
  по songid — устойчивее к правкам очереди). Позиции/адресация: документация прямо велит использовать ids;
  позиция нестабильна by design; аргумент позиции с префиксом `+`/`-` задаёт смещение относительно текущего трека.
* **B4 / Files.** Прямая цитата протокольных доков про `listall`/`listallinfo`: «Do not use this command. Do not
  manage a client-side copy of MPD's database… query MPD whenever you need something» → ленивое дерево через
  `lsinfo` по раскрытию узла, пути из ответа используем как есть (они уже относительные к `music_directory`),
  отображаем basename. `lsinfo ""` возвращает stored playlists — поведение помечено устаревшим, плейлисты только
  через `listplaylists`.
* **Обложки.** Два разных механизма: `albumart URI OFFSET` ищет в папке трека `cover.png/jpg/jxl/webp`
  (ответ `file:`, `size:`), `readpicture URI OFFSET` читает встроенную картинку (ID3v2 APIC, ответ `size:`,
  `type:` MIME); отсутствие картинки — **успешный пустой** ответ. Чанк по умолчанию 8192 байта, буфер обязан быть
  не меньше чанка, размер настраивается `binarylimit` (0.22.4+). Читаем циклом по offset до `size`; `-1` из
  `mpd_recv_albumart()` — ошибка → обязательный выход из error-state.
* **Соединение.** `mpd_connection_new(host, …)` принимает **путь к unix-сокету** в `host` (порт тогда игнорируется),
  NULL читает `MPD_HOST`/`MPD_PORT`/`MPD_TIMEOUT`; при `new_async()` таймаут по умолчанию 30 с; TCP keepalive
  выключен по умолчанию (`mpd_connection_set_keepalive(true)`, с 2.10). Обёрток для маски тегов и протокольных
  фич нет (`feature.h` даёт только имена/парсинг) → `tagtypes disable …` / `protocol available` через raw-команду;
  маска тегов реально уменьшает ответы (кандидат на ускорение загрузки 65k очереди). Одно соединение = один поток:
  библиотека не потокобезопасна, асинхронные вызовы через `mpd_connection_get_async()` запрещают синхронный API
  до конца ответа.

### Что из этого уже учтено в фазях

`window`/`sort`/`count` — 6.5 и 7.6; `plchanges` + `playlistlength` — 7.2; idle через `QSocketNotifier` — 6.7;
`seekcur`/`seekid_float` — 6.6; `binarylimit` и цикл по чанкам — раздел Cover-art pipeline; реконнект при
фатальном `mpd_error` — B5 и 6.7.

## Блокеры (P0) — без этого приложение не является клиентом MPD

* ✅ **B1. Жизненный цикл браузеров не подключён — исправлено (см. 6.1, `BrowserStack`).**
  `BrowserWidget::onActivate()/onDeactivate()/
  onConnectionChanged()` объявлены и реализованы, но в `src/` нет ни одного вызова; все четыре экрана
  вызывают `setEnabled(false)` в конструкторе (`database-browser.cpp:56`, `file-browser.cpp:92`,
  `playlist-editor.cpp:79`, `search-browser.cpp:159`). Итог: Database / Files / Search / Playlist Editor
  недоступны всегда, включая после успешного коннекта.
  *Фикс:* связать `Connection::connected/disconnected` с `onConnectionChanged()` у всех браузеров и
  вызывать `onActivate()/onDeactivate()` при смене страницы стека (сейчас там только `setCurrentIndex`) + тест на wiring.

* 🟡 **B2. «Текущий играющий трэк» — исправлено наполовину.** `PlaylistModel::currentSong()` = `songs_[0]`
  (`playlist-model.cpp:108-115`); `Connection::Status::songPos` читается, но не используется; `idle` /
  `currentsong` не запрашиваются. Следствие — SongInfo, обложка и трей врут (замер №7); MPRIS-`Metadata`
  по содержательной части пуст → те же жалобы извне.
  *Фикс:* один источник истины — `mpd_run_current_song()` (или `songPos` + индекс модели), обновление по
  смене `songId`, одна и та же структура для SongInfo / CoverWidget / TrayIcon / MPRIS.
  *Сделано (6.2):* разрешение текущего трэка по `songid` с fallback на позицию, сигнал `currentSongChanged(int)`,
  все потребители (SongInfo/CoverWidget/заголовок/tray/sidebar) кормятся от него.
  *Остаток:* целостные метаданные брать `mpd_run_current_song()`, а не строкой из очереди; поле `error` из
  `status` по-прежнему игнорируется.

* **B3. Очередь не обновляется.** `loadFromConnection()` вызывается один раз (только `onConnected`), хотя
  `queueVersion` входит в условие изменения статуса. Добавление из контекстного меню, `OpenUri` из MPRIS,
  Next/Stop при single/consume — очередь в UI не меняется до переподключения. Плюс 914 мс на загрузке.
  *Фикс:* инкрементальные обновления (`mpd_send_queue_changes_meta(conn, queueVersion)` по `queueVersion`,
  хвостовые удаления — через `playlistlength` из `status`) + события (`idle: PLAYER | MIXER | PLAYLIST`),
  дозагрузка вне GUI-потока, колонки очереди (# / Artist / Title / Album / Time).

* ✅ **B4. Files: дерево не спускается ниже корня — исправлено (см. 6.3).** Удвоение пути в
  `FileSystemModel::loadDirectory()` (замер №3b); побочный эффект — неудачная команда вешает общее соединение (B5).
  *Фикс:* имя = basename, путь = ровно то, что вернул MPD; тесты на двухуровневый `lsinfo` с реальными ответами.
  Проверено на живом MPD 2.24 (`build/b4_probe.c`): записи внутри `humor/` приходят как `humor/karcev`,
  вне префикса родителя — ни одной; повторный `lsinfo` того же пути успешен.

* ✅ **B5. Нет обработки ошибок libmpdclient — исправлено (см. 6.4).** Ни один браузер/модель не делал
  `mpd_response_finish()` / `mpd_connection_clear_error()` после неудачи (аккуратен был только
  `Connection::rawCommand()`). Замер №6: одна ошибка → все команды падают с «Cannot send a new command…»,
  `isConnected()` лжёт, `pollStatus()` останавливает таймер и шлёт `disconnected()`.
  *Фикс:* RAII-обёртка «отправить–прочитать–завершить» (`Connection::ResponseScope` + макрос `RUN()` в
  `MpdCommands`), явная очистка ошибки, состояние «ошибка команды» (`commandFailed`) отдельно от
  «нет соединения». Критерий реконнекта — возвращаемое значение `mpd_connection_clear_error()`
  (`false` → фатально: `CLOSED/OOM/TIMEOUT/SYSTEM/RESOLVER/MALFORMED` → новое соединение; `true` → командная
  ошибка, соединение живое). `isConnected()` больше не `error == MPD_ERROR_SUCCESS`; текст —
  `mpd_connection_get_error_message()` + `_get_server_error()`. Реконнект с экспоненциальной задержкой 1–30 с.
  *Уточнение по замерам (`build/rf_probe.c`, `build/b4_probe.c`):* `send()` возвращает `true` даже при ACK —
  ошибка всплывает при чтении; `MPD_ERROR_STATE` документацию не соблюдает (не лечится ничем, кроме нового
  соединения) → в `classifyMpdError()` он `Reconnect`. Порядок строгой: finish → прочитать код и текст → clear.

* ✅ **B6. Перемотка из интерфейса отсутствует — исправлено (см. 6.6).** `seekSlider_` не подключена,
  `ProgressSlider::seekRequested` никуда не приходил; работал только seek через MPRIS (5r).
  *Фикс:* в `ControlBar` стоит `ProgressSlider`, `seekRequested → MpdCommands::seek` (`seekcur`), прогресс —
  через `setPosition(elapsed, total)`; на время перетаскивания polling значения не трогает.
  Попутно найден баг единиц: релиз мыши эмитил значение промиллевой шкалы (0..1000) как секунды → `seekcur`
  получил бы «1000 секунд» вместо конца трека; теперь наружу идут настоящие секунды.

## Баги и деградации (P1)

* ✅ **Search «All fields» всегда пуст — исправлено (см. 6.5).** AND из семи тегов заменён одним any-tag,
  запрос ушёл в worker-поток (`core::SearchEngine`), появились live-поиск с дебаунсом, подключение
  `fieldCombo_` и счётчик «N из M совпадений» (замеры №4a, №8, №9). Остаток: double-click = добавить в
  очередь (7.3) и пагинация окна в UI (7.6).
* **Database: только один уровень.** Нет `canFetchMore()/fetchMore()`; колонка Year пуста; контекстное меню
  на артисте не показывается (`uri` пуст + родитель невалиден → ранний выход в `mainwindow.cpp`).
* ✅ **Current Playlist: взаимодействие отсутствовало полностью — исправлено (см. 7.1).** Не было ни
  double-click (в оригинале `row-activated` есть во всех браузерах: `playlist3-current-playlist-browser.c:430`,
  `…tag2-browser.c:1260`, `…find2-browser.c:193`, `…file-browser.c:209,220`, `…playlist-editor.c:1105`), ни
  контекстного меню, ни DnD/переупорядочивания, ни удаления строки очереди (`Delete`), ни clear/save. Теперь
  есть всё перечисленное; живое обновление очереди осталось за 7.2.
* **Playlist Editor:** «New» создаёт плейлист из текущей очереди; содержимое — `QListWidget` без колонок,
  порядка и удаления строк; нет перетаскивания очереди в плейлист; `goto done` пропускает `response_finish`.
* **Блокирующие вызовы в GUI-потоке:** загрузка очереди (0,9 с), любые `mpd_run_*` из виджетов — нужен
  рабочий поток с очередью команд. Поиск с 6.5 больше не блокирует: он живёт в `core::SearchEngine`
  (worker-поток + своё соединение).
* **Только polling вместо `idle`** — закрыто в 6.7: `core::IdleWatcher` (своё соединение, свой поток,
  `QSocketNotifier`) приносит смену состояния другого клиента за **< 1 мс** вместо до 2 с; single/consume/
  crossfade приходят как `OPTIONS`. Polling остался подстраховкой и поредел: в простое, пока слушатель жив,
  он опрашивает раз в 10 с. Остаток жалобы — нагрузка на сеть при 65-тысячной очереди (это 7.2, `plchanges`).
* **MPRIS Metadata без title/artist/album** — часть жалобы «информация о трэке неверная» извне.
* **Мёртвый код UI:** `sidebarLabel_`,
  `PlaylistEditor::onActivate` (никто не зовёт). С 6.5 больше не мёртв и `SearchBrowser::onFieldChanged`
  — combo полей подключён к нему. `ProgressSlider` с 6.6 работает в `ControlBar`.

## Расхождения UI с оригиналом (`gmpc-orig/`)

| Экран/механика оригинала | Где в оригинале | У нас |
|--------------------------|-----------------|-------|
| Current playlist: `row-activated`, контекстное меню, выбираемые колонки, live-обновление очереди | `browsers/playlist3-current-playlist-browser.c`, `gmpc-mpddata-model-playlist.gob` | 🟡 7.1: double-click/Enter, контекстное меню, DnD, четыре колонки; обновлений очереди нет (7.2), диалога выбора колонок нет |
| Database (tag browser): дерево тегов + список трэков справа, double-click, контекстное меню | `browsers/playlist3-tag2-browser.c:1029,1260` | 🔴 один уровень, ни одного действия |
| Files: дерево каталогов + список файлов, `row-activated` в обеих панелях, корректные basename | `browsers/playlist3-file-browser.c:209,220,462` | 🔴 удвоение путей, нет double-click |
| Find/Search: live-поиск, поле «any», таблица результатов, двойной клик | `browsers/playlist3-find2-browser.c:193`, `Tools/advanced-search.c` | 🟡 live-поиск с дебаунсом через worker (6.5), any-tag, окно + сортировка, счётчик; нет double-click и пагинации |
| Playlist editor: дерево плейлистов + список с операциями, double-click = load | `browsers/playlist3-playlist-editor.c:1105` | 🟡 кнопки CRUD, экран выключен |
| Now Playing (крупная обложка, теги) | `browsers/gmpc-nowplaying2.c` | 🔴 экрана нет |
| Metadata/Info: биография, похожие артисты/трэки, текст песни, фон-обложка | `browsers/gmpc-metadata-browser2.c`, `Widgets/gmpc-metadata-*` | 🔴 экрана нет; LrcLib/lyrics.ovh некуда отдавать текст |
| Server information (статистика MPD) | `browsers/server-information.c` | 🔴 нет |
| Crossfade / Stereo Effects в настройках и UI | `Tools/mpdinteraction.c:440-461`, `glade/preferences-esf-dialog.ui` | 🟡 `crossfade` читается в статус, UI и настроек нет (вместо них single/consume) |
| Выбор колонок таблицы | `egg/eggcolumnchooserdialog.c` | 🔴 нет |
| «Add URL», сохранение очереди как плейлиста | `glade/gmpc-add-url.ui`, `playlist-save-dialog.ui` | 🟡 сохранение очереди есть с 7.1 (контекстное меню Current Playlist → `save <имя>`); диалога «Add URL» нет (`addUri` вызывается только из контекстного меню и MPRIS OpenUri) |
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
- [x] ProgressSlider — кастомный слайдер прогресса (10 тестов; подключён в ControlBar в 6.6)
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

- [x] **6.1 Lifecycle браузеров** (**B1**) — *сделано:* `src/gui/browser-stack.{h,cpp}` владеет списком страниц, включает их по `connected` и активирует по смене активной; `setEnabled(false)` из конструкторов убран. *Тест:* `tests/test-browser-stack.cpp` (9 кейсов: ровно один `onActivate`, отсутствие активации до подключения, однократный `onConnectionChanged`).
- [x] **6.2 Единый источник истины о текущем треке** (**B2**) — *сделано:* `PlaylistModel::setCurrentSong(songId, songPos)` (поиск по id, fallback по pos, иначе «нет текущего»), сигнал `currentSongChanged(int)`, MainWindow кормит SongInfo/CoverWidget/заголовок/tray/sidebar только от него. *Тест:* `tests/test-playlist-model.cpp` (8 кейсов: id важнее позиции, отсутствие сигнала при повторной доставке того же трека). Остаток: запрашивать сам трэк через `mpd_run_current_song()`, а не только позицию из `status`.
- [x] **6.3 Пути в дереве файлов** (**B4**) — *сделано:* `FileNode::path` хранит путь ровно как его вернул MPD (относительный к `music_directory`, целиком), отображается `mpdBasename(path)`; дочерний `lsinfo` идёт по этому же пути, неудача не оставляет узел «загруженным» и не вешает соединение (`ResponseScope`). *Тест:* `tests/test-mpd-error.cpp` —кейсы `mpdBasename()` (включая хвостовые `/`, путь из одних слэшей, пустую строку); эмпирически двухуровневый спуск проверен на живом MPD (`build/b4_probe.c`: 0 записей вне префикса родителя).
- [x] **6.4 Выход из error-state** (**B5**) — *сделано:* `Connection::consumeError(context)` (finish → код и текст → clear) + RAII `Connection::ResponseScope`, обёрнуты все браузеры/модели и каждая команда `MpdCommands` (макрос `RUN()`); классификация в `src/core/mpd-error.{h,cpp}` (`None`/`KeepConnection`/`Reconnect`, `STATE` → Reconnect по замерам, вопреки заголовку); сигнал `commandFailed` отделён от `disconnected`; реконнект с экспоненциальной задержкой 1–30 с из сохранённых host/port/password. *Тест:* `tests/test-mpd-error.cpp` (таблица классификации, в т.ч. `STATE → Reconnect`; `ResponseScope` на неподключённом соединении не стреляет сигналами); эмпирически — ACK `NO_EXIST(50)` на `lsinfo`, после которого `status` успешен (`build/b4_probe.c`).
- [x] **6.5 Поиск на any-tag** (**B3 по семантике**) — *сделано:* `src/core/search-engine.{h,cpp}`. Чистые `makeSearchSteps()` / `makeCountSteps()`: один any-tag вместо семи AND-тегов (замер №4a), ровно один `sort ArtistSort` (второй сервер отвергает ACK, замер №8) и окно `offset..offset+limit` с потолком 1000 — без него улетает вся база (752 мс). `SearchEngine` — worker-поток с **собственным** `mpd_connection` (одно соединение = один поток), отмена по номеру запроса, бросок чтения на границе трэка и закрытие соединения при брошенном ответе (`MPD_ERROR_STATE` не лечится); счётчик «N найдено» — вторым round-trip через `searchcount`, для которого собираются **только ограничители**: sort/window он не принимает, ACK 2 «Unknown filter type» (замер №9). Фильтр собран исключительно средствами libmpdclient. `SearchBrowser`: дебаунс 300 мс, Enter без дебаунса, `fieldCombo_ → onFieldChanged`, строка «N из M совпадений», немедленная очистка при пустом вводе, отмена при уходе с экрана и при смене сервера; endpoint берётся из геттеров `Connection` (6.5 же добавил `host()/port()/password()/timeoutMs()`). *Тест:* `tests/test-search-engine.cpp` — 13 кейсов / 71 ассерт (один any-tag, ровно один sort, окно всегда, `makeCountSteps` без sort/window, отмена и смена endpoint) + интеграционный прогон на живом MPD при `GMPC_QT_TEST_MPD=/home/sss/.mpd/sock`: «получено 20 из 63 165», повторный поиск после `cancel()` успешен; `tests/test-search-browser.cpp` — 17 кейсов (модель, счётчик, ошибка движка очищает строки, пустой ввод не уходит в движок). *Остаток:* double-click «добавить в очередь» — это 7.3; пагинация (`SearchQuery::offset`) движком поддерживается, но в UI не выведена (7.6).
- [x] **6.6 Seek из GUI** (**B6**) — *сделано:* в `ControlBar` вместо голого `QSlider` стоит `ProgressSlider`, `seekRequested(int seconds) → MpdCommands::seek` (`seekcur` через `mpd_run_seek_current(MPD, s, false)`); прогресс задаётся `setPosition(elapsed, total)`, поэтому на время перетаскивания polling значения не меняет. Исправлен баг единиц: релиз мыши отдавал значение промиллевой шкалы как секунды. *Тест:* `tests/test-progress-slider.cpp` — «emits seekRequested in seconds on release» (250‰ от 120 с ⇒ 30), конец шкалы ⇒ полная длительность, отсутствие seek при неизвестной длительности, polling не перебивает drag. Сам connect() проверен только на этапе компиляции (new-style connect): отдельного теста `ControlBar → MpdCommands::seek` нет — для него нужен fake-транспорт (ср. 7.5).
- [x] **6.7 idle-цикл событий** — *сделано:* `core::IdleWatcher` (`src/core/idle-watcher.{h,cpp}`) + общий хелпер `MpdEndpoint`/`openMpdConnection()` (`src/core/mpd-endpoint.{h,cpp}`, его же теперь использует и поиск). Слушатель держит **своё** соединение в **своём** потоке: `mpd_send_idle_mask(PLAYER|OPTIONS|MIXER|QUEUE|STORED_PLAYLIST|DATABASE|UPDATE)` → `QSocketNotifier(mpd_connection_get_fd(), Read)`, создаваемый внутри worker'а (notifier обязан жить там, где крутится цикл) → по готовности `mpd_recv_idle(conn, true)`, различение «событий нет» и ошибки через `mpd_connection_get_error()`, рассылка флагов и повторный `idle`. Обрыв → переподключение 1→30 с и `watchingChanged(false)`; на TCP включён keepalive (idle-соединение молчит часами). Wiring: `Connection::refreshNow()` по `playerChanged`/`mixerChanged`/`queueChanged`, `Connection::setIdleWatched()` по `watchingChanged` — интервал в простое 2000 → **10000 мс**, при воспроизведении остаётся 500 мс (прогресс-бару нужны тики, idle о секундах не сообщает). Очередь инкрементально — по-прежнему 7.2: `queueChanged` только дергает статус, где уже есть `queueVersion`. *Тест:* `tests/test-idle-watcher.cpp` — соответствие битов `enum mpd_idle` флагам (включая алиас `PLAYLIST == QUEUE` и отбрасывание sticker/message/partition/neighbor/mount), пустой адрес и мёртвый сокет (не «живёт», не весит, падать не обязан), плюс живая прогонка: второе соединение переключает `random`, слушатель приносит `OPTIONS` за **< 1 мс**.

## Фаза 7 — Интерактив (текущий плейлист и браузеры)

- [x] **7.1 Current Playlist** — *сделано:* `PlaylistModel` переведён с `QAbstractListModel` на `QAbstractTableModel`: колонки Title/Artist/Album/Time, у времени выравнивание вправо, заголовок без тега Title откатывается на имя файла из URI, неизвестная длительность показывается `--:--`, а не `0:00`. Позиция отдельной колонкой не стала — в полном списке очереди она и есть номер строки (`renumberPositions()`), отдельный столбец «#» оригинал всё равно не показывал. Сортировка прокси идёт по новой роли `SortRole`: по `DisplayRole` десятиминутный трэк («10:00») встал бы раньше девятисекундного («9:00»). *Команды:* `MpdCommands::removeSongIds()` (`deleteid`, по одному идентификатору — формы со списком id в libmpdclient 2.24 нет), `moveQueueBlock()` (`move_range`), `saveQueueAs()` (`save`). *Координаты:* MPD считает аргумент TO по очереди, из которой переносимые строки уже вынуты, поэтому при движении вниз итоговая позиция на длину блока меньше экранной (`to = destRow - count`); расхождение вынесено в чистую `PlaylistModel::targetPosForMove()` и сверено с эталоном «вынуть блок, вставить по TO» на всех комбинациях очереди из пяти строк (блоки 1..3, все `destRow` от −1 до 5). *Qt:* `beginMoveRows()` ждёт `destinationChild` в координатах **до** перемещения и вне `[first,last]`, а внутри у него `Q_ASSERT` (возврат `false` не спасает) → предпроверка обязательна; событие дропа разбирается `eventFilter`-ом на viewport, потому что базовая обработка ушла бы в `model()->dropMimeData()`, которой у нас нет (перестановка очереди — не перемещение данных); `indexAt()` публичен, а вот `dropIndicatorPosition()` и сам `enum DropIndicatorPosition` объявлены protected, поэтому наружу позиция индикатора отдаётся готовым bool из локального наследника `QueueTreeView`. Строка очереди тащится собственным MIME-типом (`application/x-gmpc-qt-songid`, id через запятую) плюс обычный текст — перетаскивание в стороннее приложение не должно приносить пустоту. *Виджет:* double-click и Enter → `playid` (как `row-activated` в оригинале), контекстное меню Play / Remove from queue / Move up / Move down / Clear queue (с подтверждением) / Save queue as…, Delete/Backspace с контекстом `WidgetWithChildrenShortcut` — на весь стек браузеров эти клавиши перехватывать нельзя. Удаляем по идентификаторам: пока пользователь выбирал строки, очередь могла сдвинуться (другой клиент, consume). *Побочно найденный баг:* после удаления играющей строки `currentSong()` разрешала трэк по устаревшей позиции и объявляла играющим соседний трэк — позиция снимается, если идентификатор играющего трека исчез из очереди. *Тест:* `tests/test-playlist-model.cpp` — **34 кейса / 224 ассерта** (колонки и их значения, откат заголовка, `--:--`, сортировка по длительности на паре 10:00/0:09, круговой проход `mimeData → songIdsFromMimeData`, чужие данные не принимаются, перемещения/удаления и сверка координат выше); `ctest` — 22/22; дымовой запуск GUI на offscreen-платформе с коннектом к живому MPD. *Остаток:* живого обновления очереди нет (7.2), поэтому отказ команды сервера не откатывает локальную копию до ближайшего обновления; непоследовательное выделение не переносится — одна `move_range` описывает ровно один смежный блок; drag&drop и контекстное меню вживую на MPD не проверялись (проверка тасует реальную очередь из 65 279 позиций), семантика TO подтверждена только эталонной моделью и заголовками libmpdclient.
- [ ] **7.2 Обновление очереди:** перезагрузка по idle `playlist`/`player`, инкрементально через `plchanges <version>`; загрузку убрать из GUI-потока (замер: 914 ms на 65k позиций). *Тест:* изменение очереди другим клиентом видно без переключения вкладок.
- [ ] **7.3 Двойной клик во всех браузерах** (`row-activated` в оригинале): БД — добавить группу/альбом/трек; Файлы — добавить каталог/файл; Поиск — добавить трек; Редактор — добавить/удалить позицию.
- [ ] **7.4 Контекстные меню** для всех деревьев (добавить в очередь / заменить очередь / открыть папку / копировать путь). *С 7.1 снято только упоминание текущего плейлиста:* своё меню очереди там уже есть, но «добавить» в него нечего — строки уже в очереди. Остались БД (меню на артисте не показывается из-за пустого `uri`) и Редактор плейлистов.
- [ ] **7.5 Playlist Editor:** настоящий редактор — список позиций, add/remove/move/clear из дерева БД и Файлов, сохранение через `mpd_save_playlist`, загрузка через `loadPlaylist`. *Тест:* fake-соединение: New → Load → Add → Save отправляет ожидаемую последовательность команд.
- [ ] **7.6 Живой поиск:** дебаунс 250–300 ms, Enter = добавить в очередь / «заменить очередь», история запросов (Ctrl+↑), счётчик найденного. *Из этого сделано в 6.5:* дебаунс 300 мс и счётчик «N из M совпадений»; не хватает действий по Enter (сейчас — повтор поиска), истории запросов и пагинации окна.

## Фаза 8 — Недостающие экраны и функции

- [ ] Now Playing отдельной страницей (крупная обложка + лирика), а не только control bar.
- [ ] Metadata/Info для выбранного трека + просмотр лирики (потребитель для `LyricsProvider`).
- [ ] Server information (`stats`), обновление по idle `database|update`.
- [ ] Настройки: crossfade, переключение выходов (ESF в оригинале), тема/иконки — сейчас только подключение и горячие клавиши.
- [ ] Настраиваемые колонки текущего плейлиста + форматная строка (аналог `mpc-format`).
- [ ] Add URL / Save queue as playlist / Clear queue из меню. *С 7.1* сохранение очереди и очистка есть в контекстном меню текущего плейлиста; отдельного пункта главного меню и диалога «Add URL» нет.
- [ ] IPC: управление уже запущенным экземпляром вместо второго GUI-процесса.

## Политика тестирования (что менять в подходе)

172 зелёных теста почти ничего не доказывают: они проверяют парсинг строк, наличие заголовков колонок и существование виджетов. Ни один баг из B1–B6 ими не поймать.

- [ ] **Wiring-тесты lifecycle:** после подключения все браузеры enabled и вызван `onConnectionChanged`; смена вкладки → `onActivate`. Поймал бы B1, отсутствие загрузки очереди, B4.
- [ ] **Поведенческие тесты на fake-соединении:** текущий трек = `songid` из status (B2); ACK-ошибка не оставляет соединение в error-state (B5).
- [ ] **Интеграционные тесты против реального MPD** (`MPD_HOST`, отдельная служебная база, пропуск без сервера): lsinfo/дерево, any-tag поиск, plchanges. Начато в 6.5: `test-search-engine` пропускается без `GMPC_QT_TEST_MPD`, а с ним проверяет окно и счётчик на живой базе; для дерева и очереди таких тестов ещё нет.
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

