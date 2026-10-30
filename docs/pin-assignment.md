# ピンの割り当てとシート間の信号

回路図を描くための叩き台（2026-10-30）。基板の配置を決めるときに、配線が素直になるよう入れ替える可能性がある。

決めた理由は [design-decisions.md](design-decisions.md) を参照。

---

## 1. 電源のネット

| ネット名 | 作るところ | 内容 | 使うところ |
|---|---|---|---|
| `VBUS_A` / `VBUS_B` | power | 各 USB-C の VBUS | power（TPS2121 の IN2 / IN1） |
| `+5V` | power | TPS2121 の出力。常に入っている 5V | 全シート |
| `+5V_LCD` | power | LCD 用の負荷スイッチ（TPS22965）の出力 | display |
| `+3V3` | power | `+5V` から作る常時 ON の 3.3V（RP2040 用のレギュレータ） | rp2040、peripherals（RTC の電源の片側）、プルアップ |
| `+3V3_CM0` | cm0 | CM0 の 3.3V 出力。CM0 が OFF のときは 0V | cm0（GPIO_VREF、UART のバッファ）、display（FPC の 3V3） |
| `VBAT` | peripherals | CR2032 | peripherals のみ |

---

## 2. シート間の信号（グローバルラベル）

回路図では、シートをまたぐ信号はグローバルラベル、`+5V` / `+3V3` / GND は電源記号、シートの中だけのネットはローカルラベルで扱う。グローバルラベルは、各シートの中でネットごとに最低1つ残す（同じシートの中のピン同士は線でつないでよい）。

| 信号名 | 出す側 → 受ける側 | 内容 |
|---|---|---|
| `USB_A_D+` / `USB_A_D-` | power → rp2040 | Port A の USB（Full Speed） |
| `USB_B_D+` / `USB_B_D-` | power → cm0 | Port B の USB（High Speed、90 Ω 差動） |
| `CC1_A` / `CC2_A` / `CC1_B` / `CC2_B` | power → rp2040 | 各ポートの CC の電圧（ADC で読む） |
| `PWR_ST` | power → rp2040 | TPS2121 の ST（High：Port B から給電、Low：Port A から給電） |
| `LCD_EN` | rp2040 → power | LCD 用の負荷スイッチの ON。プルダウンして初期状態は OFF |
| `CM0_PWROFF` | rp2040 → cm0 | GLOBAL_EN を Low に引く N-MOSFET のゲート。**High で CM0 が OFF**。プルアップして初期状態は OFF |
| `BOOT_EN` | rp2040 → cm0 | CM0 の USB ブート要求。cm0 シートの N-MOSFET（Q202）で nRPI_BOOT を Low に引く。スイッチ（`BOOT_SW`）は rp2040 シート内だけで完結する |
| `RP_TXD` | rp2040 → cm0 | RP2040 の UART TX。cm0 シートのバッファ（SN74LVC1G125、+3V3_CM0 で動作）を通して CM0 の GPIO15 へ |
| `CM0_TXD` | cm0 → rp2040 | CM0 の GPIO14 → RP2040 の UART RX |
| `CM0_RUN_PG` | cm0 → rp2040 | CM0 の RUN_PG（電源正常、CPU 動作中） |
| `CM0_HALT` | cm0 → rp2040 | シャットダウン完了の通知（CM0 の `gpio-poweroff`） |
| `DSI_D0_P/N` 〜 `DSI_D3_P/N`、`DSI_CK_P/N` | cm0 → display | DSI（4レーン分を配線。100 Ω 差動） |
| `I2C_DSI_SDA` / `I2C_DSI_SCL` | cm0 ↔ display | パネルのタッチとバックライト（CM0 の SDA0 / SCL0） |
| `I2C1_SDA` / `I2C1_SCL` | cm0 ↔ peripherals | RTC |
| `RTC_INT` | peripherals → cm0 | RTC の割り込み（アラーム） |
| `I2S_BCLK` / `I2S_LRCLK` / `I2S_DOUT` | cm0 → peripherals | MAX98357A へのデジタル音声 |
| `AMP_SD` | cm0 → peripherals | MAX98357A の SD_MODE（プルダウンして、CM0 が OFF のときはアンプも停止） |
| `ROW0`〜`ROW6`、`COL0`〜`COL5` | rp2040 ↔ keymatrix | キーマトリクス（7 行 × 6 列 ＝ 42 キー、13 本） |

---

## 3. RP2040 の GPIO

| GPIO | 信号 | 方向 | 備考 |
|---|---|---|---|
| 0〜6 | `ROW0`〜`ROW6` | 出力 | キーマトリクスの行 |
| 7〜12 | `COL0`〜`COL5` | 入力（プルアップ） | キーマトリクスの列 |
| 13〜14 | 予備 | — | |
| 15 | `CM0_PWROFF` | 出力 | High で CM0 が OFF |
| 16 | `RP_TXD` | 出力 | UART0 TX |
| 17 | `CM0_TXD` | 入力（**プルなし**） | UART0 RX |
| 18 | `LCD_EN` | 出力 | |
| 19 | `PWR_ST` | 入力（割り込み） | |
| 20 | `BOOT_SW` | 入力 | CM0 の USB ブート要求スイッチ（10 kΩ でプルアップ） |
| 21 | `CM0_RUN_PG` | 入力（**プルなし**） | |
| 22 | `CM0_HALT` | 入力（**プルなし**） | |
| 23 | `BOOT_EN` | 出力 | High で CM0 の nRPI_BOOT を Low に引く |
| 24〜25 | 予備 | — | バックライト、インジケータなど |
| 26 | `CC1_A` | ADC0 | |
| 27 | `CC2_A` | ADC1 | |
| 28 | `CC1_B` | ADC2 | |
| 29 | `CC2_B` | ADC3 | |

- **合計：** 26 / 30 本（予備4本：GPIO13・14・24・25）。
- **CM0 から来る信号はプルなしにする：** CM0 が OFF のときに、RP2040 から CM0 に電圧をかけないため。
- **UART0：** TX / RX は GPIO16 / 17 を使う（RP2040 で UART0 を割り当てられるピンの組）。

---

## 4. CM0 の GPIO と専用ピン

| GPIO（ピン番号） | 信号 | 備考 |
|---|---|---|
| GPIO2（114）/ GPIO3（113） | `I2C1_SDA` / `I2C1_SCL` | RTC。CM0 の内部で 1.8 kΩ のプルアップあり |
| GPIO14（110） | `CM0_TXD` | UART の TX |
| GPIO15（108） | ← `RP_TXD` | UART の RX。バッファの出力を受ける |
| GPIO18（105） | `I2S_BCLK` | PCM_CLK |
| GPIO19（79） | `I2S_LRCLK` | PCM_FS |
| GPIO21（73） | `I2S_DOUT` | PCM_DOUT |
| GPIO22（102） | `AMP_SD` | |
| GPIO26（76） | `CM0_HALT` | `gpio-poweroff` |
| GPIO27（104） | `RTC_INT` | |
| SDA0（42）/ SCL0（43） | `I2C_DSI_SDA` / `I2C_DSI_SCL` | GPIO44 / 45。内部で 1.8 kΩ のプルアップあり |
| DSI（2〜15） | `DSI_*` | |
| USB_P（36）/ USB_N（35） | `USB_B_D+` / `USB_B_D-` | |
| USB_OTG（38） | 未接続 | 内部でプルアップされ、デバイスとして動く |
| GLOBAL_EN（62） | ← N-MOSFET のドレイン | 内部で 10 kΩ を介して 5V にプルアップされている |
| RUN_PG（61） | `CM0_RUN_PG` | テストパッドも付ける |
| nRPI_BOOT（121） | ← N-MOSFET（Q202）のドレイン | 内部でプルアップあり。RP2040 の `BOOT_EN` で Low に引く |
| WiFi_ON（116）/ BT_ON（117） | 未接続 | 内部でプルアップされ、有効になる |
| LED_nACT（118） | テストパッド（または LED） | |
| GPIO_VREF（120） | `+3V3_CM0` | 3.3V の信号にする |
| SD_VREF（122）、SD_*（124〜131） | 未接続 | CM0Lite 専用 |
| GPIO0〜1、4〜13、16〜17、20、23〜25 | 未使用 | テストパッドを付ける候補 |

---

## 5. 自作したシンボル（`hardware/pcb/lib/skjar.kicad_sym`）

| シンボル | ピン | 照合した資料 |
|---|---|---|
| `RaspberryPi_CM0` | 132（4ユニット：Power / Display/Camera / GPIO / System） | CM0 データシート Table 5 |
| `TPS2121RUX` | 12 | TPS2121 データシート 6章（RUX パッケージ） |
| `TPS22965DSG` | 9（8 ＋ 裏面の放熱パッド＝GND） | TPS22965 データシート 6章 |

- **同じ名前のピンが複数ある場合：** 2本目以降を同じ位置に重ねて非表示にしている。
  - CM0：5V（67, 68）、CM0_3.3V（41, 119）、CM0_1.8V（64, 65）、GND（43本。データシートの Table 5 で確認済み。Power ユニットの下側）、NC（70, 71）
  - TPS2121：OUT（1, 8）
  - TPS22965：VIN（1, 2）、VOUT（7, 8）、GND（5, 9）
  - シンボルエディタでは、重ねたピンの番号が灰色で重なって表示される。
- **フットプリントは未設定：** 回路図がある程度できてから用意する。
