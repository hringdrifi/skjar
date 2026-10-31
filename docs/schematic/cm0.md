# CM0 シート（cm0.kicad_sch）

回路図を描くための仕様。ピンの割り当ては [pin-assignment.md](../pin-assignment.md) の「4. CM0 の GPIO と専用ピン」、決めた理由は [design-decisions.md](../design-decisions.md) を参照。

**手本：** [cm0-reference.pdf](cm0-reference.pdf)（画像は [cm0-reference.png](cm0-reference.png)）。`hardware/pcb/cm0.kicad_sch` には同じ配置で部品だけを並べてある。

---

## 1. 部品

| 記号 | 部品 | シンボル | LCSC | 区分 |
|---|---|---|---|---|
| U201 | Raspberry Pi CM0100000（CM0Lite、無線あり。4ユニット） | `Skjar:RaspberryPi_CM0` | — | — |
| Q201, Q202 | 2N7002 | `Transistor_FET:2N7002` | C8545 | Basic |
| U202 | SN74LVC1G125DBV（UART の受信バッファ） | `74xGxx:SN74LVC1G125DBV` | C23654 | Extended |
| R208, R209 | 4.7 kΩ（`KB_SDA` / `KB_SCL` のプルアップ） | `Device:R` | C23162 | Basic |
| R210 | 10 kΩ（`KB_INT` のプルアップ） | `Device:R` | C25804 | Basic |
| R / C | 0402 か 0603 | — | — | Basic |

## 2. CM0 の各ユニット

| ユニット | 接続 |
|---|---|
| Power | 5V（67, 68）→ `+5V`（10 µF ＋ 100 nF）。CM0_3.3V（41, 119）→ `+3V3_CM0`。GPIO_VREF（120）・SD_VREF（122）→ `+3V3_CM0`。CM0_1.8V（64, 65）→ `+1V8_CM0`（テストパッドのみ）。GND（43本）→ GND |
| Display/Camera | DSI の 4 レーンとクロック → `DSI_*`（display へ）。SDA0 / SCL0 → `I2C_DSI_SDA` / `I2C_DSI_SCL`。CSI、HDMI、CVBS、CAM_GPIO は未接続 |
| GPIO | GPIO2 / 3 → `I2C1_SDA` / `I2C1_SCL`。GPIO14 → `CM0_TXD`。GPIO15 ← `CM0_RXD`（バッファの出力）。GPIO18 / 19 ← `I2S_BCLK` / `I2S_LRCLK`（コーデックがクロックを出す）。GPIO21 → `I2S_DOUT`。GPIO17 ← `HP_DET`。GPIO22 → `CODEC_RST`。GPIO26 → `CM0_HALT`。GPIO27 ← `RTC_INT`。GPIO23 / 24 ↔ `KB_SDA` / `KB_SCL`（`i2c-gpio`。R208 / R209 で `+3V3_CM0` にプルアップ）。GPIO25 ← `KB_INT`（R210 で `+3V3_CM0` にプルアップ）。ほかは未接続 |
| System | USB_P / USB_N ← `USB_B_D+` / `USB_B_D-`。GLOBAL_EN ← Q201。RUN_PG → `CM0_RUN_PG`。nRPI_BOOT ← Q202。WiFi_ON / BT_ON / LED_nACT → テストパッドのみ。SD_CLK / SD_CMD / SD_DAT0〜3 → microSD スロット（J201）。USB_OTG、NC は未接続 |

## 3. 電源の制御（GLOBAL_EN）

- **Q201（2N7002）：** ゲート ← `CM0_PWROFF`、ソース → GND、ドレイン → GLOBAL_EN（62）。
- **R201（10 kΩ）：** ゲートを `+3V3` にプルアップする。RP2040 が起動する前やファームが止まったときは MOSFET が ON になり、GLOBAL_EN が Low に引かれて **CM0 は OFF**。
- GLOBAL_EN は CM0 の内部で 5V にプルアップされているので、RP2040 から直接つないではいけない。

## 4. USB ブートの要求（nRPI_BOOT）

- **Q202（2N7002）：** ゲート ← `BOOT_EN`（RP2040 の GPIO23）、ソース → GND、ドレイン → nRPI_BOOT（121）。
- **R202（100 kΩ）：** ゲートを GND にプルダウンする（初期状態は通常起動）。
- **スイッチは RP2040 側だけにつなぐ**（rp2040 シートの SW102 → `BOOT_SW`）。押されたら RP2040 が `BOOT_EN` を High にしてから CM0 の電源を入れる。CM0 のピンに外から電圧をかけることがない。
- RP2040 からのコマンドでも USB ブートに入れられる（復旧に使える）。

## 5. UART の受信バッファ（U202）

| ピン | 接続 |
|---|---|
| ~OE（1） | GND（常に有効） |
| A（2） | `RP_TXD`（RP2040 の TX） |
| Y（4） | `CM0_RXD`（CM0 の GPIO15） |
| VCC（5） | `+3V3_CM0`（100 nF） |
| GND（3） | GND |

- CM0 が OFF だと `+3V3_CM0` が 0V になり、バッファの出力が切り離される（Ioff）。

## 5a. microSD スロット（J201）

| 記号 | 部品 | シンボル | フットプリント | LCSC |
|---|---|---|---|---|
| J201 | Hirose DM3AT-SF-PEJM5（押し込み式） | `Connector:Micro_SD_Card_Det_Hirose_DM3AT` | `Connector_Card:microSD_HC_Hirose_DM3AT-SF-PEJM5` | C114218 |
| R203〜R207 | 100 kΩ（**未実装**） | `Device:R` | 0603 | C25803 |
| C204 / C205 | 100 nF / 10 µF | `Device:C` | 0603 | C14663 / C96446 |

- CMD、DAT0〜3、CLK を CM0 の SD 用のピンにそのままつなぐ。VDD は `+3V3_CM0`。SH（シールド）は GND。
- CM0Lite はここに挿したカードから起動する。eMMC 版の CM0 では使われない（部品を載せなくてよい）。
- R203〜R207 は CMD と DAT0〜3 のプルアップ。なしで動く見込みなので、フットプリントだけ用意して未実装にする。CM0 のデータシートには SD のプルアップの記述がなく、CM0 の IO ボードの回路図は公開されていない。CM4Lite 用の公式の IO ボード（CM4IO）は、microSD のスロットに SD の信号を直接つないでいて、外付けのプルアップはない（カード検出のプルアップも未実装）。
- カード検出（DET_A / DET_B）は、CM0 に受け口がないので未接続。

## 6. テストパッド

`+3V3_CM0`、`+1V8_CM0`、`CM0_RUN_PG`、GLOBAL_EN、nRPI_BOOT、`CM0_TXD`、`CM0_RXD`、LED_nACT、WiFi_ON、BT_ON。

## 7. シート外とつなぐグローバルラベル

| ラベル | 向き |
|---|---|
| `USB_B_D+` / `USB_B_D-`、`CM0_PWROFF`、`BOOT_EN`、`RP_TXD`、`RTC_INT`、`KB_INT`、`I2S_BCLK` / `I2S_LRCLK` | 入力 |
| `CM0_TXD`、`CM0_RUN_PG`、`CM0_HALT`、`DSI_*`（10 本）、`I2S_DOUT`、`CODEC_RST` | 出力 |
| `HP_DET` | 入力 |
| `I2C_DSI_SDA` / `I2C_DSI_SCL`、`I2C1_SDA` / `I2C1_SCL`、`KB_SDA` / `KB_SCL` | 双方向 |

**`+3V3_CM0` はシートをまたぐ電源**（display、peripherals でも使う）。`power:+3V3` の値を `+3V3_CM0` に書き換えた電源記号で扱う（`+1V8_CM0` も同じく `power:+1V8` から）。
