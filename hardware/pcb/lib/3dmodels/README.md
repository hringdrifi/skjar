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
| `USB_C_Receptacle_HRO_TYPE-C-31-M-12.step` | Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12（KiCad 標準。標準のモデルは配布されていない） | USB-C レセプタクル。HRO の寸法図（rev A、2020-12-08）から起こした。シェル 8.94 × 7.30 × 3.26 mm、差し込み面は原点から 3.65 mm 手前。角の丸みと舌の寸法は推定 |

## リポジトリに含めていないモデル

clone した人は、入手元から取得してこのフォルダに置く。今はない。

| ファイル | 使っているフットプリント | 入手元 | ライセンス |
|---|---|---|---|
