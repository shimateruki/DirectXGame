# モデルとSpriteの未参照候補調査

## 2026-10-09 の整理結果

旧版と確認できた Game Over 背景 `gameover_backdrop_crown_v3`、`gameover_backdrop_crown_v4`、`kirby_sdx_gameover_backdrop` の3組、および削除済みのコウモリ・ビームドローン・キノコに対応する `ui/hud/morph_icons` と `ui/portraits` の12組を削除した。PNG、DDS、それぞれのメタデータを同時に整理した。現行 Game Over は `Resources/json/sprite/gameOverScene.json` の `gameover_backdrop_crown_v5.png` を使用する。HUD 肖像の自動生成対象は `ProjectWindow.cpp` の `kHudPortraitSpecs` に定義され、この3種の敵は含まれない。

以下の38モデルフォルダと、上記15組を除くSprite候補83組は**未削除**。静的な未参照だけでは、Editorで手動選択する素材や動的ロードを否定できない。38モデル・88組のSpriteをまとめて削除する操作は、自動承認レビューによりこの理由で却下された。後日整理する場合は、各モデルのシーン・制作ツール・プリセット参照と、各Spriteの動的パスを個別に確認する。

プリセットは `Resources/json/preset/presets.json` を調査した。現行の732件のうち668件は `StageObjects/`、38件は `Builtin/`、17件は `Stage1/` に属する。今回の38モデル候補を指すプリセットは見つからなかった。`StageObjects/` はゲームのシーン配置から作った再利用用データであり、ゲーム実行時に直接読まないことだけを理由に削除するとEditorでの再利用を失う。既存の未保存変更もあるため、今回プリセットの内容は変更していない。

調査日: 2026-10-09

## 目的と読み方

モデルとSpriteの「現行コード・設定データから利用を確認できないもの」を洗い出した。**ここに載ったものを削除してよいという判定ではない。** 保存しておきたい試作素材、Editorから手動で選ぶ素材、今後使う素材かどうかは別に決める。

指定により、`Resources/3DModel/Primitives`のモデルと、パーティクルの基本形状は対象外。後者は保守的に`Resources/3DModel/Effects/efect`と`Resources/sprite/particle`を候補から外した。`Resources/sprite/effect`の固有の王冠画像は基本形状とはみなさず対象に含めた。

## 調査方法と限界

- `Resources/3DModel`の`.obj`・`.gltf`などは、エンジンの`ModelManager::ResolveModelPath()`が扱う**モデルフォルダ単位**で集計した。同じモデルの複数ファイルは1件とする。候補の容量はそのフォルダ内の`.bin`・素材画像なども含み、`.meta`を除く。
- `Resources/sprite`は同名のPNG/DDSを**1組**として集計した。容量は対になった画像を合算する。単にDDSがコードに現れないことを理由に候補にはしない。
- `engine`・`game`のソース、`Resources/json`、モデル付属の`.gltf`・`.mtl`、`tools`の制作スクリプトを照合した。JSONはキーと値を構造として読み取った。短い相対パスと`Resources/...`の両方、モデルはフォルダ名とファイル名付き指定の両方を調べた。ステージ4・サンプル・Editorデータからの明示参照も「参照あり」と数える。配布用に生成されたソースコードのコピーは現行の参照元に含めない。
- `ModelManager::GetAvailableModelNames()`はモデルフォルダを列挙し、Editorから任意のモデルを選べる。この用途までは「使用中」と数えていない。同様に、`GameEditorController.Viewport.cpp`はSpriteのドラッグ投入を受け付ける。直接参照がなくても手動利用の可能性がある。
- 文字列を連結して作るパス、連番画像、外部ツールでの利用は直接照合では見落とす。確認できた動的読み込みと制作ツールからの明示参照は後述の保留欄へ移し、候補数には含めない。ゲームを起動して全画面を操作した調査ではない。

集計対象はモデル**215フォルダ**、Sprite**1,038組**。上記の除外と動的読み込み・制作ツール参照の保留を適用すると、今回ユーザーが判断するための候補は**モデル38フォルダ、Sprite98組**になった。これは静的な未参照候補数であり、未使用の確定数ではない。

## 先に確認したい大きな候補

| 候補 | 概算容量 | 根拠と確認点 |
|---|---:|---|
| `Resources/3DModel/Stages/bridge` | 97.2 MiB | モデル名・モデルファイルの明示参照なし。容量の多くは同フォルダのテクスチャ。名前が一般的なので、Editorで手動配置していないかを確認したい |
| `Resources/3DModel/Stages/garasu` | 34.5 MiB | 明示参照なし。複数のマテリアル画像を伴う |
| `Resources/sprite/fade/star_iris/frame_00～23` | 29.1 MiB | `star_iris`を指定するソース・JSONが見つからない。`Fade.cpp:135`の`crown_iris`連番とは別系列 |
| `Resources/sprite/ui/gameover`の背景3種 | 8.9 MiB | `gameover_backdrop_crown_v3`、`v4`、`kirby_sdx_gameover_backdrop`の明示参照なし。どれを現行画面が使っているか確認したい |
| `Resources/sprite/ui/control_guide/control_guide_panel` | 3.1 MiB | 明示参照なし。現行の操作ガイドは`book`以下の画像を使う |

容量は削除して得られる容量の保証ではない。モデルの付属画像やPNG/DDSのどちらを配布・生成対象にしているかで扱いが変わる。

## モデル: 明示参照が見つからない38フォルダ

以下はすべて`Resources/3DModel/`以下のフォルダ名。各フォルダに対応する`.obj`・`.gltf`、`.bin`、`.mtl`、テクスチャなどを**一体のモデル素材**として確認する。

| 区分 | 件数 | フォルダ名 |
|---|---:|---|
| `Characters/` | 6 | `bomb_slime`, `enemy_core`, `slimeBody_black`, `slimeBody_pink`, `slimeBody_yellow`, `suzanne` |
| `Gimmicks/` | 6 | `leverBou`, `leverDodai`, `magma_cannon`, `signboard`, `star_hook_anchor`, `star_spring` |
| `Miscs/` | 1 | `os` |
| `Samples/` | 5 | `multiMaterial`, `multiMesh`, `sampleBlock`, `sampleRun`, `simpleSkin` |
| `Stages/` | 20 | 下表 |

`Stages/`の20件:

| 系統 | フォルダ名 |
|---|---|
| 空・地形・従来のステージ小物 | `bossBattleSkydome`, `bridge`, `caldera_arch`, `dome`, `fence`, `flagBou`, `flagHata`, `forge_temple`, `galaxy_comet_arch`, `galaxy_orbit_platform`, `galaxy_spiral_tower`, `garasu`, `sand`, `skydome`, `soft_grass_patch`, `soft_mound`, `stairs`, `volcanic_spire`, `zimen` |
| 星の庭園系の別案・旧版候補 | `star_garden_bridge` |

38フォルダの付属ファイルを含む合計は約**171 MiB**。うち`Stages/bridge`と`Stages/garasu`の2フォルダで約132 MiBを占める。`Samples/`はゲーム本編の配置とは別に、モデル読み込み・アニメーション確認用のサンプルである可能性を考えて判断する。

### 制作ツールから明示参照されていたため候補から外した21モデル

ゲーム本編の配置だけを見ると不要そうでも、制作スクリプトの入力・生成・検証に使われる。`Characters/eye`は`tools/level_editor/model_preview.py`、`Characters/slime_white`は`tools/model_generation/generate_crown_slime.py`から参照される。`Stages/stage_select_hub_island`などのステージ選択系、`Stages/star_garden_course_v3`・`v4`などの星の庭園系、`Stages/fortress_grate_drum`などのマグマ面系も`tools/model_generation`または`tools/stage_generation`に参照がある。制作ツールを整理するときに改めて要否を判断する。

## Sprite: 明示参照が見つからない98組

以下は`Resources/sprite/`以下の拡張子を除いた名前。PNGとDDSが対になっていれば両方を1組として数える。特にUIは、現行の画像と旧版・差し替え前画像が混在し得るため、画面を開いてから要否を決める。

| フォルダ | 件数 | 名前 |
|---|---:|---|
| `fade/star_iris/` | 24 | `frame_00`～`frame_23` |
| `fade/` | 3 | `fade_slime_blob`, `fade_slime_ring`, `fade_star_blob` |
| `effect/crown/` | 2 | `crown_ray`, `crown_symbol_glow` |
| `ui/gameover/` | 4 | `gameover_backdrop_crown_v3`, `gameover_backdrop_crown_v4`, `kirby_sdx_gameover_backdrop`, `slime_swirl_eye` |
| `ui/control_guide/` | 1 | `control_guide_panel` |
| `ui/control_guide/labels/` | 1 | `bounce_evade` |
| `ui/gate/` | 2 | `gate_prompt_enter_e`, `gate_prompt_panel` |
| `ui/hud/` | 11 | `death_spotlight`, `hp_frame`, `life_meter_0`～`life_meter_6`, `reticle`, `slime_hurt_icon` |
| `ui/hud/morph_icons/` | 16 | `bat`, `bat_hurt`, `beam_drone`, `beam_drone_hurt`, `bomber`, `bomber_hurt`, `fire_slime`, `fire_slime_hurt`, `giant_slime`, `giant_slime_hurt`, `mushroom`, `mushroom_hurt`, `slime`, `slime_hurt`, `thunder_slime`, `thunder_slime_hurt` |
| `ui/pause/` | 4 | `pause_retry`, `pause_settings`, `pause_title`, `pause_title_return` |
| `ui/portraits/` | 8 | `bat`, `bat_hurt`, `beam_drone`, `beam_drone_hurt`, `giant_slime`, `giant_slime_hurt`, `mushroom`, `mushroom_hurt` |
| `ui/result/` | 5 | `bossText`, `GameOverText`, `poseText`, `restartText`, `titleReturnText` |
| `ui/result/clear/` | 1 | `stage_clear_text` |
| `ui/settings/`直下 | 5 | `settings_arrow_left_disabled`, `settings_arrow_right_disabled`, `settings_divider_line`, `settings_icon_screen`, `settings_row_disabled` |
| `ui/title/` | 2 | `title_main_menu_panel`, `title_main_menu_select` |
| `ui/tutorial/` | 2 | `tutorial_reach_enemy`, `tutorial_reach_next_enemy` |
| `title/`直下 | 2 | `title`, `titleText` |
| `misc/` | 4 | `color`, `image`, `image2`, `sample` |
| `Resources/sprite/`直下 | 1 | `font` |

98組の画像ファイルの合計は約**51.8 MiB**。このうち`star_iris`が約29.1 MiBで、最も大きい。`ui/hud/morph_icons`と`ui/portraits`は似た名前の別フォルダであり、現行HUDが使う`ui/portraits/player`等と混同しない。

## 「個別名の参照なし」でも候補から外したもの

| 素材群 | 保留理由 |
|---|---|
| `fade/crown_iris/frame_00～47` | `engine/graphics/postprocess/Fade.cpp:70,135`が連番パスを作る。個々のファイル名がコードに出ないことは正常 |
| `ui/hud/morph_gauge/fill_00～32` | `GamePlayScene.HUD.cpp:754`と`PreviewScene.cpp:793`が数字からファイル名を組み立てる |
| `Resources/sprite/number`の数字 | HUDなどが数字からファイル名を作る。個々の数字の静的参照数で判断できない |
| `ui/control_guide/book`以下 | `ControlsGuideOverlay.cpp:733,747`がページ・スロット・肖像の名前を連結する。JSONの直接参照がない画像も、ページ切替で使い得る |
| `ui/settings/text`以下 | `SettingsMenuOverlay.cpp:653`がラベルIDからファイル名を組み立てる。346枚を一律に未使用扱いできない。細かく削るなら、実際のID集合との照合が別途必要 |
| `Resources/sprite/generated/text`以下 | 生成文字画像。動的なパスやEditor保存データとの関係を確認するまで保留 |
| `Resources/sprite/particle` | ユーザー指定のパーティクル基本形状に当たる可能性があるため調査対象から除外 |

## 判断するときの順番

1. まず大容量の`Stages/bridge`、`Stages/garasu`と`fade/star_iris`を、残したい制作素材か確認する。`Characters/eye`は制作ツールから参照されるため、ツールを整理するときに別途判断する。
2. 次にゲームオーバー・HUD・操作ガイドの旧画像を実画面と比較し、現行素材と旧版を分ける。
3. 削除することになった場合は、モデルフォルダ内の依存ファイル、PNG/DDS対、`.meta`、Editor保存・Prefab・外部制作ファイルをまとめて確認する。まず別の場所へ退避して起動確認するのが安全。

今回は一覧化のみ。アセット・コードの削除や変更、ビルド、ゲーム内表示確認はしていない。
