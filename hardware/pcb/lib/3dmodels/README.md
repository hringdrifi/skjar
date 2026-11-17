# 3dmodels

フットプリントが参照する 3D モデル（STEP / WRL）の置き場。フットプリントからは `${KIPRJMOD}/lib/3dmodels/...` で参照する。

## 方針

- **koktoh 氏のモデルを優先して使う。** 再配布が許可されている（CC BY-NC-SA）ので、[koktoh/](koktoh/) にそのままコピーしてリポジトリに含める。作者・入手元・ライセンスは [koktoh/README.md](koktoh/README.md) に記録している。
- **それ以外のモデルはリポジトリに含めない**（`.gitignore` で除外）。再配布できるとは限らないため。手元に置き、下の表に入手元を記録する。
- 自作したモデルなど公開してよいものは、ルートの `.gitignore` に `!hardware/pcb/lib/3dmodels/<パス>` を追加して含める。

## リポジトリに含めている自作モデル

SKJÁR のハードウェアと同じライセンス（CERN-OHL-S-2.0）。

| ファイル | 使っているフットプリント | 内容 |
|---|---|---|
| `8.8-DSI-TOUCH-A.step` | — | 8.8″ ディスプレイ。寸法図から起こした |
| `Low Profile Keycap <幅>U.step`（1U・1.25U・1.75U・2.25U・2.75U） | CPG135001S30_<幅>U | ロープロファイルのキーキャップ。作者が自作し、前の基板 blad でも使ったもの |
| `RaspberryPi_CM0.step` | RaspberryPi_CM0_Castellated | CM0。データシート（RP-009251-DS-3）の図 2 から起こした（eMMC は EDATEC の資料の上面図の枠と、eMMC 付きの写真から。CM0Lite では未実装）。基板 39 × 33 mm、全高 2.8 mm。基板の厚さ（1.0 mm）と部品の高さは仮定 |
| `SW_Push_1P1T_XKB_TS-1187A.step` | Button_Switch_SMD:SW_Push_1P1T_XKB_TS-1187A（KiCad 標準。標準のモデルは配布されていない） | タクトスイッチ（XKB TS-1187A-B-A-B、H = 1.50 mm）。XKB の寸法図（TS-1187A-X-X-X rev A2、2025-09-11）から起こした。本体 5.20 mm 角の八角形、端子先端どうし 6.40 mm。ハウジングの高さ（0.95 mm）とボタンの径（2.0 mm）は図からの推定 |
| `USB_C_Receptacle_HRO_TYPE-C-31-M-12.step` | Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12（KiCad 標準。標準のモデルは配布されていない） | USB-C レセプタクル。HRO の寸法図（rev A、2020-12-08）から起こした。シェル 8.94 × 7.30 × 3.26 mm、差し込み面は原点から 3.65 mm 手前。角の丸みと舌の寸法は推定 |
| `microSD_HRO_TF-01A.step` | microSD_HRO_TF-01A | microSD のスロット（Korean Hroparts Elec TF-01A）。HRO の寸法図（2023-08-20）から起こした。本体 14.75 × 14.50 × 1.83 mm。カードは入れていない |
| `CP_EDLC_10x5mm_P7.50mm_Horizontal.step` | CP_EDLC_10x5mm_P7.50mm_Horizontal | 寝かせて付けるスーパーキャパシタ（C206、CDA CHP5R5L104R-TW）。CDA の CHP シリーズのデータシート（2023-08-01、`docs/references/sd/CHP_Datasheet.pdf`）から起こした。缶は断面が両端半円の 10 × 5 mm、長さ 12 mm で、足の側から 1.0 mm 離して寝かせる。足は φ0.5 mm。－側（2 番）の端に黒い帯 |
| `BatteryHolder_MYOUNG_MY-2032-08.step` | BatteryHolder_MYOUNG_MY-2032-08 | CR2032 ホルダー（BT401、MYOUNG MY-2032-08）に CR2032 を入れた状態。MYOUNG の図面（MY-CP-0154、2017-07-20、`docs/references/rtc/MY-2032-08.pdf`）から起こした。上板 21.1 × 15.2 mm、厚さ 0.25 mm、高さ 4.40 mm。横と面取りの 2 辺に深さ 3.10 mm の壁、足は 1.80 mm 幅を左右に 2 本ずつ。接点の舌（幅 3.30 mm）は電池を挿す側に付け根があり、電池の上面に乗った状態にした。電池を挿す側の 2 つの角は R0.6 で丸めた（シルクも同じ）。電池（φ20 × 3.2 mm）はパッド 2 の中心に置いた。舌の付け根の位置と角の丸めは、図面と製品写真からの推定 |
| `Jack_3.5mm_KoreanHropartsElec_PJ-320D-4A_Horizontal.step` | Connector_Audio:Jack_3.5mm_KoreanHropartsElec_PJ-320D-4A_Horizontal（KiCad 標準。標準のモデルは配布されていない） | 3.5 mm のイヤホンジャック（HRO PJ-320D-4A）。HRO の寸法図（PJ-320D、2016-05-03）から起こした。本体 12.10 × 6.10 × 5.00 mm、差し込み口 φ5.00 × 2.00 mm、穴 φ3.60 mm。端子の幅と厚さは推定 |
| `Texas_RUX_VQFN-HR-12_2.5x2mm_P0.5mm.step` | Texas_RUX_VQFN-HR-12_2.5x2mm_P0.5mm | 電源切り替え IC（U3、TI TPS2121RUX）。TI のパッケージ図（RUX0012A、4224010/A、`docs/references/power/tps2121.pdf` の 38 ページ）から起こした。本体 2.0 × 2.5 mm、高さ 0.9 mm（最大 1.0 mm）。上面に 1 番ピンの印。端子の大きさは図の範囲の中央値 |
| `Texas_X2QFN-12_1.6x1.6mm_P0.4mm.step` | Package_DFN_QFN:Texas_X2QFN-12_1.6x1.6mm_P0.4mm（KiCad 標準。標準のモデルは配布されていない） | USB-C の CC コントローラー（U6、TI TUSB320LAIRWBR）。TI のパッケージ図（RWB0012A、4221631/B、<https://www.ti.com/lit/ml/mpqf391c/mpqf391c.pdf>）から起こした。本体 1.6 mm 角、高さ 0.375 mm（最大 0.4 mm）。端子は幅 0.2 mm で、左右の 4 本（1・2・7・8 番）が長さ 0.5 mm、上下の 8 本が 0.3 mm。上面に 1 番ピンの印。端子の大きさは図の範囲の中央値 |
| `Oscillator_SMD_Abracon_ASE-4Pin_3.2x2.5mm.step` | Oscillator:Oscillator_SMD_Abracon_ASE-4Pin_3.2x2.5mm（KiCad 標準。標準のモデルは配布されていない） | 3225 サイズの 4 ピン発振器（Y402）。セラミックの台に金属のふた、裏の四隅に端子。3.2 × 2.5 mm。高さ（1.0 mm）は 3225 の発振器の一般的な値で、部品（仮に YXC OT2EL89CJI）の図面とは照合していない |

## リポジトリに含めていないモデル

clone した人は、入手元から取得してこのフォルダに置く。今はない。

| ファイル | 使っているフットプリント | 入手元 | ライセンス |
|---|---|---|---|
