#!/usr/bin/env python3
"""タッチ UI を Windows 向けにビルドする（CMake も Visual Studio も要らない）。

C コンパイラには zig（PyPI の ziglang）を使う。LVGL は .deps/lvgl に取ってくる（git が要る）。

    py -m pip install ziglang
    py tools/build_windows.py            # build-windows/skjar-ui.exe と、テストの exe を作る
    py tools/build_windows.py --test     # 作ったあとテスト（link-test、trackpad-test、keylabel-test）を走らせる
    build-windows\\skjar-ui.exe --zoom 75

2 回目からは、変わったファイルだけをコンパイルし直す。
"""

import argparse
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

LVGL_TAG = "v9.6.0"  # CMakeLists.txt と同じ版

UI = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LVGL = os.path.join(UI, ".deps", "lvgl")
OUT = os.path.join(UI, "build-windows")
OBJ = os.path.join(OUT, "obj")

ZIG = [sys.executable, "-m", "ziglang", "cc", "-target", "x86_64-windows-gnu"]
DEFS = ["-DSKJAR_UI_WINDOWS=1", "-DLV_CONF_INCLUDE_SIMPLE", "-D_CRT_SECURE_NO_WARNINGS"]
INCS = ["-I", UI, "-I", LVGL, "-I", os.path.join(LVGL, "include"), "-I", os.path.join(LVGL, "include", "lvgl")]
CFLAGS = ["-std=gnu11", "-O2"] + DEFS + INCS
LIBS = ["-luser32", "-lgdi32", "-lwinmm", "-limm32", "-lshcore", "-ldwmapi"]

UI_SOURCES = ["main.c", "ui.c", "link.c", "serial_port.c", "platform.c", "trackpad.c", "touch_source.c", "keylabel.c", "settings.c", "icons.c", "background.c", "movie.c", "widgets.c", "recorder.c",
              "sysinfo.c", "backlight.c"]


def fetch_lvgl():
    if os.path.isdir(os.path.join(LVGL, "src")):
        return
    os.makedirs(os.path.dirname(LVGL), exist_ok=True)
    print(f"fetching LVGL {LVGL_TAG} ...")
    subprocess.run(["git", "-c", "advice.detachedHead=false", "clone", "--depth", "1", "-b", LVGL_TAG,
                    "https://github.com/lvgl/lvgl.git", LVGL], check=True)


def lvgl_sources():
    out = []
    for root, _, files in os.walk(os.path.join(LVGL, "src")):
        out += [os.path.join(root, f) for f in files if f.endswith(".c")]
    return out


def compile_one(src, warn):
    rel = os.path.relpath(src, os.path.dirname(UI)).replace(os.sep, "_").replace(".", "_")
    obj = os.path.join(OBJ, rel + ".o")
    headers_newer = os.path.getmtime(os.path.join(UI, "lv_conf.h"))
    if os.path.exists(obj) and os.path.getmtime(obj) >= max(os.path.getmtime(src), headers_newer) \
            and not src.startswith(os.path.join(UI, "src")):
        return obj, 0, ""
    flags = ["-Wall", "-Wextra"] if warn else ["-w"]
    r = subprocess.run(ZIG + CFLAGS + flags + ["-c", src, "-o", obj], capture_output=True, text=True, encoding="utf-8", errors="replace")
    return obj, r.returncode, r.stderr


def link(objs, exe):
    rsp = exe + ".rsp"
    with open(rsp, "w") as f:
        f.write("\n".join(o.replace("\\", "/") for o in objs))
    r = subprocess.run(ZIG + ["@" + rsp] + LIBS + ["-o", exe], capture_output=True, text=True, encoding="utf-8", errors="replace")
    if r.returncode != 0:
        print(r.stderr)
        sys.exit(f"link failed: {exe}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--test", action="store_true", help="run the tests after building")
    ap.add_argument("-j", type=int, default=os.cpu_count() or 4)
    args = ap.parse_args()

    fetch_lvgl()
    os.makedirs(OBJ, exist_ok=True)

    lv = lvgl_sources()
    ui = [os.path.join(UI, "src", f) for f in UI_SOURCES]
    test = [os.path.join(UI, "tests", "link_test.c"), os.path.join(UI, "tests", "trackpad_test.c"),
            os.path.join(UI, "tests", "keylabel_test.c")]
    print(f"compiling {len(lv)} LVGL files and {len(ui) + len(test)} SKJAR files ...")
    with ThreadPoolExecutor(args.j) as ex:
        res_lv = list(ex.map(lambda s: compile_one(s, False), lv))
        res_ui = list(ex.map(lambda s: compile_one(s, True), ui + test))

    failed = False
    for src, (obj, rc, err) in zip(lv + ui + test, res_lv + res_ui):
        if err.strip():
            print(f"== {os.path.relpath(src, UI)}\n{err}")
        failed |= rc != 0
    if failed:
        sys.exit("compile failed")

    lv_objs = [o for o, _, _ in res_lv]
    ui_objs = {os.path.basename(s): o for s, (o, _, _) in zip(ui + test, res_ui)}
    link(lv_objs + [ui_objs[f] for f in UI_SOURCES], os.path.join(OUT, "skjar-ui.exe"))
    link([ui_objs["link_test.c"], ui_objs["link.c"], ui_objs["serial_port.c"], ui_objs["platform.c"]],
         os.path.join(OUT, "link-test.exe"))
    link([ui_objs["trackpad_test.c"], ui_objs["trackpad.c"]], os.path.join(OUT, "trackpad-test.exe"))
    link([ui_objs["keylabel_test.c"], ui_objs["keylabel.c"]], os.path.join(OUT, "keylabel-test.exe"))
    print(f"built {os.path.join(OUT, 'skjar-ui.exe')}")

    if args.test:
        rc = 0
        for t in ("link-test.exe", "trackpad-test.exe", "keylabel-test.exe"):
            rc |= subprocess.run([os.path.join(OUT, t)]).returncode
        sys.exit(rc)


if __name__ == "__main__":
    main()
