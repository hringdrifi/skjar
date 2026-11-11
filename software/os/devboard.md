# 試作機のセットアップ（Pi 4B / Pi Zero 2 W ＋ Pico / Pico W）

SKJÁR の基板ができる前に、手持ちの Pi と Pico で、ディスプレイ・タッチ・UI・UART・HID over I²C を確かめるための手順。イメージを作るスクリプトはまだないので、Raspberry Pi OS Lite に手で入れる。

| 試作機 | 確かめること |
|---|---|
| Pi 4B ＋ Pico（W）＋ 8.8-DSI-TOUCH-A | 表示、タッチ、UI、UART、HID over I²C |
| Pi Zero 2 W ＋ Pico（W） | CM0 と同じ SoC での UI の重さ（CPU の負荷、電光掲示板の滑らかさ）、UART、HID over I²C。ディスプレイは HDMI のモニター |

Pico W でもよい（無線のチップが使う GPIO23 / 24 / 25 / 29 は、ファームが使うピンと重ならない）。Seeed Studio XIAO RP2040 も使える（`--features xiao`。ピンが少ないので配線が違う。[firmware/rp2040/README.md](../../firmware/rp2040/README.md) の「XIAO RP2040 で試すとき」）。Pico 2 / Pico 2 W（RP2350）は使えない（ファームは RP2040 向けで、SKJÁR の基板も RP2040）。

## 1. Pico（W）にファームを書く

PC（Windows）で、[firmware/rp2040/](../../firmware/rp2040/) をビルドする（[README](../../firmware/rp2040/README.md) の「ビルドと書き込み」）。

```sh
cargo build --release                                         # Pico / Pico W
cargo build --release --no-default-features --features xiao   # XIAO RP2040 のとき（こちらだけ）
elf2uf2-rs target/thumbv6m-none-eabi/release/skjar-rp2040 target/skjar-rp2040.uf2
```

XIAO RP2040 は、B（BOOT）ボタンを押しながら USB-C で PC につなぐ。

BOOTSEL を押しながら Pico を PC の USB につなぎ、出てきたドライブ（RPI-RP2）に UF2 をコピーする。`cargo run --release` でも書ける。書いたあとは、USB の仮想シリアル（COM ポート）にログが出る。

## 2. Pi と Pico をつなぐ

配線は [firmware/rp2040/README.md](../../firmware/rp2040/README.md) の「Pico と Pi Zero 2 W のつなぎ方」（XIAO RP2040 は「XIAO RP2040 で試すとき」）。40 ピンのヘッダの並びは 4B と Zero 2 W で同じなので、4B でも同じ配線。

Pico の電源とログは USB から。Pico の USB は PC につなぐ（COM ポートにログが出る）。PC につながっているあいだは、ファームが「PC がつながっている」と UI に知らせる（`USB,1`）ので、UI はキーボードモードとして動く。

4B のディスプレイは、付属の MIPI-DSI-Cable-12cm で、15 ピン側を 4B の DSI に、22 ピン側をパネルに挿す。パネルの電源（付属の MX1.25 2PIN → 2.54 3PIN のケーブル）は、別の 5V の電源から取り、GND は 4B とつなぐ。

**電源に注意：** 4B は公式の 5.1V 3A の電源を使う。足りない電源に、パネルの電源（0.43 A 以上）まで 4B のヘッダの 5V から取ったら、低電圧になって Wi-Fi が落ち、SSH に入れなくなった（2026-11-12。パネルの電源を抜いたらつながり、`vcgencmd get_throttled` は `0x50005`）。電源を確かめるには `vcgencmd get_throttled` が `throttled=0x0` か見る（0 以外なら、低電圧か速度の制限が起きた）。

## 3. OS を入れる

Raspberry Pi Imager で、**Raspberry Pi OS Lite (64-bit)** を microSD に書く。OS のカスタマイズで、次を決めておく：

- ホスト名（`skjar-4b`、`skjar-z2w` など）、ユーザーとパスワード
- Wi-Fi（Zero 2 W は有線がないので必須）、ロケール（Asia/Tokyo、キーボードは jp か us）
- SSH を有効にする

起動したら、PC から `ssh <ユーザー>@skjar-4b.local` で入る。

カスタマイズ（ユーザー、Wi-Fi、SSH）は cloud-init が行う。cloud-init は毎回の起動で約 5 秒動くが、試作機では止めなくてよい（起動の速さは本番のイメージで直す。[docs/open-items.md](../../docs/open-items.md) の「起動の速さ」）。止めるなら、何度か起動して、Wi-Fi の設定が `/etc/netplan/90-NM-*.yaml` か `/etc/NetworkManager/system-connections/` に書き出されているのを確かめてから。

あとで Wi-Fi を足すときは、作り直さずに `sudo nmcli connection add type wifi con-name <名前> ssid "<SSID>" wifi-sec.key-mgmt wpa-psk wifi-sec.psk "<パスワード>"`。

## 4. Pi で入れる

```sh
sudo apt update && sudo apt full-upgrade -y
sudo apt install -y git build-essential cmake pkg-config libevdev-dev device-tree-compiler \
                    evtest fonts-ipafont-gothic
git clone -b software/rmk-lvgl-spike https://github.com/hringdrifi/skjar.git
cd skjar
```

キー入力のオーバーレイ（[overlays/README.md](overlays/README.md) の「skjar-keyboard」）：

```sh
cd software/os/overlays
dtc -@ -I dts -O dtb -o skjar-keyboard.dtbo skjar-keyboard-overlay.dts
sudo cp skjar-keyboard.dtbo /boot/firmware/overlays/
cd ~/skjar
```

config.txt（[config-devboard.txt](config-devboard.txt) を、OS の config.txt の後ろに足す。4B と Zero 2 W のどちらでも同じ）：

```sh
sudo cp /boot/firmware/config.txt /boot/firmware/config.txt.orig
cat software/os/config-devboard.txt | sudo tee -a /boot/firmware/config.txt
```

UART（ログインのシェルは出さず、ハードウェアは有効に）：

```sh
sudo raspi-config nonint do_serial_cons 1
sudo raspi-config nonint do_serial_hw 0
```

UI（[software/ui/README.md](../ui/README.md) の「ビルド」。Zero 2 W は `-j2`）：

```sh
cd software/ui
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
sudo cmake --install build --prefix /usr/local
cd ~/skjar
```

ニュースと天気（入れなくても UI は動く。入れ方は各 service の先頭）：

```sh
sudo install -D -m 755 software/ui/tools/fetch_news.py /usr/local/lib/skjar/fetch_news.py
sudo install -D -m 755 software/ui/tools/fetch_weather.py /usr/local/lib/skjar/fetch_weather.py
cd software/os/systemd
sudo cp skjar-news.service skjar-news.timer skjar-news.path \
        skjar-weather.service skjar-weather.timer skjar-weather.path /etc/systemd/system/
sudo systemctl enable --now skjar-news.timer skjar-news.path skjar-weather.timer skjar-weather.path
cd ~/skjar
```

`sudo reboot` で再起動する。

## 5. 確かめる

キー入力（HID over I²C。[overlays/README.md](overlays/README.md) の「試作で確かめること」）：

```sh
dmesg | grep -i -E 'i2c-hid|i2c_hid|hid-generic'   # input: ... と出れば認識した
sudo evtest                                        # COL と ROW をジャンパー線でつなぐと、キーが出る
```

ディスプレイ（4B）：

```sh
cat /sys/class/graphics/fb0/virtual_size           # 480,1920 ならパネル
dmesg | grep -i -E 'dsi|panel|goodix|touch'
```

UI を手で動かす（`Ctrl+C` で止める）：

```sh
sudo skjar-ui --link /dev/serial0                  # 4B（パネルは縦長なので、UI が 90° 回して描く）
sudo skjar-ui --link /dev/serial0 --rotate 0       # Zero 2 W（HDMI のモニター）
```

UART が通じていれば、UI の右上の `RP2040` の警告が消え、レイヤーが `L0` と出る。Pico の COM ポートのログにも、UI から届いた行が出る。

確かめたことと、[software/ui/README.md](../ui/README.md) の「実機で確かめること」の結果は、[docs/open-items.md](../../docs/open-items.md) に書き足す。

## 6. 電源を入れたら UI が出るようにする（任意）

[systemd/skjar-ui@.service](systemd/skjar-ui@.service) の先頭のとおり。

```sh
sudo cp software/os/systemd/skjar-ui@.service /etc/systemd/system/
sudo install -m 755 software/os/bin/skjar /usr/local/bin/skjar
sudo systemctl disable serial-getty@ttyS0.service
sudo systemctl enable skjar-ui@boot.service
```

`skjar-ui@boot` は、Pico の USB が PC につながっていなければ（`USB,0`）、UI を出さずにコンソールへ行く。そのときはコンソールで `skjar` と打つ。試作のファームは PC に HID を出さないので、キーボードモードでもキーは Pi に届く（[firmware/rp2040/README.md](../../firmware/rp2040/README.md) の「いまの状態」）。
