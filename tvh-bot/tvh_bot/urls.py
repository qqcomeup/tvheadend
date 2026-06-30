from urllib.parse import urlencode


def build_m3u_url(public_base_url: str, token: str) -> str:
    return _build_short_url(public_base_url, "m3u", token)


def build_epg_url(public_base_url: str, token: str) -> str:
    return _build_short_url(public_base_url, "epg", token)


def _build_short_url(public_base_url: str, path: str, token: str) -> str:
    base = public_base_url.rstrip("/")
    return f"{base}/{path}?{urlencode({'a': token})}"
