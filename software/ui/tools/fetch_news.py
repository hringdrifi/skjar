#!/usr/bin/env python3
"""Fetch news headlines for the SKJÁR News widget (the electric sign in the Widget strip).

Reads RSS or Atom feeds and writes their headlines to news.txt, one per line. The UI reloads
news.txt when it changes (software/ui/src/widgets.c). On the device, a systemd timer runs this
every 15 minutes (software/os/systemd/skjar-news.timer).

The UI font has only ASCII glyphs, so headlines are folded to ASCII (accents dropped, curly
quotes made straight). Headlines that are mostly non-ASCII (for example Japanese) are skipped.

    python3 fetch_news.py                       # default feed, writes next to ui.conf
    python3 fetch_news.py --feed URL --feed URL --max 30
    python3 fetch_news.py --out ./news.txt

Only the Python standard library is used.
"""
import argparse
import os
import sys
import unicodedata
import urllib.request
import xml.etree.ElementTree as ET

DEFAULT_FEEDS = ["https://feeds.bbci.co.uk/news/world/rss.xml"]
USER_AGENT = "skjar-news/1.0"
TIMEOUT = 20

# Characters that NFKD does not turn into ASCII
REPLACE = {
    "‘": "'", "’": "'", "‚": "'", "‛": "'",
    "“": '"', "”": '"', "„": '"',
    "–": "-", "—": " - ", "−": "-",
    "…": "...", " ": " ", "£": "GBP ", "€": "EUR ",
}


def default_out():
    """The folder of ui.conf (software/ui/src/settings.c)."""
    if os.name == "nt":
        base = os.environ.get("APPDATA", ".")
        folder = os.path.join(base, "skjar")
    else:
        xdg = os.environ.get("XDG_CONFIG_HOME")
        folder = os.path.join(xdg, "skjar") if xdg else os.path.expanduser("~/.config/skjar")
    return os.path.join(folder, "news.txt")


def to_ascii(text):
    text = "".join(REPLACE.get(c, c) for c in text)
    text = unicodedata.normalize("NFKD", text)
    kept = "".join(c for c in text if " " <= c <= "~")
    return " ".join(kept.split())


def headlines(url):
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=TIMEOUT) as r:
        root = ET.fromstring(r.read())
    titles = []
    for el in root.iter():
        tag = el.tag.rsplit("}", 1)[-1]
        # RSS: <item><title>, Atom: <entry><title>
        if tag in ("item", "entry"):
            for child in el:
                if child.tag.rsplit("}", 1)[-1] == "title" and child.text:
                    titles.append(child.text.strip())
                    break
    return titles


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--feed", action="append", help="RSS or Atom URL (repeat for more). Default: BBC World")
    ap.add_argument("--max", type=int, default=20, help="headlines to keep (default 20)")
    ap.add_argument("--out", default=default_out(), help="output file (default: news.txt next to ui.conf)")
    args = ap.parse_args()

    lines, seen = [], set()
    failed = 0
    for url in args.feed or DEFAULT_FEEDS:
        try:
            got = headlines(url)
        except Exception as e:  # network down, bad XML: keep the old news.txt
            print(f"fetch_news: {url}: {e}", file=sys.stderr)
            failed += 1
            continue
        for title in got:
            line = to_ascii(title)
            # Skip headlines that lost most of their letters (non-Latin scripts)
            if len(line) < max(8, len(title) // 2) or line in seen:
                continue
            seen.add(line)
            lines.append(line)

    if not lines:
        print("fetch_news: no headlines; news.txt left as it was", file=sys.stderr)
        return 1
    lines = lines[: args.max]
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    tmp = args.out + ".tmp"
    with open(tmp, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    os.replace(tmp, args.out)   # the UI never sees a half-written file
    print(f"fetch_news: {len(lines)} headlines -> {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
