# ui

CM0 で動く専用のタッチ UI。[LVGL](https://lvgl.io/) 9.6（C）で作る（選んだ理由は [docs/design-decisions.md](../../docs/design-decisions.md) の 4 章）。X も Wayland も使わず、フレームバッファ（`/dev/fb0`）に直接描き、タッチは evdev から読む。

## いまの状態（試作）

| 機能 | 状態 |
|---|---|
| 画面：480 × 1920 のパネルを 90° 回して、1920 × 480 の横長で使う | 実装した。向き（90° か 270° か）は実機で決める |
| 状態の帯：時計、物理キーの出力先、電源、レイヤー、CPU・メモリ・温度、RP2040 とのつながり | 実装した |
| タッチキー：数字・記号（Num）、F1〜F12・編集キー・矢印（F）、メディアキー（Media） | 実装した。押したとき・離したときに UART で RP2040 に送る |
| バックライトの明るさ（スライダー）と、RP2040 から届く上限 | 実装した |
| 物理キーの出力先の手動切り替え、CPU のグラフ、RP2040 の UF2、電源を切る（System） | 実装した。UF2 と電源は長押し |
| RP2040 との UART（[docs/protocol.md](../../docs/protocol.md) の 1 章。形式は案） | 実装した。RP2040 側も [firmware/rp2040/](../../firmware/rp2040/) にある |
| 確認したこと | aarch64 Linux 向けに、LVGL ごとコンパイルとリンクが通る（fbdev。zig cc で確認）。実機、SDL の窓、`link-test` の実行はまだ |

- 記号のラベルは US 配列のもの。PC 側が JIS 配列だと、Shift を使う記号（`@` など）は別の文字になる。
- 文字は英数字だけ。LVGL の Montserrat には日本語の字形がない。日本語を出すときは、フォントを足す。
- いまは root で動かす（`/dev/fb0`、`/dev/input`、`/dev/serial0`、`/sys/class/backlight` と `poweroff` を使うため）。専用のユーザーに分けるのは後で。

## ファイル

| ファイル | 内容 |
|---|---|
| [src/main.c](src/main.c) | 起動、描画先（fbdev か SDL）、タッチ、メインループ |
| [src/ui.c](src/ui.c) | 画面の中身。タッチキーの表もここ |
| [src/link.c](src/link.c) | RP2040 との UART のやり取り |
| [src/backlight.c](src/backlight.c) | `/sys/class/backlight` の明るさ |
| [src/sysinfo.c](src/sysinfo.c) | CPU・メモリ・温度 |
| [lv_conf.h](lv_conf.h) | LVGL の設定 |
| [tests/link_test.c](tests/link_test.c) | UART の行の組み立てと読み取りのテスト |
| [tools/fake_rp2040.py](tools/fake_rp2040.py) | RP2040 の代わりに UART のやり取りをする道具 |

## ビルド

LVGL は CMake が取ってくる（`FetchContent`、v9.6.0）。

### 実機（CM0、または Pi Zero 2 W）で

```sh
sudo apt install build-essential cmake git pkg-config libevdev-dev
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
sudo cmake --install build --prefix /usr/local
```

512 MB のメモリでビルドが重いときは `-j2` にする。

### PC の窓で試す（Linux か WSL）

```sh
sudo apt install build-essential cmake git pkg-config libsdl2-dev
cmake -B build-sdl -DSKJAR_UI_BACKEND=sdl
cmake --build build-sdl -j
python3 tools/fake_rp2040.py          # 疑似端末のパスが出る
./build-sdl/skjar-ui --link /dev/pts/N
```

`fake_rp2040.py` に `pwr A 1.5A`、`blmax 100`、`layer 2`、`shutdown` などを打つと、UI の表示が変わる。タッチキーを押すと、届いた `KEY` が表示される。

### テスト

```sh
ctest --test-dir build
```

## 実機での設定

1. OS の設定：[software/os/config.txt](../os/config.txt) を `/boot/firmware/config.txt` に入れる（使うオーバーレイは [software/os/overlays/](../os/overlays/) でビルドする）。
2. UART：`raspi-config` の Interface Options → Serial Port で、ログインシェルを「いいえ」、ハードウェアを「はい」にする。カーネルのログ（`console=serial0,115200`）は残してよい。UI は、ログが混ざった行を読み飛ばす。
3. 起動：[software/os/systemd/skjar-ui.service](../os/systemd/skjar-ui.service) を入れる。`getty@tty1` を止め、UI が tty1 をグラフィックモードにして、コンソールの文字が画面に出ないようにする。

起動のオプション：

| オプション | 初期値 | 内容 |
|---|---|---|
| `--link <path>` | `/dev/serial0`（SDL では `-`） | RP2040 との UART。`-` なら開かずに、送る行を標準エラーに出す |
| `--rotate <0/90/180/270>` | 90（SDL では 0） | 画面の回転 |
| `--fb <path>` | `/dev/fb0` | フレームバッファ |
| `--touch <path>` | 自動で探す | タッチの evdev。探すときは、座標が絶対値のもの（タッチパネル）だけを使う |
| `--vt <path>` | `/dev/tty1` | グラフィックモードにする仮想端末。`-` で触らない |
| `--no-poweroff` | — | 電源を切る要求を受けても、表示だけにする |

## 実機で確かめること

- パネルの向き（`--rotate`）と、タッチの向きが画面と合うか
- フレームバッファの色の深さ（16 か 32 ビット。LVGL が合わせる）と、描き換えの速さ
- バックライトのデバイスの名前と、`max_brightness`
- CPU の負荷（1920 × 480 を回して描くので、ソフトウェアの回転が重くないか）
