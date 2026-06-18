# Readeck integration

Fork feature: browse and download [Readeck](https://readeck.org) saved articles on-device.

## Setup

Drop two plaintext JSON files on the SD card root (examples in `docs/`):

- `/readeck.json` — `{ "url": "<instance>", "token": "<api-token>", "label": "" }`
  - `url`: instance root, **without** `/api` (the client appends it). Trailing slash OK.
  - `token`: long-lived API token from Readeck's *Profile → API tokens*.
  - `label` (optional): when set, the device shows and bulk-downloads only articles
    carrying that label (curate on the web UI, pull on-device). Empty ⇒ unread articles.
- `/wifi.json` — `{ "ssid": "...", "password": "..." }` (or an array). Seeds the WiFi
  credential store so the device auto-connects without on-device keyboard entry.

## Usage

Home → **Readeck** connects WiFi and lists the configured set (label or unread, newest first).

- **Tap OK** on an article → downloads its EPUB to `/readeck/` and opens it in the reader.
- **Hold OK** → bulk-downloads the whole list to `/readeck/` in one WiFi session, then returns
  home. Read offline anytime via the File Browser (`/readeck/`, sorted by date-prefixed name).

Downloaded articles are ordinary EPUBs, so the Readeck screen is the online step and reading is
just the normal offline library. Re-syncing skips files already present.

## Read/unread sync — under consideration (deferred)

Today the integration is **read-only**: it never writes back to Readeck, so an article stays
unread/labeled on the server after you read it on-device. For the label workflow this is fine
(you curate the set in the web UI). For the unread workflow it means the same articles keep
re-listing.

Two shapes were considered:

1. **Mark read when actually read — not worth it.** The device reads via the generic EPUB reader,
   which has no Readeck awareness, and reading happens **offline**. Doing it properly would need:
   a file→bookmark-id manifest, hooks into the shared reader to detect open/finish, a persisted
   "to mark read" queue flushed on the next sync, plus write-capable HTTP and a `bookmarks:write`
   token. Too much surface (especially edits to the core reader) for the payoff.

2. **Mark read / remove label at *download* time — cheap, opt-in.** During a sync we're already
   online and hold the bookmark ids, so a `PATCH /api/bookmarks/{id}` (e.g. `is_archived: true`,
   `read_progress: 100`, or `remove_labels: ["ereader"]`) right after each successful download is
   easy — no manifest, no reader hooks, no offline queue. Semantics: "pulling it to the device
   takes it out of my inbox." Gate behind a config flag (e.g. `"archiveOnDownload": true`).
   Worth it mainly for the unread workflow; mostly redundant under the label workflow.

**Status:** deferred. If revisited, implement only option 2, behind a flag, requiring a
`bookmarks:write` token.
