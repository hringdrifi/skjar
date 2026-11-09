# ui

CM0 で動く専用のタッチ UI。[LVGL](https://lvgl.io/) 9.6（C）で作る（選んだ理由は [docs/design-decisions.md](../../docs/design-decisions.md) の 4 章）。X も Wayland も使わず、フレームバッファ（`/dev/fb0`）に直接描き、タッチは evdev から読む。

## いまの状態（試作）

| 機能 | 状態 |
|---|---|
| 画面：480 × 1920 のパネルを 90° 回して、1920 × 480 の横長で使う | 実装した。向き（90° か 270° か）は実機で決める |
| 割り付け：ヘッダー（80 px）に時計・タブのボタン・状態の札、下の面（400 px、物理キーに近い側）にタブの中身 | 実装した（[docs/design-decisions.md](../../docs/design-decisions.md) の 4 章） |
| タッチキー（Keys）：2 段 × 12 キー、160 px（18.32 mm）ピッチ。割り当ては RMK のキーマップの 7〜10 行目（レイヤーで変わり、Vial で変えられる） | 実装した。押した・離したを番号で送り（`TK`）、表示は RP2040 から届く割り当て（`TKL`）。マルチタッチで同時押しできる |
| タブ：上のボタンと、Vial の USER0〜3 キーで開く。キー入力で元のタブに戻る（戻らない / キーで開いたときだけ / いつでも。System タブで選ぶ） | 実装した |
| メディアキー（Media） | 実装した。割り当ては固定（`KEY`） |
| トラックパッド（Pad）：1 本指で移動・叩いてクリック・叩いてすぐ動かしてドラッグ、2 本指でスクロール・叩いて右クリック、3 本指で叩いて中クリック、スクロールの帯、L / M / R ボタン | 実装した。UART の `PTR` / `WHEEL` で RP2040 に送る。実機（Linux）ではタッチパネルの evdev からマルチタッチを読む。実機のパネルでは未確認 |
| バックライトの明るさ（スライダー）と、RP2040 から届く上限 | 実装した |
| 物理キーの出力先の手動切り替え、CPU のグラフ、RP2040 の UF2、電源を切る（System） | 実装した。UF2 と電源は長押し |
| RP2040 との UART（[docs/protocol.md](../../docs/protocol.md) の 1 章。形式は案） | 実装した。RP2040 側も [firmware/rp2040/](../../firmware/rp2040/) にある |
| 確認したこと | Windows の窓で起動し、画面とボタンが動く。レイヤーの切り替え、USER キーでタブを開く、`ACT` で戻るを、標準入力から確かめた。テスト 3 つは Windows で通る。aarch64 Linux 向け（fbdev）はコンパイルとリンクまで。実機と、Linux の SDL の窓はまだ |

- 記号のラベルは US 配列のもの。PC 側が JIS 配列だと、Shift を使う記号（`@` など）は別の文字になる。
- 文字は英数字だけ。LVGL の Montserrat には日本語の字形がない。日本語を出すときは、フォントを足す。
- いまは root で動かす（`/dev/fb0`、`/dev/input`、`/dev/serial0`、`/sys/class/backlight` と `poweroff` を使うため）。専用のユーザーに分けるのは後で。

## ファイル

| ファイル | 内容 |
|---|---|
| [src/main.c](src/main.c) | 起動、描画先（fbdev、SDL、Windows）、タッチ、メインループ |
| [src/ui.c](src/ui.c) | 画面の中身。タッチキーの既定の割り当て（RP2040 から届くまでの表示）もここ |
| [src/keylabel.c](src/keylabel.c) | キーの動作の符号（`TKL`）をキーの表示にする |
| [src/settings.c](src/settings.c) | 設定（元のタブに戻るか）の保存 |
| [src/link.c](src/link.c) | RP2040 との UART のやり取り |
| [src/serial_port.c](src/serial_port.c) | UART の線（Linux の tty、Windows の COM ポート） |
| [src/platform.c](src/platform.c) | 時刻と待ち（Linux と Windows） |
| [src/trackpad.c](src/trackpad.c) | トラックパッドの判定（指の数と重心から、動き・クリック・スクロール） |
| [src/touch_source.c](src/touch_source.c) | 触れている指をすべて読む（Linux の evdev のマルチタッチ。ほかは LVGL のポインタ） |
| [src/backlight.c](src/backlight.c) | `/sys/class/backlight` の明るさ |
| [src/sysinfo.c](src/sysinfo.c) | CPU・メモリ・温度 |
| [lv_conf.h](lv_conf.h) | LVGL の設定 |
| [tests/link_test.c](tests/link_test.c) | UART の行の組み立てと読み取りのテスト |
| [tests/trackpad_test.c](tests/trackpad_test.c) | トラックパッドの判定のテスト |
| [tests/keylabel_test.c](tests/keylabel_test.c) | キーの表示のテスト |
| [tools/fake_rp2040.py](tools/fake_rp2040.py) | RP2040 の代わりに UART のやり取りをする道具（Linux 用） |
| [tools/build_windows.py](tools/build_windows.py) | Windows 向けのビルド（zig を使う） |

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

### PC の窓で試す（Windows）

CMake も Visual Studio も要らない。Python と git があればよい。C コンパイラには zig（PyPI の `ziglang`）を使う。

```bat
py -m pip install ziglang
py tools/build_windows.py --test
build-windows\skjar-ui.exe
```

- 初回は LVGL を `.deps/lvgl` に取ってきて、全部をコンパイルする（数分）。2 回目からは変わったファイルだけ。
- 窓は 1920 × 480。画面に収まらないときは `--zoom 75` や `--zoom 50` で縮める（Windows の拡大率の設定には従わない）。
- RP2040 とはつながないので、送る行（`$SK,TK,...`、`$SK,PTR,...`）がコンソールに出る。タッチキーは、ファームの既定の割り当て（レイヤー 0〜2）で表示する。
- **RP2040 の代わりに、コンソールに行を打てる**（`--link -` のとき）。中身だけを打てば CRC は付け足す：
  - `LAYER,1` / `LAYER,2` / `LAYER,0`：レイヤーを切り替えたことにする（タッチキーの表示が変わる）
  - `USER,1,1`：USER1 キー（Pad を開く）を押したことにする。`USER,0,1` は Keys、`USER,2,1` は Media、`USER,3,1` は System
  - `ACT`：文字のキーを押したことにする（設定によって元のタブに戻る）
  - `PWR,B,1.5A`、`BLMAX,100`、`OUT,PC,MAN` なども同じように打てる
- Pad タブはマウスで試せる。マウスは 1 本指で、**Shift を押しながら操作すると 2 本指、Ctrl を押しながらだと 3 本指**として扱う（2 本指のスクロールや右クリックを試すため）。Windows では LVGL のポインタ（1 本）しか読まないので、タッチ画面の PC でも 2 本指は Shift で代わりにする。
- USB シリアルの変換器で本物の RP2040 とつなぐときは `--link COM5` のように COM ポートを渡す。
- 電源を切るボタンは、PC で試すときは表示だけ（PC の電源は切らない）。
- CMake と MinGW / Visual Studio があれば、`cmake -B build-win` でもビルドできる（Windows では描画先が自動で `windows` になる）。

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
| `--link <path>` | `/dev/serial0`（SDL と Windows では `-`） | RP2040 との UART（Windows では `COM5` など）。`-` なら開かずに、送る行を標準エラーに出す |
| `--rotate <0/90/180/270>` | 90（SDL と Windows では 0） | 画面の回転（fbdev のみ） |
| `--fb <path>` | `/dev/fb0` | フレームバッファ |
| `--touch <path>` | 自動で探す | タッチの evdev。探すときは、座標が絶対値のもの（タッチパネル）だけを使う |
| `--vt <path>` | `/dev/tty1` | グラフィックモードにする仮想端末。`-` で触らない |
| `--zoom <%>` | 100 | 窓の大きさ（SDL と Windows のみ） |
| `--config <path>` | Linux：`~/.config/skjar/ui.conf`、Windows：`%APPDATA%\skjar\ui.conf` | 設定のファイル（元のタブに戻るか） |
| `--no-poweroff` | SDL と Windows では常に有効 | 電源を切る要求を受けても、表示だけにする |

## 実機で確かめること

- パネルの向き（`--rotate`）と、タッチの向きが画面と合うか
- フレームバッファの色の深さ（16 か 32 ビット。LVGL が合わせる）と、描き換えの速さ
- バックライトのデバイスの名前と、`max_brightness`
- CPU の負荷（1920 × 480 を回して描くので、ソフトウェアの回転が重くないか）
- タッチパネルのマルチタッチ（evdev で `ABS_MT_SLOT` が出るか、何本まで取れるか）と、Pad の指の座標が画面と合うか
- トラックパッドの速さ・加速・スクロールの量（`trackpad.c` の先頭の定数）
