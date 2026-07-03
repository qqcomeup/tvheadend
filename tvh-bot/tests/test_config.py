import pytest
from pydantic import ValidationError

from tvh_bot.config import Settings


def make_settings(**overrides):
    values = {
        "bot_token": "token",
        "admin_chat_ids": "6907590840",
        "tvh_url": "http://127.0.0.1:9981",
        "tvh_user": "ck",
        "tvh_pass": "secret",
        "public_base_url": "https://tvh.example.com",
    }
    values.update(overrides)
    return Settings(**values)


def test_settings_parses_admin_chat_ids_from_comma_list():
    settings = make_settings(admin_chat_ids="6907590840,123")

    assert settings.admin_ids == {6907590840, 123}


def test_public_base_url_strips_trailing_slash():
    settings = make_settings(
        tvh_url="http://127.0.0.1:9981/",
        public_base_url="https://tvh.example.com/",
    )

    assert settings.tvh_url == "http://127.0.0.1:9981"
    assert settings.public_base_url == "https://tvh.example.com"


def test_expected_dvb_count_must_be_non_negative():
    with pytest.raises(ValidationError):
        make_settings(expected_dvb_count=-1)
