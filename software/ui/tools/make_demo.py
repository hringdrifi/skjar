#!/usr/bin/env python3
"""Record the SKJÁR demo video: the touch UI driven by tools/demo/script.txt, made into three videos.

    py tools/make_demo.py                      # after py tools/build_windows.py
    py tools/make_demo.py --exe build-sdl/skjar-ui --out build-demo
    py tools/make_demo.py --reuse              # keep the recording, remake only the framed videos

Writes to --out (default build-demo/, ignored by git):
    demo_screen.mp4   the screen only, 1920 x 480, as recorded
    demo_16x9.mp4     1920 x 1080: the screen in the middle of a dark frame, a title above and a
                      line below, fading in and out
    demo_2.39.mp4     1920 x 804 (2.39:1): the same without the title

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
    ap.add_argument("--reuse", action="store_true", help="keep demo_screen.mp4 and only remake the framed videos")
    args = ap.parse_args()

    if not shutil.which("ffmpeg"):
        sys.exit("make_demo: ffmpeg is not on PATH")
    os.makedirs(args.out, exist_ok=True)
    screen = os.path.join(args.out, "demo_screen.mp4")
    wide = os.path.join(args.out, "demo_16x9.mp4")
    cinema = os.path.join(args.out, "demo_2.39.mp4")

    # 1. The UI records the screen. It reads news.txt / weather.txt next to the demo ui.conf,
    #    and does not save settings while recording.
    if not args.reuse:
        subprocess.run([args.exe, "--record", screen, "--demo", args.demo,
                        "--config", os.path.join(DEMO, "ui.conf"), "--link", "-", "--zoom", "25"],
                       check=True, stdin=subprocess.DEVNULL)
    elif not os.path.exists(screen):
        sys.exit(f"make_demo: --reuse needs {screen}")

    # 2. The screen in a dark frame: 16:9 with the title above, and 2.39:1 (cinema) without it
    font = args.font or next((f for f in FONT_CANDIDATES if os.path.exists(f)), None)
    if not font:
        sys.exit("make_demo: no font for the title; pass --font")
    dur = duration(screen)
    framed(screen, wide, dur, font, height=1080, screen_y=300, title=args.title, title_y=120,
           subtitle=args.subtitle, subtitle_y=860)
    # 1920 / 2.39 = 803.3 -> 804 (H.264 wants an even height). The screen sits in the middle
    framed(screen, cinema, dur, font, height=804, screen_y=162, title=None, title_y=0,
           subtitle=args.subtitle, subtitle_y=704)
    print(f"make_demo: {screen}\nmake_demo: {wide}\nmake_demo: {cinema}")


def framed(screen, out, dur, font, height, screen_y, title, title_y, subtitle, subtitle_y):
    """The 1920-wide screen on a dark 1920 x height frame, with an optional title and a line below."""
    with tempfile.TemporaryDirectory() as tmp:
        texts = []
        for name, text, y, size, color in (("title", title, title_y, 110, "white"),
                                           ("sub", subtitle, subtitle_y, 40, "0x9AA3AD")):
            if not text:
                continue
            path = os.path.join(tmp, name + ".txt")
            with open(path, "w", encoding="utf-8") as f:
                f.write(text)
            texts.append(f"drawtext=fontfile='{ff_path(font)}':textfile='{ff_path(path)}':expansion=none:"
                         f"fontsize={size}:fontcolor={color}:x=(w-tw)/2:y={y},")
        graph = os.path.join(tmp, "graph.txt")
        with open(graph, "w", encoding="utf-8") as f:
            f.write(
                f"color=c=0x0B0D10:s=1920x{height}:r=30:d={dur:.3f}[bg];"
                f"[bg][0:v]overlay=x=0:y={screen_y}:shortest=1,"
                + "".join(texts) +
                f"fade=t=in:st=0:d=0.6,fade=t=out:st={max(dur - 0.9, 0):.3f}:d=0.9[out]"
            )
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", screen, "-/filter_complex", graph,
                        "-map", "[out]", "-c:v", "libx264", "-preset", "slow", "-crf", "16",
                        "-pix_fmt", "yuv420p", "-movflags", "+faststart", out], check=True)


if __name__ == "__main__":
    main()
