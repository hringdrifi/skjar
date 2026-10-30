# キーマトリクスシート（keymatrix.kicad_sch）

42 キー、7 行 × 6 列、COL2ROW。キーとマトリクスの対応は [hardware/layout/README.md](../../hardware/layout/README.md) を参照。

## 部品

| 記号 | 部品 | シンボル | フットプリント | LCSC |
|---|---|---|---|---|
| SW501〜SW542 | Kailh Choc V2 系（ホットスワップ） | `Switch:SW_Push` | `Skjar:SW_Kailh_Choc_Hotswap` | ソケットは未選定 |
| D501〜D542 | 1N4148W | `Device:D` | `Diode_SMD:D_SOD-123` | C81598（Basic） |

- 番号は配列の左上から右下へ順に振っている（SW501 = Esc、SW542 = RCtrl）。各スイッチの `Matrix` 欄に行と列（例：`R0C0`）、`Size` 欄に幅（例：`1.25u`）を入れてある。
- 接続：`COLx` → スイッチ → ダイオードのアノード、カソード → `ROWy`。

## シートをまたぐネット

`ROW0`〜`ROW6`、`COL0`〜`COL5`（グローバルラベル、rp2040 シートの GPIO0〜12 へ）。
