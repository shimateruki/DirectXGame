# 外部ライブラリ管理方針

このプロジェクトでは、授業・チーム開発環境で同じ手順でビルドできるように、主要な外部ライブラリを `externals/` 配下に同梱して管理します。

## SDL2

- `externals/SDL2/include/` はビルド時に必要なヘッダーです。
- `externals/SDL2/lib/x64/SDL2.lib` はリンク時に必要です。
- `externals/SDL2/lib/x64/SDL2.dll` は実行時に必要で、PostBuildEvent で出力先へコピーします。
- 現状は vcpkg や NuGet などで自動取得する構成ではないため、SDL2 を `git rm --cached` でリポジトリから外さないでください。

## assimp

- `externals/assimp/include/` はビルド時に必要なヘッダーです。
- `externals/assimp/lib/` 配下の `.lib` は構成ごとのリンク時に必要です。
- 現状は自動取得構成ではないため、assimp も `externals/` 配下で同梱管理します。

## 不要ファイルの扱い

- `generated/`、`x64/`、`.vs/`、`output/`、`docs/` などのローカル生成物はコミットしません。
- 既に追跡済みの生成物が混入した場合は、対象を確認してから `git rm --cached <path>` でリポジトリ管理から外します。
- `Resources/3DModel/**/*.obj` はVisual Studioの中間ファイルではなくモデルアセットなので、削除対象にしません。
