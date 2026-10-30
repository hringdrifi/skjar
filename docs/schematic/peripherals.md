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
| 電源 | D401 の 2 つのアノードに `+3V3` と `VBAT`、カソード → `RTC_VDD` → PCF8563 の VDD（100 nF） |
| 水晶 | OSCI / OSCO の間に Y401。外付けの負荷コンデンサはなし（PCF8563 が内蔵） |
| I²C | SDA / SCL → `I2C1_SDA` / `I2C1_SCL`（CM0 の内部プルアップを使う） |
| INT | → `RTC_INT`、10 kΩ で `+3V3_CM0` にプルアップ（CM0 が OFF なら 0V になり、逆電圧にならない） |
| CLKOUT | 未接続（ソフトウェアで止める） |

- 水晶は IC のすぐ横に置き、配線を短く。周りを GND で囲む。

## 2. 音声

| 記号 | 部品 | シンボル | LCSC |
|---|---|---|---|
| U402 | MAX98357A | `Audio:MAX98357A` | C910544 |
| J401 | スピーカーのコネクタ（2 ピン） | `Connector_Generic:Conn_01x02` | 未選定 |

| 接続 | 内容 |
|---|---|
| 電源 | VDD（7, 8）→ `+5V`（10 µF ＋ 100 nF）、GND（3, 11, 15、裏面パッド 17） |
| I²S | DIN ← `I2S_DOUT`、BCLK ← `I2S_BCLK`、LRCLK ← `I2S_LRCLK` |
| SD_MODE | `AMP_SD`（CM0 の GPIO22）から R402 を介して入れる |
| GAIN_SLOT | 未接続（9 dB） |
| 出力 | OUTP / OUTN → J401（4〜8 Ω のスピーカー） |

- **[要確認] R402 の値：** SD_MODE の電圧で動作が決まる（停止 / 右 / 左右の平均 / 左）。左右の平均（モノラル）にするには、SD_MODE の電圧を約 0.77〜1.4 V にする必要がある。内部のプルダウンを 100 kΩ と仮定して 220 kΩ（3.3 V から約 1.03 V）にしたが、データシートで内部プルダウンの値を確かめて決める。
- `AMP_SD` が Low（CM0 が OFF のときも含む）ならアンプは停止する。

## 3. シートをまたぐネット

- グローバルラベル：`I2C1_SDA` / `I2C1_SCL`（双方向）、`RTC_INT`（出力）、`I2S_BCLK` / `I2S_LRCLK` / `I2S_DOUT`、`AMP_SD`（入力）
- 電源：`+5V`、`+3V3`、GND（電源シンボル）、`+3V3_CM0`（電源シンボル、cm0 シートから）
