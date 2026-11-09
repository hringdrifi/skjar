#!/usr/bin/env python3
"""Fetch news headlines for the SKJÁR News widget (the electric sign in the Widget strip).

Reads RSS or Atom feeds and writes their headlines to news.txt (UTF-8), one per line. The UI
reloads news.txt when it changes (software/ui/src/widgets.c). On the device, a systemd timer runs
this every 15 minutes (software/os/systemd/skjar-news.timer).

The UI draws the headlines with a Japanese font when it finds one (IPA Gothic, Noto Sans CJK,
BIZ UDGothic, or --font). Without one it shows ASCII headlines only; --ascii then folds Latin
text to ASCII here (accents dropped, curly quotes made straight) and drops the rest.

    python3 fetch_news.py                               # NHK top news, next to ui.conf
    python3 fetch_news.py --feed nhk-business --feed bbc
    python3 fetch_news.py --feed https://example.com/rss.xml --max 30 --out ./news.txt

Named feeds: see FEEDS below. NHK's RSS is for personal use; check each site's terms.
Only the Python standard library is used.
"""
import argparse
import os
import sys
import unicodedata
import urllib.request
import xml.etree.ElementTree as ET

FEEDS = {
    "nhk": "https://www3.nhk.or.jp/rss/news/cat0.xml",           # NHK 主要ニュース
    "nhk-society": "https://www3.nhk.or.jp/rss/news/cat1.xml",   # 社会
    "nhk-science": "https://www3.nhk.or.jp/rss/news/cat3.xml",   # 科学・医療
    "nhk-politics": "https://www3.nhk.or.jp/rss/news/cat4.xml",  # 政治
    "nhk-business": "https://www3.nhk.or.jp/rss/news/cat5.xml",  # 経済
    "nhk-world": "https://www3.nhk.or.jp/rss/news/cat6.xml",     # 国際
    "nhk-sports": "https://www3.nhk.or.jp/rss/news/cat7.xml",    # スポーツ
    "bbc": "https://feeds.bbci.co.uk/news/world/rss.xml",
}
DEFAULT_FEEDS = ["nhk"]
USER_AGENT = "skjar-news/1.0"
TIMEOUT = 20

# For --ascii: characters that NFKD does not turn into ASCII
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
    return "".join(c for c in text if " " <= c <= "~")


def tidy(text):
    """One line, no control characters, single spaces."""
    text = "".join(c for c in text if unicodedata.category(c)[0] != "C" or c in " \t")
    return " ".join(text.split())


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
    ap.add_argument("--feed", action="append",
                    help="feed name (" + ", ".join(FEEDS) + ") or RSS/Atom URL; repeat for more. Default: nhk")
    ap.add_argument("--max", type=int, default=20, help="headlines to keep (default 20)")
    ap.add_argument("--ascii", action="store_true", help="fold to ASCII and drop the rest (UI without a Japanese font)")
    ap.add_argument("--out", default=default_out(), help="output file (default: news.txt next to ui.conf)")
    args = ap.parse_args()

    lines, seen = [], set()
    for feed in args.feed or DEFAULT_FEEDS:
        url = FEEDS.get(feed, feed)
        try:
            got = headlines(url)
        except Exception as e:  # network down, bad XML: keep the old news.txt
            print(f"fetch_news: {url}: {e}", file=sys.stderr)
            continue
        for title in got:
            line = tidy(title)
            if args.ascii:
                folded = tidy(to_ascii(line))
                # Skip headlines that lost most of their letters (non-Latin scripts)
                if len(folded) < max(8, len(line) // 2):
                    continue
                line = folded
            if not line or line in seen:
                continue
            seen.add(line)
            lines.append(line)

    if not lines:
        print("fetch_news: no headlines; news.txt left as it was", file=sys.stderr)
        return 1
    lines = lines[: args.max]
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    tmp = args.out + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    os.replace(tmp, args.out)   # the UI never sees a half-written file
    print(f"fetch_news: {len(lines)} headlines -> {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
