# Implementation Plan: Additional Cover Art & Lyrics Services

Date: 2026-11-24
Based on: docs/research-additional-services.md

## Services to Implement

### Phase 1: Apple Music / iTunes Search API (Cover Art)
### Phase 2: lrc.red (Synchronized Lyrics)
### Phase 3: Lrcmux (Aggregator Lyrics)
### Phase 4: Unison (Community Lyrics)

---

## Phase 1: Apple Music Cover Provider

### Files to create/modify

1. **NEW: `src/providers/apple-music.h`** — Header for Apple Music cover provider
2. **NEW: `src/providers/apple-music.cpp`** — Implementation
3. **MODIFY: `src/metadata/cover-provider.h`** — Add `tryAppleMusic()` method
4. **MODIFY: `src/metadata/cover-provider.cpp`** — Add Apple Music cascade step
5. **MODIFY: `CMakeLists.txt`** — Add new source files

### Architecture
- `AppleMusicProvider` class: single-shot HTTP GET to iTunes Search API.
- Response: JSON → extract `artworkUrl100` → replace size → download image.
- Called from `CoverProvider::fetchCover()` cascade after MusicBrainz, before Last.FM.

### AppleMusicProvider API
```cpp
class AppleMusicProvider : public QObject {
    Q_OBJECT
public:
    explicit AppleMusicProvider(QObject *parent = nullptr);
    void searchCover(const QString &artist, const QString &album,
                     const std::function<void(const QByteArray &)> &onSuccess,
                     const std::function<void()> &onNotFound,
                     const std::function<void(const QString &)> &onError);
};
```

### Integration into CoverProvider
In `CoverProvider::tryMusicBrainz()` → if MusicBrainz fails → call `tryAppleMusic()`.
In `tryAppleMusic()` → search iTunes API → if found, download image → call `tryComplete()`.
If not found → call `tryLastFm()`.

### Key details
- Search URL: `https://itunes.apple.com/search?term={artist}+{album}&entity=album&limit=5`
- Match by artist name (case-insensitive) on `artistName` field.
- Use `artworkUrl100` → replace `100x100bb` with `600x600bb` (good balance).
- Rate limit: ~20 req/min — respect via existing `seq_` stale-check pattern.

---

## Phase 2: lrc.red Lyrics Provider

### Files to create/modify
1. **NEW: `src/providers/lrc-red.h`** — Header
2. **NEW: `src/providers/lrc-red.cpp`** — Implementation
3. **MODIFY: `src/metadata/metadata-manager.h`** — Add LrcRedProvider member
4. **MODIFY: `src/metadata/metadata-manager.cpp`** — Add to lyrics cascade
5. **MODIFY: `CMakeLists.txt`** — Add new source files

### LrcRedProvider API
```cpp
class LrcRedProvider : public QObject {
    Q_OBJECT
public:
    explicit LrcRedProvider(QObject *parent = nullptr);
    void fetchLyrics(const QString &artist, const QString &track,
                     int durationSec = 0);
signals:
    void lyricsFound(const QString &plainLyrics, const QString &syncedLyrics);
    void notFound();
    void error(const QString &message);
};
```

### Integration
In `MetadataManager::requestLyrics()` → after LRCLIB → if LRCLIB not found → try LrcRed.
In `MetadataManager` constructor:
```cpp
connect(lrcRedProvider_, &LrcRedProvider::lyricsFound, ...);
connect(lrcRedProvider_, &LrcRedProvider::notFound, ...);
connect(lrcRedProvider_, &LrcRedProvider::error, ...);
```

### Key details
- Search: `GET https://lrc.red/search.json?q={artist}+{track}`
- Match by title/artist and duration tolerance (±3s).
- Extract ISRC from search results.
- Download TTML: `GET https://lrc.red/s/{ISRC}.ttml` (word/syllable sync).
- Or download LRC: `GET https://lrc.red/s/{ISRC}.lrc` (line sync).
- Send `User-Agent: gmpc-qt/0.3.x (https://github.com/ssss/gmpc-qt; email)`.
- TTML parsing: extract text lines, strip XML. For synced, return LRC-like format.
- Store synced lyrics in `syncedLyrics` field, plain text in `plainLyrics`.

### TTML → plain text conversion
- Strip `<tt>`, `<body>`, `<div>`, `<p>` tags.
- Extract text content between `<span>` tags.
- Join with newlines.

---

## Phase 3: Lrcmux Lyrics Provider

### Files to create/modify
1. **NEW: `src/providers/lrcmux.h`** — Header
2. **NEW: `src/providers/lrcmux.cpp`** — Implementation
3. **MODIFY: `src/metadata/metadata-manager.h`** — Add LrcmuxProvider member
4. **MODIFY: `src/metadata/metadata-manager.cpp`** — Add to lyrics cascade
5. **MODIFY: `CMakeLists.txt`** — Add new source files

### LrcmuxProvider API (same interface as LrcRedProvider)
```cpp
class LrcmuxProvider : public QObject {
    Q_OBJECT
public:
    explicit LrcmuxProvider(QObject *parent = nullptr);
    void fetchLyrics(const QString &artist, const QString &track,
                     int durationSec = 0);
signals:
    void lyricsFound(const QString &plainLyrics, const QString &syncedLyrics);
    void notFound();
    void error(const QString &message);
};
```

### Integration
Added after lrc.red, before Unison in the lyrics cascade.

### Key details
- Endpoint: `GET https://api.lrcmux.dev/get?artist=...&title=...&format=json`
- JSON response → `lines[].text` joined = plain lyrics.
- If `meta.level` is `word` or `line` → build synced lyrics string.
- Rate limit: 60 req/min, cache hits free.
- Send `User-Agent`.

---

## Phase 4: Unison Lyrics Provider

### Files to create/modify
1. **NEW: `src/providers/unison.h`** — Header
2. **NEW: `src/providers/unison.cpp`** — Implementation
3. **MODIFY: `src/metadata/metadata-manager.h`** — Add UnisonProvider member
4. **MODIFY: `src/metadata/metadata-manager.cpp`** — Add to lyrics cascade
5. **MODIFY: `CMakeLists.txt`** — Add new source files

### UnisonProvider API (same interface)
```cpp
class UnisonProvider : public QObject {
    Q_OBJECT
public:
    explicit UnisonProvider(QObject *parent = nullptr);
    void fetchLyrics(const QString &artist, const QString &track,
                     int durationSec = 0);
signals:
    void lyricsFound(const QString &plainLyrics, const QString &syncedLyrics);
    void notFound();
    void error(const QString &message);
};
```

### Integration
Added as the last lyrics fallback before LyricsOvh.

### Key details
- Endpoint: `GET https://unison.betterlyrics.org/lyrics?song=...&artist=...`
- Response: `{ success, data: { lyrics, format, syncType, ... } }`
- TTML → extract text for plain lyrics; keep TTML as synced.
- LRC → use as synced lyrics.
- Plain → use as plain lyrics.
- Store `etag` from response headers; send `If-None-Match` on re-fetch.
- Rate limit: 600 req/min.
- Attribution required when displaying lyrics (store submitter info).

---

## MetadataManager Integration Pattern

For each new lyrics provider, the pattern in MetadataManager is:

```cpp
// In constructor:
connect(newProvider_, &NewProvider::lyricsFound, this, [this](const QString &plain, const QString &synced) {
    QString text = synced.isEmpty() ? plain : synced;
    if (!pendingArtistLyrics_.isEmpty()) {
        cache_->storeLyrics(pendingArtistLyrics_, pendingTitleLyrics_, text);
        emit lyricsFound(pendingArtistLyrics_, pendingTitleLyrics_, text);
        pendingArtistLyrics_.clear();
        pendingTitleLyrics_.clear();
    }
});
connect(newProvider_, &NewProvider::notFound, this, [this]() {
    // Try next provider in cascade...
    if (!pendingArtistLyrics_.isEmpty())
        nextProvider->fetchLyrics(pendingArtistLyrics_, pendingTitleLyrics_);
});
connect(newProvider_, &NewProvider::error, this, [this](const QString &msg) {
    qDebug() << msg;
    // Fall through to next provider on error too
    if (!pendingArtistLyrics_.isEmpty())
        nextProvider->fetchLyrics(pendingArtistLyrics_, pendingTitleLyrics_);
});
```

---

## CMakeLists.txt Additions

```cmake
set(PROVIDER_SOURCES
    ...
    src/providers/apple-music.cpp
    src/providers/apple-music.h
    src/providers/lrc-red.cpp
    src/providers/lrc-red.h
    src/providers/lrcmux.cpp
    src/providers/lrcmux.h
    src/providers/unison.cpp
    src/providers/unison.h
)
```

---

## Testing

- Each provider: unit test with mock HTTP (testInjectReply pattern from LrcLibProvider).
- Integration test: real HTTP request to verify API response format.
- Full cascade test: ensure fallback chain works end-to-end.

---

## Effort Estimate

| Phase | Files | Complexity | Est. time |
|-------|-------|-----------|-----------|
| 1. Apple Music cover | 4 | Low (similar to Deezer) | ~30 min |
| 2. lrc.red lyrics | 4 | Medium (TTML parsing) | ~45 min |
| 3. Lrcmux lyrics | 4 | Low (single endpoint, JSON parse) | ~30 min |
| 4. Unison lyrics | 4 | Medium (ETag, attribution) | ~30 min |
| Integration & testing | 2 | Medium | ~30 min |
| **Total** | **18** | | **~2.5-3 h** |
