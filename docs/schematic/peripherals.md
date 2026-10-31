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
| J401 | スピーカーのコネクタ（2 ピン） | `Connector_Generic:Conn_01x02` | 未選定 | 未選定 |

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

## 3. シートをまたぐネット

- グローバルラベル：`I2C1_SDA` / `I2C1_SCL`（双方向）、`RTC_INT`（出力）、`I2S_BCLK` / `I2S_LRCLK`（出力）、`I2S_DOUT`、`CODEC_RST`（入力）、`HP_DET`（出力）
- 電源：`+5V`、`+3V3`、GND（電源シンボル）、`+3V3_CM0` / `+1V8_CM0`（電源シンボル、cm0 シートから）
