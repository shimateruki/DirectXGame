# Animator Controller エディターの実装とゲームへの接続

[目次](../00_目次と読み方.md) / [アニメーション概要](07_アニメーション演出リプレイ.md) / [骨・スキニングの詳細](07A_骨の補間とAnimatorの状態遷移.md)

## 1. 最初に結論：このエディターは何を作るか

Animator Controller は、**作成済みの動きをどの状態で再生し、どの条件で次へ切り替えるか**を設定するエディターである。現在の実装は、状態、遷移、パラメータ、イベントを ImGui で編集し、JSON に保存する。プレビュー対象の Scene Object に Controller を割り当て、状態を再生する操作もある。

例えば「待機」「走行」「ジャンプ」の姿勢を作る場所は、モデルのアニメーションデータや Animation Workbench の Body Clip である。Controller はそれらを名前で参照し、再生状態と切替時間を管理する。移動速度の計算、物理座標、攻撃判定、ダメージ計算を Controller 自身が実装するわけではない。

```text
Animation Workbench / モデル制作
    └─ 動きの中身（キー、姿勢、クリップ）を作る
                  ↓ クリップ名
Animator Controller Editor（ImGui）
    └─ State・Transition・Parameter・Event を JSON に保存する
                  ↓ 読み込み
AnimatorControllerRuntime（C++ の有限状態機械）
    └─ 現在状態、時刻、遷移条件、ブレンド率を計算する
                  ↓ 現在状態・二つの時刻・ブレンド率
Object3d / PlayerSlimeAnimator
    └─ 実際の姿勢を作り、モデルに反映する
```

主な入口は [`AnimatorControllerEditor.cpp`](../../engine/utility/SystemDebug/editor/windows/AnimatorControllerEditor.cpp)、データ型と実行器は [`AnimatorController.h`](../../engine/animation/AnimatorController.h) と [`AnimatorController.cpp`](../../engine/animation/AnimatorController.cpp) にある。このエディターは **ImGui のタブとリストで編集する方式**であり、ノードを線でつなぐグラフエディターではない。

## 2. JSON に保存されるものと、再生時だけ存在するもの

`AnimatorControllerAsset` は編集可能な定義、`AnimatorControllerRuntime` は一つの再生対象が持つ実行状態である。この分離により、一つの定義を複数 Object が参照しても、現在時刻や Trigger は対象ごとに独立する。Runtime は Asset を所有しないため、参照先の Asset は Runtime より長く生存する必要がある。

| データ | 主な項目 | 意味 |
| --- | --- | --- |
| Asset 全体 | `entryState`、`states`、`transitions`、`parameters` | 初期状態と状態機械の定義 |
| State | `name`、`clipName`、`bodyClipName`、`speed`、`loop` | 再生する動きと速度・繰り返し |
| State の切替既定値 | `blendDuration`、`blendEasing` | 個別の Transition がないときの補間設定 |
| Transition | `fromState`、`toState`、`duration`、`easing`、`hasExitTime`、`exitTime`、`conditions` | どこからどこへ、いつ、何秒かけて切り替えるか |
| Parameter | Float、Int、Bool、Trigger と初期値 | 遷移条件が参照する値 |
| Event | `name`、`payload`、`normalizedTime` | 状態の時間軸上で通知するイベント |
| Runtime | 現在・前状態の番号と時刻、遷移経過、現在の Parameter、未消費 Event | 再生対象ごとに変わるため、Asset JSON には保存しない |

保存処理は [`AnimatorControllerAsset::Save`](../../engine/animation/AnimatorController.cpp) にあり、`nlohmann::json` で `Resources/json/animator/<名前>.json` を書き出す。読み込み時は配列を一度消してから JSON の定義へ置き換える。JSON の `version`、`entryState`、各 State などがそのまま「何を再生するか」の設定になる。ただし **JSON に状態を追加しただけでは、ゲーム側がその状態へ移るとは限らない**。

## 3. 編集画面はどのコードで作られているか

[`AnimatorControllerEditor::DrawImGui`](../../engine/utility/SystemDebug/editor/windows/AnimatorControllerEditor.cpp) は `USE_IMGUI` の下で、Asset 操作、Preview / Assign、States、Transitions、Parameters を描く。ImGui は毎フレーム UI を関数呼び出しで組み立てる即時モード UI なので、ボタンを押したフレームに `controller_` の値を変更し、保存ボタンで JSON へ書き込む。

### Asset と Preview / Assign

- `RefreshFiles` は `Resources/json/animator/` と `Resources/json/animation_clip/` の JSON を列挙する。前者は Controller、後者は Body Clip の候補である。
- `NewController` は `Idle` 一状態を持つ新規定義を作る。`SaveController` が JSON に保存し、既に割り当てられた対象があれば再読み込みする。
- `ResolvePreviewTarget` は保存しておいた Object 名から現在シーンの Object を探し直す。シーンを切り替えた後に古い生ポインタを使わないための実装である。
- 「このControllerをObjectへ割り当て」は `Object3d::SetAnimatorController` を呼ぶ。「選択Stateを再生」は `Object3d::PlayAnimatorState` を呼ぶ。**これは選択した Scene Object に対する確認**で、独立したプレビュー空間や攻撃判定シミュレーターではない。特に Player の通常スライム姿勢は別の `PlayerSlimeAnimator` が所有する Controller で更新するため、このボタンだけで Player 側の Body Clip 状態を切り替えたと判断してはいけない。

### States タブ

[`DrawStateEditor`](../../engine/utility/SystemDebug/editor/windows/AnimatorControllerEditor.cpp) は状態の追加・改名・削除、Entry State の指定、クリップ、速度、ループ、切替既定値、イベントを編集する。`Model Animation Clip` は対象モデルが持つ骨アニメーション名、`Body Animation Clip` は Workbench で保存した全身変形クリップ名である。状態を改名すると、このエディター上の Entry State と Transition の参照名も更新する。

### Transitions タブ

[`DrawTransitionEditor`](../../engine/utility/SystemDebug/editor/windows/AnimatorControllerEditor.cpp) は `From → To`、秒単位の `Duration`、補間曲線の `Easing`、Exit Time、条件を編集する。`From` を `Any State` にすると、現在状態に関係なく候補になる。条件を複数付けた場合は **全条件が成立**する必要がある。

### Parameters タブ

[`DrawParameterEditor`](../../engine/utility/SystemDebug/editor/windows/AnimatorControllerEditor.cpp) は Float / Int / Bool / Trigger を定義する。対象 Object に Controller が割り当て済みなら、プレビュー中の Runtime Value を操作できる。これは確認用の操作であり、**ゲーム本編が毎フレームその値を書き込む処理は別途必要**である。

## 4. ゲーム中の有限状態機械はどう動くか

有限状態機械（Finite State Machine、FSM）は、「同時に一つの主状態を持ち、条件で別の状態へ移る」仕組みである。実体は [`AnimatorControllerRuntime`](../../engine/animation/AnimatorController.cpp) にある。

1. `SetController` / `Reset` で Parameter を初期値に戻し、`entryState` を開始する。Entry が不明なら最初の State を使う。
2. `Update(deltaTime, durationResolver)` で `現在時刻 += deltaTime × State.speed` として再生時間を進める。既知のクリップ長があり `loop` なら折り返し、単発なら末尾で止める。
3. 現在時刻を基準に Event を収集する。次に State 固有の Transition、Any State の Transition の順に調べる。同じ優先度なら JSON に並んだ順序が優先される。
4. Exit Time と全条件を満たし、行き先 State が有効なら `CrossFade` を開始する。成立した遷移で使った Trigger はここで消費する。
5. 遷移中は新旧両方の時刻を進め、経過が `Duration` に達すると旧状態を破棄する。

Float は `Greater` / `Less`、Int は `Equals` / `NotEqual`、Bool は `If` / `IfNot`、Trigger は一回限りの `Trigger` 条件に対応する。条件も Exit Time もない Transition は、毎フレーム無条件で自動遷移し続けないよう自動選択から除外される。自分自身への自動遷移も除外される。

明示的な `CrossFade("Run")` は、自動遷移の条件判定を経ずに状態を要求する。この場合でも一致する Transition の切替時間・Easing は設定値として参照され得る。**「条件があるからC++からの明示切替も禁止される」わけではない**。

クリップの実長が分からない場合、Event と Exit Time は `eventTimelineDuration` を代替の時間軸として使う。ただし実際の再生時刻は、長さが不明なまま勝手にループしない。ここでいう正規化時刻は、開始を0、長さを1とした割合である。

## 5. CrossFade の技術：時間を混ぜるのではなく姿勢を混ぜる

`CrossFade` は旧 State と新 State、それぞれの再生時刻を保持する。切替に0.20秒を設定した場合、開始時に新状態の重みはほぼ0、0.10秒後にほぼ0.5、0.20秒後に1になる。ただし Easing を指定すると、中間の増え方は直線とは限らない。

```text
進行率 = 経過秒数 / 切替秒数
新状態の重み = Easing(進行率)     // 0～1
最終姿勢 = 旧姿勢と新姿勢を重みで混合
```

[`GetTransitionWeight`](../../engine/animation/AnimatorController.cpp) が重みを返す。`Linear` は等速、`Ease In` / `Ease Out` は始点・終点の変化を調整し、`Smoother Step` は両端を滑らかにする。**切替時間とEasingは姿勢の切替の設定**であり、移動速度や攻撃ダメージを補間する設定ではない。

### 骨付き Object3d の場合

[`Object3d::UpdateAnimatorController`](../../engine/graphics/3d/object/Object3d.Animation.cpp) は Runtime を更新し、現在 State の `clipName` からモデルのアニメーションを探す。遷移元と遷移先の両クリップがあれば [`Model::ApplyBlendedAnimation`](../../engine/graphics/3d/model/Model.Skeleton.cpp) が各 Joint を旧時刻・新時刻でサンプルする。位置と大きさは Lerp、Quaternion の回転は Slerp で混ぜる。最後に Joint 行列を更新し、その骨姿勢を描画で使う。骨変形と GPU スキニングの続きは [07A](07A_骨の補間とAnimatorの状態遷移.md) を参照。

`clipName` が空、またはモデル内で見つからなければ、この経路では姿勢を適用できない。**State が存在するだけで骨アニメーションが自動生成されるわけではない**。

### プレイヤースライムの場合

スライムは骨付き Object3d と同じ混ぜ方ではない。[`PlayerSlimeAnimator::SetMode`](../../game/actor/player/animation/PlayerSlimeAnimator.cpp) がC++の `Mode` を `Idle` などの State 名へ変換して `CrossFade` を呼ぶ。[`TryBuildAuthoredBodyPose`](../../game/actor/player/animation/PlayerSlimeAnimator.cpp) は State の `bodyClipName` から全身の拡縮・回転・描画用位置をサンプルする。Body Clip がない状態は C++ の `BuildModeScale` / `BuildModeRotation` が姿勢を作る。

切替中は **切替直前に保存したスライムの大きさ・傾き**から、今フレームの目標姿勢へ Runtime の重みで補間する。二つの Body Clip 全体を同時サンプルして混ぜる方式とは異なる。着地の潰れと反動、ジャンプ時の速度に応じたクリップ時刻、Ability Pose なども C++ が重ねる。Body Clip の位置キーは物理位置ではなく描画専用オフセットへ入る。

さらにコピー能力の攻撃モーションは [`PlayAbilityMotion`](../../game/actor/player/animation/PlayerSlimeAnimator.cpp) で別の Body Clip を読み、通常移動とは独立したレイヤーで再生する。呼び出し側の名前（例: `player_ability_fire_breath`）と同名の State が Controller にあれば、その `bodyClipName` を実クリップ名として読み、`speed` を呼び出し側の再生倍率に掛ける。同名 State がなければ従来どおり呼び出し名の Body Clip を読む。State 名は既存の攻撃ロジックが使う識別子として保持するため、実クリップを差し替えても攻撃分岐名は変わらない。

能力側の `loop`、ブレンドイン・アウト秒数は現在も攻撃処理の呼び出し引数が決める。State の `events` はこの別レイヤーでは発火しない。**攻撃StateをControllerに登録したことは、能力の発動条件や攻撃判定を移行したことではない**。`speed` を変更すると映像と既存の命中時刻がずれる可能性があるため、実プレイで確認する。

## 6. Event の技術：時刻をまたいだかで通知する

State の Event は `normalizedTime` で発火位置を指定する。例えば1秒の時間軸で0.3なら開始から約0.3秒の位置になる。[`CollectCrossedEvents`](../../engine/animation/AnimatorController.cpp) は `現在時刻 == 0.3` とは比較せず、**前回時刻から今回時刻までの区間を通過したか**で判定する。0.29→0.31秒の更新でも取りこぼさないためである。ループ前の生時刻を使い、一更新で複数周した場合も各周の Event を調べる。

Object3d と PlayerSlimeAnimator は `ConsumeEvents` で発火済み Event を取り出す。`FeedbackCue` と空でない `payload` の組み合わせは `VFXSequencer::PlayOneShotOnTarget` へ渡され、対応する演出 Cue を再生する。Object3d には追加の Event callback もある。**Event は通知の仕組みであり、攻撃判定やHP減少を自動で保証するものではない**。それらを Event に結びつけたい場合は、受け取り側のゲームロジックが必要になる。

## 7. このゲームで実際にどこまで使われているか

現行の [`player_slime.json`](../../Resources/json/animator/player_slime.json) には `Idle`、`Run`、`Jump`、`Dash` などの通常 State と、現在のプレイヤー攻撃から参照する能力モーション State がある。`Idle`、`Run`、`Jump` には Body Clip 名がある。ゲーム側の `PlayerState` は行動状態 `Mode` を決め、[`PlayerSlimeAnimator`](../../game/actor/player/animation/PlayerSlimeAnimator.cpp) が Int Parameter `LocomotionMode`（Idle=0、Run=1、Jump=2、Dash=3、AimHook=4、ShootHook=5、PullEnemy=6、Carry=7、GateReturn=8、Disabled=9）へ渡す。JSON の Any State Transition はその値で行き先と切替時間を選ぶ。Player の実 Runtime の現在 State とブレンド率は Controller エディターの Preview / Assign で読める。保存後は現在シーンの Player が Controller を再読込する。

旧形式の Asset のように `LocomotionMode` がない場合、`SetMode` は従来の明示的な `CrossFade` を使う。`Disabled` は `PlayerSlimeAnimator::Update` を実行しないため、現在も `SetMode` で直ちに切り替える。JSON の条件で選べるのは**見た目の状態遷移**であり、`PlayerState` の物理移動やジャンプ可否は依然として C++ の責任である。

`Object3d` 側にも Controller の読み込み・更新・骨姿勢への適用経路がある。シーン JSON の `animation.animatorController` は [`LevelLoader`](../../game/system/LevelLoader.cpp) が読み、[`SceneSerializer`](../../engine/utility/SystemDebug/editor/core/SceneSerializer.cpp) が保存する。ただし現行のシーンデータでこの項目が空なら、その Object では Controller は動かない。**エンジンに機能があることと、各敵の攻撃で使われていることは別**である。

| 操作 | 現行データで期待できること | 期待できないこと |
| --- | --- | --- |
| `LocomotionMode=1` の `Idle/Any State → Run` Transition の `duration` を変える | JSON 条件で走行姿勢へ移る時間の調整 | プレイヤーの走る速度を変えること |
| `Run` の Body Clip を差し替える | スライムの見た目の拡縮・傾きの変更 | 物理的な移動ルートの変更 |
| `player_ability_fire_breath` の Body Clip を差し替える | 次の炎ブレスで使う見た目のクリップの変更 | ブレスの発動条件・命中範囲の変更 |
| 能力Stateの `speed` を変える | Body Clipの再生倍率の変更 | C++側の攻撃判定時刻の自動同期 |
| 新 State `Attack` を追加する | State 定義として JSON に保存 | C++側の呼出し・条件なしで攻撃が出ること |
| Event に `FeedbackCue` を置く | 対応する演出 Cue の発火 | 命中判定・ダメージの自動生成 |
| Object に骨クリップ付き Controller を割り当てる | 対応モデルの骨姿勢の再生・切替 | モデルに存在しないクリップの作成 |

## 8. 実際の編集・確認手順

1. Animator Controller を開き、既存Controllerから `player_slime` を選ぶ。Scene Object で Player を選ぶと、ゲーム本編の現在 State、`LocomotionMode`、切替重みを確認できる。
2. 通常動作の切替を直すなら Transitions タブで、例として `Any State → Run` の `Duration` と `Easing` を調整する。States タブの `Run` に設定した Body Clip は姿勢の中身である。`LocomotionMode` はゲームから毎フレーム設定するため、Player選択時には読取表示にしている。
3. 攻撃の見た目を直すなら States タブで `player_ability_...` または `player_base_...` の State を選び、Body ClipとSpeedを編集する。Preview / Assign の「保存して選択した能力モーションを確認」は見た目だけ再生する。**ここでは当たり判定もダメージも発生しない**。
4. 保存すると現在シーンのPlayerが再読込する。プレビューだけで終えず、実際の攻撃をプレイして姿勢と命中時刻が一致するか確認する。別シーンのPlayerは、そのシーンを読み込んだときに保存済みJSONを使う。

## 9. 「エディター主体」に近づけるなら、次にどこを接続するか

通常動作の `LocomotionMode` と能力クリップ参照の接続を起点にした次の実装順であり、**下記の追加接続は未実装**である。

1. 現在の `LocomotionMode` に加え、速度や接地、攻撃開始など、見た目の判断に必要な値を Runtime の Float / Bool / Trigger に渡す。`SetMode` と JSON 自動遷移の責任が二重にならないようにする。
2. 攻撃のループ、ブレンドイン・アウト、割り込みをStateまたは別の能力設定へ移す。現状は Body Clip名と速度だけがState経由であり、攻撃中の上書き順はC++が管理する。
3. 攻撃判定や弾の生成は共通ランタイムに残し、State / Event から識別子とタイミングを渡す。Editor はデータを編集し、実行器は同じデータをゲーム本編とプレビューで再生する。
4. Inspector に「現在 State」「成立した条件」「ブレンド率」「発火 Event」を見せると、JSON を直しても動かない理由を追いやすくなる。

最初から全攻撃を Controller に移すと、現在のコピー能力、攻撃判定、見た目の別レイヤー処理を壊しやすい。通常動作の状態選択を先に接続し、攻撃は別レイヤーと命中処理の設計を確定してから移す。

## 10. 読む順番と調査の目印

1. [エディターUI](../../engine/utility/SystemDebug/editor/windows/AnimatorControllerEditor.cpp)：入力がどの Asset 項目へ入るか。
2. [定義と保存](../../engine/animation/AnimatorController.h)、[実装](../../engine/animation/AnimatorController.cpp)：JSON、条件判定、時刻、Event。
3. [Object3d の反映](../../engine/graphics/3d/object/Object3d.Animation.cpp)、[骨の補間](../../engine/graphics/3d/model/Model.Skeleton.cpp)：骨モデルで見た目がどう変わるか。
4. [スライムの反映](../../game/actor/player/animation/PlayerSlimeAnimator.cpp)、[現在の定義](../../Resources/json/animator/player_slime.json)：このゲームのプレイヤーが実際に何を使うか。

確認したい現象ごとに見る場所も違う。「State を作ったのに再生しない」なら、呼出しまたは Parameter 更新と遷移条件を確認する。「切替時に姿勢が跳ねる」なら、旧・新クリップの内容、Duration、Easing、上書きレイヤーを確認する。「Event が出ない」なら Event の時間軸、Payload、受け取り側を確認する。
