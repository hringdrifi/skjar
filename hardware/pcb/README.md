# pcb

KiCad 10 のプロジェクト。**KiCad 10 で開いて保存すること**（古いバージョンで保存すると、ファイル形式の違いで問題が起きることがある）。

## 回路図のシート構成（予定）

| シート | 内容 |
|---|---|
| `skjar.kicad_sch` | 最上位のシート |
| `power.kicad_sch` | USB-C ×2、TPS2121、CC の判定、負荷スイッチ |
| `rp2040.kicad_sch` | RP2040 とその周辺 |
| `cm0.kicad_sch` | Compute Module 0 |
| `display.kicad_sch` | 22ピン FPC コネクタ、LCD の電源 |
| `peripherals.kicad_sch` | RTC、音声 |
| `keymatrix.kicad_sch` | キーマトリクス |

設計の前提は [docs/design-decisions.md](../../docs/design-decisions.md) を参照。

## フォルダ

| フォルダ | 内容 |
|---|---|
| [lib/](lib/) | このプロジェクト専用のシンボルとフットプリント |
| [production/](production/) | 製造用データ（ガーバー、BOM、CPL） |
