# TVH MoviePilot Plugin Integration APIs

Chinese version: [TVH MoviePilot Plugin Integration APIs (Chinese)](tvh_bot_integration_apis_zh)

This page records the `bata` branch API additions and webhook payload changes
that are useful for the MoviePilot TVH Helper plugin, its Telegram interaction
entry, and other automation clients.

In this document, "bot" means the interaction layer implemented inside the
MoviePilot TVH Helper plugin. It is not a separate standalone bot service.
Telegram commands, buttons, and notifications are handled by the MoviePilot
plugin, while TVH only exposes APIs and webhook events.

All examples are intentionally sanitized. Replace `<tvh-host>`, `<user>`,
`<password>`, `<token>`, and `<moviepilot-webhook-url>` with deployment-specific
values. Do not commit real credentials, public hostnames, private IP addresses,
or callback secrets to this document.

## Scope

The APIs below are grouped by how the MoviePilot plugin is expected to use them:

* dashboard status
* webhook target management and tests
* playback, DVR, DVB, and service notifications
* DVR task management
* EPG lookup for interactive recording

Some endpoints are original Tvheadend APIs. They are listed only when the recent
`bata` work made them more useful for MoviePilot plugin workflows or when the
plugin should use them together with the new webhook features.

## Server Status and Health

### `GET /api/serverinfo`

Recent `bata` additions make `serverinfo` suitable for a bot home screen.

Important fields:

| Field | Use in bot |
| ----- | ---------- |
| `sw_version` | Show Tvheadend version. |
| `start_time` | Show service start time. |
| `current_time` | Calculate server-local status timestamps. |
| `uptime` | Show runtime without parsing logs. |
| `system.cpu_percent` | Show CPU usage. First request may not include it because it is calculated from two samples. |
| `system.memory_total` | Show total memory. |
| `system.memory_available` | Show available memory. |
| `system.memory_used` | Show used memory. |
| `system.memory_used_percent` | Show memory usage percentage. |
| `network.rx_bytes` / `network.tx_bytes` | Show cumulative network counters. |
| `network.rx_bps` / `network.tx_bps` | Show network speed. First request may not include it because it is calculated from two samples. |
| `recording_storage.path` | Recording storage path used by the container or host. |
| `recording_storage.total` | Total recording storage size. |
| `recording_storage.available` | Available bytes for recording. |
| `recording_storage.used_percent` | Used recording storage percentage. |

Bot usage:

* `/tvh` home summary.
* Health check before creating a DVR task.
* Storage warning before recording.
* Restart recovery check after service restart.

Sanitized example:

```text
GET http://<tvh-host>/api/serverinfo
```

### `POST /api/server/restart`

Recent `bata` addition for bot-driven restart.

Access: admin.

Response:

```json
{"restart":"requested"}
```

Bot usage:

* Add a confirmation step before calling this endpoint.
* After calling it, poll `GET /api/serverinfo` until it responds again and
  `start_time` changes.

## Status APIs for Active Playback and Download

### `GET /api/status/subscriptions`

Original Tvheadend endpoint. Recent webhook work filters DVR-internal
subscriptions separately from real user playback, and the bot should apply the
same idea when displaying active users.

Bot usage:

* Current live playback list.
* Active DVR recording stream detection.
* Bitrate, client, service, and error display.

Useful fields vary by subscription type, but commonly include:

* subscription id
* username
* client
* service
* channel or title
* input/output bitrate
* error counters

### `GET /api/status/connections`

Original Tvheadend endpoint. Useful with subscriptions to detect file downloads
or HTTP-only connections.

Bot usage:

* Current file download list.
* Distinguish live playback from recorded-file download.
* Close HTTP connections when needed.

### `POST /api/connections/cancel`

Original endpoint exposed in the status API module.

Access: admin.

Request examples:

```text
POST /api/connections/cancel
id=<connection-id>
```

```text
POST /api/connections/cancel
id=all
```

Bot usage:

* Close one online user.
* Emergency close all connections.

## Webhook Target Management

### `GET /api/webhook/targets/grid`

Recent `bata` addition. Returns `webhook_targets` as WebUI/bot-editable rows.

Access: admin.

Bot usage:

* Show configured webhook targets.
* Confirm whether MoviePilot playback, DVR, and error targets are enabled.
* Build a "Webhook health" screen.

### `POST /api/webhook/targets/save`

Recent `bata` addition. Saves webhook target rows back to
`config.webhook_targets`.

Access: admin.

Target fields:

| Field | Purpose |
| ----- | ------- |
| `name` | Human-readable target name. |
| `enabled` | Enable or disable the target. |
| `url` | Receiver URL. Use a sanitized placeholder in docs. |
| `token` | Sent as `X-Tvh-Token`. Do not log or display in plain text. |
| `hmac_secret` | Optional HMAC signing secret. Do not log or display in plain text. |
| `events` | Event filter list, for example `playback.*` or `dvr.*`. |
| `ssl_verify` | Whether to verify HTTPS certificate. |
| `timeout` | HTTP timeout in seconds. |
| `retry_count` | Retry count for transient delivery failure. |
| `retry_interval` | Retry interval in seconds. |
| `template` | Optional template selector or label for receivers. |

Sanitized target example:

```json
[
  {
    "name": "moviepilot-playback",
    "enabled": true,
    "url": "<moviepilot-webhook-url>",
    "token": "<shared-secret>",
    "hmac_secret": "<hmac-secret>",
    "events": ["system.webhooktest", "playback.*"],
    "ssl_verify": true,
    "timeout": 10,
    "retry_count": 2,
    "retry_interval": 3
  },
  {
    "name": "moviepilot-dvr",
    "enabled": true,
    "url": "<moviepilot-webhook-url>",
    "token": "<shared-secret>",
    "events": ["dvr.*", "service.error", "dvb.error"],
    "ssl_verify": true,
    "timeout": 10,
    "retry_count": 2,
    "retry_interval": 3
  }
]
```

Bot usage:

* Enable or disable one MoviePilot target.
* Split playback and DVR/error events into separate targets.
* Test target config without SSH or manual JSON editing.

### `GET /api/webhook/test`

Recent `bata` addition. Queues a `system.webhooktest` event.

Access: admin.

Success response:

```json
{"success":true,"message":"Webhook test queued"}
```

Bot usage:

* "Test Webhook" button.
* Verify MoviePilot receiver after changing URL, token, or HMAC secret.
* Do not deduplicate this event; repeated tests are expected.

## Webhook Events and Payloads

Recent `bata` work adds native TVH webhook delivery. It runs asynchronously, so
playback and DVR code paths are not blocked by receiver latency.

Supported event names:

| Event | Trigger | Bot action |
| ----- | ------- | ---------- |
| `system.webhooktest` | Manual webhook test. | Show test result. |
| `playback.start` | User playback starts. | Send start playback notification. |
| `playback.stop` | User playback stops. | Send stop playback notification with duration if receiver can calculate it. |
| `dvr.start` | DVR recording starts. | Send recording start notification. |
| `dvr.complete` | DVR recording completes successfully or completes with status fields. | Send recording completed notification and check size/duration in bot. |
| `dvr.error` | DVR recording fails or has error state. | Send recording failure notification and offer retry/re-record actions. |
| `service.error` | Service enters an error streaming status. | Send service fault alert. |
| `dvb.error` | DVB input or mux enters an error streaming status. | Send tuner/mux fault alert. |

Common webhook fields:

| Field | Meaning |
| ----- | ------- |
| `source` | Fixed source marker, currently `tvheadend`. |
| `event` | Event name. |
| `event_id` | Event identifier for receiver-side deduplication. |
| `timestamp` | Event creation time. |
| `server.name` | Tvheadend server name. |
| `server.version` | Tvheadend version that generated the event. |

Playback payload additions:

| Field | Meaning |
| ----- | ------- |
| `subscription_id` | TVH subscription id. |
| `started` | Subscription start time. |
| `user` | TVH username. |
| `ip` | Client host/IP as seen by TVH. |
| `client` | Client user agent or app name. |
| `title` | Subscription title. |
| `channel` | Channel name when available. |
| `service` | Adapter/service display name. |
| `errors` | Subscription error count. |
| `input_kbps` / `output_kbps` | Input and output bitrate. |
| `channel_uuid` | Channel UUID. |
| `channel_icon` | Channel icon URL after TVH image handling. |
| `program_event_id` | Current EPG event id. |
| `program_start` / `program_stop` | Current programme time range. |
| `program_title` | Current programme title. |
| `program_subtitle` | Current programme subtitle. |
| `program_summary` | Current programme summary. |
| `program_description` | Current programme description. |
| `program_image` | Current programme image URL when available. |

Notes for bot receivers:

* Prefer `channel_icon` as notification image.
* Use `program_image` as fallback.
* Prefer Chinese EPG fields when TVH provides multilingual text.
* Do not rely on temporary connection ids to identify stable playback; combine
  user, channel/title, client, and source where needed.

DVR payload additions:

| Field | Meaning |
| ----- | ------- |
| `dvr_uuid` | DVR entry UUID. |
| `sched_state` | Schedule state. |
| `recording_state` | Recording state. |
| `errors` | DVR error count. |
| `data_errors` | Broadcast/data error counter. |
| `last_error` | Last streaming error code. |
| `last_error_text` | Last streaming error text. |
| `start` / `stop` | Scheduled time range. |
| `title` | Recording title. |
| `subtitle` | Recording subtitle. |
| `channel` | Channel name. |
| `filename` | Recorded file path when known. |
| `user` | DVR owner. |

Bot receiver recommendations:

* Treat small `data_errors` as signal quality information, not automatic
  failure.
* Use `recording_state`, `sched_state`, `last_error`, and file size/duration
  checks together before marking an item "needs re-record".
* Do not expose full filesystem paths to public chats unless the deployment
  explicitly allows it.

Error payload additions:

| Event | Important fields |
| ----- | ---------------- |
| `service.error` | `service_uuid`, `service`, `adapter`, `status_flags`, `status_text` |
| `dvb.error` | `input_uuid`, `mux_uuid`, `input`, `mux`, `status_flags`, `status_text` |

## DVR APIs for Bot Workflows

These endpoints are not all new, but the recent bot work depends on them.

| Endpoint | Use |
| -------- | --- |
| `GET /api/dvr/entry/grid` | All DVR entries. |
| `GET /api/dvr/entry/grid_upcoming` | Scheduled/upcoming items. |
| `GET /api/dvr/entry/grid_finished` | Finished recordings. |
| `GET /api/dvr/entry/grid_failed` | Failed recordings. |
| `POST /api/dvr/entry/create_by_event` | Create DVR entry from EPG event id. |
| `POST /api/dvr/entry/create` | Create DVR entry manually. |
| `POST /api/dvr/entry/stop` | Gracefully stop active recording. |
| `POST /api/dvr/entry/cancel` | Cancel scheduled or active recording. |
| `POST /api/dvr/entry/remove` | Remove DVR entry and recorded file. `bata` includes a fix so entries without config do not crash during removal. |
| `POST /api/dvr/entry/rerecord/allow` | Allow failed/completed item to be recorded again. |
| `POST /api/dvr/entry/rerecord/deny` | Deny re-record. |
| `POST /api/dvr/entry/rerecord/toggle` | Toggle re-record flag. |

Bot usage:

* "Record this programme" from EPG.
* "Record X minutes early / stop X minutes late" by passing entry-level
  `start_extra` and `stop_extra` when creating a task.
* "Upcoming / recording / finished / failed" task views.
* "Cancel", "Stop", "Delete file", and "Allow re-record" buttons.

Recommended DVR creation behavior:

* Keep the dedicated DVR profile pre/post padding at `0`.
* Let the bot pass `start_extra` and `stop_extra` per entry.
* Keep tuner warm-up messaging in the bot UI if the deployment pre-tunes before
  recording.

## EPG APIs for Interactive Recording

These endpoints are original Tvheadend APIs, but they are central to bot
interaction:

| Endpoint | Use |
| -------- | --- |
| `GET /api/epg/events/grid` | List current/upcoming programmes. |
| `GET /api/epg/events/load` | Load one programme detail. |
| `GET /api/epg/events/alternative` | Find alternative airings. |
| `GET /api/epg/events/related` | Find related programmes. |
| `GET /api/epg/content_type/list` | Programme category list. |

Bot usage:

* Channel -> programme list -> confirm recording.
* Find replay after DVR failure.
* Show programme time, duration, progress, summary, description, channel icon,
  and programme image.

## Security and Logging Rules

Bot integrations should follow these rules:

* Never print real `token`, `hmac_secret`, API keys, passwords, or callback URLs
  in logs, screenshots, or committed documentation.
* Prefer placeholders such as `<moviepilot-webhook-url>` and `<shared-secret>`.
* Do not hard-code deployment hostnames or private IP addresses in source.
* Treat webhook body as operational data; avoid forwarding full file paths to
  public chat groups unless intentionally enabled.
* Use bot confirmation pages for destructive actions:
  `server/restart`, `connections/cancel`, `dvr/entry/cancel`,
  `dvr/entry/stop`, and `dvr/entry/remove`.

## Quick MoviePilot Plugin Feature Map

| Plugin feature | Primary TVH API |
| -------------- | --------------- |
| `/tvh` dashboard | `serverinfo`, `status/inputs`, `status/subscriptions`, `status/connections` |
| Webhook health check | `webhook/test`, `webhook/targets/grid` |
| Configure MoviePilot target | `webhook/targets/grid`, `webhook/targets/save` |
| Current playback | `status/subscriptions`, `status/connections` |
| Close online connection | `connections/cancel` |
| Channel programme list | `epg/events/grid` |
| Programme detail | `epg/events/load` |
| Schedule recording | `dvr/entry/create_by_event` or `dvr/entry/create` |
| Upcoming recordings | `dvr/entry/grid_upcoming` |
| Finished recordings | `dvr/entry/grid_finished` |
| Failed recordings | `dvr/entry/grid_failed` |
| Stop active recording | `dvr/entry/stop` |
| Cancel scheduled recording | `dvr/entry/cancel` |
| Delete recording file | `dvr/entry/remove` |
| Retry/re-record | `dvr/entry/rerecord/allow` |
