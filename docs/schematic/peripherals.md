# 周辺シート（peripherals.kicad_sch）

**手本：** [peripherals-reference.pdf](peripherals-reference.pdf)（画像は [peripherals-reference.png](peripherals-reference.png)）。

## 1. RTC

| 記号 | 部品 | シンボル | LCSC |
|---|---|---|---|
| U401 | PCF8563T（SO-8） | `Timer_RTC:PCF8563T` | C7440（Preferred） |
| Y401 | 32.768 kHz（JGHC S3132768092070、負荷容量 9 pF。フットプリント `Crystal:Crystal_SMD_3215-2Pin_3.2x1.5mm`） | `Device:Crystal` | C479190（Extended、手持ち） |
| D401 | BAT54C | `Diode:BAT54C` | Extended |
| BT401 | CR2032 ホルダー MY-2032-08 | `Device:Battery_Cell` | C964831 |

| 接続 | 内容 |
|---|---|
| 電源 | D401 の 2 つのアノードに `+3V3` と `VBAT`、カソード → `VDD_RTC`（電源記号。`power:VDD` の値を書き換えたもの） → PCF8563 の VDD（100 nF） |
| 水晶 | OSCI / OSCO の間に Y401。外付けの負荷コンデンサはなし（PCF8563 が内蔵） |
| I²C | SDA / SCL → `I2C1_SDA` / `I2C1_SCL`（CM0 の内部プルアップを使う） |
| INT | → `RTC_INT`、10 kΩ で `+3V3_CM0` にプルアップ（CM0 が OFF なら 0V になり、逆電圧にならない） |
| CLKOUT | 未接続（ソフトウェアで止める） |

- 水晶は IC のすぐ横に置き、配線を短く。周りを GND で囲む。

## 2. 音声（TLV320DAC3100）

| 記号 | 部品 | シンボル | フットプリント | LCSC |
|---|---|---|---|---|
| U402 | TLV320DAC3100IRHBR | `Skjar:TLV320DAC3100`（自作） | `Package_DFN_QFN:Texas_RHB0032E_VQFN-32-1EP_5x5mm_P0.5mm_EP3.45x3.45mm` | C179677 |
| FB401 | フェライトビーズ 600 Ω | `Device:FerriteBead` | 0805 | C1017（Basic） |
| C402 | 22 µF 25 V | `Device:C` | 0805 | C45783（Basic） |
| C403〜C411 | 100 nF / 10 µF（各電源ピン） | `Device:C` | 0603 | C14663 / C96446 |
| C412 / C413 | 220 µF 16 V 電解 | `Device:C_Polarized` | `CP_Elec_6.3x7.7` | C970695 |
| R403 / R406 | 100 kΩ | `Device:R` | 0603 | C25803 |
| R404 / R405 | 10 kΩ | `Device:R` | 0603 | C25804 |
| Y402 | 12 MHz 発振器（3.3 V、CMOS 出力、3225 4 ピン。仮に YXC OT2EL89CJI-111YLC-12M） | `Oscillator:ASE-xxxMHz` | `Oscillator:Oscillator_SMD_Abracon_ASE-4Pin_3.2x2.5mm` | C5280582（Extended、仮） |
| C414 | 100 nF（Y402 のパスコン） | `Device:C` | 0603 | C14663 |
| J402 | 3.5 mm ジャック（4 極） | `Connector_Audio:AudioJack4` | `Jack_3.5mm_KoreanHropartsElec_PJ-320D-4A_Horizontal` | C95562 |
| J401 | スピーカーのコネクタ（2 ピン、1.25 mm。スピーカー 3525BOX-4Ω3W-2P1.25、C49247004 のプラグ用。挿さるかは実物で確認） | `Connector_Generic:Conn_01x02` | `Connector_Molex:Molex_PicoBlade_53261-0271_1x02-1MP_P1.25mm_Horizontal` | C177225（仮） |

| 接続 | 内容 |
|---|---|
| 電源 | SPKVDD（21, 24）→ `+5V`（22 µF ＋ 100 nF × 2）。AVDD（17）・HPVDD（28）→ `AVDD_CODEC`（`+3V3_CM0` から FB401。10 µF ＋ 100 nF × 2）。DVDD（3）→ `+1V8_CM0`（10 µF ＋ 100 nF）。IOVDD（2）→ `+3V3_CM0`（10 µF ＋ 100 nF） |
| GND | IOVSS（1）、DVSS（18）、AVSS（16）、HPVSS（29）、SPKVSS（20, 25）、裏面パッド（33） |
| I²S | DIN（5）← `I2S_DOUT`、BCLK（7）→ `I2S_BCLK`、WCLK（6）→ `I2S_LRCLK`（コーデックがクロックを出す側。CM0 は受ける側） |
| MCLK（8） | ← Y402（12 MHz 発振器）の出力。Y402 の VDD と EN（1 番）は `+3V3_CM0`、パスコン C414 |
| I²C | SDA（9）/ SCL（10）↔ `I2C1_SDA` / `I2C1_SCL`（アドレス 0x18） |
| RESET（31） | ← `CODEC_RST`（CM0 の GPIO22）。R406 でプルダウン |
| イヤホン | HPL（27）→ C412 → `JACK_L` → J402 の T。HPR（30）→ C413 → `JACK_R` → J402 の R1。`JACK_L` / `JACK_R` は R404 / R405 で GND へ |
| ジャック | R2 → GND。S → `HP_DET`（R403 で `+3V3_CM0` にプルアップ。CM0 の GPIO17） |
| スピーカー | SPKP（22, 26）→ `SPK+`、SPKM（19, 23）→ `SPK-` → J401 |
| 未使用 | GPIO1（32）、AIN1（13）、AIN2（14）、VOL/MICDET（11）、MICBIAS（12）、NC（4, 15）は未接続 |

- `AVDD_CODEC` はフェライトビーズの先なので、PWR_FLAG を付けている。
- 配置：U402、C412 / C413、J402 は、DSI の配線、Wi-Fi のアンテナ、スピーカーの出力から離す。裏面パッドは、アナログ側の GND に複数のビアでつなぐ（データシートのレイアウトの指針）。

## 3. マイク（ICS-43434）

クラウドの AI と声で会話するため（2026-11-18）。

| 記号 | 部品 | シンボル | フットプリント | LCSC |
|---|---|---|---|---|
| MK401 | TDK InvenSense ICS-43434（I²S 出力の MEMS マイク、音の穴が下面のボトムポート、LGA-6） | `Sensor_Audio:ICS-43434` | `Sensor_Audio:InvenSense_ICS-43434-6_3.5x2.65mm` | C5656610 |
| C415 | 100 nF（VDD のパスコン） | `Device:C` | 0603 | C14663 |
| R407 | 100 kΩ（SD → GND。出力していない半分の間、線を落ち着かせる。データシートの推奨） | `Device:R` | 0603 | C25803 |

| 接続 | 内容 |
|---|---|
| 電源 | VDD（5）→ `+3V3_CM0`（C415）。CM0 の GPIO20 につながるので、CM0 と一緒に ON / OFF する電源にする |
| GND | GND（3） |
| I²S | SCK（4）← `I2S_BCLK`、WS（1）← `I2S_LRCLK`（どちらも U402 が出すクロックを共有）。SD（6）→ `I2S_DIN` → CM0 の GPIO20（PCM_DIN） |
| L/R（2） | GND（左チャンネルで出す） |

- **クロック：** ICS-43434 は 1 フレームに BCLK 64 回（32 ビット × 2）が要る。U402 に BCLK = 64 fs を出させる（48 kHz なら 3.072 MHz。マイクの範囲内）。再生と録音は同じサンプリングレートになる。
- **置き場所：** 裏面に付け、基板に音の穴（φ0.8〜1.0 mm 程度。フットプリントとデータシートで決める）を開けて、表側（キーの側）の音を拾う。スピーカーから遠い、ディスプレイの南東の角あたり。声はキーの開口部の上の端からディスプレイの裏の空間を通って届く。穴の真上にスイッチを置かない。穴の上下に配線を通さない。
- **はんだ付け：** LGA なので熱風かホットプレート。音の穴にフラックスが入らないよう、ペーストは少なめにする。

## 4. シートをまたぐネット

- グローバルラベル：`I2C1_SDA` / `I2C1_SCL`（双方向）、`RTC_INT`（出力）、`I2S_BCLK` / `I2S_LRCLK`（出力）、`I2S_DIN`（出力、マイク）、`I2S_DOUT`、`CODEC_RST`（入力）、`HP_DET`（出力）
- 電源：`+5V`、`+3V3`、GND（電源シンボル）、`+3V3_CM0` / `+1V8_CM0`（電源シンボル、cm0 シートから）
