# キーマトリクスシート（keymatrix.kicad_sch）

42 キー、7 行 × 6 列、COL2ROW。キーとマトリクスの対応は [hardware/layout/README.md](../../hardware/layout/README.md) を参照。キーごとの RGB（SK6812MINI-E）もこのシートに置く。決めた理由は [design-decisions.md](../design-decisions.md) の 6.1。

## 部品

| 記号 | 部品 | シンボル | フットプリント | LCSC |
|---|---|---|---|---|
| SW501〜SW542 | Kailh Choc V2 系（ホットスワップ） | `Switch:SW_Push` | `Skjar:SW_Kailh_Choc_Hotswap` | ソケットは未選定 |
| D501〜D542 | 1N4148W | `Device:D_Small` | `Diode_SMD:D_SOD-123` | C81598（Basic） |
| LED501〜LED542 | SK6812MINI-E | `LED:SK6812MINI-E` | `LED_SMD:LED_SK6812MINI-E_3.2x2.8mm_P1.5mm_ReverseMount` | C5149201 |
| C501〜C542 | 100 nF（各 LED の VDD） | `Device:C` | 0603 | C14663（Basic） |
| C543 | 10 µF（`+5V_LED` のまとめ） | `Device:C` | 0603 | C96446（Basic） |
| U501 | SN74AHCT1G125DBV（データ線を 5 V にするバッファ） | `74xGxx:74AHCT1G125` | `Package_TO_SOT_SMD:SOT-23-5` | C7484 |
| C544 | 100 nF（U501 の VCC） | `Device:C` | 0603 | C14663（Basic） |
| Q501 | AO3401A（`+5V_LED` のスイッチ、P-MOSFET） | `Transistor_FET:AO3401A` | `Package_TO_SOT_SMD:SOT-23` | C15127（Basic） |
| Q502 | 2N7002（Q501 のゲートを引く） | `Transistor_FET:2N7002` | `Package_TO_SOT_SMD:SOT-23` | C8545（Basic） |
| R501 | 100 kΩ（Q501 のゲート → `+5V`。初期状態は OFF） | `Device:R` | 0603 | C25803（Basic） |
| R502 | 10 kΩ（Q502 のドレイン → Q501 のゲート） | `Device:R` | 0603 | C25804（Basic） |
| C545 | 100 nF（Q501 のゲート〜ソース。ゆっくり ON にする） | `Device:C` | 0603 | C14663（Basic） |
| R503 | 100 kΩ（`RGB_EN` → GND。初期状態は OFF） | `Device:R` | 0603 | C25803（Basic） |
| R504 | 330 Ω（U501 の出力 → LED501 の DIN） | `Device:R` | 0603 | C23138（Basic） |

- 番号は配列の左上から右下へ順に振っている（SW501 = Esc、SW542 = RCtrl）。各スイッチの `Matrix` 欄に行と列（例：`R0C0`）、`Size` 欄に幅（例：`1.25u`）を入れてある。LED50x と C50x は、同じ番号の SW50x に付く。
- 接続：`COLx` → スイッチ → ダイオードのアノード、カソード → `ROWy`。

## キーの RGB

| 部品 | 接続 |
|---|---|
| Q502 | ゲート ← `RGB_EN`（R503 で GND へ）、ソース → GND、ドレイン → R502 → Q501 のゲート |
| Q501 | ソース ← `+5V`、ドレイン → `+5V_LED`、ゲート ← R502（R501 と C545 でソースへ） |
| U501 | VCC ← `+5V_LED`（C544）、GND → GND、~OE → GND（常に有効）、A ← `RGB_DATA`、Y → R504 → LED501 の DIN |
| LED501〜LED542 | VDD ← `+5V_LED`（各 100 nF）、VSS → GND。DOUT → 次の番号の DIN。LED542 の DOUT は未接続 |

- **ON / OFF：** `RGB_EN` が High → Q502 が ON → Q501 のゲートが約 0.45 V（R501 と R502 で分圧。Vgs ≈ −4.5 V）→ `+5V_LED` が ON。ON にかかる時間は約 1 ms（R502 と C545）、OFF は約 10 ms（R501 と C545）。
- **U501 を `+5V_LED` で動かす理由：** 電源を切った LED に、データ線から電流を流し込まないため。U501 の入力は電源が 0 V でも 5.5 V まで耐える。
- **数珠つなぎの順番：** 回路図では番号順（LED501 → LED542）。基板で配線が短くなる順番にしたら、番号を振り直す。

## シートをまたぐネット

| ラベル | 向き |
|---|---|
| `ROW0`〜`ROW6`、`COL0`〜`COL5` | rp2040 シートの GPIO0〜12 へ（グローバルラベル） |
| `RGB_DATA`、`RGB_EN` | 入力（← rp2040 の GPIO14 / GPIO20） |

`+5V` は電源記号。`+5V_LED` は `power:+5V` の値を書き換えた電源記号で扱う（使うのはこのシートだけ。PWR_FLAG 付き）。用紙は A2（RGB の部品を置くため A3 から広げた）。
