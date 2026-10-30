# 3dmodels

フットプリントが参照する 3D モデル（STEP / WRL）の置き場。フットプリントからは `${KIPRJMOD}/lib/3dmodels/...` で参照する。

## 方針

- **koktoh 氏のモデルを優先して使う。** 再配布が許可されている（CC BY-NC-SA）ので、[koktoh/](koktoh/) にそのままコピーしてリポジトリに含める。作者・入手元・ライセンスは [koktoh/README.md](koktoh/README.md) に記録している。
- **それ以外のモデルはリポジトリに含めない**（`.gitignore` で除外）。再配布できるとは限らないため。手元に置き、下の表に入手元を記録する。
- 自作したモデルなど公開してよいものは、ルートの `.gitignore` に `!hardware/pcb/lib/3dmodels/<パス>` を追加して含める。

## リポジトリに含めていないモデル

clone した人は、入手元から取得してこのフォルダに置く。

| ファイル | 使っているフットプリント | 入手元 | ライセンス |
|---|---|---|---|
| `Low Profile Keycap 1U.step` | SW_Kailh_Choc_Hotswap | （未記入） | （未記入） |
