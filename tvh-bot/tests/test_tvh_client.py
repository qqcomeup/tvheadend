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
