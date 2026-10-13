# JSON未参照候補調査

## 2026-10-09 整理結果

現行のLight読込先は `Resources/json/light/<name>.json` であり、ルート直下の `lightlight_gamescene.json` と `lightlight_layout.json` はコード・他JSON・制作ツールに参照がないため、各 `.meta` とともに削除した。現行の `Resources/json/light/light_layout.json` とシーン別Light設定は残している。

下表の一般候補は、この2件を除いて **30件が未削除**。32件とメタデータの一括削除は、自動承認レビューが「動的参照やEditorでの手動利用を静的調査だけでは否定できない」として却下した。シナリオ、GPU粒子、カメラ、エフェクトは用途ごとの確認が必要。ルート直下の旧 `presets.json` には、現行 `preset/presets.json` に存在しない `aaaaaaaddddddddddd` という項目があるため、ユーザー作業の可能性を考えて残した。

シーン候補 `abc`、`new_scene`、`bossStage`、`level_design_sample` は未削除。前二者はEditor保存データ、`bossStage` は8オブジェクトの旧ボス面、`level_design_sample` はプレビュー用サンプルであり、未参照という理由だけで消すと制作データが失われ得る。

調査日: 2026-10-09

## 調査の前提

`Resources/json`以下の**591件**を調べた。「候補」はコード・他のJSON・制作ツールから明示的なファイルパスや名前の参照が見つからず、確認の優先順位が高いものを指す。**未使用の確定や削除指示ではない。** Editorのファイル一覧から手動選択できるもの、今後のステージや試作を残す目的のものもある。

ソースの文字列だけでなくJSONのキーと値を構造として読み、C++と制作スクリプトの引用文字列も照合した。引用符のない動的な名前生成や外部制作ファイルは完全には追えない。前回のモデル・Sprite調査と同じく、ステージ1～3以外のJSONも含めて調べた。

別のJSONから参照があれば、その参照元が今回の候補であっても「参照あり」と数えた。関連JSONをまとめて片付ける段階では依存の連鎖を改めて調べる。

## まず守るべき動的参照

| 種類 | 単独のファイル名がコードに現れなくても使われる理由 |
|---|---|
| シーンの`_player`・`_enemy`・`_object`・`_camera.json` | `game/system/LevelLoader.cpp:349-351`がベース名から自動生成する。例: `stage1.json`を開くと`stage1_enemy.json`などを読む |
| セーブスロット1～3 | `game/system/GameDataManager.h:88-93`がスロット番号から`savedata_slotN.json`を作る。これはユーザーの進行データでもある |
| 敵攻撃設定 | `game/actor/enemy/combat/EnemyAttackProfile.cpp:692,744`が敵種別から`false_king_slime.json`などのパスを作る。`BaseEnemy.cpp:154`から利用される |
| アニメーションClip | `BodyAnimationClip.cpp:146-174`がClip名を`animation_clip/*.json`へ解決する。Animator JSONの`bodyClipName`からも利用される |
| VFXシーケンス | 名前から読み込む仕組みがあり、48件すべてに別ソースで名前の参照が見つかった |
| GPUパーティクルとDebris | `GPUParticleManager.cpp:309`と`DebrisEffectManager.cpp:232`はフォルダを列挙して登録する。**登録されることとゲーム中に発生することは別**。GPUパーティクルは後述の6件で発生名の参照が見つからない |
| Editorの素材一覧 | `SceneSerializer.cpp:694`はシーンAssetを発見し、`GhostDirector.cpp:30-40`はScenario・Animation・VFXのJSON名を一覧化する。明示参照なしでもEditorから手動利用できる |

JSON構文も全件検査した。**590件は解析でき、`Resources/json/3Dobject/scene_layout.json`のみ0バイトでJSONとしては無効**だった。このファイルは`BaseScene.h:245`と`DebugEditor.h:457`の既定名に現れるため、今回の削除候補には含めない。実際にこの既定パスを開く経路の動作は未確認。

## 参照が見つからない一般JSON: 調査時32件、残り30件

次の表はシーン分割ファイル、セーブ、敵攻撃設定を除いた確認候補。パスは`Resources/json/`からの相対表記。**Editor専用の作業素材や試作を残すか**も含めて判断する。

| 系統 | 件数 | ファイル |
|---|---:|---|
| メッシュエフェクト | 10 | `effect/effect_pink_slime_morph_aura.json`, `effect/effect_pink_slime_morph_glint.json`, `effect/effect_player_giant_rush_launch.json`, `effect/effect_player_giant_rush_trail.json`, `effect/effect_player_pink_guard_crease.json`, `effect/effect_player_pink_hit_rim.json`, `effect/effect_stage_select_rain_ripple.json`, `effect/effect_warp_beam.json`, `effect/effect_warp_ring.json`, `effect/effect_wind_dash_trail.json` |
| GPUパーティクル | 6 | `gpu_particles/bossEfect.json`, `gpu_particles/gate_entry_dissolve_glints.json`, `gpu_particles/sample1.json`, `gpu_particles/stage_select_rain_ripple.json`, `gpu_particles/stage_select_rain_streak.json`, `gpu_particles/wind_slime_cutter.json` |
| 旧形式のアニメーション | 4 | `animation/anim_test1.json`, `animation/bridge_movie.json`, `animation/plaer.json`, `animation/player_move.json` |
| Scenario | 4 | `scenario/entry_gate_studio.json`, `scenario/tutorial_exit_reveal.json`, `scenario/tutorial_intro.json`, `scenario/tutorial_path_reveal.json` |
| レベル設計の試案 | 3 | `level_design/discovery_sample_plan.json`, `level_design/stage4_astral_garden_plan_before_vertical_test.json`, `level_design/stage4_object_before_vertical_test.json` |
| Camera設定 | 1 | `camera/tutorial_camera.json` |
| Trail設定 | 1 | `trail_emitter/sword_trail.json` |
| 旧Light設定らしいルートファイル | 2（削除済み） | ~~`lightlight_gamescene.json`, `lightlight_layout.json`~~ |
| 旧Preset形式らしいルートファイル | 1 | `presets.json` |

特に`effect/`の10件は、該当ファイルパスも固有名もコード・他JSONで見つからなかった。ただしEditorで開く可能性は残る。`gpu_particles/`の6件はフォルダ走査によって**起動時には登録され得る**が、Emit・自動Emitter・他JSONでの固有名指定が見つからない、という意味で候補にした。

`Resources/json/presets.json`は`NewPreset`などの古い形式の内容で、現在の`PresetManager.h:76`が既定で読む`Resources/json/preset/presets.json`とは別ファイル。後者は大きな現行プリセット集なので、この表には含めていない。

`ui/basicGuideBook.json`はゲームコードから直接読まないが、`tools/sprite_generation/generate_ability_book.ps1:32`が文字画像生成の入力として使うため候補から外した。

## シーンJSONはベース名でまとめて確認

分割ファイルの名前がコードに直接書かれないことは正常。シーンAssetを一単位として扱うと、次の4組が整理相談の候補になる。数字は`_player / _enemy / _object / _camera`の`objects`件数。

| シーンAsset | 件数 | 現状の判断材料 |
|---|---|---|
| `3Dobject/abc.json`と分割4ファイル | 0 / 0 / 0 / 0 | `_sceneAsset`の表示名は`New Scene`。空のEditorシーンとして残す必要があるか確認 |
| `3Dobject/new_scene.json`と分割4ファイル | 0 / 0 / 0 / 0 | 表示名は`sample`。Editorの新規シーン名の既定値にも`new_scene`があるため、名前だけで削除しない |
| `3Dobject/bossStage.json`と分割3ファイル | 0 / 0 / 8 / なし | 現行のステージ選択設定・ゲーム側に`bossStage`参照は見つからない。8 Objectの試作・旧ボス面か確認 |
| `3Dobject/level_design_sample.json`と分割4ファイル | 1 / 1 / 19 / 0 | `PREVIEW`用のレベル設計サンプル。ゲーム本編に配置されないことと、制作資料として不要なことは別 |

`abc`、`new_scene`、`level_design_sample`には対応する`Resources/json/sprite/*_sprite.json`もある。いずれも現時点のSprite配置数は0だが、シーンAssetを判断する際は合わせて扱う。

`stage1`～`stage5`、`stageSelect`、`tutorial`、`titleScene`、`gameOverScene`、`sample`はこの表に入れない。`stage5`は現状の配置が少なくても`StageCatalog.cpp:92`と`stage_select/stages.json`に登録されている。`sample`は`PreviewScene.cpp`の既定配置に使われる。

## 判断を進める順番

1. まず空の`abc`・`new_scene`、旧PresetとLight設定、`bossStage`を残す理由があるか決める。
2. 次にエフェクト10件とGPUプリセット6件をEditorでプレビューし、現行演出の部品か旧案か判断する。関連するSprite・モデル・VFXシーケンスの参照も合わせて確認する。
3. Scenarioとレベル設計案は制作資料としての価値を確認する。削除を決めた場合は、配置JSON、Prefab、生成ツール、`.meta`も含む依存確認を行う。

今回は静的調査とJSON構文検査のみ。JSON・コードの変更、削除、ビルド、ゲーム内動作確認はしていない。
