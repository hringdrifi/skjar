# rp2040

RP2040 のファームウェア。担当する機能：

- PC への HID（物理キー、タッチキー、Consumer Control、マウス）
- CC の電圧による電流宣言の判定と、電源の動作の決定（CM0 の GLOBAL_EN、LCD の負荷スイッチ）
- CM0 との UART 通信（[docs/protocol.md](../../docs/protocol.md)）

QMK を使うか、pico-sdk と TinyUSB で自作するかは、まだ決めていない。書き込みは Port A から UF2 で行う。
