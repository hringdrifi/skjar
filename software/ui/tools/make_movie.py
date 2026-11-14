#!/usr/bin/env python3
"""隠し機能（時計を 5 回叩くと流れる動画）のファイルを作る。

手元の動画から、画面用のコマとキーのライト用のコマを .skm に、音を .wav にする。
動画そのものはリポジトリに入れない（著作権のため）。自分で持っている動画を使う。

    py tools/make_movie.py 動画.mp4 movie          # movie.skm と movie.wav を作る
    py tools/make_movie.py 動画.mp4 movie --seconds 20   # 試しに先頭 20 秒だけ
    py tools/make_movie.py 動画.mp4 movie --fit cover    # 左右を余らせず、上下を切る

できたファイルは、UI の設定の場所（Windows：%APPDATA%\\skjar、Linux：~/.config/skjar）に
movie.skm / movie.wav として置くか、skjar-ui の --movie で渡す。

画面とキーボードを 1 枚の画面に見立てて、動画を上下に分けて映す。実物の位置関係（スクリーンの
表示領域と、キーの位置。どちらも基板の座標、mm）のとおりに、動画を枠に重ねる：
  - 枠：横はキーボードの幅、縦はスクリーンの表示領域の上端からキーボードの下端まで
  - 上（スクリーン）：枠のうち表示領域の部分を切り出して、画面いっぱいに出す
  - 下（キーボード）：各キーの位置にあたる動画の明るさで、キーを光らせる
動画は、全体が枠に収まるように合わせる（--fit contain。余りは黒）。--fit cover なら枠を覆うように
合わせて、はみ出した分を切る。

必要なもの：ffmpeg（PATH に置く）。Python は標準ライブラリだけ。

.skm の形（数値はリトルエンディアン）：
  ヘッダー 32 バイト：
    "SKJM"、版 u16 = 1、fps u16、幅 u16、高さ u16、濃淡の段数 u8 = 4、キーの数 u8 = 42、
    コマの数 u32、予約（0）
  コマの場所の表：コマの数 × u32（ファイルの先頭からの位置）
  コマ：
    キーのライト 21 バイト（42 キー × 4 ビット。明るさ 0〜15。キーの順は RMK のマトリクスの
      行 × 6 ＋ 列。2 キーずつ、下位 4 ビットが偶数番目）
    画面の長さ u32、画面（1 バイトに 1 つの続き：上位 2 ビットが濃淡 0〜3、下位 6 ビットが続く数 − 1）
  画面は 幅 × 高さ で、UI は縦横 2 倍にして画面の真ん中に出す（960 × 240 なら画面いっぱい）
"""

import argparse
import itertools
import struct
import subprocess
import sys

FPS = 30
LEVELS = 4

# スクリーンの表示領域（TOUCH-A の図面。219.88 × 55.72 mm）。中心は基板の DISP1
VA_W, VA_H = 219.88, 55.72
VA_CX, VA_CY = 148.431, 51.275
VA_X0, VA_Y0 = VA_CX - VA_W / 2, VA_CY - VA_H / 2

# キーの中心（基板の座標、mm）。RMK のマトリクスの順（行 × 6 ＋ 列。firmware/rp2040/src/keymap.rs）
KEY_POS = [
    (43.656, 95.25), (62.706, 95.25), (81.756, 95.25), (100.806, 95.25), (119.856, 95.25), (138.906, 95.25),
    (253.206, 95.25), (234.156, 95.25), (215.106, 95.25), (196.056, 95.25), (177.006, 95.25), (157.956, 95.25),
    (46.038, 114.3), (67.469, 114.3), (86.519, 114.3), (105.569, 114.3), (124.619, 114.3), (143.669, 114.3),
    (250.825, 152.4), (246.062, 114.3), (219.869, 114.3), (200.819, 114.3), (181.769, 114.3), (162.719, 114.3),
    (50.8, 133.35), (76.994, 133.35), (96.044, 133.35), (115.094, 133.35), (134.144, 133.35), (153.194, 133.35),
    (229.394, 152.4), (250.825, 133.35), (229.394, 133.35), (210.344, 133.35), (191.294, 133.35), (172.244, 133.35),
    (46.038, 152.4), (67.469, 152.4), (88.9, 152.4), (207.963, 152.4), (127.0, 152.4), (174.625, 152.4),
]
BOARD_X0, BOARD_X1, BOARD_Y1 = 34.131, 262.731, 161.925   # キーのある範囲の左・右・下

# 枠（mm）と、その画素（スクリーンの画素の半分の細かさ。UI が 2 倍にする）
FRAME_X0, FRAME_X1 = BOARD_X0, BOARD_X1
FRAME_Y0, FRAME_Y1 = VA_Y0, BOARD_Y1
SCREEN_W, SCREEN_H = 960, 240                      # スクリーン（1920 × 480）の半分
PX_PER_MM = SCREEN_W / VA_W


def px(v):
    return int(round(v * PX_PER_MM))


CANVAS_W = px(FRAME_X1 - FRAME_X0) // 2 * 2
CANVAS_H = px(FRAME_Y1 - FRAME_Y0) // 2 * 2
SCREEN_X = px(VA_X0 - FRAME_X0)                    # 枠の中の、スクリーンの切り出しの左上
SCREEN_Y = (px(VA_H) - SCREEN_H) // 2

# 各キーで明るさを見る点（キーの中の 3 × 3 点の平均）
SAMPLES = []
for kx, ky in KEY_POS:
    pts = []
    for dy in (-0.3, 0.0, 0.3):
        for dx in (-0.3, 0.0, 0.3):
            x = min(CANVAS_W - 1, max(0, px(kx + dx * 19.05 - FRAME_X0)))
            y = min(CANVAS_H - 1, max(0, px(ky + dy * 19.05 - FRAME_Y0)))
            pts.append(y * CANVAS_W + x)
    SAMPLES.append(pts)

# 256 段 → 4 段（画面）
QUANT = bytes(min(LEVELS - 1, (v * LEVELS) // 256) for v in range(256))


def crop_screen(canvas):
    rows = []
    for y in range(SCREEN_Y, SCREEN_Y + SCREEN_H):
        start = y * CANVAS_W + SCREEN_X
        rows.append(canvas[start:start + SCREEN_W])
    return b"".join(rows)


def encode_screen(screen):
    q = screen.translate(QUANT)
    out = bytearray()
    for level, group in itertools.groupby(q):
        n = sum(1 for _ in group)
        while n > 0:
            run = min(64, n)
            out.append((level << 6) | (run - 1))
            n -= run
    return bytes(out)


def encode_leds(canvas):
    vals = []
    for pts in SAMPLES:
        avg = sum(canvas[p] for p in pts) // len(pts)
        vals.append(avg * 16 // 256)
    packed = bytearray()
    for i in range(0, len(vals), 2):
        packed.append((vals[i] & 0x0F) | ((vals[i + 1] & 0x0F) << 4))
    return bytes(packed)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("video", help="手元の動画ファイル")
    ap.add_argument("out", help="出力の名前（拡張子なし）。<out>.skm と <out>.wav を作る")
    ap.add_argument("--seconds", type=float, help="先頭から何秒だけ使うか（試し用）")
    ap.add_argument("--fit", choices=("contain", "cover"), default="contain",
                    help="contain：全体を枠に収める（余りは黒）。cover：枠を覆う（はみ出しは切る）")
    args = ap.parse_args()

    limit = ["-t", str(args.seconds)] if args.seconds else []
    if args.fit == "contain":
        fit = (f"scale={CANVAS_W}:{CANVAS_H}:force_original_aspect_ratio=decrease,"
               f"pad={CANVAS_W}:{CANVAS_H}:(ow-iw)/2:(oh-ih)/2:black")
    else:
        fit = f"scale={CANVAS_W}:{CANVAS_H}:force_original_aspect_ratio=increase,crop={CANVAS_W}:{CANVAS_H}"
    vf = f"fps={FPS},{fit},format=gray"
    cmd = ["ffmpeg", "-loglevel", "error", "-i", args.video, *limit, "-vf", vf, "-f", "rawvideo", "-"]
    try:
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    except FileNotFoundError:
        sys.exit("ffmpeg が見つからない。インストールして PATH に置く")

    print(f"frame {CANVAS_W} x {CANVAS_H} px, screen {SCREEN_W} x {SCREEN_H} at ({SCREEN_X}, {SCREEN_Y}), fit {args.fit}")
    frames = []
    size = CANVAS_W * CANVAS_H
    while True:
        raw = proc.stdout.read(size)
        if len(raw) < size:
            break
        screen = encode_screen(crop_screen(raw))
        frames.append(encode_leds(raw) + struct.pack("<I", len(screen)) + screen)
        if len(frames) % 300 == 0:
            print(f"  {len(frames)} frames ({len(frames) / FPS:.0f} s)")
    if proc.wait() != 0:
        sys.exit("ffmpeg が失敗した")
    if not frames:
        sys.exit("コマが 1 つも読めなかった")

    header = b"SKJM" + struct.pack("<HHHHBBI", 1, FPS, SCREEN_W, SCREEN_H, LEVELS, len(KEY_POS), len(frames))
    header += bytes(32 - len(header))
    offset = len(header) + 4 * len(frames)
    index = bytearray()
    for f in frames:
        index += struct.pack("<I", offset)
        offset += len(f)
    with open(args.out + ".skm", "wb") as fp:
        fp.write(header)
        fp.write(index)
        for f in frames:
            fp.write(f)
    print(f"wrote {args.out}.skm: {len(frames)} frames, {offset / 1e6:.1f} MB")

    wav = ["ffmpeg", "-loglevel", "error", "-y", "-i", args.video, *limit, "-vn", "-ac", "2", "-ar", "44100",
           "-c:a", "pcm_s16le", args.out + ".wav"]
    if subprocess.run(wav).returncode == 0:
        print(f"wrote {args.out}.wav")
    else:
        print("音を取り出せなかった（動画に音がない？）。.wav なしでも映像は流れる")


if __name__ == "__main__":
    main()
