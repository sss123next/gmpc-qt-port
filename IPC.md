# IPC (Inter-Process Communication)

Управление уже запущенным экземпляром gmpc-qt через командную строку.
Реализует одноинстансность — второй GUI-процесс не стартует, а посылает
команду первому и завершается.

**Содержание**

- [Мотивация](#мотивация)
- [Архитектура](#архитектура)
  - [Жизненный цикл](#жизненный-цикл)
  - [Компоненты](#компоненты)
- [Протокол](#протокол)
  - [Команды](#команды)
  - [Путь сокета](#путь-сокета)
- [CLI-флаги](#cli-флаги)
- [Wiring (main → IpcServer → MainWindow)](#wiring-main--ipcserver--mainwindow)
- [Исходный код](#исходный-код)
- [Тесты](#тесты)
- [Отличия от оригинала (GMPC libunique)](#отличия-от-оригинала-gmpc-libunique)
- [Безопасность и ограничения](#безопасность-и-ограничения)

---

## Мотивация

В оригинальном GMPC одноинстансность реализована через Gnome-библиотеку
**libunique** (`UniqueApp`). Для Qt-порта libunique недоступен; заменой служит
связка `QLocalServer` / `QLocalSocket` из Qt6 Network module.

Задачи:

1. **Предотвратить запуск второй копии GUI.** Пользователь кликает по ярлыку
   снова — не открывается второе окно, а поднимается уже открытое.
2. **CLI-управление.** Скрипты и hotkey-демоны (xbindkeys, sxhkd,
   медиа-клавиши) могут отдавать команды через оболочку:
   ```bash
   gmpc-qt --toggle          # свернуть / развернуть
   gmpc-qt --quit             # завершить работающий экземпляр
   gmpc-qt --stream http://… # загрузить поток
   ```
3. **Совместимость с оригиналом.** Оригинал понимал команды `show`, `hide`,
   `toggle`, `quit`, `preferences`, `show notification`, `url <адрес>` —
   наш IPC сохраняет их все.

## Архитектура

### Жизненный цикл

```
main()
  │
  ├─ QApplication app(argc, argv)
  ├─ IpcCommand cmd = parseIpcCommandLine(argc, argv)
  │
  ├─ if (!Config::allowMultiple())
  │   ├─ if (sendIpcCommand(cmd))  // есть работающий экземпляр?
  │   │   └─ return 0              // команда ушла, наш процесс завершён
  │   └─ if (cmd.kind == Quit)
  │       └─ return 0              // --quit, но никто не запущен
  │
  ├─ IpcServer *ipc = new IpcServer
  ├─ QLocalServer::removeServer(ipcSocketPath())  // чистим stale-сокет
  ├─ ipc->start()                                 // начинаем слушать
  │
  ├─ MainWindow w
  ├─ connect(ipc → MainWindow::showWindow / hideWindow / …)
  ├─ w.show()
  └─ app.exec()
```

1. **Ранняя проверка:** до создания MainWindow пробуем соединиться с сокетом.
2. **Два исхода:**
   - Соединение успешно → отправляем команду и `exit(0)`.
   - Соединение не удалось (сокета нет) → мы первый экземпляр.
3. **Запуск сервера:** удаляем возможный stale-сокет (если прошлая копия
   упала), слушаем на том же пути.
4. **Разводка сигналов:** `IpcServer::showRequested` → `MainWindow::showWindow()`
   и т.д.

### Компоненты

| Компонент | Файл | Роль |
|-----------|------|------|
| `IpcServer` | `src/tools/ipc.{h,cpp}` | QObject-обёртка над `QLocalServer`. Принимает входящие соединения, читает строки, диспетчеризует сигналы. |
| `sendIpcCommand()` | `src/tools/ipc.cpp` | Создаёт `QLocalSocket`, подключается к сокету, пишет одну строку-команду, отключается. |
| `parseIpcCommandLine()` | `src/tools/ipc.cpp` | Разбирает `argc/argv` через `QCommandLineParser` в `IpcCommand`. |
| `ipcSocketPath()` | `src/tools/ipc.cpp` | Возвращает абсолютный путь Unix-сокета. |
| `IpcCommand` | `src/tools/ipc.h` | Struct с полем `kind` (enum) и `arg` (payload). |

## Протокол

Однонаправленный, текст-based. Второй экземпляр открывает Unix-сокет, пишет
ровно одну строку с **LF** (`\n`) на конце и закрывает соединение.

Формат строки:

```
команда\n
команда:payload\n
```

Команда и payload — UTF-8. Payload отделяется двоеточием. Команда с payload
может содержать только один `:` — второй и последующие двоеточия считаются
частью payload.

### Команды

| Строка в сокете | Сигнал IpcServer | Действие на MainWindow |
|----------------|-------------------|-------------------------|
| `show` | `showRequested()` | `showNormal(); raise(); activateWindow()` |
| `hide` | `hideRequested()` | Если трей виден → `hide()`, иначе `showMinimized()` |
| `toggle` | `toggleRequested()` | `isVisible() && !isMinimized()` → hide, иначе show |
| `quit` | `quitRequested()` | `QApplication::quit()` |
| `preferences` | `preferencesRequested()` | Показать окно настроек |
| `show notification` | `showNotificationRequested()` | Нотификация через SystemTray |
| `url:*address*` | `urlRequested(address)` | `MpdCommands::addUrl(address)` — `load` на сервер |
| `stream:*url*` | `streamRequested(url)` | То же, что и url (оба завязаны на `handleIpcUrl`) |
| `easycommand:*text*` | `easyCommandReceived(text)` | Fallback — текст идёт как есть |
| `bug information` | `easyCommandReceived("bug information")` | Не имеет dedicated-слота, уходит в fallback |

**Важно:** если соединение с MPD разорвано, `url` и `stream` молча
логируют предупреждение, но не падают — команду некому отдать.
Оригинал делал то же самое.

### Путь сокета

```
{QStandardPaths::TempLocation}/gmpc-qt.sock   — обычно /tmp/gmpc-qt.sock
{XDG_RUNTIME_DIR}/gmpc-qt.sock                — если переменная установлена
```

**Приоритет:** `$XDG_RUNTIME_DIR` предпочтительнее — директория очищается при
выходе из пользовательской сессии и не требует ручной чистки stale-сокетов.

Сокет удаляется:
- В деструкторе `IpcServer` (нормальное завершение).
- В `main.cpp` перед `ipcServer->start()` (чистка после упавшей копии).

Сокет **не удаляется** внутри `IpcServer::start()` — это позволило бы
второму экземпляру захватить его у первого (race condition). Зачистка
происходит только после того, как `sendIpcCommand()` подтвердил отсутствие
другого экземпляра.

## CLI-флаги

Все флаги обрабатываются `parseIpcCommandLine()` → `IpcCommand`.

| Флаг | Синоним | Аргумент | Команда |
|------|---------|----------|---------|
| `--show` | | — | `show` |
| `--hide` | | — | `hide` |
| `--toggle` | | — | `toggle` |
| `--quit` | `-q` (через parseAsLongOptions) | — | `quit` |
| `--preferences` | | — | `preferences` |
| `--show-notification` | | — | `show notification` |
| `--stream` | | URL | `stream:<url>` |
| `--url` | | адрес | `url:<address>` |
| `--easycommand` | | текст | `easycommand:<text>` |
| `--bug-information` | | — | `bug information` |
| позиционный аргумент | | URL | `stream:<url>` (shorthand) |

**Примеры:**

```bash
# Поднять окно (поведение по умолчанию)
gmpc-qt

# Свернуть в трей
gmpc-qt --hide

# Переключить видимость
gmpc-qt --toggle

# Завершить
gmpc-qt --quit

# Открыть настройки
gmpc-qt --preferences

# Добавить поток/плейлист в очередь
gmpc-qt --stream http://radio.example/stream.ogg
gmpc-qt http://radio.example/stream.ogg       # то же, сокращённо

# Добавить URL как сохранённый список
gmpc-qt --url http://radio.example/stream.ogg

# Произвольная easy-команда
gmpc-qt --easycommand "preferences"
```

Когда экземпляр уже запущен и **allowMultiple** выключен, все эти флаги
отправляют команду работающему процессу и завершаются. Когда экземпляр не
запущен — `--show` (или без флагов) стартует GUI, `--quit` молча выходит
с сообщением, остальные флаги стартуют GUI (но команда не выполняется —
её некому было обработать).

## Wiring (main → IpcServer → MainWindow)

```cpp
// main.cpp (упрощённо)
if (!Config::instance().allowMultiple()) {
    if (sendIpcCommand(ipcCmd))
        return 0;   // команда ушла работающему экземпляру
    if (ipcCmd.kind == IpcCommandKind::Quit)
        return 0;   // --quit, но никто не запущен
}

IpcServer *ipc = new IpcServer(&app);
QLocalServer::removeServer(ipcSocketPath());
ipc->start();

MainWindow w;

connect(ipc, &IpcServer::showRequested,            &w, &MainWindow::showWindow);
connect(ipc, &IpcServer::hideRequested,            &w, &MainWindow::hideWindow);
connect(ipc, &IpcServer::toggleRequested,          &w, &MainWindow::toggleVisibility);
connect(ipc, &IpcServer::quitRequested,            &w, &MainWindow::handleIpcQuit);
connect(ipc, &IpcServer::preferencesRequested,     &w, &MainWindow::showPreferences);
connect(ipc, &IpcServer::showNotificationRequested,&w, &MainWindow::handleIpcShowNotification);
connect(ipc, &IpcServer::urlRequested,             &w, &MainWindow::handleIpcUrl);
connect(ipc, &IpcServer::streamRequested,          &w, &MainWindow::handleIpcUrl);

w.show();
```

Сигнал `IpcServer::easyCommandReceived` не имеет постоянного wiring —
он предназначен для нестандартных команд и может быть подключён внешним
кодом или обработчиком плагинов.

## Исходный код

| Назначение | Путь |
|------------|------|
| Заголовок | `src/tools/ipc.h` |
| Реализация | `src/tools/ipc.cpp` |
| Интеграция в `main()` | `src/main.cpp` |
| Слоты MainWindow | `src/gui/mainwindow.h` / `mainwindow.cpp` |
| Методы TrayIcon | `src/gui/trayicon.h` / `trayicon.cpp` |
| Настройка allowMultiple | `src/core/config.h` / `config.cpp` |
| Тесты | `tests/test-ipc.cpp` |
| Регистрация теста | `tests/CMakeLists.txt` |
| Сборка | `CMakeLists.txt` (TOOL_SOURCES) |

## Тесты

`test-ipc` — 20 кейсов, 84 ассерта:

| Категория | Кейсы | Что проверяют |
|-----------|-------|---------------|
| **Жизненный цикл сервера** | 2 | Старт и приём соединения; отказ дубликата (`listen()` занят) |
| **Сигналы (без payload)** | 6 | `show`, `hide`, `toggle`, `quit`, `preferences`, `show notification` — каждый проверяет `QSignalSpy::count() == 1` |
| **Сигналы (с payload)** | 3 | `stream:`, `url:`, `easycommand:` — проверяют, что value сигнала совпадает с отправленным |
| **Обработка на стороне клиента** | 5 | `sendIpcCommand` без сервера, с сервером (show, stream, easycommand, None) |
| **Краевые случаи** | 3 | Неизвестная команда (не падает), несколько команд в одном соединении, несколько одновременных соединений |
| **Путь сокета** | 1 | `ipcSocketPath()` использует `$XDG_RUNTIME_DIR`, оканчивается на `gmpc-qt.sock` |

Запуск:
```bash
cd build-tests/tests && ./test-ipc
```

Сборка только теста:
```bash
cd build-tests && make test-ipc
```

## Отличия от оригинала (GMPC libunique)

Оригинал (`Tools/ipc.c`):

- Использует **libunique** (`UniqueApp`) — Gnome-библиотеку для
  одноинстансности, работающую через D-Bus или X11 selections.
- Регистрирует две команды: `stream` (с filename) и `easycommand` (с текстом).
- `stream` → `url_start_real(fn)` — загружает URL через HTTP-фетчер
  (умел скачивать m3u/xspf) и отдаёт `mpd_run_load`.
- `easycommand` → `gmpc_easy_command_do_query()` — таблица команд,
  включая `quit`, `hide`, `show`, `toggle`, `show notification`,
  `preferences`, `bug information`, `url [адрес]`.
- `allow-multiple` в конфиге — настройка, отключающая IPC-проверку.
- При запуске без IPC (CLIENT_ONLY или без libunique) приложение
  стартует без одноинстансности.

Наш IPC:

- Использует **QLocalServer** / **QLocalSocket** — портабельно между
  Unix и Windows (в теории; на практике тестируется только Linux).
- Сохраняет ту же таблицу easy-команд, но диспетчеризует их сигналами,
  а не таблицой callback'ов.
- `stream` и `url` разделены на два сигнала, но оба завязаны на
  `MainWindow::handleIpcUrl(url)` → `MpdCommands::addUrl(url)`
  (команда `load` серверу). Клиентского HTTP-фетчера нет — сервер MPD
  сам разворачивает URL своей реализацией `urlhandlers`.
- `bug information` не имеет dedicated-слота в MainWindow; сигнал
  уходит как `easyCommandReceived` — может быть подключён позже.
- Добавлена поддержка нескольких одновременных соединений (оригинал
  обрабатывал по одному).
- Добавлен короткий `--stream <url>` через позиционный аргумент.

## Безопасность и ограничения

- **Сокет доступен только одному пользователю.** `$XDG_RUNTIME_DIR`
  имеет права `0700`; сокет внутри неё автоматически недоступен другим
  пользователям.
- **Никакой аутентификации.** Любой процесс того же пользователя может
  подключиться к сокету и послать команду. Это дизайн оригинала —
  для настольного плеера достаточно.
- **Неатомарная проверка «есть ли сервер?».** Между вызовом
  `sendIpcCommand()` и `IpcServer::start()` существует микро-окно,
  в котором второй экземпляр может пройти ту же проверку. Вероятность
  ничтожна для настольного приложения; при желании можно закрыть
  блокировкой файла (`QLockFile`).
- **Stale-сокет** после `kill -9` зачищается вызовом
  `QLocalServer::removeServer()` в `main.cpp` **после** подтверждения,
  что никто другой не слушает. Без этой проверки второй экземпляр мог бы
  удалить сокет работающего первого.
- **allowMultiple = true** полностью отключает IPC-проверку. Полезно
  для отладки или запуска нескольких экземпляров с разными профилями.
  Команды `--quit` и другие при этом игнорируются.
