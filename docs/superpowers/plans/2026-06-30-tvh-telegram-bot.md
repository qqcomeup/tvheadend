# TVH Telegram Bot Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and deploy an admin-only Telegram bot that reports TVH status, DVB status, user M3U/EPG URLs, and DVB drop/recovery alerts.

**Architecture:** Add an independent Python service under `tvh-bot/`. The service reads runtime configuration from environment variables, calls TVH over HTTP, checks DVB device paths locally, and uses aiogram long polling for Telegram interaction.

**Tech Stack:** Python 3.12, aiogram 3.x, aiohttp, pydantic-settings, pytest, Docker.

---

## File Structure

- Create `tvh-bot/pyproject.toml`: Python package metadata, dependencies, pytest configuration.
- Create `tvh-bot/Dockerfile`: container image for VPS deployment.
- Create `tvh-bot/.env.example`: safe example configuration without secrets.
- Create `tvh-bot/tvh_bot/__init__.py`: package marker.
- Create `tvh-bot/tvh_bot/config.py`: settings parsing and validation.
- Create `tvh-bot/tvh_bot/security.py`: Telegram admin authorization helper.
- Create `tvh-bot/tvh_bot/urls.py`: public M3U/EPG URL formatting.
- Create `tvh-bot/tvh_bot/dvb.py`: DVB adapter scanning and state transition detection.
- Create `tvh-bot/tvh_bot/tvh_client.py`: TVH HTTP API client and user parsing.
- Create `tvh-bot/tvh_bot/bot.py`: aiogram command handlers and inline callbacks.
- Create `tvh-bot/tvh_bot/main.py`: application entry point and monitor loop.
- Create `tvh-bot/tests/`: pytest suite for focused units.

### Task 1: Project Skeleton and Configuration

**Files:**
- Create: `tvh-bot/pyproject.toml`
- Create: `tvh-bot/.env.example`
- Create: `tvh-bot/tvh_bot/__init__.py`
- Create: `tvh-bot/tvh_bot/config.py`
- Test: `tvh-bot/tests/test_config.py`

- [ ] **Step 1: Write the failing configuration tests**

```python
import pytest
from pydantic import ValidationError

from tvh_bot.config import Settings


def test_settings_parses_admin_chat_ids_from_comma_list():
    settings = Settings(
        bot_token="token",
        admin_chat_ids="6907590840,123",
        tvh_url="http://127.0.0.1:9981",
        tvh_user="ck",
        tvh_pass="secret",
        public_base_url="https://m3u.066671.xyz",
    )

    assert settings.admin_ids == {6907590840, 123}


def test_public_base_url_strips_trailing_slash():
    settings = Settings(
        bot_token="token",
        admin_chat_ids="6907590840",
        tvh_url="http://127.0.0.1:9981/",
        tvh_user="ck",
        tvh_pass="secret",
        public_base_url="https://m3u.066671.xyz/",
    )

    assert settings.tvh_url == "http://127.0.0.1:9981"
    assert settings.public_base_url == "https://m3u.066671.xyz"


def test_expected_dvb_count_must_be_non_negative():
    with pytest.raises(ValidationError):
        Settings(
            bot_token="token",
            admin_chat_ids="6907590840",
            tvh_url="http://127.0.0.1:9981",
            tvh_user="ck",
            tvh_pass="secret",
            public_base_url="https://m3u.066671.xyz",
            expected_dvb_count=-1,
        )
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tvh-bot; python -m pytest tests/test_config.py -v`

Expected: FAIL because `tvh_bot.config` does not exist.

- [ ] **Step 3: Implement project files and settings**

Create `pyproject.toml`, `.env.example`, package marker, and `config.py` with `Settings` using `pydantic-settings`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cd tvh-bot; python -m pytest tests/test_config.py -v`

Expected: 3 passed.

### Task 2: Security and URL Helpers

**Files:**
- Create: `tvh-bot/tvh_bot/security.py`
- Create: `tvh-bot/tvh_bot/urls.py`
- Test: `tvh-bot/tests/test_security_urls.py`

- [ ] **Step 1: Write failing tests**

```python
from tvh_bot.security import is_admin
from tvh_bot.urls import build_epg_url, build_m3u_url


def test_is_admin_matches_integer_chat_id():
    assert is_admin(6907590840, {6907590840})
    assert not is_admin(1, {6907590840})


def test_short_urls_are_built_from_public_base():
    assert build_m3u_url("https://m3u.066671.xyz", "user-pass_123") == (
        "https://m3u.066671.xyz/m3u?a=user-pass_123"
    )
    assert build_epg_url("https://m3u.066671.xyz/", "user-pass_123") == (
        "https://m3u.066671.xyz/epg?a=user-pass_123"
    )
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tvh-bot; python -m pytest tests/test_security_urls.py -v`

Expected: FAIL because helper modules do not exist.

- [ ] **Step 3: Implement helpers**

Implement exact helper functions with URL encoding through `urllib.parse.urlencode`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cd tvh-bot; python -m pytest tests/test_security_urls.py -v`

Expected: 2 passed.

### Task 3: DVB Scanner and Alert State

**Files:**
- Create: `tvh-bot/tvh_bot/dvb.py`
- Test: `tvh-bot/tests/test_dvb.py`

- [ ] **Step 1: Write failing tests**

```python
from tvh_bot.dvb import DvbMonitor, scan_dvb_adapters


def test_scan_dvb_adapters_lists_adapter_directories(tmp_path):
    (tmp_path / "adapter0").mkdir()
    (tmp_path / "adapter1").mkdir()
    (tmp_path / "not_adapter").mkdir()

    assert scan_dvb_adapters(tmp_path) == ["adapter0", "adapter1"]


def test_monitor_sends_drop_once_then_recovery():
    monitor = DvbMonitor(expected_count=2)

    assert monitor.evaluate(["adapter0", "adapter1"]) is None
    assert monitor.evaluate(["adapter0"]) == "drop"
    assert monitor.evaluate(["adapter0"]) is None
    assert monitor.evaluate(["adapter0", "adapter1"]) == "recover"
    assert monitor.evaluate(["adapter0", "adapter1"]) is None
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tvh-bot; python -m pytest tests/test_dvb.py -v`

Expected: FAIL because `tvh_bot.dvb` does not exist.

- [ ] **Step 3: Implement scanner and monitor**

Implement `scan_dvb_adapters(path=Path("/dev/dvb"))` and `DvbMonitor.evaluate(adapters)`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cd tvh-bot; python -m pytest tests/test_dvb.py -v`

Expected: 2 passed.

### Task 4: TVH Client

**Files:**
- Create: `tvh-bot/tvh_bot/tvh_client.py`
- Test: `tvh-bot/tests/test_tvh_client.py`

- [ ] **Step 1: Write failing tests**

```python
from tvh_bot.tvh_client import parse_users, token_for_user


def test_parse_users_reads_entries_from_tvh_grid_response():
    payload = {
        "entries": [
            {"username": "test", "authcode": "test-test_123456"},
            {"username": "ck", "authcode": ""},
        ]
    }

    users = parse_users(payload)

    assert users[0].username == "test"
    assert users[0].token == "test-test_123456"
    assert users[1].username == "ck"
    assert users[1].token is None


def test_token_for_user_returns_matching_token():
    users = parse_users({"entries": [{"username": "test", "authcode": "abc12345"}]})

    assert token_for_user(users, "test") == "abc12345"
    assert token_for_user(users, "missing") is None
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tvh-bot; python -m pytest tests/test_tvh_client.py -v`

Expected: FAIL because `tvh_bot.tvh_client` does not exist.

- [ ] **Step 3: Implement client model and parsing**

Implement `TvhUser`, `parse_users`, `token_for_user`, and an async `TvhClient` with `get_status()` and `get_users()` methods.

- [ ] **Step 4: Run test to verify it passes**

Run: `cd tvh-bot; python -m pytest tests/test_tvh_client.py -v`

Expected: 2 passed.

### Task 5: Bot Handlers and Entrypoint

**Files:**
- Create: `tvh-bot/tvh_bot/bot.py`
- Create: `tvh-bot/tvh_bot/main.py`
- Test: `tvh-bot/tests/test_bot_messages.py`

- [ ] **Step 1: Write failing message formatting tests**

```python
from tvh_bot.bot import format_status_message, format_user_message
from tvh_bot.tvh_client import TvhUser


def test_format_status_message_contains_service_and_dvb_info():
    message = format_status_message(True, "4.3-test", ["adapter0"], 1)

    assert "TVH: OK" in message
    assert "版本: 4.3-test" in message
    assert "DVB: 1/1" in message


def test_format_user_message_contains_short_urls():
    user = TvhUser(username="test", token="test-test_123456")

    message = format_user_message("https://m3u.066671.xyz", user)

    assert "用户: test" in message
    assert "https://m3u.066671.xyz/m3u?a=test-test_123456" in message
    assert "https://m3u.066671.xyz/epg?a=test-test_123456" in message
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tvh-bot; python -m pytest tests/test_bot_messages.py -v`

Expected: FAIL because `tvh_bot.bot` does not exist.

- [ ] **Step 3: Implement formatting, routers, and main loop**

Implement command handlers for `/start`, `/help`, `/status`, `/dvb`, `/users`, user callbacks, and the background DVB monitor.

- [ ] **Step 4: Run test to verify it passes**

Run: `cd tvh-bot; python -m pytest tests/test_bot_messages.py -v`

Expected: 2 passed.

### Task 6: Container and VPS Deployment

**Files:**
- Create: `tvh-bot/Dockerfile`
- Modify: `README.md`

- [ ] **Step 1: Run full bot test suite**

Run: `cd tvh-bot; python -m pytest -v`

Expected: all tests passed.

- [ ] **Step 2: Build Docker image locally**

Run: `docker build -t tvh-bot:local tvh-bot`

Expected: image builds successfully.

- [ ] **Step 3: Add README documentation**

Document first-stage bot scope, required env vars, Docker run example, and security note that the real Telegram token must not be committed.

- [ ] **Step 4: Deploy to VPS**

Create `/home/ck/app/tvh-bot/.env` on the VPS with runtime secrets, then run:

```bash
docker rm -f tvh-bot || true
docker run -d --name tvh-bot --restart always \
  --env-file /home/ck/app/tvh-bot/.env \
  -v /dev/dvb:/dev/dvb:ro \
  --network host \
  tvh-bot:local
```

If building on the VPS is simpler, copy `tvh-bot/` to `/home/ck/app/tvh-bot/src`, build `tvh-bot:local` there, then run the same container.

- [ ] **Step 5: Verify on VPS**

Check:

```bash
docker logs --tail 80 tvh-bot
docker ps --filter name=tvh-bot
```

Expected: container is running and logs show bot startup without exposing the token.

Manual Telegram verification from admin account:

- `/start` returns the menu.
- `/status` returns TVH and DVB status.
- `/dvb` returns adapter list.
- `/users` returns TVH users as buttons.
- selecting a user returns short M3U and EPG URLs.

