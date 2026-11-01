# gmpc-qt

**G**nome **M**usic **P**layer **C**lient — перерождённый в Qt.

Порт оригинального [gmpc](https://github.com/nicklan/gmpc) (GNOME Music Player Client,
последний стабильный релиз 2011) на **Qt6** и **C++20**, сгенерированный в основном LLM
под чутким — *и не очень* — руководством человека с клавиатурой.

## Почему

Оригинальный gmpc был прекрасен: лёгкий, быстрый, с идеальным для MPD клиента
интерфейсом. Но GTK2 умер, libglade никому не нужен, а патчи под современные
MPD протоколы копятся в дистрибутивных форках без надежды на мейнлайн.
Вместо того, чтобы оплакивать ушедшую эпоху, мы (я — LLM, и мой коллега-человек)
решили воскресить проект на современном стеке.

## Возможности

- Подключение к MPD-серверу по протоколу TCP
- Отображение очереди воспроизведения (Current Playlist)
- Навигация по медиатеке (Database / Files / Search)
- Редактор сохранённых плейлистов (Playlist Editor)
- Управление воспроизведением: play/pause/stop/prev/next
- Режимы: repeat, random, single, consume
- Регулятор громкости и ползунок позиции

*Что дальше — см. `PLAN.md`.*

## Сборка / разработка

```bash
# Войти в dev-окружение (Nix flake, nixos-unstable)
nix develop

# Собрать
cmake -B build && cmake --build build

# Собрать документацию
cmake --build build --target doc
# результат: build/docs/doxygen/html/index.html

# Установить
nix build
# или cmake --install build --prefix /usr/local
```

## Архитектура

Базовый план — «уйти от внутренней архитектуры gmpc, оставив похожий интерфейс».

```
src/
├── core/          # Connection (libmpdclient), MpdCommands, PlaylistModel, Config
├── gui/           # MainWindow, ControlBar, StatusBar, TrayIcon
├── browsers/      # 5 браузеров, унаследованных от BrowserWidget
├── widgets/       # VolumeWidget, ProgressSlider, SongInfo, CoverWidget
├── metadata/      # MetadataManager, CoverProvider, CacheSqlite
├── providers/     # LastFM, Discogs, LyrDB (заготовки)
└── tools/         # AsyncDownload, Log
```

## Технологии

| Компонент     | Версия / детали                         |
|---------------|-----------------------------------------|
| Qt6           | qtbase, qtsvg, qttools, qtwayland       |
| Стандарт C++  | 20 (C++17 где Clang/GCC не сошлись)     |
| CMake         | ≥ 3.20                                  |
| libmpdclient  | ≥ 2.24                                  |
| SQLite3       | кеш метаданных                          |
| libcurl       | HTTP-загрузка                           |
| Nix           | flake (nixos-unstable)                  |
| Doxygen       | генерация API-документации с UML        |

## Лицензия

GPLv2 (как и оригинальный gmpc). См. файл `COPYING` (TODO: добавить).

## Благодарности

- **Qball Cow** — за оригинальный gmpc, который остаётся образцом MPD-клиента
- **Qwen Code** — за 90% кода, 10% терпения и 0% сна
