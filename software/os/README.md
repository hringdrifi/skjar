# os

Raspberry Pi OS Lite をベースにした OS の設定。カーネルとドライバは標準のまま、オーバーレイと `config.txt` の設定だけで、大体の機能が動くようにする。置く予定のもの：

- `config.txt`（ディスプレイ、RTC、音声、キー入力などの `dtoverlay`）
- 専用のオーバーレイ（[overlays/](overlays/)。音声の下書きあり。キー入力の `i2c-gpio` ＋ `hid-over-i2c` は未作成）
- systemd のユニット（UI の起動、RP2040 との通信）
- イメージを作るスクリプト
