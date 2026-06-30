from tvh_bot.tvh_client import load_passwd_tokens, merge_user_tokens, parse_users, token_for_user


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


def test_load_passwd_tokens_reads_tvh_password_files(tmp_path):
    (tmp_path / "abc").write_text(
        '{\n'
        '  "enabled": true,\n'
        '  "username": "test",\n'
        '  "authcode": "test-test_123456"\n'
        '}\n',
        encoding="utf-8",
    )

    assert load_passwd_tokens(tmp_path) == {"test": "test-test_123456"}


def test_merge_user_tokens_uses_passwd_token_when_grid_omits_it():
    users = parse_users({"entries": [{"username": "test"}]})

    merged = merge_user_tokens(users, {"test": "test-test_123456"})

    assert merged[0].token == "test-test_123456"
