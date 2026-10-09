# GPU 命令の同期と画面合成

[目次](00_目次と読み方.md) / [描画基盤の概要](02_DirectX12と描画パイプライン.md)

## 1. CPU が Draw を呼んだ時点では画面は完成していない

[`DirectXCommon`](../engine/graphics/core/DirectXCommon.cpp) は Command List に描画・コピー・状態遷移を記録する。記録は CPU の仕事であり、GPU がその処理を終えたことを意味しない。

`PostDraw` は Back Buffer を描画先から Present 用へ遷移し、計測用 Query を Resolve し、Command List を Close して Queue へ提出する。その後 `Present(1, 0)`、Fence の Signal、必要なら完了待ちへ進む。

```text
CPU：命令を記録 → Close → ExecuteCommandLists → Signal → 完了待ち
GPU：                        描画・コピーを実行 → Fence 到達
```

Fence の値は「GPU がキューのどこまで処理したか」を表す目印である。CPU が値を増やすだけでは完了しない。Queue に Signal を記録し、GPU がそこへ到達してから completed value が進む。

## 2. 現行フレームは末尾で GPU を待つ

`PostDraw` はそのフレームの Fence に届いていなければイベントを待つ。次回の `PreDrawRenderTexture` で同じ Command Allocator と Command List を Reset できる背景には、この待機がある。

Command Allocator は命令記録用メモリを管理する。GPU がまだその命令を読んでいる間に再利用すれば内容を壊しうるため、単に C++ 側の描画関数から戻っただけでは Reset の根拠にならない。

この方式は寿命管理を理解しやすい一方、CPU が数フレーム先の命令を準備して GPU と重ねる余地を制限する。複数フレーム分の Allocator・定数データと Fence を持つ方式は比較対象であり、現行コードをその方式として説明してはいけない。Swap Chain に複数画像があることと、CPU 側の全データが複数フレーム分管理されることも別である。

## 3. Resource と Descriptor は同じものではない

Resource は画素や頂点の実データが置かれる GPU リソース、Descriptor はそれをどの形式・範囲で読むか／書くかの情報である。同じ Resource に読み取り用 SRV と書き込み用 UAV を作れる。

[`SRVManager`](../engine/graphics/core/SRVManager.cpp) の番号は GPU メモリの生アドレスではなく、Descriptor Heap 内の位置である。CPU/GPU の Descriptor Handle は、それぞれ heap の先頭へ `番号 × descriptorSize` を足して求める。

初期払い出しは 2 からで、0 と 1 は ImGui 用予約。`Allocate` は mutex で番号払い出しを保護し、`nextIndex_` を増やす。ここには汎用的な空きスロット回収処理は見当たらない。Object を削除すれば全 Descriptor が自動的に番号再利用される、とは考えない。

mutex は CPU 上の同時操作を調整するもの。GPU がその Descriptor を使用中かどうかは別問題なので、mutex があるだけで GPU 寿命管理も済むわけではない。

## 4. Root Signature と PSO を分ける

Root Signature は「Shader にどんなリソースをどう結び付けるか」、PSO は「どの Shader・Blend・Depth・Rasterizer 設定で処理するか」の約束である。

HLSL の `register(t0)` と C++ の Root Parameter 番号は同じ番号体系ではない。Root Parameter 3 が SRV の t0 を含む Table を指す設計も可能である。`SetGraphicsRootDescriptorTable(3, ...)` の 3 を、t3 と読み替えてはいけない。

バッファに正しい値を書いていても、別の Root Parameter に渡した、PSO と異なる Root Signature を使った、CBV の配置と HLSL が食い違った、といった問題で結果が崩れる。データ内容と結び付けの両方を追う必要がある。

## 5. Barrier と Fence の違い

Transition Barrier は Resource の用途変更を GPU の処理列へ伝える。UAV Barrier は UAV アクセス間の依存を扱う。Fence はキューの進行と CPU 側の待機などに使う。三つを単なる「待つ命令」とまとめると役割を見失う。

GPU パーティクルなら Compute が粒子を書き、描画 Shader が粒子を読む。その間に UAV の書き込みと後続参照の関係を整え、必要な状態へ遷移する。Indirect 引数は Compute が書いた後、Indirect Argument として読める状態にする。

CPU が関数を順番に呼んだという事実だけで、GPU のすべての読み書きの依存が表現できるわけではない。[GPU パーティクル詳細](04A_GPUパーティクルのメモリと計算.md)では、実際の五種類のバッファに対応させて説明する。

## 6. GrabTexture はその時点の背景を固定する

`UpdateGrabTexture` は現在の RenderTexture を別の GrabTexture へ CopyResource する。

```text
RenderTexture：RENDER_TARGET → COPY_SOURCE → RENDER_TARGET
GrabTexture：PIXEL_SHADER_RESOURCE → COPY_DEST → PIXEL_SHADER_RESOURCE
```

このコピーには「ここまでに描いた背景」を後続効果用に保存する役割がある。現在書いている描画先を同時にそのまま背景入力として扱う代わりに、独立した画像を読む。

屈折が何をゆがめるかはコピー位置で決まる。コピー後に描かれた物体は、そのコピーの背景に含まれない。水やメッシュ効果の見た目を追う際は、Shader の UV 式だけでなく、Scene と `Game::DrawSceneToRenderTexture` のどこでコピーしたかを見る。

## 7. Shadow は別の視点の深度画像

Shadow Pass は光から見た深度を作り、後の物体描画で参照する。通常カメラの深度画像と混同しない。光から一番近い面より奥なら、その光が遮られているという比較になる。

現行の通常 Object Shader には 3×3 近傍の比較を平均する PCF がある。平均するのは遮蔽比較の結果であり、光の見える面積を直接レイトレーシングしているわけではない。Shadow Map の解像度、投影範囲、深度バイアスも輪郭や接地に影響する。

`SetShadowMapResolution` は GPU 完了を待ってからリソースを作り直す。解像度 UI を変えることは数値だけの変更ではなく、使用中の GPU リソースを置き換える操作を含む。

## 8. Bloom の多段画像処理

[`Game::ApplyPostEffectPipeline`](../engine/core/Game.cpp) は Bloom が無効なら抽出・縮小・加算を省略し、最終 Composite は実行する。Bloom を切ることと、色調補正や Tone Mapping を全部切ることは同じではない。

有効時の流れは次の通り。

1. 元画像から Bloom 用の明るい部分を抽出する。
2. 別の小さい画像へ順に Downsample する。
3. 元画像を合成用画像へ Copy する。
4. 各段の Bloom 画像を加算する。
5. 最終 Composite へ渡す。

小さい画像では一画素が元画像の広い範囲に対応するため、複数段を合わせると大小のにじみを作れる。大きなぼかしを元解像度だけで何度も処理する方法と、負荷と模様が異なる。

加算の前に `PreDrawScene(..., false)` を使う部分は、既にコピーした結果を消さずに上へ足すために重要である。パスごとに何を clear し、どの画像を読み、どの画像へ書くかを追う。

## 9. Game View と通常出力の分岐

エディター向けは最終結果を PostEffect の画像へ描き、SRV にして UI から参照する。通常実行は Back Buffer へ Composite する。Game View の大きさと内部の描画解像度、マウス座標と内部画素座標は別の空間になる。

エディターに画像が出るためには、シーン描画の成功に加えて、その出力 SRV が UI へ届いている必要がある。別カメラの Preview も独自の出力先を持つため、Play の描画とまったく同じパスが実行されるとは限らない。

## 10. CPU 時間と GPU 時間を分けて読む

CPU の時計で Draw 関数を囲むと、主に命令記録と、その範囲に入った待機時間が測られる。GPU Shader 単体の実行時間ではない。GPU 側では Timestamp Query を使い、結果を Resolve して読み出す。

「Draw 関数が長い」場合、CPU の命令作成が重いのか、Present や Fence で待っているのかを分ける。「Draw Call が多い」は命令数の情報であり、一回の重い Pixel Shader や大量の半透明重なりを直接表すものではない。

このコードの明示的な描画順、末尾の待機、各 Pass の GPU 計測を合わせて読むと、最適化すべき場所を誤りにくくなる。
