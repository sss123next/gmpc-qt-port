# Research: Additional Cover Art & Lyrics Services

Date: 2026-11-24
Author: Qwen Code
License: GPLv2 (same as gmpc-qt)

## Context

gmpc-qt currently has these providers:

### Cover art cascade
1. SQLite cache → local files (cover.jpg etc.) → TagLib embedded tags
2. **MusicBrainz / Cover Art Archive** (free, no key)
3. **Last.FM** (requires API key)
4. **Deezer** (free, no key)
5. **Discogs** (requires API token)

### Lyrics cascade
1. SQLite cache → embedded tags (USLT/LYRICS via TagReader)
2. **LRCLIB** (`lrclib.net`, free, no key) — primary
3. **LyricsOvh** (`api.lyrics.ovh`, free, no key) — fallback

---

## 1. Apple Music / iTunes Search API — Cover Art

**Status:** ✅ Strong candidate, recommended for implementation

### Endpoints
- Search: `GET https://itunes.apple.com/search?term=ARTIST+ALBUM&entity=album&limit=5`
- Lookup by ID: `GET https://itunes.apple.com/lookup?id=CATALOG_ID&entity=album`

### Authentication
- **None.** No API key, no registration.
- Official Apple API, documented since 2008.

### Artwork URL
- Response contains `artworkUrl60` and `artworkUrl100` (JPEG).
- **Size hack:** Replace `100x100bb.jpg` or `60x60bb.jpg` with `1200x1200bb.jpg` (or `3000x3000bb.jpg`, `600x600bb.jpg`) to get higher resolution.
- Apple's CDN serves the closest available resolution — typically up to 3000×3000 for popular albums.

### Rate Limits
- ~20 requests/minute per IP.
- HTTP 403 if exceeded.

### Terms
- Official Apple API. Editorial-licensed artwork. Must be used to promote store content.
- Widely used in FOSS projects (Navidrome plugin, beets, albumart-dl, coverpy, etc.).
- Safe for display in a music client identifying albums.

### Implementation
- New class `AppleMusicCoverProvider` (or integrate into `CoverProvider` cascade).
- Simple HTTP GET → JSON parse → extract `artworkUrl100` → swap size → download.
- Add after MusicBrainz, before Last.FM in cascade.

### References
- https://developer.apple.com/library/archive/documentation/AudioVideo/Conceptual/iTuneSearchAPI/
- https://github.com/matteing/coverpy (Python client)
- https://github.com/navidrome/apple-music-plugin

---

## 2. Spotify (Unofficial) — Cover Art

**Status:** ❌ Not recommended for direct C++ integration

### Approach
- **SpotifyScraper** (Python library, MIT license) reads Spotify's public web player.
- Requires Python 3.10+, httpx dependency.
- Cannot be called from C++/Qt without a Python subprocess.

### Spotify Cover API (`open.voidware.de`)
- Third-party proxy service, unreliable.
- Requires Spotify track/album ID, not artist+name search.

### Conclusion
- Adding a Python dependency and subprocess management is not worth it for a fallback provider.
- If Spotify is desired, the official Spotify Web API requires OAuth/app registration — also heavy.
- **Skip.**

---

## 3. lrc.red — Synchronized Lyrics

**Status:** ✅ Strong candidate, recommended for implementation

### Endpoints
- Search: `GET https://lrc.red/search.json?q=ARTIST+TRACK`
- Track by ISRC: `GET https://lrc.red/s/{ISRC}.json`
- TTML download: `GET https://lrc.red/s/{ISRC}.ttml`
- LRC download: `GET https://lrc.red/s/{ISRC}.lrc`

### Authentication
- **None.** Free for anyone.
- Must send `User-Agent` identifying your app (with link + email). Browser clients use `X-Client-ID`.
- Rate limits may apply if unidentified.

### Lyrics quality
- Word-level (syllable) sync in TTML.
- Line-level sync in LRC.
- Community-maintained Apple Music TTML database.
- Very high quality: word spaces, background vocals, singer alignment preserved.

### Attribution
- **Required.** Must credit lrc.red with link back wherever lyrics are shown.

### Implementation
- New class `LrcRedProvider` (similar to `LrcLibProvider`).
- Search by artist+title to find ISRC → download TTML.
- Duration matching ±2-3 seconds for accuracy.
- Parse TTML or return LRC as plain text.
- Add after LRCLIB, before LyricsOvh in cascade.

### References
- https://lrc.red/api
- https://github.com/rukamori/lyrics (Kotlin client example)
- https://github.com/skippingclass/plasma-lrc (KDE Plasma integration)

---

## 4. Lrcmux — Aggregator Lyrics API

**Status:** ✅ Good candidate as additional fallback

### Endpoint
- `GET https://api.lrcmux.dev/get?artist=...&title=...&format=json`

### Authentication
- **None.** Free, no API key.
- User-Agent recommended but not required.

### Rate Limits
- 60 requests per minute per IP (burst 10).
- Cache hits don't count against limit.

### Lyrics quality
- Aggregates Deezer (word-level), KuGou (word-level), YouTube Music (line-level).
- Returns best available result.
- Supports instrumental detection.
- Multiple output formats: json, lrc, txt, vtt, srt.

### Response format (JSON)
```json
{
  "track": { "artist": "...", "title": "...", "duration": 213 },
  "meta": {
    "level": "word",       // word | line | none
    "instrumental": false,
    "source": { "id": "deezer", "url": "..." }
  },
  "lines": [
    { "start": 12345, "end": 23456, "text": "Never", "words": [
      { "start": 12345, "end": 13000, "text": "Ne-" },
      { "start": 13000, "end": 23456, "text": "ver" }
    ]}
  ]
}
```

### Self-hosting
- MIT license, Docker Compose or Go binary.
- `docker compose up` runs the full stack.
- Can point at a self-hosted instance.

### Attribution
- Not required by the API, but encouraged.

### Implementation
- New class `LrcmuxProvider`.
- Single GET request, parse JSON lines.
- Optional: parse word-level timing for karaoke display.
- Add as final lyrics fallback.

### References
- https://lrcmux.dev/docs
- https://github.com/f1nniboy/lrcmux

---

## 5. Unison — Community-Synced Lyrics Database

**Status:** ✅ Good candidate, but requires attribution display

### Endpoints
- Lookup by song+artist: `GET https://unison.betterlyrics.org/lyrics?song=...&artist=...`
- Lookup by ISRC: `GET https://unison.betterlyrics.org/lyrics?isrc=...`
- Lookup by videoId: `GET https://unison.betterlyrics.org/lyrics?v=...`
- Search: `GET https://unison.betterlyrics.org/lyrics/search?q=...`

### Authentication
- **None** for reading. No key, no sign-in.

### Rate Limits
- 600 requests per minute per IP.

### Lyrics quality
- Community-submitted, voted on.
- TTML (richsync/line-level), LRC, or plain text.
- `confidence` field: low/medium/high based on votes.

### Licensing
- **Free for open-source projects** (OSI-approved license).
- Requires attribution: "Lyrics from Unison" + link + submitter credit.
- Commercial use not allowed.

### ETag caching
- Server sends `ETag`. Send `If-None-Match` to get `304 Not Modified`.
- Reduces bandwidth for frequently-checked tracks.

### Implementation
- New class `UnisonProvider`.
- Lookup by song+artist.
- Parse response, extract lyrics (TTML/LRC/plain).
- Store `etag` for cache revalidation.
- Requires attribution in UI when displaying lyrics.
- Add as final fallback (or after lrc.red, before LyricsOvh).

### References
- https://unison.betterlyrics.org/docs
- https://github.com/better-lyrics/unison

---

## 6. Better Lyrics API

**Status:** ❌ Low priority, redundant with lrc.red

### Why skip
- API keys currently **not issued** to new users.
- Cache misses require API key → unreliable for uncached songs.
- **Primary upstream source is lrc.red** — implementing lrc.red directly gives the same data without the caching layer.
- `/kugou/getLyrics` and `/qq/getLyrics` never need a key, but provide Chinese-market lyrics.
- Good for projects that can accept the caching limitation, but for gmpc-qt, direct lrc.red integration is better.

---

## Implementation Priority Summary

| Priority | Service | Type | Rationale |
|----------|---------|------|-----------|
| P0 | Apple Music / iTunes Search API | Cover art | Free, no key, massive catalog, easy API |
| P1 | lrc.red | Lyrics | Best quality synced lyrics, free, no key |
| P2 | Lrcmux | Lyrics | Good aggregator fallback, word-level sync |
| P3 | Unison | Lyrics | Open-source friendly, community quality, needs attribution |
| Skip | Spotify (unofficial) | Cover art | Python dependency, unreliable |
| Skip | Better Lyrics API | Lyrics | Redundant with lrc.red, API key wall |

---

## Cascade Order After Implementation

### Cover art
1. SQLite cache
2. Local files (cover.jpg, folder.jpg, ...)
3. TagLib embedded tags
4. MusicBrainz / Cover Art Archive
5. **Apple Music / iTunes Search API** ← NEW
6. Last.FM
7. Deezer
8. Discogs

### Lyrics
1. SQLite cache
2. Embedded tags (USLT/LYRICS via TagReader)
3. LRCLIB
4. **lrc.red** ← NEW
5. **Lrcmux** ← NEW
6. **Unison** ← NEW
7. LyricsOvh (final fallback)
