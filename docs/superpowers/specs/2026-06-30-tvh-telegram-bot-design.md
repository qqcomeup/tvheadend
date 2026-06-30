# TVH Telegram Bot Design

## Goal

Build a first-stage Telegram bot for TVHeadend administration on the existing VPS deployment. The bot is an independent service and does not change the TVH C application.

## Scope

The first stage is admin-only. Only the configured Telegram administrator can use commands or receive alerts.

Included features:

- Show TVH service status.
- Show DVB device status and notify when device count drops or recovers.
- List TVH users.
- Show a selected user's token, short M3U URL, and short EPG URL.
- Deploy on the VPS next to the existing TVH container.

Excluded from the first stage:

- Ordinary Telegram user self-service.
- Telegram-to-TVH account binding.
- Editing TVH permissions from Telegram.
- Writing the Telegram bot token into Git.

## Architecture

Create a separate `tvh-bot/` Python service. It talks to TVH through HTTP APIs and checks local DVB device paths from the host/container environment.

The bot uses long polling through aiogram. Configuration is read from environment variables or a `.env` file on the VPS.

```text
Telegram
  -> aiogram bot
  -> TVH HTTP API and local DVB checks
  -> Tvheadend
```

## Configuration

Runtime configuration:

```env
BOT_TOKEN=telegram-bot-token
ADMIN_CHAT_IDS=6907590840
TVH_URL=http://127.0.0.1:9981
TVH_USER=ck
TVH_PASS=ck10028
PUBLIC_BASE_URL=https://m3u.066671.xyz
EXPECTED_DVB_COUNT=1
CHECK_INTERVAL_SECONDS=60
```

`BOT_TOKEN` must only exist in VPS runtime configuration. Repository files must use examples or templates without real secrets.

## Bot Commands

`/start` and `/help` show the available admin actions.

`/status` returns:

- TVH HTTP reachability.
- TVH version when available.
- DVB device count.

`/dvb` returns current DVB adapter paths and whether the count is below `EXPECTED_DVB_COUNT`.

`/users` lists TVH users as inline buttons. Selecting a user shows:

- username
- token when available
- M3U short URL: `${PUBLIC_BASE_URL}/m3u?a=${token}`
- EPG short URL: `${PUBLIC_BASE_URL}/epg?a=${token}`

## DVB Notification Behavior

The monitor keeps the last known healthy/unhealthy state in memory.

- When the current DVB count is lower than `EXPECTED_DVB_COUNT`, send one warning notification.
- Do not repeat the same warning every interval.
- When the count recovers, send one recovery notification.

## Error Handling

Unauthorized Telegram users receive a short rejection message and no TVH data.

TVH API failures should return readable messages to the admin instead of crashing the bot.

Missing or invalid configuration should fail fast at startup with a clear error.

## Testing

Use pytest for unit tests:

- configuration parsing
- admin authorization
- URL formatting
- DVB state transitions
- TVH API response parsing

Manual VPS verification:

- bot responds to the configured admin
- unauthorized chat ID is denied
- `/status`, `/dvb`, `/users` work
- selected user shows short M3U and EPG URLs
- simulated DVB count drop sends one alert and recovery sends one alert

