#!/usr/bin/env python3
"""隠し機能（時計を 5 回叩くと流れる動画）のファイルを作る。

手元の動画から、画面用のコマとキーのライト用のコマを .skm に、音を .wav にする。
動画そのものはリポジトリに入れない（著作権のため）。自分で持っている動画を使う。

    py tools/make_movie.py 動画.mp4 movie          # movie.skm と movie.wav を作る
    py tools/make_movie.py 動画.mp4 movie --seconds 20   # 試しに先頭 20 秒だけ

できたファイルは、UI の設定の場所（Windows：%APPDATA%\\skjar、Linux：~/.config/skjar）に
movie.skm / movie.wav として置くか、skjar-ui の --movie で渡す。

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
"""

import argparse
import itertools
import os
import struct
import subprocess
import sys

W, H, FPS = 320, 240, 30
LEVELS = 4

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
BOARD_X0, BOARD_Y0, BOARD_W, BOARD_H = 34.131, 85.725, 228.6, 76.2   # キーのある範囲（12u × 4u）

# 各キーで明るさを見る点（キーボード全体を動画全体に引き伸ばして重ね、キーの中の 3 × 3 点の平均）
SAMPLES = []
for kx, ky in KEY_POS:
    pts = []
    for dy in (-0.3, 0.0, 0.3):
        for dx in (-0.3, 0.0, 0.3):
            u = (kx + dx * 19.05 - BOARD_X0) / BOARD_W
            v = (ky + dy * 19.05 - BOARD_Y0) / BOARD_H
            x = min(W - 1, max(0, int(u * W)))
            y = min(H - 1, max(0, int(v * H)))
            pts.append(y * W + x)
    SAMPLES.append(pts)

# 256 段 → 4 段（画面）
QUANT = bytes(min(LEVELS - 1, (v * LEVELS) // 256) for v in range(256))


def encode_screen(frame):
    q = frame.translate(QUANT)
    out = bytearray()
    for level, group in itertools.groupby(q):
        n = sum(1 for _ in group)
        while n > 0:
            run = min(64, n)
            out.append((level << 6) | (run - 1))
            n -= run
    return bytes(out)


def encode_leds(frame):
    vals = []
    for pts in SAMPLES:
        avg = sum(frame[p] for p in pts) // len(pts)
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
    args = ap.parse_args()

    limit = ["-t", str(args.seconds)] if args.seconds else []
    vf = (f"fps={FPS},scale={W}:{H}:force_original_aspect_ratio=decrease,"
          f"pad={W}:{H}:(ow-iw)/2:(oh-ih)/2:black,format=gray")
    cmd = ["ffmpeg", "-loglevel", "error", "-i", args.video, *limit, "-vf", vf, "-f", "rawvideo", "-"]
    try:
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    except FileNotFoundError:
        sys.exit("ffmpeg が見つからない。インストールして PATH に置く")

    frames = []
    size = W * H
    while True:
        raw = proc.stdout.read(size)
        if len(raw) < size:
            break
        screen = encode_screen(raw)
        frames.append(encode_leds(raw) + struct.pack("<I", len(screen)) + screen)
        if len(frames) % 300 == 0:
            print(f"  {len(frames)} frames ({len(frames) / FPS:.0f} s)")
    if proc.wait() != 0:
        sys.exit("ffmpeg が失敗した")
    if not frames:
        sys.exit("コマが 1 つも読めなかった")

    header = b"SKJM" + struct.pack("<HHHHBBI", 1, FPS, W, H, LEVELS, len(KEY_POS), len(frames))
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
