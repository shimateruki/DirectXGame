# 編集履歴と保存をデータ構造から読む

[目次](00_目次と読み方.md) / [シーン編集の概要](06_シーン編集とデータ保存.md)。ここでは Inspector で値を変え、Undo し、Scene を保存する一連の処理を説明する。対象は `EditorPropertyTransaction`、`EditorTransactionManager`、`SceneSerializer`、`SceneSavePreview`。

## 1. 編集操作を「値の前後」として保持する

位置を 1→2→3→4 とドラッグする場合、画面は途中の値を毎フレーム適用する。しかし Undo を四回しなければ元へ戻らない UI は使いにくい。必要なのは「操作開始前の1」と「確定後の4」を一つの履歴として保存すること。

[`EditorPropertyTransaction::TrackLastItem`](../engine/utility/SystemDebug/editor/core/EditorPropertyTransaction.h) は直前に描いた ImGui 項目の `IsItemActivated` で開始値を捕まえ、`IsItemDeactivatedAfterEdit` で終了値を捕まえる。`GetItemID` がどの UI 項目の操作かを結び付ける。ImGui は毎フレーム UI を描くが、編集中かどうかの状態は Item ID に関連付いて継続する。

開始と終了の値は lambda の値キャプチャに入る。`undo=[before,apply]{apply(before);}`、`redo=[after,apply]{apply(after);}` のように、「保存したデータ」と「再適用する操作」を一緒に保持する。これは **Command パターンに近い操作履歴**で、差分パッチを全メモリへ自動生成する仕組みではない。

## 2. 二つのスタックで戻す・やり直す

[`EditorTransactionManager`](../engine/utility/SystemDebug/editor/core/EditorTransactionManager.cpp) は Undo 用と Redo 用の deque を持つ。新規編集は Undo 側の末尾へ置き、Redo を空にする。Undo は末尾の操作を取り出して undo を呼び、同じ操作を Redo へ移す。Redo は逆方向。

```text
編集 A → 編集 B : Undo=[A,B], Redo=[]
Undo B          : Undo=[A],   Redo=[B]
新しく編集 C    : Undo=[A,C], Redo=[]
```

最後の行で B を残すと、B が前提とする状態が C によって変わっていて、意図しない状態へ戻る。通常の直線的な履歴なので新しい枝を作ったら旧 Redo を捨てる。履歴数は128件を上限に古いものを落とす。全操作の永久保存ではない。

Undo 中に setter が新しい履歴を登録すると、戻している操作がまた履歴に積まれる。`isApplying_` はこの再入登録を抑える。これはマルチスレッドの排他 mutex ではなく、履歴適用中という状態フラグである。

## 3. Groupは依存関係を逆順に戻す

複数 Object の編集や「生成してから値を設定する」を一回の Undo にまとめるため Group がある。最外側の EndGroup で一つの操作に包む。Redo は実行順、Undo は逆順。

例えば A=Object生成、B=位置設定なら、取り消しは B を戻してから A を戻す。先に A を取り消して Object を消すと、B が消えた Object へ触る可能性がある。逆順はこの依存を守る基本になる。ただし callback 自体が対象の寿命を安全に扱う必要は残る。生ポインタを捕まえた lambda は、履歴があるだけで Object の寿命を延ばさない。

`CancelInteractive` は登録待ちの操作を破棄するだけで、undo callback を呼ばない。**Cancel という名前だけで編集中の値も元へ戻ると判断しない**。また Transaction という名前でも DB のような一括永続化・障害時 rollback を提供するわけではない。

## 4. Property Registryが一括編集を可能にする

[`EditorPropertyRegistry`](../engine/utility/SystemDebug/editor/core/EditorPropertyRegistry.cpp) は編集項目の型・識別・取得・設定などを記述する登録情報を持つ。Inspectorなどの編集画面が同じ登録情報を参照できるため、Object型ごとにプロパティの定義を重複させずに済む。

これは **メタデータ駆動のProperty UI**。C++ の全メンバーを自動的に反射しているとは限らない。登録されていない値は出ず、Object が対応しない値は編集できない。Registry と Serializer も別の責務なので、表に出た値が必ず Scene JSON に保存されるとは考えず保存経路を追う。

## 5. Serializeはメモリを丸ごと保存することではない

[`SceneSerializer::SerializeObject`](../engine/utility/SystemDebug/editor/core/SceneSerializer.cpp) は Object の getter から必要な値を取り、JSON キーへ書く。GPU Resource のアドレス、C++ vtable、Scene の生ポインタなどをそのまま保存しない。次の起動でも意味を持つモデル名、設定、GUID、Transform等へ変換する。

GUID は表示名や配列番号が変わっても同じ Object を指すための持続的な識別子。`BuildSceneSaveTargets` は保存前に `EnsureUniquePersistentObjectGuids` で重複を検査し、名前なしや Editor 専用 Object を除外する。`SaveCategory` ごとに Player / Enemy / Object / Camera の JSON を作り、All の場合には Scene Asset のメタ情報も追加する。

**名前、型、保存カテゴリ、GUID は別の情報**。名前は表示・検索、型は復元時にどの種類を作るか、カテゴリは保存先、GUIDは同一性に関係する。これを全部「ID」とまとめると、名前変更で参照が壊れる問題を読み違える。

## 6. 保存Previewは候補データを作った後の確認

保存は概ね「BuildSceneSaveTargetsで候補を作る→SceneSavePreviewで既存ファイルと比べる→UIがConfirmを返す→SaveTargetsが書く」の順。Preview は JSON の文字列差分だけでなく Object ごとの追加・削除・値変更を整理する。利用者が何を保存するのか確認しやすくするため。

候補作成も完全に副作用ゼロとは限らない。GUID が未割当なら生成する等、保存に必要な識別の整備を伴う。ファイル書き込み前だからメモリも一切変化しない、という前提では読まない。

現行 `SaveTargets` の戻り値は保存対象ラベルをつないだ表示用文字列で、各ファイルの書込成功を集計した結果ではない。成功表示らしい文字が出たことだけで全ファイルの保存を証明できない。ファイルの実値と再ロードを確認する理由はこの契約にもある。

## 7. Scene・Preset・Prefab・Play変更の違い

Scene は配置された実体の集合。Preset は作成や再利用の元になる設定。Prefab の同期・Override は元設定からの差を扱う別の仕組みで、[`PresetManager.PrefabSync`](../engine/utility/SystemDebug/editor/core/preset/PresetManager.PrefabSync.cpp) と [`Overrides`](../engine/utility/SystemDebug/editor/core/preset/PresetManager.Overrides.cpp) が入口。元を変更したときに Instance の独自設定をどう保持するかが焦点になる。

Play 中は Player の位置、敵の状態、生成した弾などゲーム自身が大量の値を変える。[`PlayModeChangeTracker`](../engine/utility/SystemDebug/editor/core/PlayModeChangeTracker.cpp) は全てを編集値として保存せず、対象とプロパティを絞った差分候補を作る。持ち帰りを適用してもメモリ上の編集値なので、Scene への保存は別。ここでも GUID・実行時状態・保存設定の区別が効く。

## 8. 故障の場所を一段ずつ絞る

例としてライト強度を変更したのに次回消える場合、まず UI が setter を呼んだか、次に Render 用データへ反映されたか、Undo 用 apply が同じ再適用処理を通るか、SerializeObject にキーがあるか、SaveTarget が対象ファイルへ入ったか、読込側が同じキーを読むかを追う。

「保存処理がおかしい」と一括りにすると、UI未適用、別カテゴリ保存、読込キーの不一致、Play停止での復元を区別できない。コメントにはそれぞれの関数が**どの段階を完了させるか**を残す必要がある。
