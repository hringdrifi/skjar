# 参考資料

SKJÁR の設計検討で参照した一次資料の出典と、読み取った要点。取得日はすべて 2026-10-30。

**下の表に出てくる PDF・図面・3D モデルは、著作権の都合でリポジトリに含めていません**（`.gitignore` で除外）。各節の「出典」のリンクから取得し、同じパスに置けば手元で参照できます。

## cm0/ — Raspberry Pi Compute Module 0

出典: [Raspberry Pi Product Information Portal / Compute Module Zero](https://pip.raspberrypi.com/categories/1286-raspberry-pi-compute-module-zero)

| ファイル | 内容 |
|---|---|
| `RP-009251-DS-3-cm0-datasheet.pdf` | CM0 データシート（第3版、21ページ） |
| `RP-009404-MM-1-cm0-product-brief.pdf` | CM0 製品概要 |

### データシートの要点

| 項目 | 内容 | 参照 |
|---|---|---|
| 外形 | 39 × 33 × 2.8 mm | 1, 4.1.1 |
| 端子 | 132本の側面端子（キャスタレーション）、1 mm ピッチ、ENIG 仕上げ。手はんだ可 | 1, 4.1 |
| DSI | **4レーン** | 1, 2.4.2 |
| USB | USB 2.0 High Speed、90 Ω 差動配線。OTG（デバイスモード）は **公式には未記載**（動いた報告はある） | 2.2 |
| 無線 | CYW43439。IPEX-1 の外付けアンテナのみ。`WiFi_ON` / `BT_ON` を Low に引くと無効化できる | 2.1 |
| DSI 用 I²C | SDA0 = GPIO44、SCL0 = GPIO45 | 2.5.1 |
| GPIO | 28本、全ピン合計 50 mA まで。`GPIO_VREF` は CM0_3.3V か CM0_1.8V に接続する | 2.6 |
| 5 V 電源 | 単調に立ち上がり、**動作中は 4.75 V 以上を保つこと** | 3.1 |
| 電源投入の順序 | **5 V が立ち上がる前に、どのピンにも電圧をかけないこと** | 3.1 |
| nRPI_BOOT | 5 V が立ち上がってから 1 ms 以内に Low になっていれば USB ブート | 3.1 |
| GLOBAL_EN | Low で最小電力状態（約 500 µA）。OS をシャットダウンしてから Low にすること | 2.9, 3.3 |
| 消費電流 | アイドル時 typ. 225 mA、動作時 typ. 675 mA | 3.3 |
| 3.3 V / 1.8 V 出力 | それぞれ最大 600 mA を外部に供給できる | 3.4 |
| 突然の電源断 | 安全にシャットダウンできない場合は btrfs / f2fs / overlayfs を推奨 | 3.2 |
| 推奨フットプリント | パッド 0.6 × 2.4 mm、1.0 mm ピッチ（LCSC / EasyEDA のフットプリントと一致） | 4.1.2 |
| GLOBAL_EN（62番） | 内部で 10 kΩ を介して **5 V** にプルアップされている。RP2040 からはオープンドレイン（N-MOSFET）で駆動すること | 4.2 |
| RUN_PG（61番） | 出力：High なら電源正常で CPU が動作中。入力：220 Ω を介して Low に引くとリセット | 4.2 |
| USB_OTG（38番） | 内部でプルアップされている（デバイス側）。GND に落とすとホストになる | 4.2 |
| 逆電圧 | **CM0 の電源が切れているときは、どのピンにも外部から電圧をかけないこと** | 4.2.1 |

### ピンが並ぶ辺（上から見て、左上の1番から反時計回り）

| 辺 | ピン番号 | 主な信号 |
|---|---|---|
| 左 | 1–33 | DSI（2–15）、HDMI |
| 下 | 34–66 | USB（35, 36）、USB_OTG、CM0_3.3V、SDA0/SCL0（42, 43）、CSI、RUN_PG（61）、GLOBAL_EN（62）、CM0_1.8V |
| 右 | 67–99 | **5V 入力（67, 68。右下の角）**、GPIO 群 |
| 上 | 100–132 | GPIO 群、UART（GPIO14/15 = 110/108番）、WiFi_ON / BT_ON（116, 117）、nRPI_BOOT（121）、SD（CM0Lite のみ） |

IPEX-1 のアンテナ端子は、右の辺の上寄り（下の辺から 17.225 mm）にある。

### Linux の対応状況（raspberrypi/linux rpi-6.12.y）

- **CM0 用のデバイスツリー：** `bcm2710-rpi-cm0.dts` がある。I²C0 は GPIO0/1 と GPIO44/45 の切り替え式（`i2c0mux_0_44`）。
- **Waveshare パネル用のドライバ：** `vc4-kms-dsi-waveshare-panel` に `8_8_inch` と `11_9_inch` のパラメータがある。
- **UART：** UART0 は Bluetooth が使っている。GPIO14/15 で RP2040 と UART で通信するには、設定の変更が必要（Zero 2 W と同様）。

## giteki-020-260249/ — CM0 の技適（工事設計認証）

出典: [総務省 電波利用ポータル 工事設計認証番号 020-260249](https://www.tele.soumu.go.jp/giteki/SearchServlet?pageID=jg01_01&PC=020&TC=N&PK=1&FN=261019N020&SN=%E8%AA%8D%E8%A8%BC&LN=34&R1=*****&R2=*****)

| 項目 | 内容 |
|---|---|
| 認証日 | 令和8年9月28日 |
| 取得者 | Raspberry Pi Limited |
| 対象機器 | Raspberry Pi Compute Module 0（または Compute Module Zero） |
| 周波数 | 2.4 GHz 帯のみ（Wi-Fi 2412〜2472 MHz、Bluetooth 2402〜2480 MHz） |
| 登録証明機関 | (一社)TAC |
| 試験機関 | DEKRA Testing and Certification (Suzhou) |

| ファイル | 内容 |
|---|---|
| `020-260249_01_002.pdf` | 外観写真・内部写真（アンテナ写真を含む）、7ページ |
| `020-260249_02_003.pdf` | 試験成績書 Wi-Fi 802.11b/g/n（Report 2670360R.703）、76ページ |
| `020-260249_02_004.pdf` | 試験成績書 Bluetooth BR/EDR（Report 2670360R.701）、65ページ |
| `020-260249_02_005.pdf` | 試験成績書 Bluetooth LE（Report 2670360R.702）、44ページ |

### 認証の対象アンテナ（各試験成績書の 1.1 節）

| # | 種類 | 利得 | 型番 | 備考 |
|---|---|---|---|---|
| 1 | FPC アンテナ | 3.5 dBi | ED-ANT-C（HD0110-WIFI-ANT V2.0） | EDATEC 製。IPEX-1、ケーブル 60 mm。[製品ページ](https://edatec.cn/ed-ant-c) |
| 2 | FPC アンテナ | 0.071 dBi | CardputerZero（R-CardputerZero v2.0） | M5Stack CardputerZero の内蔵品 |
| 3 | 外付けアンテナ | 2 dBi | YH2400-5800-SMA-108 | Raspberry Pi アンテナキットのロッドアンテナ（長さ 108 mm） |

## lcd/ — Waveshare DSI LCD

出典: [8.8inch DSI LCD Wiki](https://www.waveshare.com/wiki/8.8inch_DSI_LCD) / [11.9inch DSI LCD Wiki](https://www.waveshare.com/wiki/11.9inch_DSI_LCD)

| ファイル | 内容 |
|---|---|
| `waveshare-8.8inch-dsi-lcd-2d-20241025.pdf` | 8.8″ の 2D 図面 |
| `waveshare-8.8inch-dsi-lcd-3d-20241025.stp` | 8.8″ の 3D モデル（STEP） |
| `waveshare-11.9inch-dsi-lcd-2d.pdf` | 11.9″ の 2D 図面 |
| `waveshare-11.9inch-dsi-lcd-3d-20221112.zip` | 11.9″ の 3D モデル（zip。未展開） |

| `waveshare-8.8-dsi-touch-a-2d-20251215.pdf` | 8.8-DSI-TOUCH-A の 2D 図面。出典: [Wiki](https://www.waveshare.com/wiki/8.8-DSI-TOUCH-A) |
| `waveshare-8.8-dsi-touch-a-dwg.zip` | 8.8-DSI-TOUCH-A の DWG 図面（zip。未展開） |

### 8.8inch DSI LCD と 8.8-DSI-TOUCH-A の比較

| 項目 | 8.8inch DSI LCD | 8.8-DSI-TOUCH-A |
|---|---|---|
| ガラスの外形 / 表示領域 | 240.5 × 68.0 / 219.88 × 55.72 mm | 同じ |
| 解像度 / DSI | 480×1920、2レーン | 480×1920、2レーン（パネル IC は OTA7290B） |
| 厚み（背面の部品を含む） | 約 16.5 mm（背面にドライバ基板） | **13.45 mm**（背面はアルミカバー） |
| DSI コネクタ | 15ピン 1.0 mm ピッチ | **22ピン 0.5 mm ピッチ**（Raspberry Pi 5 / CM4 と同じ並び。1番ピンが 3V3 側） |
| 5 V の入力 | 4ピン PH2.0 | 2ピン（5V / GND）。コネクタの種類は実物で確認 |
| 起動に必要な電流 | 0.5 A 以上 | 0.43 A 以上 |
| バックライト | I²C（sysfs で 0〜255） | I²C（アドレス 0x45 のレジスタ 0x96、0x00〜0xFF） |
| Linux のドライバ | `vc4-kms-dsi-waveshare-panel,8_8_inch` | `vc4-kms-dsi-waveshare-panel-v2,8_8_inch_a` |
| 取り付け穴 | ドライバ基板に 58 × 49 mm（M2.5） | 背面カバーに 58 × 49 mm と 218 × 57 mm（M2.5） |

### 図面から読み取った寸法

| 項目 | 8.8″ | 11.9″ |
|---|---|---|
| 外形（カバーガラス / タッチパネル） | **240.50 × 68.00 mm** | **287.30 × 69.80 mm** |
| 表示領域 | 219.88 × 55.72 mm | 268.18 × 57.98 mm |
| パネルの厚み | 6.45 mm（ガラスを含めて 8.50 mm） | 7.00 mm |
| 背面のドライバ基板 | 109 × 64 mm。M2.5 の穴は Raspberry Pi と同じ 58 × 49 mm 配置 | 同じ |
| 背面の部品の高さ | 約 8 mm | 約 8〜9.6 mm |

40% 配列（約 230〜245 mm）は 8.8″ の 240.5 mm と、60% 配列（約 286 mm）は 11.9″ の 287.3 mm と、幅がほぼ一致する。

### 共通の仕様

- 入力電圧: 4.75〜5.30 V
- 入力電流: typ. 0.5 A（最大は TBD）
- DSI は2レーンで、15ピンの FPC（Raspberry Pi 3 / 4 と同じ）
- 5 V と I²C 用の 4ピン PH2.0 コネクタ
- バックライトは I²C で制御（`/sys/class/backlight/*/brightness`、0〜255）

## power/ — 電源 IC

| ファイル | 内容 |
|---|---|
| `tps2121.pdf` | TPS2120 / TPS2121 データシート（SLVSEA3F）。出典: [TI](https://www.ti.com/lit/ds/symlink/tps2121.pdf) |
| `tps22965.pdf` | TPS22965 データシート（SLVSBJ0F）。出典: [TI](https://www.ti.com/lit/ds/symlink/tps22965.pdf) |

## rtc/ — RTC

| ファイル | 内容 |
|---|---|
| `pcf8563.pdf` | PCF8563 データシート（NXP、Rev. 11.1）。出典: [NXP](https://www.nxp.com/docs/en/data-sheet/PCF8563.pdf) |
| `rv-8263-c7.pdf` | RV-8263-C7 データシート（概要版）。出典: [Micro Crystal](https://www.microcrystal.com/fileadmin/Media/Products/RTC/Datasheet/RV-8263-C7.pdf)。**不採用**（PCF8563T に変更） |

**PCF8563T（SO-8）の要点**
- ピン配置：1 OSCI、2 OSCO、3 INT、4 VSS、5 SDA、6 SCL、7 CLKOUT、8 VDD。
- 計時は VDD 1.0〜5.5 V、I²C は 1.8 V 以上。
- SDA / SCL / OSCI の入力の絶対最大定格は −0.5〜+6.5 V で、VDD に依存しない。
- INT と CLKOUT はオープンドレイン。発振用のコンデンサを内蔵している。
- 特性表は、負荷容量 8 pF、Rs 40 kΩ の水晶が前提。

## switch/ — キースイッチ

| ファイル | 内容 |
|---|---|
| `SPEC-CPG135301D01_Kailh_Choc_V2_Low_Profile_Red_Switch.pdf` | Kailh Choc V2（PG1353、Red）の仕様書と図面（2020-04-15 版）。出典: [Kailh の仕様書（Shopify の CDN）](https://cdn.shopify.com/s/files/1/0657/6075/5954/files/SPEC-CPG135301D01_Kailh_Choc_V2_Low_Profile_Red_Switch.pdf?v=1666690444)（取得日 2026-11-01） |

**推奨の穴の配置（Recommended PCB Layout、パターン面から見た図）の要点**
- 中心の穴は Ø5.00。端子の穴は Ø1.20 が 2 個で、中心からの位置は 5.90 と、3.80 / 5.00。
- ガイドピンの穴は 2 × 1.5 の長穴。中心から 5.15 / 5.00 の位置にある。
- **LED は中心から 4.93**。端子と反対の側にある（キーの RGB の位置。[design-decisions.md](../design-decisions.md) の 6.1）。
- 本体は 13.95 × 13.95 mm、天面は 15 × 15 mm。全ストロークは 3.2 mm、動作点は 1.3 mm。
