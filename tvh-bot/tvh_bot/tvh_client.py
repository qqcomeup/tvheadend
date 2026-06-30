from dataclasses import dataclass
import json
from pathlib import Path
from typing import Any

import aiohttp
from aiohttp import BasicAuth


@dataclass(frozen=True)
class TvhUser:
    username: str
    token: str | None


def parse_users(payload: dict[str, Any]) -> list[TvhUser]:
    users: list[TvhUser] = []
    for entry in payload.get("entries", []):
        username = entry.get("username") or entry.get("user") or entry.get("name")
        if not username:
            continue
        token = entry.get("authcode") or entry.get("auth") or entry.get("token") or None
        users.append(TvhUser(username=str(username), token=str(token) if token else None))
    return users


def token_for_user(users: list[TvhUser], username: str) -> str | None:
    for user in users:
        if user.username == username:
            return user.token
    return None


def load_passwd_tokens(path: Path | str | None) -> dict[str, str]:
    if not path:
        return {}

    root = Path(path)
    if not root.exists() or not root.is_dir():
        return {}

    tokens: dict[str, str] = {}
    for item in root.iterdir():
        if not item.is_file():
            continue
        try:
            payload = json.loads(item.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
        username = payload.get("username")
        token = payload.get("authcode")
        if username and token:
            tokens[str(username)] = str(token)
    return tokens


def merge_user_tokens(users: list[TvhUser], tokens: dict[str, str]) -> list[TvhUser]:
    return [
        TvhUser(username=user.username, token=user.token or tokens.get(user.username))
        for user in users
    ]


class TvhClient:
    def __init__(
        self,
        base_url: str,
        username: str,
        password: str,
        passwd_path: Path | str | None = None,
    ) -> None:
        self.base_url = base_url.rstrip("/")
        self.auth = BasicAuth(username, password)
        self.passwd_path = passwd_path

    async def get_status(self) -> tuple[bool, str | None]:
        try:
            async with aiohttp.ClientSession(auth=self.auth) as session:
                async with session.get(f"{self.base_url}/api/serverinfo", timeout=10) as resp:
                    if resp.status >= 400:
                        return False, None
                    payload = await resp.json(content_type=None)
                    version = payload.get("sw_version") or payload.get("version")
                    return True, str(version) if version else None
        except aiohttp.ClientError:
            return False, None

    async def get_users(self) -> list[TvhUser]:
        async with aiohttp.ClientSession(auth=self.auth) as session:
            async with session.get(f"{self.base_url}/api/access/entry/grid", timeout=10) as resp:
                resp.raise_for_status()
                users = parse_users(await resp.json(content_type=None))
                return merge_user_tokens(users, load_passwd_tokens(self.passwd_path))
