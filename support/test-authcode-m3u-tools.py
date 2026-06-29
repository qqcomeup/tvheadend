#!/usr/bin/env python3

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def test_authcode_is_admin_editable():
    access = read("src/access.c")
    require("passwd_entry_class_authcode_set" in access,
            "authcode needs a dedicated setter")
    authcode_block = access.split('.id       = "authcode"', 1)[1].split("},", 1)[0]
    require(".set      = passwd_entry_class_authcode_set" in authcode_block,
            "authcode property must use the authcode setter")
    require("PO_RDONLY" not in authcode_block,
            "authcode property must not be read-only")


def test_authcode_validation_guards():
    access = read("src/access.c")
    require("passwd_authcode_valid" in access,
            "authcode setter must validate allowed characters and length")
    require("passwd_auth_exists_for_other" in access,
            "authcode setter must reject duplicate tokens owned by another user")
    require("pw->pw_auth_enabled = 1" in access,
            "setting a valid authcode must enable persistent authentication")


def test_password_editor_can_copy_playlist_urls():
    acl = read("src/webui/static/app/acleditor.js")
    require("tvheadend.passwdM3uBaseUrl" in acl,
            "password editor must build URLs from the current browser origin")
    require("window.location.protocol" in acl and "window.location.host" in acl,
            "URL builder must use the current Lucky/reverse-proxy origin")
    require("/playlist/auth/channels.m3u?download=1&auth=" in acl,
            "password editor must build the authenticated M3U URL")
    require("/xmltv/channels?auth=" in acl,
            "password editor must build the authenticated XMLTV URL")


def test_password_editor_copy_ui_is_chinese():
    acl = read("src/webui/static/app/acleditor.js")
    css = read("src/webui/static/app/ext.css")
    require("'复制 M3U 地址'" in acl,
            "M3U copy button must display Chinese text directly")
    require("'复制 XMLTV 地址'" in acl,
            "XMLTV copy button must display Chinese text directly")
    require("'复制地址'" in acl,
            "copy warning dialogs must display Chinese titles directly")
    require("text: '复制'" in acl and "text: '关闭'" in acl,
            "URL popup must include explicit Chinese copy and close buttons")
    require("passwd-copy-status" in acl and "已复制" in acl,
            "URL popup must show a Chinese copied confirmation")
    require("iconCls: 'passwd-copy-url'" in acl,
            "copy URL buttons must use the Tvheadend toolbar icon style")
    require(".passwd-copy-url" in css and "../icons/linked.gif" in css,
            "copy URL icon class must use a bundled Tvheadend icon")
    require("tvheadend.passwdCopyText(url, showCopied);" in acl,
            "URL popup must copy the generated URL and report success")


if __name__ == "__main__":
    tests = [
        test_authcode_is_admin_editable,
        test_authcode_validation_guards,
        test_password_editor_can_copy_playlist_urls,
        test_password_editor_copy_ui_is_chinese,
    ]
    for test in tests:
        test()
        print(f"PASS {test.__name__}")
