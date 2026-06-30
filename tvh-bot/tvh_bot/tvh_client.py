from dataclasses import dataclass
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


class TvhClient:
    def __init__(self, base_url: str, username: str, password: str) -> None:
        self.base_url = base_url.rstrip("/")
        self.auth = BasicAuth(username, password)

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
                return parse_users(await resp.json(content_type=None))
