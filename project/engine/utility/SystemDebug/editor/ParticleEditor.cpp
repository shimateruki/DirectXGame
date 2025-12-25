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
        ImGui::Text("No active scene.");
        return;
    }

    ParticleSystem* targetSystem = currentScene->GetParticleSystem();
    if (targetSystem == nullptr) {
        ImGui::Text("Current scene has no particle system.");
        return;
    }

    ParticleSystem::EmitterParams& params = targetSystem->params_;

    ImGui::Begin("Particle Editor"); // ウィンドウ開始（もし外側でやってないなら）

    ImGui::Checkbox("Emit", &params.isEmitting);
    ImGui::DragFloat("Particles/Sec", &params.particlesPerSecond, 1.0f, 0.0f, 1000.0f);
    ImGui::DragFloat("Lifetime", &params.particleLifetime, 0.1f, 0.1f, 10.0f);

    ImGui::Separator();
    ImGui::Text("Position & Velocity");
    ImGui::DragFloat3("Spawn Pos", &params.spawnPosition.x, 0.1f);
    ImGui::DragFloat3("Spawn Area", &params.spawnArea.x, 0.1f);
    ImGui::DragFloat3("Velocity", &params.initialVelocity.x, 0.1f);
    ImGui::DragFloat3("Velocity Rand", &params.velocityRandomness.x, 0.1f);

    ImGui::Separator();
    ImGui::Text("Color");
    ImGui::ColorEdit4("Start Color", &params.startColor.x);
    ImGui::ColorEdit4("End Color", &params.endColor.x);

    ImGui::Separator();
    ImGui::Text("Size over Lifetime (Graph)");

    // ★ここが新機能: サイズの推移をグラフで編集
    // 配列のポインタを渡して、マウスで書き換えられるようにする
    // 範囲は 0.0f ～ 5.0f と仮定
    EditCurve("Draw Curve with Mouse", params.sizeCurve, 10, 0.0f, 5.0f);

    // リセットボタン（直線を引く）
    if (ImGui::Button("Reset Curve")) {
        for (int i = 0; i < 10; i++) params.sizeCurve[i] = 1.0f;
    }

    ImGui::End();
#endif
}