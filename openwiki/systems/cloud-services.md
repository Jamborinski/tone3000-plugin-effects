---
type: "Reference"
title: "TONE3000 Cloud and session services"
openwiki_generated: true
verified:
  - by: openwiki/0.7.2
    at: 2026-10-10T18:58:45.842Z
sources:
  - id: openwiki-source-5f5b95b3d6a215fa02ceb945
    resource: repo://.env.example
  - id: openwiki-source-e592877ce1adba0c2539910f
    resource: repo://plugin/ui/NativeUi.cmake
  - id: openwiki-source-77483613fe356991dbe4357c
    resource: repo://plugin/ui/services/ConnectionGate.h
  - id: openwiki-source-7a30b26a8a324d642707cf0c
    resource: repo://plugin/ui/services/Tone3000Client.h
  - id: openwiki-source-b428949b6827fd7bd188b717
    resource: repo://plugin/ui/services/Tone3000Session.h
  - id: openwiki-source-a8e64dd05069a97ffce9e334
    resource: repo://plugin/ui/services/UpdateCheck.h
generated: { by: "pi", at: "2026-10-10T18:58:45.842Z" }
---


# TONE3000 Cloud and session services

The UI talks to the TONE3000 cloud (tone3000.com) for tone browsing, model
downloads, favorites, and an optional update check. Everything lives in
`plugin/ui/services/` under namespace `t3k::ui`, all callbacks land on the
message thread, and nothing network-related ever blocks the audio thread.

## Build-time config injection (`.env` → `T3kConfig.h`)

`plugin/ui/NativeUi.cmake` reads three variables at **configure time** — from
`.env` / `.env.local` at the repo root, overridable by configure-environment
variables:

| Variable | Default | Purpose |
|---|---|---|
| `T3K_PUBLISHABLE_KEY` | *(required)* | The OAuth `client_id` (looks like `t3k_pub_…`; tone3000.com → Settings → API Keys). Without it, sign-in surfaces an error. |
| `T3K_API_DOMAIN` | `https://www.tone3000.com` | API origin (no trailing slash — the cmake strips one). Set to point at staging or a local server. |
| `T3K_UPDATE_NOTICE` | `false` | `"true"` enables the startup version check (`/api/v1/plugin/version`). Off for forks. |

They are baked into the generated `T3kConfig.h`
(`configure_file(…/core/T3kConfig.h.in → build/t3k_ui/T3kConfig.h)`), and
`Tone3000Session::Config::fromBuild()` exposes that pair to the session
layer. A changed `.env` therefore requires a **reconfigure**
(`docs/agents/windows-build.md` fast path, or the cmake reconfigure), not a
plain build.

## OAuth token store (`Tone3000Client.h`)

`Tone3000Client` is the port of the web app's `T3KClient`. Key properties:

- **Tokens persist in `UiPrefs`** (JSON on disk), not `localStorage` — a
  sign-in from a webview build does not carry over. A signed-in user stays
  signed in across editor sessions.
- **Proactive refresh:** an access token is refreshed automatically when it
  is within **60 s of expiry** (`kRefreshLeadMs = 60_000`); concurrent
  callers share one in-flight refresh (`refreshWaiters_`).
- **One retry after a stray 401:** `fetch` (a Bearer request) retries once
  when it gets a 401 — that is the expiry-check race (expired between the
  `fresh()` check and the request). A second 401 gives up and fires
  `onAuthRequired`.
- **Rotation-aware:** the prefs file is shared with every other host
  running the plugin (multiple DAWs), and a refresh rotates the token pair.
  So *before* refreshing — and before giving up on a rejected refresh —
  the store is **re-read**: a pair another host rotated meanwhile is
  adopted rather than fought.
- **Error shape** mirrors the web client: codes and messages like
  `"token_refresh_failed"` or `"getTone failed: 404"`
  (non-2xx → `"<label> failed: <status>"`).
- The clock is injectable (`std::function<juce::int64()> now = [] { return
  juce::Time::currentTimeMillis(); }`), so tests can substitute it.

**Typed endpoints** exposed: `getUser`, `getTone(int, Reply)`,
`setFavorite`, `listTones(path, Reply)` (ready-made `ToneQuery::requestPath`
→ `PaginatedResponse`), `listTrending(gear, Reply)` (the homepage top-10
`/tones/trending` feed; no session required — the signed-out preview),
`listTaxonomy(kind, query, pageSize, Reply)` (`/tags`, `/makes`, or `/users`),
`listModels(toneId, pageSize, architecture, Reply)` (architecture < 0 omits
it), and `fetchPluginVersion(deviceId, localVersion, Reply)` (the
`/plugin/version` check with `X-Device-Id` and `X-Plugin-Version` headers,
each omitted when empty).

## Login flows (`Tone3000Session.h`)

`Tone3000Session` (the port of `useToneSession.ts` + `useT3kSelect.ts`)
adds three competing sign-in paths, all feeding the same token endpoints —
**first to finish wins, the others are dropped**:

1. **Loopback redirect** (RFC 8252): the system browser sends the user to
   `http://localhost:<port>/`, which is auto-allowed for publishable keys
   (no client registration). JUCE's `launch` reports success once the
   launcher forks — not when the browser actually opens — so the sign-in
   screen **always also shows the authorize URL** to copy and paste.
2. **Copy-URL:** the same URL, manually navigated in any browser.
3. **Device flow (RFC 8628):** `requestDeviceAuthorization` → a
   `deviceCode` (never shown) + `userCode` ("BCDF-GHJK") +
   `verificationUri`; `pollDeviceToken(deviceCode, Reply)` polls the token
   endpoint with `intervalS` cadence (bumped by 5 s on `"slow_down"`).
   Poll failures are server codes: `"authorization_pending"` (retry),
   `"expired_token"`, `"access_denied"`, or a transport error
   (`"token_refresh_failed"`).

`Tone3000Session::kProbeTimeoutMs = 8000` (the port of
`useConnectionGate.ts`'s `PROBE_TIMEOUT_MS`) — background connection probes
time out at **8 s** and never alarm on timeout alone (blips are
inconclusive). `probeSecureConnection` is what `ConnectionGate` calls to
decide offline vs insecure (see below). `selectTone` (a.k.a. `load`) calls
`fetchToneAndModels` (tone + first loadable model) and hands the tone to
the backend for off-thread loading; the browser→session→client→cloud→load
trace is `plugin/ui/views/browser/*` → `Tone3000Session.searchTones / getTone /
listTrending` → `Tone3000Client` (Bearer, transparent refresh) → `HttpClient`
→ cloud JSON → backend.

## Connection gate (`ConnectionGate.h`)

The *first* line of defence for network-dependent actions (`add` / `swap` /
`login`); the port of `useConnectionGate.ts`. Key constants:

- **`kProbeTtlMs = 30_000`** — minimum gap between background probes (the
  user can force a fresh one via "Try again").
- **`kConfirmDelayMs = 2000`** — one transient failure (DAW-startup
  contention, Wi-Fi renegotiating) must not raise the insecure modal; the
  confirmation probe waits this long before rechecking.

Two `Problem` values drive the UI:

- **`offline`** — no OS network interface up: the offline modal opens and
  the action is **queued** for "Try again" (instant, no probe needed).
- **`insecure`** — diagnostic, not a gate: appears only after **two
  consecutive network-layer failures** while the OS still reports a
  connection (usually a wrong system clock). Timeouts are *inconclusive* and
  never alarm; a later successful probe (or retry) closes the modal.

`requireConnection(action)` never blocks: with no network interface up it
queues; otherwise it runs the action **now** and probes in the background
on the side — **nothing ever waits on a probe**.

## Update check (`UpdateCheck.h`)

Optional and **off by default** (forks skip it): when the build set
`T3K_UPDATE_NOTICE=true`, a startup check calls
`fetchPluginVersion` (a single, one-time GET to
`${T3K_API_DOMAIN}/api/v1/plugin/version`) and raises an update notice if
the server's version is newer than the running build (`localVersion`).
The local version is the build's git tag / commit (whatever
`T3kConfig.h` carries); when empty (a fork), the check is skipped.

## Offline degradation

`LocalFiles.h` (same directory) is the offline complement: it lets the
user open `.nam` / `.wav` files from disk without a network connection
(`TONE3000Processor::loadLocalTonePath` / `loadLocalToneUrls`).
`ConnectionGate` keeps these actions visible (they don't `requireConnection`),
while everything that needs the cloud (browser, tone download, favorites,
device-flow sign-in) is queued or blocked behind the offline modal.
