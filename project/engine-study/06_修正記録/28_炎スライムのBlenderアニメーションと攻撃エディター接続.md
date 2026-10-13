# 炎スライムの Blender アニメーションと攻撃エディター接続

## 全体像

炎スライムでは、**姿勢を作る場所**、**姿勢の切替と補間を決める場所**、**攻撃の判定・弾・エフェクトを決める場所**を分ける。

| 場所 | 編集するもの | ゲーム中の役割 |
| --- | --- | --- |
| Blender の `fireslimeanim.blend` | Idle、Move、Breath、Fireball の4アクション | 本体メッシュの伸縮と前後の傾きのキーを作る。 |
| Animator Controller の `enemy_fire_slime.json` | 状態、ループ、速度、遷移条件、補間時間 | `MotionMode` の値で4つの Body Clip を切り替え、前後の姿勢を混ぜる。 |
| 敵攻撃エディターの `fire_slime.json` | ブレス・火球の本体状態、発生時刻、弾・判定・VFX | 攻撃ごとに Controller 状態を指定し、既存の攻撃実行経路を保つ。 |

編集元は [fireslimeanim.blend](../../Resources/3DModel/Characters/fireslimeanim/fireslimeanim.blend)。ゲーム本体は既存の `Characters/slime_red` を描画する。書き出した glTF はアニメーション制作エディターで4アクションと外観を確認するために使い、ゲーム中は同じキーから変換した Body Clip を**敵の個体ごと**に評価する。共有モデルのアニメーション時刻を複数の敵が上書きしないための構成である。

## 編集から実行まで

1. Blender で `Resources/3DModel/Characters/fireslimeanim/fireslimeanim.blend` を開く。スライムのアクション `Idle`、`Move`、`Breath`、`Fireball` のキーを編集して保存する。各アクションの最初のキーが再生時刻0秒になる。
2. リポジトリの `project` ディレクトリで次を実行する。この通常コマンドは保存済み `.blend` を**上書きしない**。

   ```powershell
   & 'C:/Program Files/Blender Foundation/Blender 4.4/blender.exe' --background --factory-startup --python-exit-code 1 --python tools/animation/build_fire_slime_animation.py
   ```

3. 書き出し結果は `Resources/3DModel/Characters/fireslimeanim/fireslimeanim.gltf` と `Resources/json/animation_clip/enemy_fire_slime_{idle,move,breath,fireball}.json`。前者はアニメーション制作エディターで `Characters/fireslimeanim` を読み込んで確認する。後者がゲームで読まれる姿勢データである。
4. 「ツール → 演出 → Animator Controller」で `enemy_fire_slime.json` を開き、状態と遷移・補間を調整する。各状態の `bodyClipName` が上記 Body Clip を参照する。`MotionMode` の整数条件は Idle=0、Move=1、Breath=2、Fireball=3 を初期設定としている。
5. 「ツール → 戦闘 → 敵攻撃プレビュー」で炎スライムの攻撃を開く。右の「演出参照 → 本体アニメーション状態」で攻撃に使う状態を選び、保存して再生する。火球は `Fireball`、近距離ブレスは `Breath`。状態の切替時に Controller の補間が適用される。

`.blend` を新しく作り直す場合だけスクリプトの末尾に `-- --create-sample` を付ける。この操作は既存の編集済み `.blend` を上書きするため、通常の再書き出しでは使わない。

## コード上の流れ

```text
Blender Action ──書き出し──→ Body Clip JSON
                              ↓ 読み込み（4件）
enemy_fire_slime.json ──→ Object3d の AnimatorControllerRuntime
                              ↑ MotionMode 設定
fire_slime.json ──攻撃の状態名──→ EnemyFireSlime::ApplySlimeAnimation
                              ↓
                  MeshRenderer の表示用 Transform

fire_slime.json ──弾・命中判定・VFX──→ 従来の攻撃実行経路
```

- `EnemyFireSlime::Initialize` は攻撃プロファイルを読み込み、`SetAnimatorController("enemy_fire_slime")` で Controller と4つの Body Clip を読み込む。読み込みに失敗した場合だけ旧来の手続きアニメーションを使う。
- `EnemyFireSlime::ApplySlimeAnimation` は移動中なら Move、火球中なら攻撃 JSON の `animation`、ブレス中なら同じく `animation`、それ以外は Idle を選ぶ。値は Controller の該当遷移に書かれた `MotionMode == 整数` 条件から取得するため、状態の配列順を並べ替えても壊れない。
- `AnimatorControllerRuntime::Update` は条件から状態遷移を選び、クリップの長さとループを管理する。`Object3d::ApplyAnimatorControllerPose` は遷移元・遷移先の Body Clip を別々の時刻で評価し、重み付きで伸縮・回転・位置・シアーを補間する。
- 姿勢は `MeshRenderer` の**表示用 Transform**に渡す。敵の物理 Transform と接触判定のスケールをアニメーションで変更しない。火球の生成、軌跡、ヒットボックス、プレイヤーのダメージ処理は攻撃 JSON と既存の C++ 実行経路が引き続き担当する。
- 火球の旧 `body_animation` トラックは外した。同じ本体姿勢を攻撃 Sequence と Controller の二重で適用しないためである。

## このアセットの表現範囲

この4アクションは**本体の伸縮と X 軸の前後傾き**を扱う。Blender の Z 上方向とゲームの Y 上方向に合わせ、書き出し時に拡縮の Y/Z を入れ替える。Body Clip 書き出しは位置移動・Blender の Y/Z 軸回転を検出するとエラーにする。対応していない動きを黙って捨てないためである。移動経路は敵AI、弾道は攻撃データ、炎の見た目は GPU パーティクル／VFX が担当する。

任意のボーン変形を必要とする敵は、この Body Clip 経路では表現できない。その場合はスケルトンを敵の個体ごとに評価する再生経路を別途実装する必要がある。今回の接続は炎スライムの本体モーション4種を Blender 編集・Controller 遷移・攻撃編集まで通したもので、すべての敵へ自動適用する汎用スケルトン基盤ではない。

## 確認項目

- Blender 再書き出しで4つの Body Clip と4つの glTF アニメーションが生成される。
- アニメーション制作エディターで `Characters/fireslimeanim` を読み込むと、メッシュ1、アニメーション4と表示される。
- 敵攻撃プレビューで炎スライムの火球を再生し、Controller 状態が Idle/Move から Fireball に変わることを確認する。
- 火球が標的に命中すると HP が減り、旧攻撃判定が消えていないことを確認する。ブレスも同様に `Breath` 状態と命中を確認する。
