# overlays

CM0 用の Device Tree オーバーレイ。

## skjar-audio

[skjar-audio-overlay.dts](skjar-audio-overlay.dts)：TLV320DAC3100 を `simple-audio-card` でサウンドカード「skjar」にする。

- コーデック：I2C1（GPIO2 / 3）の 0x18。ドライバは本家の `tlv320aic31xx`（`ti,tlv320dac3100`）
- I2S：GPIO18 ← BCLK、GPIO19 ← WCLK、GPIO21 → DIN。**コーデックが BCLK と WCLK のマスター**（Pi は `i2s_clk_consumer`）
- MCLK：12 MHz の発振器（Y402）。`system-clock-frequency = <12000000>` でドライバに伝える
- リセット：GPIO22（Low でリセット）
- 電源：SPKVDD 5 V、AVDD / HPVDD / IOVDD 3.3 V、DVDD 1.8 V を `regulator-fixed` で定義
- イヤホン検出：GPIO17（Low で挿入）。`Headphones Jack` コントロールができ、挿入中だけイヤホン出力に電源が入る
- GPIO17 の内蔵プルダウン（リセット時の既定）を切る。基板の 100 kΩ プルアップと引き合うため
- スピーカー：`Internal Speaker Switch` を作る。イヤホン挿入中のミュートはユーザー空間でする（下記）

### ビルドと導入

CM0 で：

```sh
sudo apt install device-tree-compiler
dtc -@ -I dts -O dtb -o skjar-audio.dtbo skjar-audio-overlay.dts
sudo cp skjar-audio.dtbo /boot/firmware/overlays/
```

`/boot/firmware/config.txt`：

```
dtparam=i2c_arm=on
dtparam=i2s=on
dtparam=audio=off
dtoverlay=skjar-audio
dtoverlay=i2c-rtc,pcf8563
```

### クロックの構成

`simple-audio-card` は、コーデックの `set_sysclk()` を常に clk_id 0（MCLK）で呼ぶ。MCLK に 12 MHz の発振器をつなぎ、`system-clock-frequency` でその値を渡せば、ドライバ（`tlv320aic31xx.c`）は MCLK を PLL の入力にして、分周表から 44.1 / 48 kHz の設定を選ぶ。コーデックがマスターなので、BCLK も MCLK から作って出す（`hw_params` で BCLK の分周比を計算する）。マシンドライバもドライバのパッチも要らない。

- `mclk-fs` は書かない。書くと `hw_params` のたびにサンプリング周波数 × `mclk-fs` で `set_sysclk()` が呼ばれ、12 MHz と合わなくなる
- **当初の構成（MCLK を GND、BCLK から PLL）は使わない。** BCLK を PLL の入力に選ぶには、マシンドライバが `AIC31XX_PLL_CLKIN_BCLK` で `set_sysclk()` を呼ぶ必要があり、`simple-audio-card` ではできなかった

### 実機で確かめること

- 分周表（MCLK 12 MHz）：44.1 / 48 kHz とも、16 / 24 / 32 ビットのフレームで BCLK の分周比が割り切れる設定がある（48 kHz の 24 ビットは DOSR 96 × MDAC 5 = 480 = 48 × 10）。実機で `dmesg` の `pll ... bclk_n` を確かめる
- Pi の I2S がクロックを受ける側（`i2s_clk_consumer`）で、再生を始めたときに正しく同期するか
- イヤホン検出の極性：J402（PJ-320D-4A）はスイッチのない 4 極ジャック。S は 4 極目（スリーブ）の接点で、R2 が GND。3 極プラグではスリーブが R2 と S をまたいで S が GND に落ちる。CTIA の 4 極プラグでは S はマイク極に当たり、マイク素子を通して GND に落ちる（Low になる見込みだが未確認）。抜き差しの途中でチップ（`JACK_L`）が S に触れるので、150 ms のデバウンスで足りるか確かめる
- `tlv320dac3100-coeffs.bin`（フィルタ係数）がないと警告が出る。動作には関係ない

### ミキサー

`amixer -c skjar contents` で確認する。ドライバのソースで名前を確かめたもの：

| コントロール | 内容 |
|---|---|
| `DAC Playback Volume` | DAC の音量（左右） |
| `DAC Left Input` / `DAC Right Input` | `Off` / `Left Data` / `Right Data` / `Mono` |
| `Output Left From Left DAC` / `Output Right From Right DAC` | DAC → 出力ミキサー |
| `HP Left Switch` / `HP Right Switch` | 出力ミキサー → イヤホン |
| `HP Driver Playback Switch` / `HP Driver Playback Volume` | イヤホンドライバ（0〜9 dB） |
| `HP Analog Playback Volume` | イヤホンのアナログ音量 |
| `Speaker Switch` | 出力ミキサー（左）→ スピーカー |
| `Speaker Driver Playback Switch` / `Speaker Driver Playback Volume` | D 級アンプ（6〜24 dB） |
| `Speaker Analog Playback Volume` | スピーカーのアナログ音量 |
| `Internal Speaker Switch` | カード側のピンスイッチ（この .dts で作る） |
| `Headphones Jack` | イヤホン検出の状態（読むだけ） |

最初に試す例：

```sh
amixer -c skjar sset 'Output Left From Left DAC' on
amixer -c skjar sset 'Output Right From Right DAC' on
amixer -c skjar sset 'HP Left' on
amixer -c skjar sset 'HP Right' on
amixer -c skjar sset 'Speaker' on
amixer -c skjar sset 'HP Driver' on
amixer -c skjar sset 'Speaker Driver' on
```

- スピーカーは左の出力ミキサーにしかつながらない。左右を混ぜるには `DAC Left Input` を `Mono` にする（このときイヤホンの左も L+R になる）
- イヤホン挿入中のスピーカーのミュートは、`Headphones Jack` を見て `Internal Speaker Switch`（と必要なら `DAC Left Input`）を切り替える。ALSA UCM の設定か、小さなスクリプトでする（未作成）
