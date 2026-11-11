# rp2040

RP2040 のファームウェア。[RMK](https://github.com/rmk-rs/rmk)（Rust、Embassy）で作る。担当する機能：

- キーマトリクス（7 行 × 6 列、42 キー）の読み取り
- PC への USB HID（物理キー、タッチキー、Consumer Control、マウス）
- CM0 への HID over I²C（物理キーを CM0 の Linux に普通のキーボードとして渡す。仕様は [docs/protocol.md](../../docs/protocol.md) の 2 章）
- キーボードモード（物理キーは PC へ）とサイバーデックモード（CM0 へ）の切り替えと、モードごとのキーマップ
- キーの RGB（SK6812MINI-E ×42。GPIO14 から PIO で出す。GPIO20 で LED の電源を入れ切りする）
- CC の電圧による電流宣言の判定と、電源の動作の決定（CM0 の GLOBAL_EN、LCD の負荷スイッチ、キーの RGB の明るさの上限）
- CM0 の USB ブート（rpiboot）の要求。キーの組み合わせで受ける（専用のスイッチはない）
- CM0 との UART 通信（[docs/protocol.md](../../docs/protocol.md) の 1 章）

RMK を選んだ理由は [docs/design-decisions.md](../../docs/design-decisions.md) の 3 章。書き込みは Port A から UF2 で行う。

## いまの状態（試作）

HID over I²C を確かめるための試作。基板ができる前に、Raspberry Pi Pico と Pi Zero 2 W（CM0 と同じ SoC）で試す。

| 機能 | 状態 |
|---|---|
| キーマトリクスと RMK のキーボード処理（レイヤー、タップ・ホールド） | 実装した。キーマップは仮（[src/keymap.rs](src/keymap.rs)） |
| HID over I²C のターゲット（[src/hid_i2c.rs](src/hid_i2c.rs)） | 実装した。実機では未確認 |
| CM0 との UART（[src/cm0_link.rs](src/cm0_link.rs)、[docs/protocol.md](../../docs/protocol.md) の 1 章） | 実装した。実機では未確認。`PWR` と `SHUTDOWN` はまだ送らない（CC と電源の判定がないため）。`BLMAX` は 255 固定、`LCD` は記録だけ |
| タッチキー（[src/reports.rs](src/reports.rs)） | 実装した。UART で届いたキーを、RMK の物理キーのレポートと合わせて送る（修飾キーは OR、キーは合わせて 6 個まで） |
| キーの RGB（[src/rgb.rs](src/rgb.rs)） | 隠し機能の動画に合わせて光らせるところだけ実装した。UART の `LEDF` を受けて、GPIO14 から PIO で SK6812 を駆動し、GPIO20 で LED の電源を入れ切りする。明るさは 1 色あたり 48/255 まで（全部白でも約 0.5 A）。普段の光らせ方と、電流宣言ごとの上限はまだ。Pico で試すときは GP14 / GP20 に WS2812 のテープ LED などをつなぐ。実機では未確認 |
| トラックパッド（UART の `PTR` / `WHEEL`） | 実装した。HID over I²C にマウスのレポート（ID 3）を足し、RMK のマウスキーと合わせて送る（ボタンは OR、動きはそのまま） |
| 画面のタッチキー（Keys タブの 2 段 × 12 キー） | 実装した。キーマップを 11 行にして、7〜10 行目をタッチキーにした（[src/keymap.rs](src/keymap.rs)）。UART の `TK` を RMK にキーの出来事として渡すので、レイヤーと Vial が効く。割り当てを `TKL` で CM0 に送る |
| タブを開くキーと、元のタブに戻すきっかけ | 実装した。RMK の `ActionEvent` を受け、USER キー（Vial の USER0〜3）で `USER`、文字や機能のキーで `ACT` を送る。受け取り口の数は [keyboard.toml](keyboard.toml) で増やしてある |
| モードの切り替え（[docs/protocol.md](../../docs/protocol.md) の 2.6） | 実装した。UART の `OUT,PC` / `OUT,CM0` で、RMK の既定のレイヤーを 0 / 3 にする（キーマップの 11 行目の `DF(0)` / `DF(3)` を押したことにする）。キーマップは 6 レイヤー：0〜2 がキーボードモード、3〜5 がサイバーデックモード（いまは 0〜2 の物理キーの写しで、タッチキーはなし）。CM0 が 3 秒黙ったらキーボードモードに戻す |
| USB | HID にせず、ログ用の仮想シリアルにしている。USB の状態（認識された・スリープした・リセット）を見て、PC がつながっているかを `USB` で CM0 に送る |
| PC への USB HID、電源、普段の RGB、CC | まだ。出力先は CM0 だけで、キーボードモード（`OUT,PC`）でも実際には CM0 に出す |

RMK は、USB がつながっているときだけ USB 用のチャンネル（`USB_REPORT_CHANNEL`）にレポートを出す。試作では USB の HID を使わないので、つながったことにして（`set_usb_state(Configured)`）、そのチャンネルを横取りして I²C に流している。USB の HID と並べるときは、レポートの行き先を切り替える仕組みが別に要る（RMK の改造か、上流への提案）。

### ビルドと書き込み

```sh
rustup target add thumbv6m-none-eabi
cargo install elf2uf2-rs
cargo build --release                                          # Pico 用（初期値）
cargo build --release --no-default-features --features xiao    # Seeed Studio XIAO RP2040 用
cargo build --release --no-default-features --features skjar   # SKJÁR の基板用
elf2uf2-rs target/thumbv6m-none-eabi/release/skjar-rp2040 target/skjar-rp2040.uf2   # ドラッグして書く UF2
```

BOOTSEL を押しながら USB につなぎ、`cargo run --release` で書き込む（`elf2uf2-rs -d` が UF2 に変えてコピーする）。書き込んだあとは、USB の仮想シリアル（COM ポート）にログが出る。

### Pico と Pi Zero 2 W のつなぎ方

キーマトリクスのピンは SKJÁR と同じ。I²C は、SKJÁR の GPIO24 / 25 が Pico では外に出ていないので、I2C1 の GPIO26 / 27 を使う。

| 信号 | Pico | Pi Zero 2 W | 備考 |
|---|---|---|---|
| `KB_SDA` | GP26（31 番ピン） | GPIO23（16 番ピン） | 4.7 kΩ で Pi の 3.3V へ |
| `KB_SCL` | GP27（32 番ピン） | GPIO24（18 番ピン） | 4.7 kΩ で Pi の 3.3V へ |
| `KB_INT` | GP13（17 番ピン） | GPIO25（22 番ピン） | 10 kΩ で Pi の 3.3V へ |
| `RP_TXD` | GP16（21 番ピン） | GPIO15 / RXD（10 番ピン） | 1 kΩ を直列に入れる（下記） |
| `CM0_TXD` | GP17（22 番ピン） | GPIO14 / TXD（8 番ピン） | |
| GND | GND | GND | |

- プルアップは Pi の 3.3V（1 番ピン）へつなぐ。SKJÁR と同じく、Pico 側の内部プルアップは使わない。
- キーは、列（COL、出力）と行（ROW、入力）をジャンパー線でつなぐと押したことになる。たとえば COL1（GP5）と ROW0（GP10）で `Q`。ダイオードを入れるなら、アノードを列、カソードを行に向ける。
- Pi 側の設定は [software/os/overlays/README.md](../../software/os/overlays/README.md) の「skjar-keyboard」と、[software/ui/README.md](../../software/ui/README.md) の「実機での設定」（UART）。
- UART のピンは SKJÁR と同じ（UART0、GPIO16 / 17）。SKJÁR では `RP_TXD` を CM0 の 3.3V で動くバッファに通すが、試作にはないので、Pi の電源を切ったまま Pico を動かすときに流れ込む電流を 1 kΩ で抑える。
- タッチ UI と組み合わせて試すときは、Pi で `skjar-ui` を動かす（`--link /dev/serial0`）。UI がなくても、Pi で `python3 -c` などから `$SK,KEY,07,1E,1*9B` と `$SK,KEY,07,1E,0*9C` を送れば、「1」が入力される（CRC は [software/ui/tools/fake_rp2040.py](../../software/ui/tools/fake_rp2040.py) の `frame()` で作れる）。

### XIAO RP2040 で試すとき

Seeed Studio XIAO RP2040 は外に出ているピンが 11 本（D0〜D10）だけなので、`--features xiao` でビルドする。I²C は Pico と同じ GPIO26 / 27。割り込みと UART は別のピンに移し、キーマトリクスは 2 行 × 4 列（8 キー）だけを試す。抵抗とプルアップのつなぎ方は、上の Pico と同じ。

| 信号 | XIAO RP2040 | Pi（Zero 2 W / 4B） | 備考 |
|---|---|---|---|
| `KB_SDA` | D0（GPIO26） | GPIO23（16 番ピン） | 4.7 kΩ で Pi の 3.3V へ |
| `KB_SCL` | D1（GPIO27） | GPIO24（18 番ピン） | 4.7 kΩ で Pi の 3.3V へ |
| `KB_INT` | D2（GPIO28） | GPIO25（22 番ピン） | 10 kΩ で Pi の 3.3V へ |
| `RP_TXD` | D6（GPIO0、TX） | GPIO15 / RXD（10 番ピン） | 1 kΩ を直列に入れる |
| `CM0_TXD` | D7（GPIO1、RX） | GPIO14 / TXD（8 番ピン） | |
| GND | GND | GND | |

キー（列と行をジャンパー線でつなぐと押したことになる。ダイオードを入れるなら、アノードを列、カソードを行に向ける）：

| | COL0：D9（GPIO4） | COL1：D3（GPIO29） | COL2：D10（GPIO3） | COL3：D8（GPIO2） |
|---|---|---|---|---|
| **ROW0：D4（GPIO6）** | Esc | Q | W | E |
| **ROW2：D5（GPIO7）** | Tab | A | S | D |

- 残りの行と列は、外に出ていない GPIO に割り当ててある（押されることはない）。
- キーの RGB（GPIO14 / 20）も外に出ていないので、XIAO では試せない。基板の RGB LED（GPIO11 / 12）と LED（GPIO16 / 17 / 25）は使わない。

## CM0 に電流を流し込まないための約束

CM0 は OFF になることがあるが、RP2040 は常に電源が入っている。CM0 につながるピンでは次を守る（理由は [docs/design-decisions.md](../../docs/design-decisions.md) の 3 章）。

- `KB_SDA` / `KB_SCL` / `KB_INT`（GPIO24 / 25 / 13）：High を出さない（`KB_INT` は出力値を 0 に固定し、出力の有効・無効の切り替えでオープンドレインを真似る）。内部プルアップを使わない（初期設定のプルダウンも切る）。
  - 試作のコードでは、`i2c_slave::Config` の `sda_pullup` / `scl_pullup` を false にし（embassy-rp の初期値は true）、`KB_INT` は `Flex` で出力値を Low にしたまま、出力と入力を切り替えている。
- `CM0_TXD` / `CM0_RUN_PG` / `CM0_HALT`（GPIO17 / 21 / 22）：内部のプルを使わない。
- キーの RGB の電源を切っているときは、`RGB_DATA`（GPIO14）を Low にしておく。

## キーマトリクスの向き

ダイオードは列 → 行（COL2ROW）。RMK の COL2ROW は、列を順に High にして、行をプルダウンで読む（QMK とは逆で、QMK は行を Low にして列をプルアップで読む）。どちらも同じダイオードの向きで動く。
