# Tab能力ガイドの調整

基本操作・能力ページを同じ巻物で統一し、能力ページは縦スクロールにしています。
説明は実装に合わせた特徴・技の操作・使いこなしのヒントに分けています。

## 調整場所

- 文章: `Resources/json/ui/abilityGuideBook.json`
- 基本操作の文章: `Resources/json/ui/basicGuideBook.json`
- 配置: `Resources/json/sprite/controlsGuide.json`
- 画像: `Resources/sprite/ui/control_guide/book/`
- エディター: コマンド「能力ガイドの調整」でInspectorを開き、能力別プレビュー、配置、スクロール速度、肖像の揺れを調整して保存できます。プレビューはプレイヤーの能力を変更しません。

文章を変えたら、プロジェクト直下で以下を実行してください。

```powershell
& tools/sprite_generation/generate_ability_book.ps1
```

`-UpdateLayout` は初期配置を作り直します。エディターで保存した調整を残す通常の文章更新には指定しないでください。実行にはDevelopmentのTextPngTool.exeが必要です。更新した画像はゲームを再起動して確認してください。

`-UpdateBasics` は基本操作ページだけ初期配置を作り直します。能力ページの配置には触れません。エディターの「基本操作を表示」から基本操作側の部品も調整・保存できます。

## 実モデルの肖像

通常・ピンク・炎・雷・風・ボムの実ゲームglTFをBlenderで透過レンダリングしています。架空のイラストや装備は追加していません。これはライブ3Dではなく、モデルから生成した静止画と小さな上下揺れです。元モデルは変更しません。

```powershell
& 'C:/Program Files/Blender Foundation/Blender 4.4/blender.exe' --background --factory-startup --python-exit-code 1 --python tools/sprite_generation/render_ability_portraits.py -- --root (Get-Location).Path
```

## 装飾素材の生成方針

- `scroll_panel.png`: 透過背景の横長の巻物。アイボリーの面、シアンの縁、濃紺の影、右上のピンクのリボン。文字とキャラクターを入れず、広い余白を確保。
- `technique_row.png`: 透過背景の横長の技カード。白〜淡いシアンの丸い輪郭で、文字やアイコンなし。元画像の透明余白はレイアウトのtextureCropで切り取ります。

以上2点だけ画像生成を使用し、日本語は同梱フォントとTextPngToolで生成しています。

## 確認項目

Developmentビルドと実素材を使った配置・上下クリップのプレビューを確認済みです。プレビューは実機スクリーンショットではありません。実機ではTab開閉、左右ページ移動、上下・W/S・ホイールのスクロール、能力変更時の文章切替、再開後の入力を確認してください。
