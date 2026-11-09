# CM0 シート（cm0.kicad_sch）

回路図を描くための仕様。ピンの割り当ては [pin-assignment.md](../pin-assignment.md) の「4. CM0 の GPIO と専用ピン」、決めた理由は [design-decisions.md](../design-decisions.md) を参照。

**手本：** [cm0-reference.pdf](cm0-reference.pdf)（画像は [cm0-reference.png](cm0-reference.png)）。2026-11-06 の回路図から書き出した版。回路図を変えたら書き出し直す。正は `hardware/pcb/cm0.kicad_sch`。

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
- **要求は RP2040 が出す**（キーの組み合わせ）。RP2040 が `BOOT_EN` を High にしてから CM0 の電源を入れる。CM0 のピンに外から電圧をかけることがない。
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
| J201 | Korean Hroparts Elec TF-01A（プッシュプッシュ。2026-11-05 に Hirose DM3AT-SF-PEJM5 から変えた） | `Connector:Micro_SD_Card_Det1` | `Skjar:microSD_HRO_TF-01A` | C91145 |
| R203〜R207 | 100 kΩ（**未実装**） | `Device:R` | 0603 | C25803 |
| C204 / C205 | 100 nF / 10 µF | `Device:C` | 0603 | C14663 / C96446 |

- CMD、DAT0〜3、CLK を CM0 の SD 用のピンにそのままつなぐ。VDD は `+3V3_SD`（[5b](#5b-sd-カードの電源3v3_sd)。C204・C205 もこちら）。SH（シールド）は GND。
- CM0Lite はここに挿したカードから起動する。eMMC 版の CM0 では使われない（部品を載せなくてよい）。
- R203〜R207 は CMD と DAT0〜3 のプルアップ。なしで動く見込みなので、フットプリントだけ用意して未実装にする。CM0 のデータシートには SD のプルアップの記述がなく、CM0 の IO ボードの回路図は公開されていない。CM4Lite 用の公式の IO ボード（CM4IO）は、microSD のスロットに SD の信号を直接つないでいて、外付けのプルアップはない（カード検出のプルアップも未実装）。
- カード検出（9 番の DET。カードを挿すとシェルにつながる）は、CM0 に受け口がないので未接続。
- **TF-01A にした理由：** ケースの外からカードを押し出してつまめるようにするため。挿したカードが本体の前から出る長さは、ロックで 2.50 mm、飛び出しで 5.60 mm（DM3AT は 1.6 / 5.6 mm）。本体の前を基板の縁にそろえて置ける（DM3AT は前のシェルのパッドが縁にかかり、本体が基板の外へ 1.4 mm 出ていた）。接点は金めっき。XUNPU TF-115、SHOU HAN TF PUSH、XKB XKTF-015-N、SOFNG TF-015 が同じ寸法で、そのまま載せ替えられる。図面は `docs/references/sd/`（Git には入れていない）。

## 5b. SD カードの電源（`+3V3_SD`）

電源がいきなり切れても、カードの電源だけを約 0.5 秒持たせて、カードの中の書き込み作業（管理表の書き換えや消去）を終えさせる。理由は design-decisions.md の [1.5](../design-decisions.md#15-いきなりの電源断への備え)。

| 記号 | 部品 | シンボル | フットプリント | LCSC |
|---|---|---|---|---|
| D201 / D202 | B5819W（ショットキー 40 V 1 A） | `Device:D_Schottky` | `Diode_SMD:D_SOD-123` | C8598 |
| R211 | 100 Ω（C206 の充電） | `Device:R` | 1206 | C17901 |
| C206 | 0.1 F 5.5 V のスーパーキャパシタ（CDA CHP5R5L104R-TW、ESR 0.9 Ω） | `Device:C_Polarized` | `Skjar:CP_EDLC_10x5mm_P7.50mm_Horizontal` | なし（DigiKey などで買う） |
| C207 / C208 | 1 µF（U203 の入力・出力） | `Device:C` | 0603 | C15849 |
| U203 | AP2127K-3.3TRG1（EN 付きの LDO。U5 の AP2127N と同じ系列） | `Regulator_Linear:AP2127K-3.3` | SOT-23-5 | C156285 |
| U204 | APX809S05-31SA-7（3.08 V の電圧検出。プッシュプル、Low でリセット、解除まで 240 ms 以上） | `Power_Supervisor:TCM809`（ピン配置が同じ：1 GND、2 RESET、3 VCC） | `Package_TO_SOT_SMD:SOT-23` | C2067622 |
| JP201 | はんだジャンパ（開） | `Jumper:SolderJumper_2_Open` | `Jumper:SolderJumper-2_P1.3mm_Open_RoundedPad1.0x1.5mm` | — |

- **つなぎ方：** `+5V` → D201 → `SD_VIN` → U203 → `+3V3_SD` → J201 の VDD。C206 は `SD_VIN` から R211 を通して充電し、D202 を通して `SD_VIN` に戻す（R211 と D202 は並列）。
- **普段：** D201 を通して `+5V` から給電する（`SD_VIN` は約 4.7 V）。C206 は R211 でゆっくり充電する（最初の電流は 50 mA 以下、時定数 10 秒）。電源を入れてから 25 秒ほどで、保護が効く電圧になる。
- **電源が切れたとき：** D201 が `+5V` 側を切り離し、C206 から D202 を通して U203 にだけ給電する。100 mA で 4.7 V → 3.5 V まで約 0.5 秒（C = 0.1 F、ΔV = I・t / C）。U203 と U204 の自己消費は合わせて 10 µA 程度なので、ほぼ全部がカードに回る。
- **すぱっと切る：** `SD_VIN` が 3.08 V を下回ると、U204 が `SD_EN` を Low にして U203 を止める。カードの電圧が中途半端な値でだらだら下がる時間を作らない。
- **起動の順番：** 電源を入れてから U204 が U203 を ON にするまで 240 ms 以上かかる。RP2040 のファームは、電源投入から 0.5 秒以上たってから CM0 を ON にする。
- **CM0 との関係：** CM0 の SD の信号は 3.3 V 固定（SD_VREF は `+3V3_CM0`）。カードの電源を CM0 と分けても問題ない（1.8 V で通信するモードは使わない）。CM0 が OFF でカードに電源が入っているときは、カードの DAT3 の内部プルアップ（約 50 kΩ）から CM0 のピンに数十 µA 流れるだけ。R203〜R207（未実装）は `+3V3_CM0` 側のまま。
- **外してもよい部品：** C206・R211・D202 を外しても、持たないだけで普通に動く。U203 も外すときは JP201 を閉じて、`+3V3_SD` を `+3V3_CM0` につなぐ（今までと同じ）。U203 を載せたまま JP201 を閉じてはいけない。
- **C206 の付け方：** 足を根元で 90° 曲げて寝かせ、缶の下をシリコーン接着剤か両面テープで基板に貼る（高さ約 5 mm）。フットプリントは缶を寝かせた外形（10 × 13.5 mm）で、1 番が ＋。足の太さ（穴 1.0 mm）と極性は、部品が届いたら確かめる。

## 6. テストパッド

`+3V3_CM0`、`+1V8_CM0`、`CM0_RUN_PG`、GLOBAL_EN、nRPI_BOOT、`CM0_TXD`、`CM0_RXD`、LED_nACT、WiFi_ON、BT_ON。

## 7. シート外とつなぐグローバルラベル

| ラベル | 向き |
|---|---|
| `USB_B_D+` / `USB_B_D-`、`CM0_PWROFF`、`BOOT_EN`、`RP_TXD`、`RTC_INT`、`KB_INT`、`I2S_BCLK` / `I2S_LRCLK` | 入力 |
| `CM0_TXD`、`CM0_RUN_PG`、`CM0_HALT`、`DSI_*`（10 本）、`I2S_DOUT`、`CODEC_RST` | 出力 |
| `HP_DET` | 入力 |
| `I2C_DSI_SDA` / `I2C_DSI_SCL`、`I2C1_SDA` / `I2C1_SCL`、`KB_SDA` / `KB_SCL` | 双方向 |

`+3V3_SD` はこのシートの中だけの電源で、`power:+3V3` の値を書き換えた電源記号で扱う。

**`+3V3_CM0` はシートをまたぐ電源**（display、peripherals でも使う）。`power:+3V3` の値を `+3V3_CM0` に書き換えた電源記号で扱う（`+1V8_CM0` も同じく `power:+1V8` から）。
