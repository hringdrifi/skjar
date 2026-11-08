# os

Raspberry Pi OS Lite をベースにした OS の設定。カーネルとドライバは標準のまま、オーバーレイと `config.txt` の設定だけで、大体の機能が動くようにする。置く予定のもの：

- [config.txt](config.txt)（ディスプレイ、キー入力、RTC、音声、UART、シャットダウンの通知。書いた。実機では未確認）
- 専用のオーバーレイ（[overlays/](overlays/)。音声とキー入力の下書きあり）
- systemd のユニット（[systemd/skjar-ui.service](systemd/skjar-ui.service)：UI の起動。RP2040 との UART も UI が持つ）
- ルートを読み取り専用にする設定（Overlay File System）と、書き込みが要るデータのパーティション（ケーブルをいきなり抜かれても壊れないようにするため。[design-decisions.md](../../docs/design-decisions.md) の 1.5）
- イメージを作るスクリプト
