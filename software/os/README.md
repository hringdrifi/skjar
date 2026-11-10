# os

Raspberry Pi OS Lite をベースにした OS の設定。カーネルとドライバは標準のまま、オーバーレイと `config.txt` の設定だけで、大体の機能が動くようにする。置く予定のもの：

- [config.txt](config.txt)（ディスプレイ、キー入力、RTC、音声、UART、シャットダウンの通知。書いた。実機では未確認）
- 専用のオーバーレイ（[overlays/](overlays/)。音声とキー入力の下書きあり）
- systemd のユニット（[systemd/skjar-ui@.service](systemd/skjar-ui@.service)：UI の起動。RP2040 との UART も UI が持つ。入れ方はファイルの先頭）と、コンソールから UI に戻るコマンド（[bin/skjar](bin/skjar)）
  - UI（キーボードモード）と tty1 のコンソール（サイバーデックモード）を行き来する。UI の System タブの Open console で UI が終わり、systemd が `getty@tty1` を立ち上げる。コンソールで `skjar` と打つと `skjar-ui@manual` が立ち上がり、tty1 を UI に戻す
  - 電源を入れたときは `skjar-ui@boot`：PC がつながっていなければ、UI を出さずにコンソールへ
  - ニュースの見出しを 15 分ごとに取ってくるタイマー（[systemd/skjar-news.timer](systemd/skjar-news.timer)、[systemd/skjar-news.service](systemd/skjar-news.service)）。UI の Widget strip の News が読む `news.txt` を書く。ジャンルはウィジェットで選び、選んだら [systemd/skjar-news.path](systemd/skjar-news.path) がすぐに取り直す。入れ方は service の先頭
  - 天気を 30 分ごとに取ってくるタイマー（[systemd/skjar-weather.timer](systemd/skjar-weather.timer)、[systemd/skjar-weather.service](systemd/skjar-weather.service)）。Weather が読む `weather.txt` を書く。場所はウィジェットで選び、選んだら [systemd/skjar-weather.path](systemd/skjar-weather.path) がすぐに取り直す。入れ方は service の先頭
- ルートを読み取り専用にする設定（Overlay File System）と、書き込みが要るデータのパーティション（ケーブルをいきなり抜かれても壊れないようにするため。[design-decisions.md](../../docs/design-decisions.md) の 1.5）
- イメージを作るスクリプト
