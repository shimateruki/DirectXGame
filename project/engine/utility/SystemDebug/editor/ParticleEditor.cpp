#include "ParticleEditor.h"
#include "imgui.h"
#include "SceneManager.h" 
#include "BaseScene.h"    
#include "ParticleSystem.h" 
#include "ParticleManager.h"
namespace fs = std::filesystem;
void ParticleEditor::Initialize(SceneManager* sceneManager) {
    sceneManager_ = sceneManager;
}

// ロジック専用の Update (今は空)
void ParticleEditor::Update() {

}

void EditCurve(const char* label, float* values, int count, float minVal, float maxVal) {
#ifdef USE_IMGUI
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
#endif
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
    ImGui::Text("描画設定 (Rendering)");

    // コンボボックスで切り替え
    const char* modes[] = { "Alpha (Normal)","Add (Glow)",
    "Subtract (Dark)",
    "Multiply (Shadow)",
    "Screen (Soft Light)" };
    int currentMode = (int)params.blendMode;

    if (ImGui::Combo("Blend Mode", &currentMode, modes, IM_ARRAYSIZE(modes))) {
        params.blendMode = (ParticleBlendMode)currentMode;
    }
    ImGui::Separator();
    ImGui::Text("形状 (Shape)");

    const char* shapes[] = { "Box", "Sphere", "Cone" };
    int currentShape = (int)params.emitterType;

    if (ImGui::Combo("Type", &currentShape, shapes, IM_ARRAYSIZE(shapes))) {
        params.emitterType = (EmitterType)currentShape;
    }

    // 選択中のタイプに合わせて、表示するパラメータを変える (UX向上！)
    if (params.emitterType == EmitterType::Box) {
        ImGui::DragFloat3("発生範囲 (Size)", &params.spawnArea.x, 0.1f);
    } else if (params.emitterType == EmitterType::Sphere) {
        ImGui::DragFloat("半径 (Radius)", &params.spawnRadius, 0.1f);
    } else if (params.emitterType == EmitterType::Cone) {
        ImGui::DragFloat("角度 (Angle)", &params.coneAngle, 1.0f, 0.0f, 90.0f);
    }
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
    ImGui::Text("回転 (Rotation)");

    ImGui::DragFloat("回転スピード (度/秒)", &params.initialRotationSpeed, 1.0f);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("プラスなら左回り、マイナスなら右回り");

    ImGui::DragFloat("回転のばらつき", &params.rotationSpeedRandomness, 1.0f);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("例: 180を入れると、様々な速度で回ります");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("初速度に加えるランダムなブレ幅です");
    ImGui::DragFloat3("加速度 (Gravity)", &params.acceleration.x, 0.01f);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Yをマイナスにすると重力がかかります");
    ImGui::DragFloat("HDR Intensity (発光の強さ)", &params.hdrIntensity, 0.1f, 0.0f, 50.0f);

    ImGui::Separator();

    // 色設定
    ImGui::Text("カラー推移 (Color)");
    ImGui::ColorEdit4("開始色 (Start)", &params.startColor.x);
    ImGui::ColorEdit4("終了色 (End)", &params.endColor.x);

    ImGui::Separator();

    // カーブ設定（サイズ推移）
    ImGui::Text("サイズ推移 (Lifetime Graph)");
    ImGui::TextDisabled("※グラフをマウスでなぞって描けます");
    EditCurve("サイズ変化カーブ", params.sizeCurve, 10, 0.0f, 5.0f);

    if (ImGui::Button("カーブをリセット (平坦化)")) {
        for (int i = 0; i < 10; i++) params.sizeCurve[i] = 1.0f;
    }
    ImGui::Separator();
    ImGui::Text("テクスチャ (Texture)");

    // 1. テクスチャフォルダをスキャンしてリストを作る
    std::string directoryPath = "Resources/sprite/";

    std::vector<std::string> files;
    int currentItem = 0;
    int index = 0;

    // ディレクトリ内を探索
    if (fs::exists(directoryPath)) {
        for (const auto& entry : fs::directory_iterator(directoryPath)) {
            // PNGファイルだけピックアップ (必要なら .jpg も)
            if (entry.path().extension() == ".png") {
                std::string fileName = entry.path().filename().string();
                files.push_back(fileName);

                // 今使っているファイル名と一致したら、そこを選択状態にする
                if (params.textureName.find(fileName) != std::string::npos) {
                    currentItem = index;
                }
                index++;
            }
        }
    }

    // 2. ImGui用のchar*配列に変換
    std::vector<const char*> filePtrs;
    for (const auto& f : files) {
        filePtrs.push_back(f.c_str());
    }

    // 3. コンボボックス表示
    if (!filePtrs.empty()) {
        if (ImGui::Combo("Texture File", &currentItem, filePtrs.data(), (int)filePtrs.size())) {
       
            std::string fullPath = directoryPath + files[currentItem];

            targetSystem->SetTexture(fullPath);
        }
    } else {
        ImGui::Text("No .png files found in %s", directoryPath.c_str());
    }
    // 保存・読み込み
    static char saveName[64] = "NewEffect";

    ImGui::Separator();
    ImGui::Text("Save / Load Parameter");
    ImGui::InputText("Name", saveName, sizeof(saveName));

    if (ImGui::Button("Save JSON")) {
        ParticleManager::GetInstance()->SaveParam(std::string(saveName), params);
    }

    ImGui::SameLine(); // ボタンを横並びに

    if (ImGui::Button("Load JSON")) {
        ParticleManager::GetInstance()->LoadParam(std::string(saveName));
        params = ParticleManager::GetInstance()->GetParam(std::string(saveName));
        targetSystem->SetTexture(params.textureName);
  
    }

    ImGui::End();
#endif
}