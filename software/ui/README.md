# ui

CM0 で動く専用のタッチ UI。[LVGL](https://lvgl.io/) 9.6（C）で作る（選んだ理由は [docs/design-decisions.md](../../docs/design-decisions.md) の 4 章）。X も Wayland も使わず、フレームバッファ（`/dev/fb0`）に直接描き、タッチは evdev から読む。

## いまの状態（試作）

| 機能 | 状態 |
|---|---|
| 画面：480 × 1920 のパネルを 90° 回して、1920 × 480 の横長で使う | 実装した。向き（90° か 270° か）は実機で決める |
| 割り付け：ヘッダー（80 px）に時計・タブのボタン・状態の札、下の面（400 px、物理キーに近い側）にタブの中身 | 実装した（[docs/design-decisions.md](../../docs/design-decisions.md) の 4 章） |
| タッチキー（Keys）：2 段 × 12 キー、160 px（18.32 mm）ピッチ。割り当ては RMK のキーマップの 7〜10 行目（レイヤーで変わり、Vial で変えられる） | 実装した。押した・離したを番号で送り（`TK`）、表示は RP2040 から届く割り当て（`TKL`）。マルチタッチで同時押しできる |
| タブ：上のボタンと、Vial の USER0〜3 キーで開く。キー入力で元のタブに戻る（戻らない / キーで開いたときだけ / いつでも。System タブで選ぶ） | 実装した |
| Widget strip（Keys タブのタッチキーの上、1920 × 80 px）：左右にスワイプして切り替える。None（初期値）と、System タブで有効にしたもの（ポモドーロ・タイマー、ニュースの電光掲示板、打鍵の速さ、天気） | 実装した（[src/widgets.c](src/widgets.c)。下の「Widget strip」）。Windows の窓で、スワイプ、タイマーの開始、ニュースが流れる、打鍵の速さ（標準入力の `ACT`）、天気の表示を確かめた |
| メディアキー（Media） | 実装した。割り当ては固定（`KEY`） |
| 隠し機能：ヘッダーの時計を 5 回叩くと、動画を全画面で流し、キーのライトも合わせて光らせる。叩くと止まる | 実装した（[src/movie.c](src/movie.c)）。動画は自分で用意して [tools/make_movie.py](tools/make_movie.py) で変換する（下の「隠し機能の動画」） |
| 見た目（試し）：背景の画像と、半透明のボタン・パネル（すりガラス風）。System タブの Look で Plain / Picture を選ぶ | 実装した。画像は `--background` の PNG か、なければ起動時に描くオーロラ風の絵 |
| トラックパッド（Pad）：1 本指で移動・叩いてクリック・叩いてすぐ動かしてドラッグ、2 本指でスクロール・叩いて右クリック、3 本指で叩いて中クリック、スクロールの帯、L / M / R ボタン | 実装した。UART の `PTR` / `WHEEL` で RP2040 に送る。実機（Linux）ではタッチパネルの evdev からマルチタッチを読む。実機のパネルでは未確認 |
| バックライトの明るさ（スライダー）と、RP2040 から届く上限 | 実装した |
| サイバーデックモードへ（System の Open console で UI を閉じる。コンソールで `skjar` と打つと戻る）、CPU のグラフ、RP2040 の UF2、電源を切る（System） | 実装した。UF2 と電源は長押し |
| PC につないでいないとき（RP2040 から `USB,0`）に「Connect SKJAR to a PC」と Open console のボタンを重ねて出す。`--start boot` では、PC がなければ UI を出さずにコンソールへ | 実装した。標準入力の `USB,0` / `USB,1` で確かめた |
| RP2040 との UART（[docs/protocol.md](../../docs/protocol.md) の 1 章。形式は案） | 実装した。RP2040 側も [firmware/rp2040/](../../firmware/rp2040/) にある |
| 確認したこと | Windows の窓で起動し、画面とボタンが動く。レイヤーの切り替え、USER キーでタブを開く、`ACT` で戻るを、標準入力から確かめた。テスト 3 つは Windows で通る。aarch64 Linux 向け（fbdev）はコンパイルとリンクまで。実機と、Linux の SDL の窓はまだ |

- 記号のラベルは US 配列のもの。PC 側が JIS 配列だと、Shift を使う記号（`@` など）は別の文字になる。
- 文字は、ニュースの電光掲示板のほかは英数字だけ。LVGL の Montserrat には日本語の字形がない。電光掲示板だけ、日本語の TTF フォントを読んで描く（下の「Widget strip」）。
- いまは root で動かす（`/dev/fb0`、`/dev/input`、`/dev/serial0`、`/sys/class/backlight` と `poweroff` を使うため）。専用のユーザーに分けるのは後で。

## ファイル

| ファイル | 内容 |
|---|---|
| [src/main.c](src/main.c) | 起動、描画先（fbdev、SDL、Windows）、タッチ、メインループ |
| [src/ui.c](src/ui.c) | 画面の中身。タッチキーの既定の割り当て（RP2040 から届くまでの表示）もここ |
| [src/keylabel.c](src/keylabel.c) | キーの動作の符号（`TKL`）をキーの表示にする |
| [src/settings.c](src/settings.c) | 設定（元のタブに戻るか）の保存 |
| [src/background.c](src/background.c) | 背景の画像（PNG を読むか、オーロラ風の絵を描く） |
| [src/movie.c](src/movie.c) | 隠し機能の動画（再生、キーのライトの見本、音） |
| [src/icons.c](src/icons.c) | LVGL の記号にないアイコン（明るさの太陽、見た目のパレット、天気）を、起動時に描く |
| [src/link.c](src/link.c) | RP2040 との UART のやり取り |
| [src/serial_port.c](src/serial_port.c) | UART の線（Linux の tty、Windows の COM ポート） |
| [src/platform.c](src/platform.c) | 時刻と待ち（Linux と Windows） |
| [src/trackpad.c](src/trackpad.c) | トラックパッドの判定（指の数と重心から、動き・クリック・スクロール） |
| [src/touch_source.c](src/touch_source.c) | 触れている指をすべて読む（Linux の evdev のマルチタッチ。ほかは LVGL のポインタ） |
| [src/backlight.c](src/backlight.c) | `/sys/class/backlight` の明るさ |
| [src/sysinfo.c](src/sysinfo.c) | CPU・メモリ・温度 |
| [src/widgets.c](src/widgets.c) | Widget strip（スワイプでの切り替え、ポモドーロ・タイマー、ニュースの電光掲示板、打鍵の速さ、天気） |
| [lv_conf.h](lv_conf.h) | LVGL の設定 |
| [tests/link_test.c](tests/link_test.c) | UART の行の組み立てと読み取りのテスト |
| [tests/trackpad_test.c](tests/trackpad_test.c) | トラックパッドの判定のテスト |
| [tests/keylabel_test.c](tests/keylabel_test.c) | キーの表示のテスト |
| [tools/fake_rp2040.py](tools/fake_rp2040.py) | RP2040 の代わりに UART のやり取りをする道具（Linux 用） |
| [tools/make_movie.py](tools/make_movie.py) | 隠し機能の動画を作る（手元の動画 → .skm と .wav。ffmpeg を使う） |
| [tools/fetch_news.py](tools/fetch_news.py) | ニュースの見出しを RSS / Atom から取ってきて `news.txt` にする（Python の標準ライブラリだけ） |
| [tools/fetch_weather.py](tools/fetch_weather.py) | 天気を Open-Meteo から取ってきて `weather.txt` にする（Python の標準ライブラリだけ） |
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
  - `USB,0`：PC が外れたことにする（「PC につないで」が出る）。`USB,1` で消える
  - `PWR,B,1.5A`、`BLMAX,100`、`OUT,PC,MAN` なども同じように打てる
- Pad タブはマウスで試せる。マウスは 1 本指で、**Shift を押しながら操作すると 2 本指、Ctrl を押しながらだと 3 本指**として扱う（2 本指のスクロールや右クリックを試すため）。Windows では LVGL のポインタ（1 本）しか読まないので、タッチ画面の PC でも 2 本指は Shift で代わりにする。
- USB シリアルの変換器で本物の RP2040 とつなぐときは `--link COM5` のように COM ポートを渡す。
- 電源を切るボタンは、PC で試すときは表示だけ（PC の電源は切らない）。
- CMake と MinGW / Visual Studio があれば、`cmake -B build-win` でもビルドできる（Windows では描画先が自動で `windows` になる）。

### テスト

```sh
ctest --test-dir build
```

## Widget strip

Keys タブの、ヘッダーとタッチキーの間の帯（1920 × 80 px）。ウィジェットを 1 つ出し、左右にスワイプして切り替える。

- **None**（何も出さない。初期値）の右に、System タブの「Widget strip」で有効にしたものが並ぶ（いくつでも選べる。`ui.conf` に `widgets=pomodoro,news` のように保存）。並びは輪になっていて、いちばん右から左へはらうと None に戻る（None から右へはらうと、いちばん右へ）。1 つも有効にしていなければ、スワイプしても何も起きない。
- 帯の中のボタンは、指で押した・離したをこの帯で決める。横に 40 px 動くとスワイプになり、ボタンは押されない。160 px 以上はらうと次のウィジェットへ移る。
- **Pomodoro：** 集中 25 分、休憩 5 分、4 回ごとに長い休憩 15 分。▶ / ⏸、⏭（次の区切りへ飛ばす）、⟳（最初から）。区切りが終わったら止めて待ち、画面に文字を出して、キーのライトを点滅させる（`LEDF`）。ほかのウィジェットやタブにしていても数え続ける。
- **Typing：** 打鍵の速さ。RP2040 の `ACT`（文字や機能のキーが押された）を数え、直近 15 秒の WPM（5 打鍵を 1 語）、今日の打鍵数と最高の WPM、直近 2 分のグラフ（1 本 5 秒）を出す。修飾キーやレイヤーキーは数えない。PC で試すときは、標準入力に `ACT` を続けて打つ。
- **Weather：** 設定のフォルダの `weather.txt` を読んで、いまの天気と気温、今日の最高・最低・降水確率、3 時間おきの 4 つ先まで、あしたを出す。30 秒ごとにファイルが変わったかを見る。
  - 天気は [tools/fetch_weather.py](tools/fetch_weather.py) で取ってくる（Open-Meteo。API キー不要、個人利用は無料）。場所は `--city Sapporo` か `--lat` / `--lon`（初期値は東京）。実機では systemd のタイマーで 30 分ごと（[software/os/systemd/skjar-weather.timer](../os/systemd/skjar-weather.timer)）。
- 最後に出していたウィジェットは `ui.conf` に `widget=weather` のように覚えておき、次の起動でもそれから出す。
- **News：** 設定のフォルダ（`ui.conf` と同じ所）の `news.txt` の見出し（1 行に 1 つ）を、右から左へ流し続ける。30 秒ごとにファイルが変わったかを見て読み直す。左の札の時刻は、`news.txt` を書いた時刻。
  - 見出しは [tools/fetch_news.py](tools/fetch_news.py) で取ってくる。初期値は NHK の主要ニュースの RSS。`--feed nhk-business` のような名前（NHK の各ジャンル、`bbc`）か、RSS / Atom の URL で変える（何度でも書ける）。NHK の RSS は個人で使う範囲で。
  - **日本語は、電光掲示板だけ TTF のフォントで描く**（LVGL の Tiny TTF。起動時にフォントのファイルを丸ごと読む）。フォントは `--font` で渡すか、決まった場所を探す：実機（Linux）は IPA ゴシック（`sudo apt install fonts-ipafont-gothic`）か Noto Sans CJK（`fonts-noto-cjk`）、Windows は BIZ UDゴシック・游ゴシック・メイリオ。どれもなければ英数字のフォントで、ASCII の見出しだけを出す（`fetch_news.py --ascii` で、英語の見出しを ASCII にして書ける）。
  - 実機では systemd のタイマーで 15 分ごとに回す（[software/os/systemd/skjar-news.timer](../os/systemd/skjar-news.timer)）。
  - Windows で試すとき：

```bat
py tools/fetch_news.py
```

## 隠し機能の動画

ヘッダーの時計を続けて 5 回叩くと、動画が全画面で流れ、キーのライトも映像に合わせて光る。画面のどこかを叩くと止まる（始まって 1 秒は止まらない）。

- **動画は自分で用意する。** リポジトリには入れない（作品の著作権のため。`.skm` と `.wav` は `.gitignore` で外してある）。
- 手元の動画を [tools/make_movie.py](tools/make_movie.py) で変換する（ffmpeg が要る）。白黒の影絵のような動画が向いている。

```bat
py tools/make_movie.py 手元の動画.mp4 movie
```

- できた `movie.skm` と `movie.wav` を、設定と同じフォルダ（Windows：`%APPDATA%\skjar`、Linux：`~/.config/skjar`）に置くか、`--movie <path>.skm` で渡す。音は同じ名前の `.wav`（なければ映像だけ）。
- **スクリーンとキーボードを 1 枚の画面に見立てて、動画を上下に分けて映す。** 実物の位置関係（スクリーンの表示領域と、キーの位置）のとおりに、動画を枠（横はキーボードの幅、縦はスクリーンの上端からキーボードの下端まで）に重ねる。動画は全体が枠に収まるように合わせる（`--fit cover` なら枠を覆って、はみ出しを切る）。
  - 上（スクリーン）：表示領域にあたる部分を 960 × 240、4 段階の濃淡、30 fps で切り出し、縦横 2 倍にして画面いっぱいに出す。
  - 下（キーボード）：各キーの位置にあたる明るさ（16 段階）を、コマごとに UART の `LEDF` で RP2040 に送る。
  - RP2040 とつながっていないとき（PC で試すとき）は、左下にキーのライトの見本（42 キーの並び）を小さく重ねる。
- 音：Linux は `aplay`、Windows は PlaySound で鳴らす。音を鳴らし始めた時刻からの経過時間で、コマを決める。

## 実機での設定

1. OS の設定：[software/os/config.txt](../os/config.txt) を `/boot/firmware/config.txt` に入れる（使うオーバーレイは [software/os/overlays/](../os/overlays/) でビルドする）。
2. UART：`raspi-config` の Interface Options → Serial Port で、ログインシェルを「いいえ」、ハードウェアを「はい」にする。カーネルのログ（`console=serial0,115200`）は残してよい。UI は、ログが混ざった行を読み飛ばす。
3. 起動：[software/os/systemd/skjar-ui@.service](../os/systemd/skjar-ui@.service) と [software/os/bin/skjar](../os/bin/skjar) を入れ、`skjar-ui@boot` を有効にする（入れ方はユニットの先頭）。UI が動いている間は `getty@tty1` を止め、UI が tty1 をグラフィックモードにして、コンソールの文字が画面に出ないようにする。UI が終わると（Open console）、tty1 にログインのコンソールが戻る。

起動のオプション：

| オプション | 初期値 | 内容 |
|---|---|---|
| `--link <path>` | `/dev/serial0`（SDL と Windows では `-`） | RP2040 との UART（Windows では `COM5` など）。`-` なら開かずに、送る行を標準エラーに出す |
| `--rotate <0/90/180/270>` | 90（SDL と Windows では 0） | 画面の回転（fbdev のみ） |
| `--fb <path>` | `/dev/fb0` | フレームバッファ |
| `--touch <path>` | 自動で探す | タッチの evdev。探すときは、座標が絶対値のもの（タッチパネル）だけを使う |
| `--vt <path>` | `/dev/tty1` | グラフィックモードにする仮想端末。`-` で触らない |
| `--zoom <%>` | 100 | 窓の大きさ（SDL と Windows のみ） |
| `--background <png>` | 起動時に描くオーロラ風の絵 | 背景の画像（Look が Picture のとき）。1920 × 480 だと描き換えが軽い。ほかの大きさは画面を覆うように拡大・縮小する |
| `--movie <skm>` | 設定のフォルダの `movie.skm` | 隠し機能の動画 |
| `--config <path>` | Linux：`~/.config/skjar/ui.conf`、Windows：`%APPDATA%\skjar\ui.conf` | 設定のファイル（元のタブに戻るか、見た目、Widget strip に出すもの）。ニュースの `news.txt` と隠し機能の動画も、このファイルと同じフォルダから読む |
| `--font <ttf>` | 決まった場所を探す（上の「Widget strip」） | ニュースの電光掲示板の日本語のフォント（TTF / OTF / TTC。TTC は 1 つ目の書体） |
| `--start <boot/manual>` | `manual` | `boot`：電源を入れたときの起動。RP2040 から PC がつながっているかを 3 秒まで待ち、なければ UI を閉じてコンソールへ。`manual`：コンソールで `skjar` と打ったとき。PC がなければ「PC につないで」を出す |
| `--no-poweroff` | SDL と Windows では常に有効 | 電源を切る要求を受けても、表示だけにする |

## 実機で確かめること

- パネルの向き（`--rotate`）と、タッチの向きが画面と合うか
- フレームバッファの色の深さ（16 か 32 ビット。LVGL が合わせる）と、描き換えの速さ
- バックライトのデバイスの名前と、`max_brightness`
- CPU の負荷（1920 × 480 を回して描くので、ソフトウェアの回転が重くないか）
- タッチパネルのマルチタッチ（evdev で `ABS_MT_SLOT` が出るか、何本まで取れるか）と、Pad の指の座標が画面と合うか
- トラックパッドの速さ・加速・スクロールの量（`trackpad.c` の先頭の定数）
- 背景の画像（Look が Picture）のときの描き換えの速さ。キーを押すたびに、背景ごと重ねて描き直す（画面を回して描くので、Plain より重い）
