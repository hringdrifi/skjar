# pcb

KiCad 10 のプロジェクト。**KiCad 10 で開いて保存すること**（古いバージョンで保存すると、ファイル形式の違いで問題が起きることがある）。

## 回路図のシート構成

| シート | 内容 | 仕様 |
|---|---|---|
| `skjar.kicad_sch` | 最上位のシート | |
| `power.kicad_sch` | USB-C ×2、TPS2121、CC の判定、負荷スイッチ、3.3 V のレギュレータ | [power.md](../../docs/schematic/power.md) |
| `rp2040.kicad_sch` | RP2040 とその周辺（フラッシュ、水晶、USB、テストパッド） | [rp2040.md](../../docs/schematic/rp2040.md) |
| `cm0.kicad_sch` | Compute Module 0、microSD、UART のバッファ、キー入力の I²C のプルアップ | [cm0.md](../../docs/schematic/cm0.md) |
| `display.kicad_sch` | 22ピン FPC コネクタ、LCD の電源 | [display.md](../../docs/schematic/display.md) |
| `peripherals.kicad_sch` | RTC、音声（TLV320DAC3100、12 MHz の発振器、スピーカー、イヤホンジャック） | [peripherals.md](../../docs/schematic/peripherals.md) |
| `keymatrix.kicad_sch` | キーマトリクス、キーの RGB（SK6812MINI-E ×42、電源スイッチ、バッファ） | [keymatrix.md](../../docs/schematic/keymatrix.md) |

設計の前提は [docs/design-decisions.md](../../docs/design-decisions.md) を参照。

## フォルダ

| フォルダ | 内容 |
|---|---|
| [lib/](lib/) | このプロジェクト専用のシンボルとフットプリント |
| [production/](production/) | 製造用データ（ガーバー、BOM、CPL） |
