#!/bin/sh
set -eu

url="${1:-}"

if [ -z "$url" ]; then
  echo "Usage: $0 PLAYLIST_URL" >&2
  echo "Example: $0 'http://user:pass@example:9981/playlist/auth/channels.m3u'" >&2
  exit 2
fi

tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT

if command -v curl >/dev/null 2>&1; then
  curl -fsSL "$url" >"$tmp"
elif command -v wget >/dev/null 2>&1; then
  wget -q -O "$tmp" "$url"
else
  echo "curl or wget is required" >&2
  exit 2
fi

fail=0

check_present() {
  name="$1"
  pattern="$2"
  if grep -q "$pattern" "$tmp"; then
    echo "ok: $name"
  else
    echo "missing: $name" >&2
    fail=1
  fi
}

check_absent() {
  name="$1"
  pattern="$2"
  if grep -q "$pattern" "$tmp"; then
    echo "unexpected: $name" >&2
    fail=1
  else
    echo "ok: no $name"
  fi
}

check_present "#EXTM3U header" "^#EXTM3U"
check_present "group-title" "group-title="
check_present "tvg-name" "tvg-name="
check_present "tvg-id" "tvg-id="
check_present "stream URL" "/stream/channelid/"
check_absent "local file logo path" "tvg-logo=\"file://"

exit "$fail"
