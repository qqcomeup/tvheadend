from functools import cached_property

from pydantic import Field, field_validator
from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(
        env_file=".env",
        env_file_encoding="utf-8",
        populate_by_name=True,
    )

    bot_token: str = Field(alias="BOT_TOKEN")
    admin_chat_ids: str = Field(alias="ADMIN_CHAT_IDS")
    tvh_url: str = Field(alias="TVH_URL")
    tvh_user: str = Field(alias="TVH_USER")
    tvh_pass: str = Field(alias="TVH_PASS")
    public_base_url: str = Field(alias="PUBLIC_BASE_URL")
    expected_dvb_count: int = Field(default=1, ge=0, alias="EXPECTED_DVB_COUNT")
    check_interval_seconds: int = Field(default=60, ge=10, alias="CHECK_INTERVAL_SECONDS")

    @field_validator("tvh_url", "public_base_url")
    @classmethod
    def strip_trailing_slash(cls, value: str) -> str:
        return value.rstrip("/")

    @cached_property
    def admin_ids(self) -> set[int]:
        ids: set[int] = set()
        for item in self.admin_chat_ids.split(","):
            item = item.strip()
            if item:
                ids.add(int(item))
        return ids
