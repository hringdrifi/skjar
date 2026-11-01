# os

Raspberry Pi OS Lite をベースにした OS の設定。カーネルとドライバは標準のまま、オーバーレイと `config.txt` の設定だけで、大体の機能が動くようにする。置く予定のもの：

- `config.txt`（ディスプレイ、RTC、音声、キー入力などの `dtoverlay`）
- 専用のオーバーレイ（[overlays/](overlays/)。音声の下書きあり。キー入力の `i2c-gpio` ＋ `hid-over-i2c` は未作成）
- systemd のユニット（UI の起動、RP2040 との通信）
- ルートを読み取り専用にする設定（Overlay File System）と、書き込みが要るデータのパーティション（ケーブルをいきなり抜かれても壊れないようにするため。[design-decisions.md](../../docs/design-decisions.md) の 1.5）
- イメージを作るスクリプト
