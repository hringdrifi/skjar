# ディスプレイシート（display.kicad_sch）

**手本：** [display-reference.pdf](display-reference.pdf)（画像は [display-reference.png](display-reference.png)）。

## 1. 22 ピン DSI コネクタ（J301、0.5 mm ピッチ）

Raspberry Pi 5 / CM4 と同じピン配置。8.8-DSI-TOUCH-A は付属の 22 ピン FFC（両端で接点の向きが逆）で、8.8inch DSI LCD は付属の DSI-Cable-12cm（15 → 22 ピン）でつなぐ。

| ピン | 信号 | ピン | 信号 |
|---|---|---|---|
| 1 | GND | 12 | `DSI_D2_P` |
| 2 | `DSI_D0_N` | 13 | GND |
| 3 | `DSI_D0_P` | 14 | `DSI_D3_N` |
| 4 | GND | 15 | `DSI_D3_P` |
| 5 | `DSI_D1_N` | 16 | GND |
| 6 | `DSI_D1_P` | 17 | 未接続 |
| 7 | GND | 18 | 未接続 |
| 8 | `DSI_CK_N` | 19 | GND |
| 9 | `DSI_CK_P` | 20 | `I2C_DSI_SCL` |
| 10 | GND | 21 | `I2C_DSI_SDA` |
| 11 | `DSI_D2_N` | 22 | `+3V3_CM0`（100 nF） |

- **TOUCH-A 側のピン配置と照合済み：** [Wiki の Interfaces の図](https://www.waveshare.com/wiki/8.8-DSI-TOUCH-A)では、パネル側は 1 番 3V3、2 番 I2C_SDA、3 番 I2C_SCL … 22 番 GND。付属の FFC（両端で向きが逆）で基板の n 番がパネルの 23 − n 番につながるとして、22 本すべてが一致する。
  - 基板の 17 / 18 番（未接続）は、パネルの 6 / 5 番（RESERVE）に当たる。Raspberry Pi の CM4IO ボードの DSI コネクタでも 17 / 18 番は未接続。
  - 残る確認は、FFC の向き（接点の面）を実物で見ることだけ。
- **3V3 は `+3V3_CM0` から取る：** CM0 が OFF のときはパネル側の I²C にも電圧がかからないので、CM0 に逆電圧がかからない。
- **コネクタの部品：** 上下両面に接点があるタイプを選ぶ（ケーブルを基板の穴に通すと向きが変わるため）。部品は未選定。

## 2. LCD の電源（J302）

ライトアングルの 2.54 mm 1×3 ピンヘッダ（`Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Horizontal`）。1 番 `+5V_LCD`、2 番 `+5V_LCD`、3 番 GND（Raspberry Pi の 2・4・6 番と同じ並び）。挿し込み口を基板の穴の方に向けて配置する。

## 3. シートをまたぐネット

- グローバルラベル：`DSI_*`（10 本、入力）、`I2C_DSI_SDA` / `I2C_DSI_SCL`（双方向）
- 電源記号：`+5V_LCD`（power シートから）、`+3V3_CM0`（cm0 シートから）、GND
