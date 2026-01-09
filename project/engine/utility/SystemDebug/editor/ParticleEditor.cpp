#include "ParticleEditor.h"
#include "imgui.h"
#include "SceneManager.h" 
#include "BaseScene.h"    
#include "ParticleSystem.h" 

void ParticleEditor::Initialize(SceneManager* sceneManager) {
    sceneManager_ = sceneManager;
}

// ロジック専用の Update (今は空)
void ParticleEditor::Update() {

}

void EditCurve(const char* label, float* values, int count, float minVal, float maxVal) {
    ImGui::Text("%s", label);

    // グラフの描画サイズ
    ImVec2 graphSize(ImGui::GetContentRegionAvail().x, 100.0f);

    // 1. プロットを描画 (OverlayTextなし、Min/Max指定)
    ImGui::PlotLines("##Curve", values, count, 0, nullptr, minVal, maxVal, graphSize);

    // 2. マウス操作で値を書き換える処理
    // グラフの上にマウスがあり、クリックまたはドラッグ中なら
    if (ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        ImGuiIO& io = ImGui::GetIO();

        // マウス位置がグラフの左端から何%の位置にあるか計算
        float mouseX = io.MousePos.x - ImGui::GetItemRectMin().x;
        float normalizedX = mouseX / graphSize.x;

        // インデックスに変換 (0 ～ count-1)
        int index = (int)(normalizedX * count);

        // 配列の範囲内に収める
        if (index >= 0 && index < count) {
            // マウスのY位置から値を計算
            float mouseY = io.MousePos.y - ImGui::GetItemRectMin().y;
            // Y軸は上がMin、下がMaxになっていることが多いので反転して正規化
            float normalizedY = 1.0f - (mouseY / graphSize.y);

            // 値を更新
            float newValue = minVal + normalizedY * (maxVal - minVal);
            values[index] = std::clamp(newValue, minVal, maxVal);
        }
    }
}

void ParticleEditor::DrawImGui() {
#ifdef USE_IMGUI
    if (sceneManager_ == nullptr) return;

    BaseScene* currentScene = sceneManager_->GetCurrentScene();
    if (currentScene == nullptr) {
        ImGui::Begin("Particle Editor");
        ImGui::Text("アクティブなシーンがありません");
        ImGui::End();
        return;
    }

    ParticleSystem* targetSystem = currentScene->GetParticleSystem();
    if (targetSystem == nullptr) {
        ImGui::Begin("Particle Editor");
        ImGui::Text("このシーンにはパーティクルシステムがありません");
        ImGui::End();
        return;
    }

    ParticleSystem::EmitterParams& params = targetSystem->params_;

    ImGui::Begin("Particle Editor");

    // 基本設定
    ImGui::Checkbox("放出有効 (Emit)", &params.isEmitting);

    ImGui::DragFloat("生成レート (個/秒)", &params.particlesPerSecond, 1.0f, 0.0f, 1000.0f);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("1秒間に何個パーティクルを出すか");

    ImGui::DragFloat("生存時間 (Lifetime)", &params.particleLifetime, 0.1f, 0.1f, 10.0f);

    ImGui::Separator();

    // 位置・速度設定
    ImGui::Text("位置と速度 (Pos & Velocity)");

    ImGui::DragFloat3("発生中心座標", &params.spawnPosition.x, 0.1f);
    ImGui::DragFloat3("発生範囲 (乱数幅)", &params.spawnArea.x, 0.1f);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("中心座標から±この範囲内でランダムに出現します");

    ImGui::DragFloat3("初速度 (Velocity)", &params.initialVelocity.x, 0.1f);
    ImGui::DragFloat3("速度のばらつき", &params.velocityRandomness.x, 0.1f);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("初速度に加えるランダムなブレ幅です");

    ImGui::Separator();

    // 色設定
    ImGui::Text("カラー推移 (Color)");
    ImGui::ColorEdit4("開始色 (Start)", &params.startColor.x);
    ImGui::ColorEdit4("終了色 (End)", &params.endColor.x);

    ImGui::Separator();

    // カーブ設定（サイズ推移）
    ImGui::Text("サイズ推移 (Lifetime Graph)");
    ImGui::TextDisabled("※グラフをマウスでなぞって描けます");

    // EditCurveはおそらく自作関数かと思いますが、ラベルを日本語化します
    EditCurve("サイズ変化カーブ", params.sizeCurve, 10, 0.0f, 5.0f);

    // リセットボタン
    if (ImGui::Button("カーブをリセット (平坦化)")) {
        for (int i = 0; i < 10; i++) params.sizeCurve[i] = 1.0f;
    }

    ImGui::End();
#endif
}