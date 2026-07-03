from tvh_bot.bot import format_status_message, format_user_message
from tvh_bot.tvh_client import TvhUser


def test_format_status_message_contains_service_and_dvb_info():
    message = format_status_message(True, "4.3-test", ["adapter0"], 1)

    assert "TVH: OK" in message
    assert "版本: 4.3-test" in message
    assert "DVB: 1/1" in message


def test_format_user_message_contains_short_urls():
    user = TvhUser(username="test", token="test-test_123456")

    message = format_user_message("https://tvh.example.com", user)

    assert "用户: test" in message
    assert "https://tvh.example.com/m3u?a=test-test_123456" in message
    assert "https://tvh.example.com/epg?a=test-test_123456" in message
