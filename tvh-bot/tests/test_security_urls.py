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
