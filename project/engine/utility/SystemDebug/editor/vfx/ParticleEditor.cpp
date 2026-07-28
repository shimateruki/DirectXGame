#include "ParticleEditor.h"
#include "imgui.h"
#include "SceneManager.h" 
#include "BaseScene.h"    
#include "ParticleSystem.h" 
#include "ParticleManager.h"
#include "IconsFontAwesome5.h"
#include "EffectPreviewStage.h"
#include "EditorManager.h"
#include "ProfilerManager.h"
#include <algorithm>
#include <vector>
namespace fs = std::filesystem;
void ParticleEditor::Initialize(SceneManager* sceneManager) {
    sceneManager_ = sceneManager;
}

void ParticleEditor::Update(float deltaTime, bool sceneIsPlaying) {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) {
        targetSystem_ = nullptr;
        return;
    }

    targetSystem_ = sceneManager_->GetCurrentScene()->GetParticleSystem();
    if (!targetSystem_) {
        return;
    }

    EffectPreviewStage* previewStage = EffectPreviewStage::GetInstance();
    const bool isSelected = EditorManager::GetInstance()->GetSelectedObject() == this;
    if (!isSelected || !previewStage) {
        targetSystem_->SetEditorPreviewOffset({ 0.0f, 0.0f, 0.0f });
        targetSystem_->SetSimulationTimeScale(1.0f);
        return;
    }

    previewStage->EnableForToolPreview();
    targetSystem_->SetEditorPreviewOffset(previewStage->GetPreviewPosition());
    ParticleSystem::EmitterParams& params = targetSystem_->params_;
    const float duration = (std::max)(3.0f, params.particleLifetime + 0.75f);

    const auto restartPreview = [&](float seekTime) {
        targetSystem_->ResetSimulation();
        previewTime_ = 0.0f;
        if (!params.isEmitting) {
            targetSystem_->EmitOneShot(
                params,
                params.spawnPosition + previewStage->GetPreviewPosition());
        }

        targetSystem_->SetSimulationTimeScale(1.0f);
        constexpr float kSeekStep = 1.0f / 60.0f;
        float remaining = std::clamp(seekTime, 0.0f, duration);
        while (remaining > 0.0001f) {
            const float step = (std::min)(kSeekStep, remaining);
            targetSystem_->Update(step);
            previewTime_ += step;
            remaining -= step;
        }
    };

    if (previewStage->GetPlayRequestSerial() != lastStagePlayRequestSerial_) {
        lastStagePlayRequestSerial_ = previewStage->GetPlayRequestSerial();
        restartPreview(0.0f);
    }
    if (previewStage->GetStopRequestSerial() != lastStageStopRequestSerial_) {
        lastStageStopRequestSerial_ = previewStage->GetStopRequestSerial();
        targetSystem_->ResetSimulation();
        previewTime_ = 0.0f;
    }
    if (previewStage->GetSeekRequestSerial() != lastStageSeekRequestSerial_) {
        lastStageSeekRequestSerial_ = previewStage->GetSeekRequestSerial();
        restartPreview(previewStage->GetSeekTargetTime());
    }

    const float effectiveSpeed = previewStage->GetPlaybackSpeed();
    targetSystem_->SetSimulationTimeScale(effectiveSpeed);
    if (!sceneIsPlaying && effectiveSpeed > 0.0f) {
        targetSystem_->Update(deltaTime);
    }
    if (effectiveSpeed > 0.0f) {
        previewTime_ += deltaTime * effectiveSpeed;
        if (previewStage->IsLoopEnabled() && previewTime_ >= duration) {
            restartPreview(0.0f);
            targetSystem_->SetSimulationTimeScale(effectiveSpeed);
        } else {
            previewTime_ = (std::min)(previewTime_, duration);
        }
    }

    std::vector<EffectPreviewStage::TimelineEvent> events;
    events.push_back({
        params.isEmitting ? "連続発生" : "単発発生",
        0.0f,
        params.isEmitting ? duration : 0.08f,
        { 0.30f, 0.82f, 1.0f, 0.90f }
    });
    events.push_back({
        "粒子寿命",
        0.0f,
        (std::min)(params.particleLifetime, duration),
        { 0.36f, 1.0f, 0.62f, 0.85f }
    });
    EffectPreviewStage::PerformanceMetrics performance;
    performance.available = true;
    performance.particleCount = static_cast<int>(targetSystem_->GetActiveParticleCount());
    performance.cpuTimeMs = targetSystem_->GetLastUpdateCpuTimeMs() + targetSystem_->GetLastDrawCpuTimeMs();
    performance.gpuTimeMs = ProfilerManager::GetInstance()->GetLatestGpuTime("Particle CPU pass");
    performance.frameDeltaSeconds = deltaTime;
    performance.memoryBytes = targetSystem_->GetEstimatedMemoryBytes();
    previewStage->ReportToolState(
        EffectPreviewStage::ToolKind::CpuParticle,
        "通常パーティクル",
        previewTime_,
        duration,
        effectiveSpeed > 0.0f,
        static_cast<int>(targetSystem_->GetActiveParticleCount()),
        events,
        performance);
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
        ImGui::TextDisabled(ICON_FA_EXCLAMATION_TRIANGLE " アクティブなシーンがありません");
        return;
    }

    ParticleSystem* targetSystem = currentScene->GetParticleSystem();
    if (targetSystem == nullptr) {
        ImGui::TextDisabled(ICON_FA_EXCLAMATION_TRIANGLE " このシーンにはパーティクルシステムがありません");
        return;
    }

    ParticleSystem::EmitterParams& params = targetSystem->params_;

    // -------------------------------------------------------------
    // 1. 描画・形状設定
    // -------------------------------------------------------------
    if (ImGui::CollapsingHeader(ICON_FA_PAINT_BRUSH " 描画・形状設定 (Rendering & Shape)", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* modes[] = { "Alpha (Normal)", "Add (Glow)", "Subtract (Dark)", "Multiply (Shadow)", "Screen (Soft Light)" };
        int currentMode = (int)params.blendMode;
        if (ImGui::Combo(ICON_FA_ADJUST " Blend Mode", &currentMode, modes, IM_ARRAYSIZE(modes))) {
            params.blendMode = (ParticleBlendMode)currentMode;
        }

        const char* shapes[] = { "Box", "Sphere", "Cone" };
        int currentShape = (int)params.emitterType;
        if (ImGui::Combo(ICON_FA_SHAPES " Type (形状)", &currentShape, shapes, IM_ARRAYSIZE(shapes))) {
            params.emitterType = (EmitterType)currentShape;
        }

        // 選択中のタイプに合わせてパラメータUIを切り替え
        if (params.emitterType == EmitterType::Box) {
            ImGui::DragFloat3(" 発生範囲 (Size)", &params.spawnArea.x, 0.1f);
        }
        else if (params.emitterType == EmitterType::Sphere) {
            ImGui::DragFloat(" 半径 (Radius)", &params.spawnRadius, 0.1f);
        }
        else if (params.emitterType == EmitterType::Cone) {
            ImGui::DragFloat(" 角度 (Angle)", &params.coneAngle, 1.0f, 0.0f, 90.0f);
        }

        ImGui::Checkbox(ICON_FA_TOGGLE_ON " 放出有効 (Emit)", &params.isEmitting);
        if (ImGui::DragInt(
            ICON_FA_SORT_NUMERIC_UP " 単発の発生数 (Emit Count)",
            &params.emitCount, 1.0f, 1, ParticleSystem::GetMaxParticles())) {
            params.emitCount = std::clamp(params.emitCount, 1, ParticleSystem::GetMaxParticles());
            if (!params.isEmitting) {
                targetSystem->ResetSimulation();
                Vector3 origin = params.spawnPosition;
                if (EffectPreviewStage* stage = EffectPreviewStage::GetInstance(); stage && stage->IsEnabled()) {
                    origin += stage->GetPreviewPosition();
                }
                targetSystem->EmitOneShot(params, origin);
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("単発プレビューとEmitOneShot 1回で生成する粒子数です");
        }
        ImGui::DragFloat(ICON_FA_STREAM " 生成レート (個/秒)", &params.particlesPerSecond, 1.0f, 0.0f, 1000.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("1秒間に何個パーティクルを出すか");

        ImGui::DragFloat(ICON_FA_HOURGLASS_HALF " 生存時間 (Lifetime)", &params.particleLifetime, 0.1f, 0.1f, 10.0f);
    }

    // -------------------------------------------------------------
    // 2. 位置・速度・回転・重力設定
    // -------------------------------------------------------------
    if (ImGui::CollapsingHeader(ICON_FA_COGS " 挙動設定 (Transform & Physics)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3(ICON_FA_CROSSHAIRS " 発生中心座標", &params.spawnPosition.x, 0.1f);
        ImGui::DragFloat3(ICON_FA_EXPAND_ARROWS_ALT " 発生範囲 (乱数幅)", &params.spawnArea.x, 0.1f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("中心座標から±この範囲内でランダムに出現します");

        ImGui::DragFloat3(ICON_FA_TACHOMETER_ALT " 初速度 (Velocity)", &params.initialVelocity.x, 0.1f);
        ImGui::DragFloat3(ICON_FA_RANDOM " 速度のばらつき", &params.velocityRandomness.x, 0.1f);

        ImGui::DragFloat(ICON_FA_SYNC " 回転スピード (度/秒)", &params.initialRotationSpeed, 1.0f);
        ImGui::DragFloat(ICON_FA_REDO " 回転のばらつき", &params.rotationSpeedRandomness, 1.0f);

        ImGui::DragFloat3(ICON_FA_ARROW_CIRCLE_DOWN " 加速度 (Gravity)", &params.acceleration.x, 0.01f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Yをマイナスにすると重力がかかります");
    }

    // -------------------------------------------------------------
    // 3. 色・サイズ・テクスチャ設定
    // -------------------------------------------------------------
    if (ImGui::CollapsingHeader(ICON_FA_MAGIC " ビジュアル設定 (Visuals)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat(ICON_FA_SUN " HDR Intensity", &params.hdrIntensity, 0.1f, 0.0f, 50.0f);

        ImGui::ColorEdit4(ICON_FA_PALETTE " 開始色 (Start)", &params.startColor.x);
        ImGui::ColorEdit4(ICON_FA_FILL_DRIP " 終了色 (End)", &params.endColor.x);

        ImGui::Spacing();
        ImGui::Text(ICON_FA_CHART_LINE " サイズ推移 (Lifetime Graph)");
        ImGui::TextDisabled("※グラフをマウスでなぞって描けます");
        EditCurve("##SizeCurve", params.sizeCurve, 10, 0.0f, 5.0f);
        if (ImGui::Button(ICON_FA_UNDO " カーブをリセット")) {
            for (int i = 0; i < 10; i++) params.sizeCurve[i] = 1.0f;
        }

        ImGui::Spacing();
        ImGui::Text(ICON_FA_IMAGE " テクスチャ (Texture)");
        std::string directoryPath = "Resources/sprite/";
        std::vector<std::string> files;
        int currentItem = 0;
        int index = 0;

        if (fs::exists(directoryPath)) {
            for (const auto& entry : fs::recursive_directory_iterator(directoryPath)) {
                if (!entry.is_regular_file() || entry.path().extension() != ".png") {
                    continue;
                }

                std::string relativePath = fs::relative(entry.path(), directoryPath).generic_string();
                files.push_back(relativePath);
                if (params.textureName.find(relativePath) != std::string::npos ||
                    params.textureName.find(entry.path().filename().string()) != std::string::npos) {
                    currentItem = index;
                }
                index++;
            }
        }

        std::vector<const char*> filePtrs;
        for (const auto& f : files) filePtrs.push_back(f.c_str());

        if (!filePtrs.empty()) {
            if (ImGui::Combo(ICON_FA_FILE_IMAGE " Texture File", &currentItem, filePtrs.data(), (int)filePtrs.size())) {
                std::string fullPath = directoryPath + files[currentItem];
                targetSystem->SetTexture(fullPath);
            }
        }
    }

    // -------------------------------------------------------------
    // 4. データ保存・読み込み
    // -------------------------------------------------------------
    ImGui::Separator();
    if (ImGui::CollapsingHeader(ICON_FA_SAVE " 保存と読み込み (Save & Load)", ImGuiTreeNodeFlags_DefaultOpen)) {
        static char saveName[64] = "NewEffect";
        ImGui::InputText(ICON_FA_FILE_SIGNATURE " Name", saveName, sizeof(saveName));

        if (ImGui::Button(ICON_FA_DOWNLOAD " Save JSON", ImVec2(120, 0))) {
            ParticleManager::GetInstance()->SaveParam(std::string(saveName), params);
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_UPLOAD " Load JSON", ImVec2(120, 0))) {
            ParticleManager::GetInstance()->LoadParam(std::string(saveName));
            params = ParticleManager::GetInstance()->GetParam(std::string(saveName));
            targetSystem->SetTexture(params.textureName);
        }
    }

#endif
}
