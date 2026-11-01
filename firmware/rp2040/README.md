# rp2040

RP2040 のファームウェア。担当する機能：

- キーマトリクス（7 行 × 6 列、42 キー）の読み取り
- PC への USB HID（物理キー、タッチキー、Consumer Control、マウス）
- CM0 への HID over I²C（物理キーを CM0 の Linux に普通のキーボードとして渡す。仕様は [docs/protocol.md](../../docs/protocol.md) の 2 章）
- 物理キーの出力先（PC か CM0 か）の切り替え
- キーの RGB（SK6812MINI-E ×42。GPIO14 から PIO で出す。GPIO20 で LED の電源を入れ切りする）
- CC の電圧による電流宣言の判定と、電源の動作の決定（CM0 の GLOBAL_EN、LCD の負荷スイッチ、キーの RGB の明るさの上限）
- CM0 の USB ブート（rpiboot）の要求。キーの組み合わせで受ける（専用のスイッチはない）
- CM0 との UART 通信（[docs/protocol.md](../../docs/protocol.md) の 1 章）

QMK を使うか、pico-sdk と TinyUSB で自作するかは、まだ決めていない。QMK は I²C のターゲットにも HID over I²C にも対応していないので、どちらにしてもその部分は自作になる。書き込みは Port A から UF2 で行う。

## CM0 に電流を流し込まないための約束

CM0 は OFF になることがあるが、RP2040 は常に電源が入っている。CM0 につながるピンでは次を守る（理由は [docs/design-decisions.md](../../docs/design-decisions.md) の 3 章）。

- `KB_SDA` / `KB_SCL` / `KB_INT`（GPIO24 / 25 / 13）：High を出さない（`KB_INT` は出力値を 0 に固定し、出力の有効・無効の切り替えでオープンドレインを真似る）。内部プルアップを使わない（初期設定のプルダウンも切る）。
- `CM0_TXD` / `CM0_RUN_PG` / `CM0_HALT`（GPIO17 / 21 / 22）：内部のプルを使わない。
- キーの RGB の電源を切っているときは、`RGB_DATA`（GPIO14）を Low にしておく。
