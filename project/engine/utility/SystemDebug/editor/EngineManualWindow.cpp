#include "EngineManualWindow.h"
#include "imgui.h"
#include "IconsFontAwesome5.h"

void EngineManualWindow::Draw() {
#ifdef USE_IMGUI
    if (!isOpen_) return;

    // ウィンドウの初期サイズを大きめに設定
    ImGui::SetNextWindowSize(ImVec2(800, 550), ImGuiCond_FirstUseEver);

    // ウィンドウ開始 (閉じるボタン付き)
    if (ImGui::Begin(ICON_FA_BOOK " エンジン説明書 (Engine Manual)", &isOpen_)) {

        // ==========================================================
        // 左ペイン：目次リスト (幅200ピクセルで固定)
        // ==========================================================
        ImGui::BeginChild("LeftPane", ImVec2(200, 0), true);

        const char* topics[] = {
            ICON_FA_INFO_CIRCLE " はじめに",
            ICON_FA_KEYBOARD " ショートカット一覧",
            ICON_FA_CUBES " 3Dエディタの基本操作",
            ICON_FA_CODE " シーンの初期化・実装例",
            ICON_FA_CUBE " オブジェクトの取得と連携",
            ICON_FA_BOLT " トリガーイベントの作り方",
            ICON_FA_VIDEO " カメラ設定",            
            ICON_FA_FILM " 録画機能 (GhostRecorder)", 
            ICON_FA_FIRE " GPUパーティクルの使い方",
            ICON_FA_LIGHTBULB " ライトと環境設定",
            ICON_FA_MAGIC " 通常パーティクル",
            ICON_FA_IMAGE " ポストエフェクト",
            ICON_FA_STAR " VFXシーケンサー (必殺技)",
            ICON_FA_BULLHORN " シネマティック監督 (GhostDirector)",
            ICON_FA_IMAGES " 2D UIエディタ (Sprite)"
        };

        for (int i = 0; i < IM_ARRAYSIZE(topics); i++) {
            // 選ばれている項目はハイライトされる
            bool isSelected = (selectedIndex_ == i);
            if (ImGui::Selectable(topics[i], isSelected)) {
                selectedIndex_ = i;
            }
        }
        ImGui::EndChild();

        // 左ペインと右ペインを横に並べる魔法の関数
        ImGui::SameLine();

        // ==========================================================
        // 右ペイン：詳細コンテンツ (残りの幅すべてを使用)
        // ==========================================================
        ImGui::BeginChild("RightPane", ImVec2(0, 0), true);

        switch (selectedIndex_) {
        case 0: // はじめに
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "[ エンジン説明書へようこそ ]");
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextWrapped("このウィンドウでは、エンジンの使い方や実装のヒントを確認できます。\n左側のリストから見たい項目を選択してください。");
            break;

        case 1: // ショートカット一覧
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "[ ショートカットキー一覧 ]");
            ImGui::Separator();
            if (ImGui::BeginTable("shortcuts", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("キー操作", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn("アクション");
                ImGui::TableHeadersRow();

                auto AddRow = [](const char* key, const char* action) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), key);
                    ImGui::TableSetColumnIndex(1); ImGui::Text(action);
                    };

                AddRow("F", "選択中のオブジェクトにカメラをフォーカス");
                AddRow("Delete", "選択中のオブジェクトを削除");
                AddRow("Ctrl + C", "選択中のオブジェクトを複製");
                AddRow("Ctrl + Z", "元に戻す (Undo)");
                AddRow("Ctrl + Y", "やり直し (Redo)");

                ImGui::EndTable();
            }
            break;
        case 2:
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), ICON_FA_CUBES " [ 3Dエディタの基本操作 ]");
            ImGui::Separator();
            ImGui::TextWrapped("ゲーム空間(Game View)上で、オブジェクトを直接配置・編集するためのメインエディタです。");
            ImGui::Spacing();

            if (ImGui::CollapsingHeader(ICON_FA_MOUSE_POINTER " 選択とギズモ(変形)操作", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::BulletText("Game View 上のオブジェクトを【左クリック】すると選択できます。");
                ImGui::BulletText("選択中、以下のキーボード操作でギズモ(操作ハンドラ)を切り替えます：");

                ImGui::Indent();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[ T ] 移動 (Translate)");
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[ R ] 回転 (Rotate)");
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[ S ] 拡大縮小 (Scale)");
                ImGui::Unindent();

                ImGui::BulletText("表示された矢印やリングをドラッグすることで、直感的にオブジェクトを変形できます。");
            }

            if (ImGui::CollapsingHeader(ICON_FA_PLUS_SQUARE " オブジェクトの新規配置 (プレビューモード)", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::BulletText("Hierarchy等から新しいオブジェクトを生成すると、マウスに追従する「プレビューモード」になります。");
                ImGui::BulletText("レイキャスト(自動地形判定)により、床や他の障害物の上に沿って自動的に高さが調整されます。");
                ImGui::BulletText("【左クリック】でその位置に確定(配置)し、【 E キー】を押すと配置をキャンセルします。");
            }

            if (ImGui::CollapsingHeader(ICON_FA_MAGNET " 便利機能", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::BulletText("グリッドスナップ: Inspectorの「スナップ」をONにすると、1m単位などで正確に整列配置できます。");
                ImGui::BulletText("Undo/Redo: ギズモでの移動を間違えた時は【Ctrl + Z】で戻し、【Ctrl + Y】でやり直せます。");
                ImGui::BulletText("複製 (Clone): 選択中に【Ctrl + C】を押すと、少し横にズレた位置にクローンが生成されます。");
            }
            break;
        case 3:
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), ICON_FA_CODE " [ シーンの初期化とレベル構築 ]");
            ImGui::Separator();
            ImGui::TextWrapped(
                "新しいシーン（ステージ）を作る際の、標準的なC++コードのテンプレートです。\n"
                "エディタで配置したJSONデータの読み込みと、C++側の操作準備(ポインタ取得)をここで行います。"
            );
            ImGui::Spacing();

            if (ImGui::CollapsingHeader(ICON_FA_LIST_OL " 初期化の基本4ステップ", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::BulletText("1. マネージャーの準備: ObjectManager や LevelLoader などを生成します。");
                ImGui::BulletText("2. JSONロード: エディタで作成したステージの配置データを読み込みます。");
                ImGui::BulletText("3. ポインタ取得: 読み込んだオブジェクトの中から、操作したいものを名前で探します。");
                ImGui::BulletText("4. 個別リソース: 必要に応じてBGMや特殊なエフェクトの読み込みを行います。");
            }

            ImGui::Spacing();

            // コードブロック風の背景色
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
            ImGui::BeginChild("CodeBlockSceneInit", ImVec2(0, 300), true);
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "// 例：Stage1Scene.cpp の Initialize() 内に記述します");
            ImGui::TextUnformatted(
                "void Stage1Scene::Initialize() {\n"
                "    // 1. サブシステムの初期化\n"
                "    objectManager_ = std::make_unique<ObjectManager>();\n"
                "    levelLoader_ = std::make_unique<LevelLoader>();\n"
                "\n"
                "    // 2. エディタで作成したJSONデータ(配置情報)をロード\n"
                "    levelLoader_->LoadObjectLayout(this, \"Resources/json/3Dobject/stage1.json\");\n"
                "    // levelLoader_->LoadSpriteLayout(this, \"Resources/json/sprite/stage1_ui.json\");\n"
                "\n"
                "    // 3. プログラムで操作したいオブジェクトのポインタを取得・保存\n"
                "    auto& objects = objectManager_->GetObjects();\n"
                "    for (auto& obj : objects) {\n"
                "        if (obj->GetName() == \"Player\") {\n"
                "            player_ = dynamic_cast<Player*>(obj.get());\n"
                "        }\n"
                "        // ※ボス等、専用クラスへの差し替え(昇格)もここで行う\n"
                "    }\n"
                "\n"
                "    // 4. BGMなどの個別ロード\n"
                "    // bgmHandle_ = AudioPlayer::GetInstance()->LoadSoundFile(\"...\");\n"
                "}"
            );
            ImGui::EndChild();
            ImGui::PopStyleColor();

            if (ImGui::Button(ICON_FA_COPY " テンプレートをコピー##SceneInit")) {
                ImGui::SetClipboardText(
                    "void Stage1Scene::Initialize() {\n"
                    "    objectManager_ = std::make_unique<ObjectManager>();\n"
                    "    levelLoader_ = std::make_unique<LevelLoader>();\n"
                    "\n"
                    "    levelLoader_->LoadObjectLayout(this, \"Resources/json/3Dobject/stage1.json\");\n"
                    "\n"
                    "    auto& objects = objectManager_->GetObjects();\n"
                    "    for (auto& obj : objects) {\n"
                    "        if (obj->GetName() == \"Player\") {\n"
                    "            player_ = dynamic_cast<Player*>(obj.get());\n"
                    "        }\n"
                    "    }\n"
                    "}"
                );
            }
            break;
        case 4:
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), ICON_FA_CUBE " [ オブジェクトの取得と連携 ]");
            ImGui::Separator();
            ImGui::TextWrapped(
                "エディタで配置したオブジェクトをC++側のプログラムで操作するための手法です。\n"
                "用途に合わせて「ポインタの取得」と「クラスの差し替え」の2パターンを使い分けます。"
            );
            ImGui::Spacing();

            // ---------------------------------------------------------
            // パターン1：ただのObject3dとしてポインタをもらう（武器やパーツ）
            // ---------------------------------------------------------
            if (ImGui::CollapsingHeader(ICON_FA_LINK " 【基本】ポインタの取得 (武器やパーツ)", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextWrapped("剣やボスの浮遊ブロックなど、専用クラスを作らないオブジェクトは、名前で検索して Object3d* として保持します。");
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
                ImGui::BeginChild("CodeBlockObj1", ImVec2(0, 160), true);
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "// 例: Playerの初期化時などに剣のポインタを確保する");
                ImGui::TextUnformatted(
                    "Object3d* swordObj_ = nullptr;\n"
                    "\n"
                    "auto& objects = SceneManager::GetInstance()->GetCurrentScene()->GetObjects();\n"
                    "for (auto& obj : objects) {\n"
                    "    if (obj->GetName().find(\"Sword\") != std::string::npos) {\n"
                    "        swordObj_ = obj.get(); // ポインタだけもらう\n"
                    "        break;\n"
                    "    }\n"
                    "}"
                );
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }

            // ---------------------------------------------------------
            // パターン2：専用クラスに差し替える（ボス本体など）
            // ---------------------------------------------------------
            if (ImGui::CollapsingHeader(ICON_FA_LEVEL_UP_ALT " 【応用】専用クラスへの差し替え (ボス本体など)", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextWrapped("AIや独自のUpdateを持つ敵キャラなどは、読み込んだ Object3d を独自のクラス(BossCoreなど)に中身ごと差し替えます。");
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
                ImGui::BeginChild("CodeBlockObj2", ImVec2(0, 200), true);
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "// 例: GamePlayScene::Initialize() でボスを昇格させる");
                ImGui::TextUnformatted(
                    "auto& objects = objectManager_->GetObjects();\n"
                    "for (auto it = objects.begin(); it != objects.end(); ++it) {\n"
                    "    if ((*it)->GetName() == \"Enemy_BossCore\") {\n"
                    "        // 新しいボスクラスを作り、元のデータをコピー\n"
                    "        auto newBoss = std::make_unique<BossCore>();\n"
                    "        newBoss->Initialize(object3dCommon_.get(), (*it)->GetModelName());\n"
                    "        newBoss->CopyFrom(it->get());\n"
                    "        \n"
                    "        // 実体を独自のボスクラスに差し替え！\n"
                    "        *it = std::move(newBoss);\n"
                    "        break;\n"
                    "    }\n"
                    "}"
                );
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }
            break;

        case 5:
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), ICON_FA_BOLT " [ トリガーと GameRule の使い方 ]");
            ImGui::Separator();
            ImGui::TextWrapped(
                "プレイヤーが特定の場所(透明ボックス)に触れた時の処理は、\n"
                "全て「GameRuleクラス」で一括管理されるイベント駆動(Dispatch & Subscribe)方式になっています。"
            );
            ImGui::Spacing();

            if (ImGui::CollapsingHeader(ICON_FA_CUBES " 1. エディタでの配置 (ワープやダメージ床)", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::BulletText("Hierarchyのシステム設定から【透明ボックス生成(トリガー用)】を配置します。");
                ImGui::BulletText("Inspectorの「コライダー属性」で、イベントやGround等、プレイヤーと当たる属性にします。");
                ImGui::BulletText("Inspectorの「ギミック設定(EventType)」から、動作のタイプを選びます：");

                ImGui::Indent();
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "【 Damage (ダメージ) 】");
                ImGui::Text("触れるとプレイヤーがダメージを受けます。");

                ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "【 Warp (ワープ入口) 】");
                ImGui::Text("送信先ID(Target)に「ワープ先のEventID」を指定すると、そこに瞬間移動します。");

                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "【 None (普通のスイッチ) 】");
                ImGui::Text("送信先ID(Target)と同じEventIDを持つギミック(ドア等)の OnTrigger() を遠隔起動します。");
                ImGui::Unindent();
            }

            if (ImGui::CollapsingHeader(ICON_FA_WRENCH " 2. 新しいイベント処理を自作する (プログラマ向け)", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextWrapped(
                    "「回復アイテム」や「即死トラップ」など、新しい種類のギミックを作りたい場合は、"
                    "GameRule.cpp に処理を追記するだけで簡単に実装できます！"
                );
                ImGui::Spacing();

                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
                ImGui::BeginChild("CodeBlockTrigger1", ImVec2(0, 320), true);

                ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "// ① Event.h の EventType に新しい種類を追加");
                ImGui::TextUnformatted(
                    "enum class EventType {\n"
                    "    None = 0,\n"
                    "    Damage,\n"
                    "    Warp,\n"
                    "    Heal, // ← ★追加！\n"
                    "};"
                );
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "// ② GameRule.cpp の switch(type) の中に処理を書く！");
                ImGui::TextUnformatted(
                    "switch (type) {\n"
                    "    // ...(WarpやDamageの処理)...\n"
                    "\n"
                    "    case EventType::Heal: // ← ★追加！\n"
                    "        if (whoHit->param_.has_value()) {\n"
                    "            whoHit->param_->hp += 50.0f;\n"
                    "            DebugConsole::GetInstance()->AddLog(\"Healed!\");\n"
                    "            // パーティクルを出したり、SEを鳴らす処理もここに書ける！\n"
                    "        }\n"
                    "        break;\n"
                    "}"
                );
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }
            break;

        case 6: // カメラと録画
           ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), ICON_FA_VIDEO " [ カメラ設定 (Camera Editor) ]");
            ImGui::Separator();
            ImGui::TextWrapped(
                "ゲーム中のプレイヤー追従カメラの設定や、エディタ編集時の自由カメラの操作を行います。\n"
                "画面上部の「▶再生 / ■停止」ボタンに合わせて、カメラのモードが自動的に切り替わります。"
            );
            ImGui::Spacing();

            // --- 自由操作モード ---
            if (ImGui::CollapsingHeader(ICON_FA_MOUSE_POINTER " 自由操作モード (Editor Mode)", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::BulletText("【■ 停止】(編集) 中のカメラ操作です。シーン全体を見渡すのに使います。");
                ImGui::Spacing();

                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "基本の視点移動:");
                ImGui::Indent();
                ImGui::BulletText("右クリック(長押し) + マウス移動 : 視点(首)を振る");
                ImGui::BulletText("W / A / S / D キー : 前後左右へ移動");
                ImGui::BulletText("Q / E キー : 上下(垂直)へ移動");
                ImGui::Unindent();

                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "便利な操作:");
                ImGui::Indent();
                ImGui::BulletText("Shift キー (押しっぱなし) : 移動速度アップ(ブースト)");
                ImGui::BulletText("中クリック(ホイール押し込み) + ドラッグ : 視点を変えずに平行移動(パン)");
                ImGui::BulletText("マウスホイール : ズームイン / アウト");
                ImGui::Unindent();
            }

            // --- ゲームカメラモード ---
            if (ImGui::CollapsingHeader(ICON_FA_GAMEPAD " ゲームカメラモード (Game Mode)", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::BulletText("【▶ 再生】中のカメラ挙動を設定します。Inspectorの「Camera Editor」から調整できます。");
                ImGui::Spacing();

                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "View Type (追従モード):");
                ImGui::Indent();
                ImGui::Text("・3人称 (Aimable) : プレイヤーの背後から追従。右クリックや右スティックで視点回転が可能。");
                ImGui::Text("・固定 (Fixed)    : 角度を固定して追従。(見下ろし型アクション等に最適)");
                ImGui::Text("・1人称 (FPS)     : プレイヤーの目線になるモード。");
                ImGui::Text("・周回 (Orbit)    : プレイヤーの周りを自動でぐるぐる回るモード。");
                ImGui::Text("・定点注視        : カメラの位置は固定し、プレイヤーを見つめ続けるモード。");
                ImGui::Unindent();

                ImGui::Spacing();
                ImGui::TextWrapped("※各モードに合わせて、プレイヤーからの距離(Distance)や高さ(Height)をリアルタイムに調整できます。");
            }

            // --- ファイル管理 ---
            if (ImGui::CollapsingHeader(ICON_FA_SAVE " 設定の保存と読み込み", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::BulletText("設定したカメラのパラメータは、名前をつけてJSONファイルとして保存できます。");
                ImGui::BulletText("保存先: Resources/json/camera/〇〇.json");
                ImGui::Spacing();

                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
                ImGui::BeginChild("CodeBlockCamera", ImVec2(0, 90), true);
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "// C++側でステージ固有のカメラ設定を読み込む例");
                ImGui::TextUnformatted(
                    "CameraEditor::GetInstance()->Initialize();\n"
                    "CameraEditor::GetInstance()->LoadFile(\"boss_camera.json\");"
                );
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }
            break;
		case 7:
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), ICON_FA_FILM " [ 録画とパス生成 (GhostRecorder) ]");
            ImGui::Separator();
            ImGui::TextWrapped(
                "オブジェクトの移動パス（軌跡）をエディタ上で視覚的に作成したり、\n"
                "実際の動きを録画してカットシーンやギミックのアニメーションを作るツールです。"
            );
            ImGui::Spacing();

            if (ImGui::CollapsingHeader(ICON_FA_MAP_SIGNS " パスエディタの直感的な操作 (Game View)", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextWrapped("Game View 上に表示される軌跡やピンを直接マウスで操作できます。");
                ImGui::Spacing();

                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "◆ 基本ショートカット");
                ImGui::BulletText("Ctrl + 左クリック : 線上をクリックして新しい中継点(Waypoint)を追加します。");
                ImGui::BulletText("ピンを選択して Delete : 選択中の中継点を削除します。");
                ImGui::BulletText("Ctrl + Z / Ctrl + Y : 操作を元に戻す / やり直す (Undo/Redo)");

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "◆ ギズモによる変形 (ピン選択時)");
                ImGui::TextWrapped("ピンを選択中に以下のキーを押すと、操作モードが切り替わります：");
                ImGui::Indent();
                ImGui::Text("[ T ] 移動  /  [ R ] 回転  /  [ S ] 拡大縮小");
                ImGui::Unindent();
            }

            if (ImGui::CollapsingHeader(ICON_FA_LINK " アンカーと相対座標 (ボス戦などで活躍!)", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextWrapped(
                    "「アンカー(相対基準)」を設定すると、パスの座標がそのオブジェクト基準になります。\n"
                    "例えば、ボスの腕の動きをパスで作る際、ボス本体をアンカーにしておけば、"
                    "ボスがどこに移動しても腕のアニメーションが正確に追従します。"
                );
                ImGui::BulletText("「相対データ化」にチェックを入れて生成(Generate)すると相対座標で保存されます。");
            }

            if (ImGui::CollapsingHeader(ICON_FA_VIDEO " シネマカメラとカットシーンの作成", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextWrapped(
                    "CinematicCamera をターゲットにしてパスを作り、\n"
                    "「Cinema Mode (カメラ乗っ取り)」にチェックを入れると、再生時にゲームカメラがその軌跡を辿ります。\n"
                    "これによりカットシーン(ムービー)を簡単に作成できます！"
                );
            }

            if (ImGui::CollapsingHeader(ICON_FA_CODE " C++側からのアニメーション再生", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextWrapped("保存したアニメーション(JSON)を、特定のオブジェクトに適用して再生します。");

                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
                ImGui::BeginChild("CodeBlockRecorder", ImVec2(0, 160), true);
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "// ギミックや敵の Update 等で呼び出す例");
                ImGui::TextUnformatted(
                    "// 1. レコーダーのターゲットに自分をセットする\n"
                    "ghostRecorder_->SetTarget(this);\n"
                    "\n"
                    "// 2. Play(ファイル名, ループするか, 相対座標か, カメラを乗っ取るか)\n"
                    "bool isLoop = true;\n"
                    "bool isRelative = true;\n"
                    "bool isCinematic = false;\n"
                    "ghostRecorder_->Play(\"door_open\", isLoop, isRelative, isCinematic);"
                );
                ImGui::EndChild();
                ImGui::PopStyleColor();

                if (ImGui::Button(ICON_FA_COPY " コードをコピー##Recorder")) {
                    ImGui::SetClipboardText("ghostRecorder_->SetTarget(this);\n    ghostRecorder_->Play(\"door_open\", true, true, false);");
                }
            }
            break;

        case 8:
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), ICON_FA_FIRE " [ GPUパーティクルエディタの使い方 ]");
            ImGui::Separator();
            ImGui::TextWrapped("数万個のエフェクトを軽量に処理できる、コンピュートシェーダー(CS)駆動のパーティクルシステムです。\n"
                "Hierarchyの「システム設定」>「GPUパーティクル」からエディタを起動できます。");
            ImGui::Spacing();

            if (ImGui::CollapsingHeader(ICON_FA_MAGIC " 基本的なワークフロー", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::BulletText("エディタ下部の「エディタ操作」にて、【連続発生テスト】にチェックを入れます。");
                ImGui::BulletText("各種パラメータを調整してエフェクトを作成します。");
                ImGui::BulletText("一番下の「保存と読み込み」から、プリセット名をつけて【保存】します。");
            }

            if (ImGui::CollapsingHeader(ICON_FA_STAR " 強力な機能ピックアップ", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "◆ 色とサイズの3段階イージング");
                ImGui::TextWrapped("Base(発生時) → Mid(中間) → End(消滅時) の3段階で変化します。\n"
                    "「色がMidになる時間(割合)」を調整し、30種類以上のカーブから最適なアニメーションを選択できます。");
                ImGui::Spacing();

                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "◆ 地形との衝突＆馴染み (Soft Fade)");
                ImGui::TextWrapped("「地形と衝突させる」をONにすると、パーティクルが物理的に床をバウンドします。\n"
                    "また「地形との馴染み (Soft Fade)」を上げると、地面にめり込んだ部分の境界線が自然にぼやけます。");
                ImGui::Spacing();

                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "◆ 乱気流 (Turbulence)");
                ImGui::TextWrapped("値を上げると、炎や魔法のような「うねり」のあるランダムな動きを再現できます。");
            }

            if (ImGui::CollapsingHeader(ICON_FA_CODE " C++側からの呼び出し方", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextWrapped("プログラム側でパーティクルを発生させる場合は、マネージャーを呼び出します。");

                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
                ImGui::BeginChild("CodeBlockGPUP", ImVec2(0, 180), true);

                ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "// 例1：固定の座標から発生させる場合");
                ImGui::TextUnformatted(
                    "Vector3 emitPosition = { 0.0f, 5.0f, 0.0f };\n"
                    "GPUParticleManager::GetInstance()->EmitFromPreset(\"FirePreset\", emitPosition);\n\n"
                );

                ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "// 例2：オブジェクト(Playerなど)の位置から発生させる場合");
                ImGui::TextUnformatted(
                    "if (player_) { // ※Initializeでポインタを取得しておくこと\n"
                    "    Vector3 playerPos = player_->GetTransform()->translate;\n"
                    "    GPUParticleManager::GetInstance()->EmitFromPreset(\"FirePreset\", playerPos);\n"
                    "}"
                );
                ImGui::EndChild();
                ImGui::PopStyleColor();

                if (ImGui::Button(ICON_FA_COPY " コードをコピー##GPUP")) {
                    ImGui::SetClipboardText("GPUParticleManager::GetInstance()->EmitFromPreset(\"FirePreset\", emitPosition);");
                }
            }
            break;
        case 9:
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), ICON_FA_LIGHTBULB " [ ライトと環境設定 (Light Editor) ]");
            ImGui::Separator();
            ImGui::TextWrapped(
                "ステージ全体の太陽光やフォグ、局所的な点光源・スポットライトを配置・調整するエディタです。\n"
                "Hierarchyのシステム設定から開くことができます。"
            );
            ImGui::Spacing();

            if (ImGui::CollapsingHeader(ICON_FA_SUN " 太陽とフォグ (Directional Light & Fog)", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "◆ 太陽と環境光");
                ImGui::BulletText("「光の向き」や「色」、影の濃さのベースとなる「環境光(Ambient)」を調整します。");
                ImGui::Spacing();

                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "◆ フォグとゴッドレイ (Volumetric Fog)");
                ImGui::BulletText("「フォグ関連をすべて有効化」にチェックを入れると、距離や高さに応じた霧が発生します。");
                ImGui::BulletText("ボリューメトリックフォグの「強さ」を上げると、光の筋(ゴッドレイ)が表現できます。");
            }

            if (ImGui::CollapsingHeader(ICON_FA_STREET_VIEW " 点光源とスポットライト (Point & Spot Lights)", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextWrapped("炎や街灯など、局所的な光源を配置します。【追加】ボタンでいくつでも生成できます。");
                ImGui::Spacing();

                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "◆ 追従モードと特殊な挙動 (超便利機能！)");
                ImGui::BulletText("「追従対象」を設定すると、特定のオブジェクト(プレイヤーや敵など)に光がくっついて動きます。");
                ImGui::BulletText("「位置ズレ(Offset)」を使って、キャラクターの頭上や手の位置などに微調整できます。");
                ImGui::BulletText("モードを「点滅(Flicker)」や「明滅(Sine)」にすると、炎の揺らぎやアラートランプのような演出が可能です。");
            }

            if (ImGui::CollapsingHeader(ICON_FA_EYE " エディタ上での視覚化 (Gizmos)", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextWrapped(
                    "「ライト位置を表示 (Gizmos)」にチェックを入れると、"
                    "各光源の位置に四角いブロックが表示され、空間のどこにライトがあるか一目で分かります。"
                );
            }

            if (ImGui::CollapsingHeader(ICON_FA_CODE " C++側からの読み込み", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextWrapped("作成したライティングはJSONとして保存し、各シーンの初期化時に読み込みます。");

                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
                ImGui::BeginChild("CodeBlockLight", ImVec2(0, 100), true);
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "// GamePlayScene::Initialize() 内の記述例");
                ImGui::TextUnformatted(
                    "LightManager::GetInstance()->LoadState(\"Resources/json/light/light_layout.json\");"
                );
                ImGui::EndChild();
                ImGui::PopStyleColor();

                if (ImGui::Button(ICON_FA_COPY " コードをコピー##Light")) {
                    ImGui::SetClipboardText("LightManager::GetInstance()->LoadState(\"Resources/json/light/light_layout.json\");");
                }
            }
            break;
        case 10:
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), ICON_FA_MAGIC " [ 通常パーティクルエディタの使い方 ]");
            ImGui::Separator();
            ImGui::TextWrapped(
                "UIエフェクトや、少数のリッチな演出を作成するためのCPUベースのパーティクルシステムです。\n"
                "GPUパーティクルとは異なり、時間経過による細かい変化(カーブ)を直感的に描けるのが最大の特徴です。"
            );
            ImGui::Spacing();

            if (ImGui::CollapsingHeader(ICON_FA_CHART_LINE " 直感的なカーブ編集 (Curve Editor)", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextWrapped(
                    "エディタ内に表示されている折れ線グラフの上で【左クリックしながらドラッグ】してみてください！\n"
                    "マウスの軌跡に合わせて、グラフの形を直接書き換えることができます。"
                );
                ImGui::Spacing();
                ImGui::BulletText("例：最初だけ一気に大きくなって、その後ゆっくり小さくなるような「サイズの時間変化」を自由に描けます。");
                ImGui::BulletText("アルファ値(透明度)のグラフをいじれば、フワッと消えるフェードアウトも自由自在です。");
            }

            if (ImGui::CollapsingHeader(ICON_FA_IMAGE " テクスチャの変更と保存", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::BulletText("「Texture File」のプルダウンから、使用する画像(Resources/sprite内)を選択できます。");
                ImGui::BulletText("作成したエフェクトは、名前をつけて保存(Save JSON)することで、いつでも呼び出せるようになります。");
            }

            if (ImGui::CollapsingHeader(ICON_FA_CODE " C++側からの呼び出し方", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextWrapped("保存した名前(例: NewEffect)を指定して、マネージャー経由で発生させます。");

                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
                ImGui::BeginChild("CodeBlockParticle", ImVec2(0, 100), true);
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "// 任意の座標にエフェクトを発生させる例");
                ImGui::TextUnformatted(
                    "// プレイヤーの座標などを取得\n"
                    "Vector3 pos = player_->GetWorldPosition();\n"
                    "ParticleManager::GetInstance()->Emit(\"NewEffect\", pos);"
                );
                ImGui::EndChild();
                ImGui::PopStyleColor();

                if (ImGui::Button(ICON_FA_COPY " コードをコピー##NormalParticle")) {
                    ImGui::SetClipboardText("ParticleManager::GetInstance()->Emit(\"NewEffect\", pos);");
                }
            }
            break;
        case 11:
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), ICON_FA_IMAGE " [ ポストエフェクト (Post Effect Settings) ]");
            ImGui::Separator();
            ImGui::TextWrapped(
                "ゲーム画面全体に対して、光の溢れ出しや色調補正、レンズの歪みなどをかける最終仕上げ(ポストプロセス)の機能です。\n"
                "ゲームの「空気感」や「リッチさ」を劇的に向上させます。"
            );
            ImGui::Spacing();

            if (ImGui::CollapsingHeader(ICON_FA_ADJUST " トーンマッピングとカラーグレーディング", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::BulletText("Tone Mapping: 物理的に正しい光の表現(ACES)を行います。リアル調かアニメ調かを選択できます。");
                ImGui::BulletText("LUT Intensity: 画面全体の色調(カラーグレーディング)の適用度を調整します。");
            }

            if (ImGui::CollapsingHeader(ICON_FA_SUN " 光とレンズの表現 (Bloom & Lens Effects)", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "◆ ブルーム (Bloom)");
                ImGui::TextWrapped("明るい部分から光が溢れ出す表現です。「Threshold(閾値)」で光らせる明るさの基準を決め、「Intensity(強度)」で光の強さを調整します。");
                ImGui::Spacing();

                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "◆ レンズ効果 (Lens Effects)");
                ImGui::BulletText("Vignette (周辺減光): 画面の四隅を暗くして、映画のような雰囲気や視線誘導を作ります。");
                ImGui::BulletText("Chromatic Aberration (色収差): 画面の端に行くほどRGBの色がズレるレンズ特有の現象を再現します。");
                ImGui::BulletText("Film Grain: 画面全体にザラザラとしたフィルムノイズを乗せます。");
            }

            if (ImGui::CollapsingHeader(ICON_FA_BOLT " 特殊演出・画面エフェクト (Screen Effects)", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextWrapped("ダメージを受けた瞬間や、特定のイベント時に使える強力な画面演出です。C++側から数値を操作して使います。");
                ImGui::Spacing();

                ImGui::BulletText("Radial Blur: 画面中央から外側に向かって放射状にぼかします。(ダッシュ時などに有効)");
                ImGui::BulletText("Damage Flash: 画面全体を赤く光らせます。");
                ImGui::BulletText("Cinema Bars: 画面の上下に黒帯を出して、ムービーシーンを演出します。");
                ImGui::BulletText("Wobble / Scanline / Mosaic: 画面の歪み、ブラウン管風の横線、モザイク処理など、特殊な状況(毒状態やゲームオーバーなど)で活躍します。");
            }

            if (ImGui::CollapsingHeader(ICON_FA_SAVE " 設定の保存と読み込み", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextWrapped("調整したパラメータは、エディタ下部の「Save / Load Params」からJSONファイルとして保存できます。デフォルトでは起動時に自動でロードされます。");
            }
            break;
        case 12:
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), ICON_FA_STAR " [ VFXシーケンサー (必殺技エディタ) ]");
            ImGui::Separator();
            ImGui::TextWrapped(
                "複数のGPUパーティクルを時間差で組み合わせて、「メテオ」や「特大ビーム」のような"
                "ド派手な複合エフェクト(必殺技)を作るためのタイムラインエディタです。"
            );
            ImGui::Spacing();

            if (ImGui::CollapsingHeader(ICON_FA_WRENCH " 必殺技の作り方 (ワークフロー)", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "◆ 事前準備");
                ImGui::BulletText("まずは「GPUパーティクルエディタ」で、素材となる単発のエフェクト(魔法陣、炎、爆発など)を作って保存しておきます。");
                ImGui::Spacing();

                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "◆ シーケンスの組み立て");
                ImGui::BulletText("エディタ上でイベント(Event)を追加し、「どの素材」を「何秒後」に出すかを設定します。");
                ImGui::BulletText("位置(Offset)をズラすことで、「空中に魔法陣が出て、その少し下に爆発が起きる」といった立体的な構成が可能です。");
                ImGui::BulletText("エディタ上で再生テストを行い、完成したら名前(例: UltimateMeteor)をつけて保存します。");
            }

            if (ImGui::CollapsingHeader(ICON_FA_SAVE " データの管理", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::BulletText("保存されたシーケンスは Resources/json/vfx_sequence/ フォルダ内にJSONとして保存されます。");
                ImGui::BulletText("既存の必殺技を読み込んで、少しタイミングを弄って別名保存する(亜種を作る)ことも簡単です。");
            }

            if (ImGui::CollapsingHeader(ICON_FA_CODE " C++側からの呼び出し方", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextWrapped("作成したシーケンスは、キャラやボスのクラス内でインスタンス化して再生します。");

                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
                ImGui::BeginChild("CodeBlockVFX", ImVec2(0, 180), true);
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "// 例：ボスの攻撃ステート等での呼び出し");
                ImGui::TextUnformatted(
                    "// 1. メンバ変数として持たせておく\n"
                    "// VFXSequencer ultimateAttack_;\n"
                    "\n"
                    "// 2. 初期化時にデータをロード\n"
                    "ultimateAttack_.Initialize(nullptr);\n"
                    "ultimateAttack_.Load(\"UltimateMeteor\");\n"
                    "\n"
                    "// 3. 攻撃の瞬間に、発生させたい座標を渡して再生！\n"
                    "Vector3 targetPos = player_->GetWorldPosition();\n"
                    "ultimateAttack_.Play(targetPos);"
                );
                ImGui::EndChild();
                ImGui::PopStyleColor();

                if (ImGui::Button(ICON_FA_COPY " コードをコピー##VFX")) {
                    ImGui::SetClipboardText(
                        "ultimateAttack_.Initialize(nullptr);\n"
                        "ultimateAttack_.Load(\"UltimateMeteor\");\n"
                        "ultimateAttack_.Play(targetPos);"
                    );
                }
            }
            break;
        case 13:
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), ICON_FA_BULLHORN " [ シネマティック監督 (GhostDirector) ]");
            ImGui::Separator();
            ImGui::TextWrapped(
                "GhostRecorderで作った複数のアニメーション(役者の演技)を束ねて、"
                "時間差をつけて一斉に再生するための「監督」エディタです。\n"
                "カットシーン(ムービー)や、ボスの複雑な連携攻撃を作るのに使用します。"
            );
            ImGui::Spacing();

            if (ImGui::CollapsingHeader(ICON_FA_USERS " 複数オブジェクトの連携 (マルチトラック)", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::BulletText("エディタ上で【トラックを追加】し、「動かしたいオブジェクト」と「再生するパス(JSON)」をセットします。");
                ImGui::BulletText("「遅延(Delay)」を設定することで、「ボスが動いた2秒後に、腕のパーツが飛んでいく」といった時間差アクションが簡単に作れます。");
                ImGui::BulletText("完成したシナリオは、名前をつけてJSONファイルとして保存できます。");
            }

            if (ImGui::CollapsingHeader(ICON_FA_BULLHORN " タイムライン・イベントの発火 (超重要!)", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextWrapped(
                    "GhostRecorderで記録したパスの途中に「EventID」が仕込まれている場合、"
                    "ディレクターが再生中にそのタイミングに到達すると、C++側へイベントを通知してくれます。"
                );
                ImGui::BulletText("例：「剣を振り下ろすアニメーション」の途中でEventIDを発火させ、C++側でそれを検知して衝撃波のパーティクルを出す！など。");
            }

            if (ImGui::CollapsingHeader(ICON_FA_CODE " C++側からの呼び出しとイベント検知", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextWrapped("ゲーム中にシナリオを再生し、途中で発生したイベントをキャッチする例です。");

                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
                ImGui::BeginChild("CodeBlockDirector", ImVec2(0, 240), true);
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "// 例：BossCore::Update() 内の記述");
                ImGui::TextUnformatted(
                    "// 1. シナリオの再生開始 (特定の攻撃ステートに入った時など)\n"
                    "// director_->LoadScenario(\"boss_ultimate_attack\");\n"
                    "// director_->PlayScenario();\n"
                    "\n"
                    "// 2. 毎フレームの更新と、イベントの監視\n"
                    "director_->Update(deltaTime);\n"
                    "\n"
                    "// 3. アニメーション中に仕込まれたイベントをキャッチ！\n"
                    "ActiveEvent ev = director_->GetActiveEvent();\n"
                    "if (ev.id != 0) {\n"
                    "    if (ev.id == 1) {\n"
                    "        // ID:1が来たら、特定のパーツからビームを撃つ！\n"
                    "        ShootBeam(ev.targetObject->GetWorldPosition());\n"
                    "    }\n"
                    "}"
                );
                ImGui::EndChild();
                ImGui::PopStyleColor();

                if (ImGui::Button(ICON_FA_COPY " コードをコピー##Director")) {
                    ImGui::SetClipboardText(
                        "director_->Update(deltaTime);\n"
                        "ActiveEvent ev = director_->GetActiveEvent();\n"
                        "if (ev.id != 0) {\n"
                        "    // イベント処理\n"
                        "}"
                    );
                }
            }
            break;
        case 14:
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), ICON_FA_IMAGES " [ 2D UIエディタ (Sprite Editor) ]");
            ImGui::Separator();
            ImGui::TextWrapped(
                "ゲームのUI（HPバー、ミニマップ、ボタン等）を配置するための専用エディタです。\n"
                "Hierarchy, Inspector, Assetsの3つのウィンドウを使い、Unityライクな操作感でサクサク配置できます。"
            );
            ImGui::Spacing();

            if (ImGui::CollapsingHeader(ICON_FA_MOUSE_POINTER " 直感的な配置とショートカット", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::BulletText("ドラッグ移動 : マウスで直感的にスプライトを移動できます。");
                ImGui::BulletText("スナップ移動 : 【Shiftキー】を押しながらドラッグすると、10ピクセル単位でピタッと整列します。");
                ImGui::Spacing();

                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "◆ キーボードによる微調整 (Nudge)");
                ImGui::BulletText("【矢印キー】で1ピクセルずつ座標を微調整できます。（Shiftキー同時押しで5ピクセル）");
                ImGui::Spacing();

                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "◆ 複製と削除");
                ImGui::BulletText("【Ctrl + C】または【Ctrl + D】で選択中のUIをポンッと複製できます。");
                ImGui::BulletText("【Deleteキー】でサクッと削除可能です。");
            }

            if (ImGui::CollapsingHeader(ICON_FA_IMAGE " ドラッグ＆ドロップ (画像の生成と差し替え)", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextWrapped("Sprite Assets ウィンドウ(下パネル)から画像を掴んでドラッグします。");
                ImGui::BulletText("空きスペースにドロップ : 新しいスプライトとして生成されます。");
                ImGui::BulletText("Inspectorの「テクスチャ変更」にドロップ : 位置やサイズはそのままに、画像だけを差し替えます。");
            }

            if (ImGui::CollapsingHeader(ICON_FA_CROSSHAIRS " 最強機能：クイック配置 (Quick Snap)", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextWrapped(
                    "解像度が変わってもレイアウトが崩れないUIを作るための必須機能です。\n"
                    "Inspectorの3x3のボタン（↖ ⬆ ↗ など）を押すだけで、画面端への「座標設定」と「アンカー設定」が同時に完璧に行われます。"
                );
            }

            if (ImGui::CollapsingHeader(ICON_FA_LOCK " 誤操作防止と描画順 (Zオーダー)", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::BulletText("鍵アイコン (Lock) : クリック・ドラッグを無効化します。背景画像など、間違って動かしたくない物に便利です。");
                ImGui::BulletText("目玉アイコン (Hide) : 編集時に邪魔なUIを一時的に隠します（保存データには影響しません）。");
                ImGui::BulletText("前面へ / 背面へ : UI同士の重なり順（Zオーダー）をボタン一つで入れ替えられます。");
            }

            if (ImGui::CollapsingHeader(ICON_FA_CODE " C++側からの読み込みと色の変更", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextWrapped("作成したUIレイアウト(JSON)は、シーン初期化時にロードします。透明度はJSONに保存されないため、フェードイン等のプログラムと競合しません！");

                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
                ImGui::BeginChild("CodeBlockSprite", ImVec2(0, 150), true);
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "// 例：シーンの Initialize() 内で呼び出す");
                ImGui::TextUnformatted(
                    "// 1. JSONから一括ロード (透明度は上書きされません)\n"
                    "levelLoader_->LoadSpriteLayout(this, \"Resources/json/sprite/boss_ui.json\");\n"
                    "\n"
                    "// 2. ゲージ等の長さをプログラムから操作する例\n"
                    "Sprite* hpBar = GetSpriteByName(\"BossHpBar_Red\");\n"
                    "if (hpBar) {\n"
                    "    hpBar->SetSize({ maxHpBarSize * (currentHp / maxHp), hpBar->GetSize().y });\n"
                    "}"
                );
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }
            break;
        }

        ImGui::EndChild(); // 右ペイン終了
    }
    ImGui::End(); // ウィンドウ終了
#endif
}