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
            ICON_FA_CODE " シーンの初期化・実装例",
            ICON_FA_BOLT " トリガーイベントの作り方",
            ICON_FA_CAMERA " カメラと録画機能",
            ICON_FA_FIRE " GPUパーティクルの使い方"
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

        case 2: // シーンの初期化
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "[ シーンの初期化と呼び出し方 ]");
            ImGui::Separator();
            ImGui::TextWrapped("新しいシーン（ステージ）を作る際の、標準的なC++コードのテンプレートです。");
            ImGui::Spacing();

            // コードブロック風の背景色
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
            ImGui::BeginChild("CodeBlock", ImVec2(0, 200), true);
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "// Stage1Scene.cpp の Initialize() 内に記述します");
            ImGui::TextUnformatted(
                "void Stage1Scene::Initialize() {\n"
                "    // 1. カメラマネージャーのセットアップ\n"
                "    camera_ = std::make_unique<Camera>();\n"
                "    CameraManager::GetInstance()->SetActiveCamera(camera_.get());\n"
                "\n"
                "    // 2. エディタで作成したJSONデータをロード\n"
                "    // ※ _player, _enemy, _object は自動で読み込まれます\n"
                "    LoadScene(\"stage1\");\n"
                "}"
            );
            ImGui::EndChild();
            ImGui::PopStyleColor();

            if (ImGui::Button(ICON_FA_COPY " コードをコピー")) {
                ImGui::SetClipboardText("void Stage1Scene::Initialize() {\n    camera_ = std::make_unique<Camera>();\n    CameraManager::GetInstance()->SetActiveCamera(camera_.get());\n    LoadScene(\"stage1\");\n}");
            }
            break;

        case 3: // トリガー
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "[ トリガーイベントの作り方 ]");
            ImGui::Separator();
            ImGui::TextWrapped(
                "1. Hierarchyの「システム設定」から、雷マークの【透明ボックス生成(トリガー用)】を押します。\n"
                "2. Inspectorの「ギミック設定」で、自分ID(Event)と送信先ID(Target)を設定します。\n"
                "3. プログラム側で衝突判定を取り、対象のIDを発火させます。"
            );
            break;

        case 4: // カメラと録画
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "[ カメラと録画機能 (GhostRecorder) ]");
            ImGui::Separator();
            break;

        case 5:
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
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
                ImGui::BeginChild("CodeBlockGPUP", ImVec2(0, 100), true);
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "// プリセット名と発生座標を指定するだけ！");
                ImGui::TextUnformatted(
                    "Vector3 emitPosition = { 0.0f, 5.0f, 0.0f };\n"
                    "GPUParticleManager::GetInstance()->EmitFromPreset(\"FirePreset\", emitPosition);"
                );
                ImGui::EndChild();
                ImGui::PopStyleColor();

                if (ImGui::Button(ICON_FA_COPY " コードをコピー##GPUP")) {
                    ImGui::SetClipboardText("GPUParticleManager::GetInstance()->EmitFromPreset(\"FirePreset\", emitPosition);");
                }
            }
            break;
        }

        ImGui::EndChild(); // 右ペイン終了
    }
    ImGui::End(); // ウィンドウ終了
#endif
}