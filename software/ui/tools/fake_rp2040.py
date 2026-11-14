#!/usr/bin/env python3
"""RP2040 の代わりに UART のやり取り（docs/protocol.md の 1 章）をする、試験用の道具（Linux 用）。

引数なしで起動すると疑似端末を作り、そのパスを表示する。UI をそのパスにつなぐ：
    python3 tools/fake_rp2040.py
    ./build/skjar-ui --link /dev/pts/N

本物の UART で試すときは、パスを渡す（115200 bps に設定する）：
    python3 tools/fake_rp2040.py /dev/ttyUSB0

標準入力から送るもの：
    pwr A 3A | pwr B 1.5A | pwr A DEF     電源の状態
    blmax 128                             バックライトの上限
    out PC AUTO | out CM0 MAN             物理キーの出力先
    layer 1                               レイヤー
    shutdown                              シャットダウンの要求
    quiet / alive                         ALIVE を止める・再開する（つながりが切れたときの確認）
"""

import os
import select
import sys
import termios
import time
import tty


def crc8(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def frame(body: str) -> bytes:
    payload = ("SK," + body).encode()
    return b"$" + payload + b"*%02X\n" % crc8(payload)


def parse(line: bytes):
    i = line.rfind(b"$SK,")
    if i < 0:
        return None
    line = line[i:]
    star = line.rfind(b"*")
    if star < 0 or len(line) != star + 3:
        return None
    try:
        crc = int(line[star + 1:], 16)
    except ValueError:
        return None
    if crc8(line[1:star]) != crc:
        return None
    return line[4:star].decode(errors="replace").split(",")


class FakeRp2040:
    def __init__(self, fd: int):
        self.fd = fd
        self.pwr = ("B", "3A")
        self.blmax = 255
        self.out = ("PC", "AUTO")
        self.layer = 0
        self.alive = True
        self.seq = 0
        self.pressed = set()

    def send(self, body: str):
        os.write(self.fd, frame(body))
        print(f"> {body}")

    def send_state(self):
        self.send("PWR,%s,%s" % self.pwr)
        self.send(f"BLMAX,{self.blmax}")
        self.send("OUT,%s,%s" % self.out)
        self.send(f"LAYER,{self.layer}")

    def handle(self, f):
        cmd = f[0]
        if cmd == "ALIVE":
            return  # 1 秒ごとに来るので表示しない
        print(f"< {','.join(f)}")
        if cmd == "HELLO":
            self.pressed.clear()
            self.send_state()
        elif cmd == "KEY" and len(f) == 4:
            key = (f[1], f[2])
            if f[3] == "1":
                self.pressed.add(key)
            else:
                self.pressed.discard(key)
            print(f"  pressed: {sorted(self.pressed)}")
        elif cmd == "OUT" and len(f) == 2:
            # 手動の切り替えを受けたことにして、今の出力先を返す
            self.out = ("PC", "AUTO") if f[1] == "AUTO" else (f[1], "MAN")
            self.send("OUT,%s,%s" % self.out)

    def command(self, words):
        if not words:
            return
        c = words[0].lower()
        if c == "pwr" and len(words) == 3:
            self.pwr = (words[1].upper(), words[2].upper())
            self.send("PWR,%s,%s" % self.pwr)
        elif c == "blmax" and len(words) == 2:
            self.blmax = int(words[1])
            self.send(f"BLMAX,{self.blmax}")
        elif c == "out" and len(words) == 3:
            self.out = (words[1].upper(), words[2].upper())
            self.send("OUT,%s,%s" % self.out)
        elif c == "layer" and len(words) == 2:
            self.layer = int(words[1])
            self.send(f"LAYER,{self.layer}")
        elif c == "shutdown":
            self.send("SHUTDOWN")
        elif c == "quiet":
            self.alive = False
        elif c == "alive":
            self.alive = True
        else:
            print("?")


def open_port(path):
    if path is None:
        master, slave = os.openpty()
        tty.setraw(slave)
        print(f"pseudo terminal: {os.ttyname(slave)}  (skjar-ui --link {os.ttyname(slave)})")
        return master, slave
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY)
    tty.setraw(fd)
    attrs = termios.tcgetattr(fd)
    attrs[4] = attrs[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd, None


def main():
    fd, keep = open_port(sys.argv[1] if len(sys.argv) > 1 else None)
    rp = FakeRp2040(fd)
    buf = b""
    next_alive = time.monotonic()
    while True:
        now = time.monotonic()
        if rp.alive and now >= next_alive:
            os.write(fd, frame(f"ALIVE,{rp.seq}"))
            rp.seq = (rp.seq + 1) % 256
            next_alive = now + 1.0
        ready, _, _ = select.select([fd, sys.stdin], [], [], 0.1)
        if fd in ready:
            try:
                buf += os.read(fd, 256)
            except OSError:
                pass  # 疑似端末の相手がまだ開いていない
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                f = parse(line.rstrip(b"\r"))
                if f:
                    rp.handle(f)
        if sys.stdin in ready:
            text = sys.stdin.readline()
            if not text:
                break
            rp.command(text.split())
    del keep


if __name__ == "__main__":
    main()
