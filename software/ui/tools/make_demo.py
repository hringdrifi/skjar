#!/usr/bin/env python3
"""Record the SKJÁR demo video: the touch UI driven by tools/demo/script.txt, made into two videos.

    py tools/make_demo.py                      # after py tools/build_windows.py
    py tools/make_demo.py --exe build-sdl/skjar-ui --out build-demo

Writes to --out (default build-demo/, ignored by git):
    demo_screen.mp4   the screen only, 1920 x 480, as recorded
    demo_16x9.mp4     1920 x 1080: the screen in the middle of a dark frame, a title above and a
                    line below, fading in and out

The UI records itself (--record / --demo, src/recorder.h), so no window capture and no mouse:
it runs on its own clock and gives the same video every time. The demo uses its own settings,
news and weather (tools/demo/), not yours. Needs ffmpeg on PATH.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
UI = os.path.dirname(HERE)
DEMO = os.path.join(HERE, "demo")

FONT_CANDIDATES = [
    r"C:\Windows\Fonts\BIZ-UDGothicB.ttc",
    r"C:\Windows\Fonts\YuGothB.ttc",
    "/usr/share/fonts/opentype/ipafont-gothic/ipag.ttf",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
]


def ff_path(path):
    """A path inside an ffmpeg filter: forward slashes, and the drive colon escaped."""
    return os.path.abspath(path).replace("\\", "/").replace(":", "\\:")


def duration(path):
    out = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "json", path],
                         check=True, capture_output=True, text=True).stdout
    return float(json.loads(out)["format"]["duration"])


def main():
    default_exe = os.path.join(UI, "build-windows", "skjar-ui.exe") if os.name == "nt" else os.path.join(UI, "build-sdl", "skjar-ui")
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=default_exe, help="the UI built for the PC (default: %(default)s)")
    ap.add_argument("--demo", default=os.path.join(DEMO, "script.txt"), help="the script")
    ap.add_argument("--out", default=os.path.join(UI, "build-demo"), help="output folder")
    ap.add_argument("--title", default="SKJÁR", help="title above the screen (16:9)")
    ap.add_argument("--subtitle", default="40% keyboard  ×  8.8-inch touch screen  ×  cyberdeck",
                    help="line below the screen (16:9)")
    ap.add_argument("--font", help="font for the title (default: BIZ UDGothic Bold, IPA Gothic, ...)")
    args = ap.parse_args()

    if not shutil.which("ffmpeg"):
        sys.exit("make_demo: ffmpeg is not on PATH")
    os.makedirs(args.out, exist_ok=True)
    screen = os.path.join(args.out, "demo_screen.mp4")
    wide = os.path.join(args.out, "demo_16x9.mp4")

    # 1. The UI records the screen. It reads news.txt / weather.txt next to the demo ui.conf,
    #    and does not save settings while recording.
    subprocess.run([args.exe, "--record", screen, "--demo", args.demo,
                    "--config", os.path.join(DEMO, "ui.conf"), "--link", "-", "--zoom", "25"],
                   check=True, stdin=subprocess.DEVNULL)

    # 2. 16:9 with a title
    font = args.font or next((f for f in FONT_CANDIDATES if os.path.exists(f)), None)
    if not font:
        sys.exit("make_demo: no font for the title; pass --font")
    dur = duration(screen)
    with tempfile.TemporaryDirectory() as tmp:
        title = os.path.join(tmp, "title.txt")
        sub = os.path.join(tmp, "sub.txt")
        with open(title, "w", encoding="utf-8") as f:
            f.write(args.title)
        with open(sub, "w", encoding="utf-8") as f:
            f.write(args.subtitle)
        graph = os.path.join(tmp, "graph.txt")
        with open(graph, "w", encoding="utf-8") as f:
            f.write(
                f"color=c=0x0B0D10:s=1920x1080:r=30:d={dur:.3f}[bg];"
                f"[bg][0:v]overlay=x=0:y=300:shortest=1,"
                f"drawtext=fontfile='{ff_path(font)}':textfile='{ff_path(title)}':expansion=none:"
                f"fontsize=110:fontcolor=white:x=(w-tw)/2:y=120,"
                f"drawtext=fontfile='{ff_path(font)}':textfile='{ff_path(sub)}':expansion=none:"
                f"fontsize=40:fontcolor=0x9AA3AD:x=(w-tw)/2:y=860,"
                f"fade=t=in:st=0:d=0.6,fade=t=out:st={max(dur - 0.9, 0):.3f}:d=0.9[out]"
            )
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", screen, "-/filter_complex", graph,
                        "-map", "[out]", "-c:v", "libx264", "-preset", "slow", "-crf", "16",
                        "-pix_fmt", "yuv420p", "-movflags", "+faststart", wide], check=True)
    print(f"make_demo: {screen}\nmake_demo: {wide}")


if __name__ == "__main__":
    main()
