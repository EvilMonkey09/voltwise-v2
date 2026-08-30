"""GitHub releases/latest fetch, semver compare, and optional disk cache."""
from __future__ import annotations

import json
import os
import time
import urllib.error
import urllib.request
from pathlib import Path

from packaging.version import InvalidVersion, Version

DEFAULT_REPO = "EvilMonkey09/voltwise-v2"
UA = "VoltWise-UpdateCheck"


def github_repo() -> str:
    return os.environ.get("VOLTWISE_GITHUB_REPO", DEFAULT_REPO).strip() or DEFAULT_REPO


def normalize_tag(tag: str) -> str:
    t = (tag or "").strip()
    if t.startswith("v"):
        return t[1:]
    return t


def parse_ver(s: str) -> Version:
    try:
        return Version(normalize_tag(s))
    except InvalidVersion:
        return Version("0")


def fetch_latest_release_dict() -> dict | None:
    repo = github_repo()
    url = f"https://api.github.com/repos/{repo}/releases/latest"
    req = urllib.request.Request(
        url,
        headers={
            "Accept": "application/vnd.github+json",
            "User-Agent": UA,
        },
    )
    try:
        with urllib.request.urlopen(req, timeout=20) as resp:
            return json.loads(resp.read().decode())
    except (urllib.error.URLError, json.JSONDecodeError, TimeoutError, OSError):
        return None


def _build_response(local_version: str, rel: dict, from_cache: bool) -> dict:
    tag = rel.get("tag_name") or ""
    lv = parse_ver(local_version)
    rv = parse_ver(tag)
    assets = []
    for a in rel.get("assets") or []:
        if isinstance(a, dict) and a.get("name"):
            assets.append(
                {
                    "name": a["name"],
                    "browser_download_url": a.get("browser_download_url") or "",
                }
            )
    return {
        "ok": True,
        "update_available": rv > lv,
        "current": local_version,
        "latest_tag": tag,
        "latest_version": normalize_tag(tag),
        "html_url": rel.get("html_url") or "",
        "published_at": rel.get("published_at") or "",
        "assets": assets,
        "from_cache": from_cache,
    }


def check_cached_or_fetch(
    local_version: str,
    cache_path: Path,
    max_age_seconds: float = 6 * 3600,
) -> dict:
    """
    Returns keys including ok, update_available, current, latest_tag, html_url, assets,
    from_cache, error (if failed with no stale cache).
    """
    now = time.time()
    stale: dict | None = None
    if cache_path.is_file():
        try:
            cached = json.loads(cache_path.read_text(encoding="utf-8"))
            stale = cached.get("release")
            if cached.get("fetched_at", 0) + max_age_seconds > now and stale:
                return _build_response(local_version, stale, True)
        except (json.JSONDecodeError, OSError, TypeError, KeyError):
            stale = None

    rel = fetch_latest_release_dict()
    if rel is None:
        if stale:
            return _build_response(local_version, stale, True)
        return {
            "ok": False,
            "error": "Could not reach GitHub or parse release.",
            "update_available": False,
            "current": local_version,
        }

    try:
        cache_path.parent.mkdir(parents=True, exist_ok=True)
        cache_path.write_text(
            json.dumps({"fetched_at": now, "release": rel}, indent=2),
            encoding="utf-8",
        )
    except OSError:
        pass

    return _build_response(local_version, rel, False)
