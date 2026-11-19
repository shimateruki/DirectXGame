#include "GpuParticleEditor.h"
#include "GpuParticlePresetJsonCodec.h"
#include <imgui.h>
#include <fstream>
#include <json.hpp> // nlohmann/json
#include "DebugConsole.h"
#include <TextureManager.h>
#include "IconsFontAwesome5.h"
#include "GpuParticleSystem.h"
#include "CameraManager.h"
#include "EffectPreviewStage.h"
#include "EditorManager.h"
#include "ProfilerManager.h"
#include <algorithm>
#include "VfxAuthoringEditor.h"
#include "VfxAssetPicker.h"
#include <vector>

using json = nlohmann::json;

void GpuParticleEditor::Initialize() {
    emitTimer_ = 0.0f;
}

void GpuParticleEditor::Update(float deltaTime, bool sceneIsPlaying) {
    // Preview StageがこのEditorを操作中のときだけManagerの再生速度と時刻を占有する。
    EffectPreviewStage* previewStage = EffectPreviewStage::GetInstance();
    const bool usePreviewStage = previewStage && previewStage->IsEnabled();
    const bool isThisEditorSelected = EditorManager::GetInstance()->GetSelectedObject() == this;
    GpuParticleManager* manager = GpuParticleManager::GetInstance();

    if (!usePreviewStage || !isThisEditorSelected) {
        manager->SetTimeScale(1.0f);
        return;
    }

    // 粒子寿命の後に0.25秒の余白を加え、タイムライン上で消える瞬間を確認できるようにする。
    const float previewDuration = (std::max)(0.1f, config_.emitLife + 0.25f);
    auto restartPreview = [&](float seekTime) {
        // シークは一度発生をやり直し、目標時刻までEditor用の時計を進めて近似する。
        manager->ResetSimulation();
        emitTimer_ = 0.0f;
        previewTime_ = 0.0f;
        EmitWithPreview();

        const float clampedSeek = std::clamp(seekTime, 0.0f, previewDuration);
        const float previousScale = manager->GetTimeScale();
        manager->SetTimeScale(1.0f);

        // このループはManagerの時計を進める。GPU DispatchはDraw側であり、物理の固定刻み再計算ではない。
        // シーク後のGPU状態はEmit要求のinitialAgeで近似復元される。
        while (previewTime_ + (1.0f / 60.0f) < clampedSeek) {
            manager->UpdateEditorPreviewStep(1.0f / 60.0f);
            previewTime_ += 1.0f / 60.0f;
        }

        if (clampedSeek > previewTime_) {
            manager->UpdateEditorPreviewStep(clampedSeek - previewTime_);
            previewTime_ = clampedSeek;
        }

        manager->SetTimeScale(previousScale);
    };

    // 操作シリアルを比較し、Play・Stop・Seekを一回ずつ反映する。
    if (previewStage->GetPlayRequestSerial() != lastStagePlayRequestSerial_) {
        lastStagePlayRequestSerial_ = previewStage->GetPlayRequestSerial();
        restartPreview(0.0f);
    }

    if (previewStage->GetStopRequestSerial() != lastStageStopRequestSerial_) {
        lastStageStopRequestSerial_ = previewStage->GetStopRequestSerial();
        manager->ResetSimulation();
        previewTime_ = 0.0f;
        emitTimer_ = 0.0f;
    }

    if (previewStage->GetSeekRequestSerial() != lastStageSeekRequestSerial_) {
        lastStageSeekRequestSerial_ = previewStage->GetSeekRequestSerial();
        restartPreview(previewStage->GetSeekTargetTime());
    }

    // エディタのプレビュー間隔もスローモーションに対応させる
    manager->SetTimeScale(previewStage->GetPlaybackSpeed());
    const float scaledDelta = deltaTime * manager->GetTimeScale();

    // Loop発生の間隔もプレビュー速度に従わせる。
    if (config_.isLooping && scaledDelta > 0.0f) {
        emitTimer_ += scaledDelta;

        if (emitTimer_ >= (std::max)(0.01f, config_.emitInterval)) {
            EmitWithPreview();
            emitTimer_ = 0.0f;
        }
    }

    if (scaledDelta > 0.0f) {
        previewTime_ += scaledDelta;

        if (previewTime_ >= previewDuration) {
            if (previewStage->IsLoopEnabled()) {
                restartPreview(0.0f);
            } else {
                previewTime_ = previewDuration;
            }
        }
    }

    if (!sceneIsPlaying) {
        // 編集停止中はGame側の0秒更新済みフラグを解除し、Preview時間を確実に進めます。
        manager->UpdateEditorPreviewStep(deltaTime);
    }

    // タイムラインと性能表示は実GPUの粒子数でなく設定からの生存数見積もりを使う。
    std::vector<EffectPreviewStage::TimelineEvent> events;
    events.push_back({ "Spawn", 0.0f, 0.05f, engine::Vector4{ 0.35f, 0.85f, 1.0f, 1.0f } });
    events.push_back({ "Particle life", 0.0f, (std::min)(config_.emitLife, previewDuration), engine::Vector4{ 0.25f, 0.65f, 1.0f, 1.0f } });

    if (config_.isLooping) {
        events.push_back({ "Emit interval", 0.0f, (std::min)((std::max)(config_.emitInterval, 0.01f), previewDuration), engine::Vector4{ 0.55f, 0.95f, 0.75f, 1.0f } });
    }

    const uint32_t capacity = GpuParticleManager::ResolveParticleCapacity(config_);
    uint64_t estimatedParticleCount = static_cast<uint64_t>((std::max)(config_.emitCount, 0));

    if (config_.isLooping && config_.emitInterval > 0.0f) {
        // 寿命内に重なる発生回数の上限と、開始から現在まで実際に発生できた回数の小さい方を使う。
        const double batchesByLifetime = std::ceil(
            static_cast<double>(config_.emitLife) / config_.emitInterval);
        const double batchesByElapsed = 1.0 + std::floor(
            static_cast<double>(previewTime_) / config_.emitInterval);
        const double aliveBatchCount = (std::max)(
            1.0, (std::min)(batchesByLifetime, batchesByElapsed));
        const uint64_t aliveBatches = static_cast<uint64_t>(aliveBatchCount);
        estimatedParticleCount *= aliveBatches;
    }

    EffectPreviewStage::PerformanceMetrics performance;
    performance.available = true;
    // 見積もった粒子数の上限は、このプリセットに確保する容量。
    performance.particleCount = static_cast<int>((std::min)(
        estimatedParticleCount,
        static_cast<uint64_t>(capacity)));
    performance.cpuTimeMs = manager->GetLastUpdateCpuTimeMs() + manager->GetLastDrawCpuTimeMs();
    performance.gpuTimeMs = ProfilerManager::GetInstance()->GetLatestGpuTime("Particle GPU pass");
    performance.frameDeltaSeconds = deltaTime;
    performance.memoryBytes = manager->GetEstimatedMemoryBytesForConfig(config_);
    previewStage->ReportToolState(
        EffectPreviewStage::ToolKind::GpuParticle,
        "GPU Particle",
        previewTime_,
        previewDuration,
        previewStage->IsTransportPlaying(),
        performance.particleCount,
        events,
        performance);
}

void GpuParticleEditor::DrawImGui() {
#ifdef USE_IMGUI
    // 編集パネルを発生条件、寿命曲線、環境、プレビュー、保存に分けて表示する。
    ImGui::Text("--- GPUパーティクルエディタ ---");

    // ⚙️ システム設定
    if (ImGui::CollapsingHeader(ICON_FA_COGS " システム設定 (System)", ImGuiTreeNodeFlags_DefaultOpen)) {
        float timeScale = GpuParticleManager::GetInstance()->GetTimeScale();

        if (ImGui::DragFloat(ICON_FA_CLOCK " 全体再生速度 (Time Scale)", &timeScale, 0.05f, 0.0f, 5.0f)) {
            GpuParticleManager::GetInstance()->SetTimeScale(timeScale);
        }

        ImGui::Text("※0.5でスローモーション、2.0で倍速になります");
    }

    if (ImGui::CollapsingHeader(ICON_FA_EYE " プレビュー環境 (Preview Environment)", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (EffectPreviewStage::GetInstance()->IsEnabled()) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.7f, 1.0f), "Effect Preview Stage の原点を使用中");
        }

        ImGui::Checkbox("カメラの前に発生させる (Spawn in front of Camera)", &isPreviewMode_);

        if (isPreviewMode_) {
            ImGui::Indent();
            ImGui::DragFloat("カメラからの距離 (Distance)", &previewDistance_, 0.1f, 1.0f, 50.0f);
            ImGui::Unindent();
        }

        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "※プレビュー用です。実際の座標は「発生位置(Position)」に保存されます");
    }

    // ✨ 発生パラメータ
    if (ImGui::CollapsingHeader(ICON_FA_MAGIC " 発生パラメータ (Emit Parameters)", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* blendModes[] = { "加算 (光・魔法)", "半透明 (霧・煙)", "歪み (衝撃波・陽炎)" };
        ImGui::Combo(ICON_FA_ADJUST " 合成モード (Blend Mode)", &config_.blendModeIndex, blendModes, IM_ARRAYSIZE(blendModes));
        ImGui::Separator();

        const char* shapeTypes[] = { "ボックス (Box)", "スフィア (Sphere)", "コーン (Cone)", "メッシュ (Mesh)", "ハート (Heart)" };
        ImGui::Combo(ICON_FA_SHAPES " 発生形状 (Shape Type)", &config_.shapeType, shapeTypes, IM_ARRAYSIZE(shapeTypes));

        if (ImGui::CollapsingHeader(ICON_FA_COMPRESS_ALT " コリジョン (Collision)", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool isCollide = config_.enableCollision != 0;

            if (ImGui::Checkbox("地形と衝突させる", &isCollide)) {
                config_.enableCollision = isCollide ? 1 : 0;
            }

            if (isCollide) {
                ImGui::DragFloat("跳ね返り係数 (Restitution)", &config_.restitution, 0.05f, 0.0f, 1.0f);
            }
        }

        ImGui::DragFloat3(ICON_FA_CROSSHAIRS " 発生位置 (Position)", &config_.emitPos.x, 0.1f);

        const char* easeTypes[] = {
                 "0: Linear (一定)",
                 "1: InSine", "2: OutSine", "3: InOutSine",
                 "4: InQuad", "5: OutQuad", "6: InOutQuad",
                 "7: InCubic", "8: OutCubic", "9: InOutCubic",
                 "10: InQuart", "11: OutQuart", "12: InOutQuart",
                 "13: InQuint", "14: OutQuint", "15: InOutQuint",
                 "16: InExpo", "17: OutExpo", "18: InOutExpo",
                 "19: InCirc", "20: OutCirc", "21: InOutCirc",
                 "22: InBack", "23: OutBack", "24: InOutBack",
                 "25: InElastic", "26: OutElastic", "27: InOutElastic",
                 "28: InBounce", "29: OutBounce", "30: InOutBounce"
        };

        ImGui::Combo(ICON_FA_PALETTE " 色の変化カーブ", &config_.colorEaseType, easeTypes, IM_ARRAYSIZE(easeTypes));
        ImGui::Separator();
        ImGui::Combo(ICON_FA_EXPAND_ARROWS_ALT " サイズの変形カーブ", &config_.sizeEaseType, easeTypes, IM_ARRAYSIZE(easeTypes));

        // 形に合わせて出すUIを変える！
        if (config_.shapeType == 0) {
            ImGui::DragFloat3("発生範囲 (Area)", &config_.emitArea.x, 0.1f);
        }
        else if (config_.shapeType == 1) {
            ImGui::DragFloat("半径 (Radius)", &config_.shapeRadius, 0.1f, 0.0f, 100.0f);
        }
        else if (config_.shapeType == 2) {
            ImGui::DragFloat("半径 (Radius)", &config_.shapeRadius, 0.1f, 0.0f, 100.0f);
            ImGui::DragFloat("広がり角度 (Angle)", &config_.shapeAngle, 1.0f, 0.0f, 90.0f);
        }
        else if (config_.shapeType == 4) {

            ImGui::DragFloat("ハートの大きさ (Radius)", &config_.shapeRadius, 0.1f, 0.0f, 100.0f);
            ImGui::DragFloat("厚み (Thickness)", &config_.emitArea.x, 0.1f, 0.0f, 50.0f);
            ImGui::DragFloat("輪郭の太さ (Line Thickness)", &config_.emitArea.y, 0.01f, 0.0f, 1.0f);
        }

        ImGui::Separator();

        ImGui::DragFloat3(ICON_FA_TACHOMETER_ALT " 初期速度 (Velocity)", &config_.emitVelocity.x, 0.1f);
        ImGui::DragInt(ICON_FA_SORT_NUMERIC_UP " 発生数 (Count)", &config_.emitCount, 10, 1, GpuParticleSystem::kMaxParticles);
        ImGui::DragFloat(ICON_FA_HOURGLASS_HALF " 寿命 (Life Time)", &config_.emitLife, 0.05f, 0.1f, 10.0f);
        ImGui::DragInt("最大同時数 (0 = Auto)", &config_.maxParticles, 256, 0, GpuParticleSystem::kMaxParticles);
        config_.maxParticles = (std::max)(config_.maxParticles, 0);
        const uint32_t resolvedCapacity = GpuParticleManager::ResolveParticleCapacity(config_);
        ImGui::TextDisabled("実行容量: %u particles / System", resolvedCapacity);

        // 一度に出す数が確保枠を超えると全粒子を保持できない。手動で決めた容量がこの条件のとき警告する。
        if (config_.maxParticles > 0 && config_.maxParticles < config_.emitCount) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.55f, 0.2f, 1.0f),
                "最大同時数が1回の発生数より小さいため、粒子が欠けます。");
        }

        if (config_.maxParticles == 0 && resolvedCapacity == GpuParticleSystem::kMaxParticles) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.55f, 0.2f, 1.0f),
                "Auto容量が上限です。発生数・寿命を減らすか、Loop間隔を長くするとGPU負荷を下げられます。");
        }

        ImGui::TextDisabled("AutoはLoop時の同時生存数、単発時は6回分の重なりを見込んで計算します。");
        ImGui::DragFloat(ICON_FA_RANDOM " 速度のばらつき (Variance)", &config_.velocityVariance, 0.1f, 0.0f, 50.0f);
        ImGui::DragFloat(ICON_FA_SYNC " 回転スピード (Rot Speed)", &config_.rotSpeed, 0.05f, 0.0f, 20.0f);

        ImGui::ColorEdit4("発生時の色 (Base Color)", &config_.baseColor.x);
        ImGui::ColorEdit4("中間の色 (Mid Color)", &config_.midColor.x);
        // 中間色の時刻は秒ではなく寿命に対する割合。0.5 なら寿命の半分で中間色に達する。
        ImGui::DragFloat("色がMidになる時間(割合)", &config_.colorMidTime, 0.01f, 0.01f, 0.99f);
        ImGui::ColorEdit4("消滅時の色 (End Color)", &config_.endColor.x);
        ImGui::DragFloat(ICON_FA_SUN " 発光強度 (HDR Intensity)", &config_.colorIntensity, 0.1f, 0.0f, 20.0f);

        // 発生用テクスチャの候補だけに絞る。選択値はEmit時に適用する。
        std::vector<std::string> allPaths = TextureManager::GetInstance()->GetAvailableTexturePaths();
        std::vector<std::string> filteredPaths;

        for (const auto& path : allPaths) {
            // spriteフォルダの中の particle フォルダのみを参照するように絞り込み
            if (path.find("Resources/sprite/particle/") != std::string::npos) {
                filteredPaths.push_back(path);
            }
        }

        std::sort(filteredPaths.begin(), filteredPaths.end());
        int currentIndex = -1;

        for (int i = 0; i < static_cast<int>(filteredPaths.size()); ++i) {
            if (config_.texturePath == filteredPaths[i]) {
                currentIndex = i;
                break;
            }
        }

        if (VfxAssetPicker::DrawCombo(ICON_FA_IMAGE " テクスチャ画像", filteredPaths,
                                      currentIndex, textureSearch_, sizeof(textureSearch_))) {
            // テクスチャは次回の発生時に適用される。
            config_.texturePath = filteredPaths[currentIndex];
        }

        if (currentIndex < 0 && !config_.texturePath.empty()) {
            ImGui::TextDisabled("現在の画像: %s", config_.texturePath.c_str());
        }

        ImGui::DragFloat("中間の大きさ (Mid Size)", &config_.midSize, 0.1f, 0.0f, 50.0f);
        ImGui::DragFloat("サイズがMidになる時間(割合)", &config_.sizeMidTime, 0.01f, 0.01f, 0.99f);
        ImGui::DragFloat("発生時の大きさ (Base Size)", &config_.baseSize, 0.1f, 0.1f, 50.0f);
        ImGui::DragFloat("消滅時の大きさ (End Size)", &config_.endSize, 0.1f, 0.1f, 50.0f);
    }

    if (ImGui::CollapsingHeader("共通Curve / Gradient / LOD", ImGuiTreeNodeFlags_DefaultOpen)) {
        // 共通Curve UIの編集結果をGPU設定の始点・中間点・終点に書き戻す。
        VfxFloatCurve sizeCurve = VfxFloatCurve::FromThreePoints(
            config_.baseSize,
            config_.sizeMidTime,
            config_.midSize,
            config_.endSize,
            static_cast<VfxCurveEasing>(std::clamp(config_.sizeEaseType, 0, 4)));
        VfxAuthoringEditor::DrawFloatCurve(
            "GpuParticleSize",
            sizeCurve,
            0.0f,
            50.0f,
            3,
            [this](const VfxFloatCurve& value) {
                if (value.keys.empty())
                    return;
                config_.baseSize = value.keys.front().value;
                config_.endSize = value.keys.back().value;
                if (value.keys.size() >= 3) {
                    config_.sizeMidTime = value.keys[1].time;
                    config_.midSize = value.keys[1].value;
                } else {
                    config_.sizeMidTime = 0.5f;
                    // 中央キーがない入力でも GPU 用の三点形式を保つため、寿命の半分で評価したサイズを使う。
                    config_.midSize = value.Evaluate(0.5f, config_.baseSize);
                }
                config_.sizeEaseType = static_cast<int>(value.keys.front().easing);
            },
            false,
            false);

        VfxColorGradient colorGradient = VfxColorGradient::FromThreeColors(
            config_.baseColor,
            config_.colorMidTime,
            config_.midColor,
            config_.endColor,
            static_cast<VfxCurveEasing>(std::clamp(config_.colorEaseType, 0, 4)));
        VfxAuthoringEditor::DrawColorGradient(
            "GpuParticleColor",
            colorGradient,
            3,
            [this](const VfxColorGradient& value) {
                if (value.keys.empty())
                    return;
                config_.baseColor = value.keys.front().color;
                config_.endColor = value.keys.back().color;
                if (value.keys.size() >= 3) {
                    config_.colorMidTime = value.keys[1].time;
                    config_.midColor = value.keys[1].color;
                } else {
                    config_.colorMidTime = 0.5f;
                    // 中央キーがない入力では、寿命の半分の補間色を GPU 用の中間値へ入れる。
                    config_.midColor = value.Evaluate(0.5f, config_.baseColor);
                }
                config_.colorEaseType = static_cast<int>(value.keys.front().easing);
            },
            false);

        ImGui::SeparatorText("距離LOD / Budget");
        VfxAuthoringEditor::DrawLodSettings(
            "GpuParticleLod",
            config_.lod,
            [this](const VfxLodSettings& value) { config_.lod = value; },
            static_cast<int>(GpuParticleSystem::kMinParticles),
            static_cast<int>(GpuParticleSystem::kMaxParticles));
        // 入力したプレビュー距離における LOD の発生率を、元の発生数に対する割合で確認する。
        const float previewScale = config_.lod.EvaluateEmissionScale(previewDistance_);
        ImGui::TextDisabled(
            "Preview距離 %.1f では発生数 %.0f%%",
            previewDistance_,
            previewScale * 100.0f);
    }

    // 🌍 環境変化
    if (ImGui::CollapsingHeader(ICON_FA_GLOBE " 環境変化 (Environment)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("重力 (Gravity)", &config_.envGravity.x, 0.01f);
        ImGui::DragFloat("空気抵抗 (Air Drag)", &config_.envDrag, 0.001f, 0.8f, 1.0f);
        ImGui::DragFloat3(ICON_FA_WIND " 風 (Wind)", &config_.envWind.x, 0.1f);
        ImGui::DragFloat("乱流・ノイズ (Turbulence)", &config_.envTurbulence, 0.1f, 0.0f, 100.0f);
        const char* fieldTypes[] = { "なし", "吸引 (Attract)", "渦 (Vortex)", "反発 (Repulse)" };
        ImGui::Combo("Fieldタイプ", &config_.fieldType, fieldTypes, IM_ARRAYSIZE(fieldTypes));

        if (config_.fieldType != 0) {
            ImGui::DragFloat3("Field位置（発生位置からの相対）", &config_.fieldPosition.x, 0.05f);
            ImGui::DragFloat("Field強度", &config_.fieldStrength, 0.1f, -100.0f, 100.0f);
            ImGui::DragFloat("Field半径", &config_.fieldRadius, 0.05f, 0.05f, 100.0f);
            ImGui::DragFloat("Field減衰", &config_.fieldFalloff, 0.05f, 0.05f, 8.0f);
            ImGui::TextDisabled("吸引・渦・反発をGPU上で各粒子へ適用します。");
        }

        ImGui::DragFloat(ICON_FA_FEATHER " 地形との馴染み (Soft Fade)", &config_.softParticleFade, 0.1f, 0.1f, 20.0f);
    }

    ImGui::Separator();

    // 🎮 エディタ操作
    if (ImGui::CollapsingHeader(ICON_FA_GAMEPAD " エディタ操作 (Editor Controls)", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button(ICON_FA_PLAY " 1回発生 (Emit Once)", ImVec2(ImGui::GetContentRegionAvail().x, 30))) {
            EmitWithPreview();
        }

        ImGui::Checkbox(ICON_FA_REDO " 連続発生テスト (Loop Emit)", &config_.isLooping);

        if (config_.isLooping) {
            ImGui::Indent();
            ImGui::DragFloat("発生間隔 (Interval)", &config_.emitInterval, 0.01f, 0.01f, 2.0f);
            ImGui::Unindent();
        }
    }

    // 🚀 クイック・プリセット (Quick Presets)
    if (ImGui::CollapsingHeader(ICON_FA_BOLT " クイック・プリセット (Quick Presets)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("ボタン一つで標準的な設定を流し込みます。");
        
        if (ImGui::Button("⭐ スター (Star)", ImVec2(120, 30))) {
            config_.blendModeIndex = 0; // Additive
            config_.shapeType = 1;      // Sphere
            config_.shapeRadius = 2.0f;
            config_.emitCount = 300;
            config_.emitLife = 1.2f;
            config_.baseColor = { 1.0f, 0.9f, 0.2f, 1.0f }; // Yellowish
            config_.midColor = { 1.0f, 0.5f, 0.1f, 1.0f };  // Orange
            config_.endColor = { 1.0f, 0.0f, 0.0f, 0.0f };  // Red fade
            config_.colorMidTime = 0.5f;
            config_.colorIntensity = 5.0f;
            config_.baseSize = 0.5f;
            config_.midSize = 0.8f;
            config_.endSize = 0.0f;
            config_.sizeMidTime = 0.5f;
            config_.emitVelocity = { 0.0f, 2.0f, 0.0f };
            config_.velocityVariance = 3.0f;
            config_.envGravity = { 0.0f, -0.5f, 0.0f };
            config_.texturePath = "Resources/sprite/particle/particle.png";
            config_.rotSpeed = 5.0f;
        }

        ImGui::SameLine();

        if (ImGui::Button("✨ コイン待機 (Idle)", ImVec2(150, 30))) {
            config_.blendModeIndex = 0; // Additive
            config_.shapeType = 1;      // Sphere
            config_.shapeRadius = 1.5f;
            config_.emitCount = 15;     // 少なめで上品に
            config_.emitLife = 2.5f;
            config_.baseColor = { 1.0f, 1.0f, 0.5f, 1.0f }; // Pale Yellow
            config_.midColor = { 1.0f, 0.8f, 0.2f, 1.0f };  // Gold
            config_.endColor = { 1.0f, 0.5f, 0.0f, 0.0f };
            config_.colorMidTime = 0.5f;
            config_.colorIntensity = 3.0f;
            config_.baseSize = 0.1f;    // 小さめから
            config_.midSize = 0.4f;     // ふわっと大きくなる
            config_.endSize = 0.0f;
            config_.sizeMidTime = 0.5f;
            config_.emitVelocity = { 0.0f, 0.2f, 0.0f }; // ゆっくり上に
            config_.velocityVariance = 0.5f;
            config_.envGravity = { 0.0f, 0.3f, 0.0f }; // フワッと浮上
            config_.texturePath = "Resources/sprite/particle/particle.png";
            config_.rotSpeed = 2.0f;
        }

        ImGui::SameLine();

        if (ImGui::Button("🌟 コイン取得 (Burst)", ImVec2(150, 30))) {
            config_.blendModeIndex = 0; // Additive
            config_.shapeType = 1;      // Sphere
            config_.shapeRadius = 0.5f; // 中心から
            config_.emitCount = 150;    // 一気にたくさん
            config_.emitLife = 0.8f;    // 短命で派手に
            config_.baseColor = { 1.0f, 1.0f, 1.0f, 1.0f }; // Flash White
            config_.midColor = { 1.0f, 0.9f, 0.0f, 1.0f };  // Bright Gold
            config_.endColor = { 1.0f, 0.5f, 0.0f, 0.0f };
            config_.colorMidTime = 0.3f;
            config_.colorIntensity = 10.0f; // まぶしく
            config_.baseSize = 0.6f;
            config_.midSize = 0.3f;
            config_.endSize = 0.0f;
            config_.sizeMidTime = 0.3f;
            config_.emitVelocity = { 0.0f, 0.0f, 0.0f }; 
            config_.velocityVariance = 12.0f; // 四方八方に高速で弾ける
            config_.envGravity = { 0.0f, -2.0f, 0.0f }; // 重力で落ちる
            config_.envDrag = 0.9f;     // 失速して消える
            config_.texturePath = "Resources/sprite/particle/particle.png";
            config_.rotSpeed = 10.0f;
        }

        // --- 2段目 ---
        if (ImGui::Button("💫 オーラ (Aura)", ImVec2(120, 30))) {
            config_.blendModeIndex = 0; // Additive
            config_.shapeType = 1;      // Sphere
            config_.shapeRadius = 2.0f; // 広め
            config_.emitCount = 60;
            config_.emitLife = 1.5f;
            config_.baseColor = { 0.3f, 0.7f, 1.0f, 0.0f }; // 水色
            config_.midColor = { 0.1f, 0.4f, 1.0f, 1.0f };
            config_.endColor = { 0.0f, 0.0f, 1.0f, 0.0f };
            config_.colorMidTime = 0.5f;
            config_.colorIntensity = 2.0f;
            config_.baseSize = 2.0f;    // 大きくぼんやり
            config_.midSize = 0.8f;
            config_.endSize = 0.0f;
            config_.sizeMidTime = 0.5f;
            config_.emitVelocity = { 0.0f, 0.0f, 0.0f }; 
            config_.velocityVariance = 0.3f; // ほとんど動かない
            config_.envGravity = { 0.0f, 0.0f, 0.0f };
            config_.texturePath = "Resources/sprite/particle/white.png";
            config_.rotSpeed = 1.0f;
        }

        ImGui::SameLine();

        if (ImGui::Button("🔥 焚き火 (Fire)", ImVec2(120, 30))) {
            config_.blendModeIndex = 0; // Additive
            config_.shapeType = 1;      // Sphere
            config_.shapeRadius = 1.0f;
            config_.emitCount = 500;
            config_.emitLife = 1.0f;
            config_.baseColor = { 1.0f, 0.3f, 0.1f, 1.0f };
            config_.midColor = { 1.0f, 0.1f, 0.0f, 1.0f };
            config_.endColor = { 0.2f, 0.0f, 0.0f, 0.0f };
            config_.colorMidTime = 0.4f;
            config_.colorIntensity = 8.0f;
            config_.baseSize = 0.8f;
            config_.midSize = 1.2f;
            config_.endSize = 0.2f;
            config_.sizeMidTime = 0.6f;
            config_.emitVelocity = { 0.0f, 5.0f, 0.0f };
            config_.velocityVariance = 1.5f;
            config_.envGravity = { 0.0f, 2.0f, 0.0f };
            config_.texturePath = "Resources/sprite/particle/particle.png";
        }

        ImGui::SameLine();

        if (ImGui::Button("💨 煙 (Smoke)", ImVec2(120, 30))) {
            config_.blendModeIndex = 1; // Alpha
            config_.shapeType = 0;      // Box
            config_.emitArea = { 2.0f, 0.1f, 2.0f };
            config_.emitCount = 100;
            config_.emitLife = 3.0f;
            config_.baseColor = { 0.3f, 0.3f, 0.3f, 0.5f };
            config_.midColor = { 0.5f, 0.5f, 0.5f, 0.3f };
            config_.endColor = { 0.8f, 0.8f, 0.8f, 0.0f };
            config_.colorMidTime = 0.5f;
            config_.colorIntensity = 1.0f;
            config_.baseSize = 1.0f;
            config_.midSize = 3.0f;
            config_.endSize = 5.0f;
            config_.sizeMidTime = 0.5f;
            config_.emitVelocity = { 0.0f, 1.0f, 0.0f };
            config_.velocityVariance = 0.5f;
            config_.envWind = { 1.0f, 0.0f, 0.0f };
            config_.texturePath = "Resources/sprite/particle/white.png";
        }
    }

    ImGui::Separator();

    files_.Draw("GPUパーティクルJSONファイル", "Resources/json/gpu_particles",
        [this](const std::filesystem::path& path) { return Load(path); },
        [this](const std::filesystem::path& path, const std::string&) { return Save(path); },
        [this] { config_ = GpuParticleConfig{}; });
    if (ImGui::Button(ICON_FA_PLAY " 現在の設定をプレビュー")) EmitWithPreview();

    ImGui::Separator();

    if (ImGui::CollapsingHeader("Sprite Sheet Animation")) {
        const char* particleTypes[] = { "Billboard", "Trail" };
        ImGui::Combo("Particle Type", &config_.particleType, particleTypes, IM_ARRAYSIZE(particleTypes));

        if (config_.particleType == 1) {
            ImGui::DragFloat("Trail Length", &config_.trailLength, 0.005f, 0.0f, 1.0f, "%.3f");
            ImGui::TextDisabled("速度方向へ自動整列し、移動量に応じて軌跡を伸ばします。");
        }

        bool receiveLighting = config_.receiveLighting != 0;

        if (ImGui::Checkbox("ライトの影響を受ける", &receiveLighting)) {
            config_.receiveLighting = receiveLighting ? 1 : 0;
        }

        if (receiveLighting) {
            ImGui::DragFloat3("ライト方向", &config_.lightDirection.x, 0.01f, -1.0f, 1.0f);
            ImGui::ColorEdit3("ライト色", &config_.lightColor.x);
            ImGui::SliderFloat("ライティング反映率", &config_.lightingStrength, 0.0f, 1.0f);
        }

        ImGui::Separator();
        ImGui::DragInt("Columns", &config_.spriteSheetColumns, 1.0f, 1, 16);
        ImGui::DragInt("Rows", &config_.spriteSheetRows, 1.0f, 1, 16);
        ImGui::DragInt("Frame Count", &config_.spriteSheetFrameCount, 1.0f, 1, 256);
        ImGui::DragFloat("FPS", &config_.spriteSheetFps, 0.5f, 0.0f, 60.0f);

        bool loop = config_.spriteSheetLoop != 0;

        if (ImGui::Checkbox("Loop", &loop)) {
            config_.spriteSheetLoop = loop ? 1 : 0;
        }

        bool randomStart = config_.spriteSheetRandomStart != 0;

        if (ImGui::Checkbox("Random Start", &randomStart)) {
            config_.spriteSheetRandomStart = randomStart ? 1 : 0;
        }

        // 旧モードのランダムな回転を残し、細い軌跡用に方向を固定する選択肢を追加します。
        const char* alignmentModes[] = {"自由回転", "速度方向＋ランダム回転（従来）", "速度方向へ固定"};
        ImGui::Combo("Spriteの向き", &config_.alignToVelocity, alignmentModes, IM_ARRAYSIZE(alignmentModes));

        ImGui::DragFloat("速度による横伸び", &config_.velocityStretch, 0.005f, 0.0f, 0.20f, "%.3f");
        ImGui::TextDisabled("横向きの風・斬撃・軌跡Spriteを移動方向へ揃える設定です。");
    }
#endif
}

void GpuParticleEditor::EmitWithPreview() {
    // 保存用の発生位置は保持し、プレビュー時だけ一時コピーの位置を差し替える。
    GpuParticleConfig tempConfig = config_;
    EffectPreviewStage* previewStage = EffectPreviewStage::GetInstance();

    // 隔離空間が有効なら、その発生位置を最優先する。無効の場合のみカメラ前方の一時位置を使う。
    if (previewStage && previewStage->IsEnabled()) {
        tempConfig.emitPos = previewStage->GetPreviewPosition();
    }
    else if (isPreviewMode_) {
        const engine::Camera* camera = CameraManager::GetInstance()->GetActiveCamera();

        if (camera) {
            engine::Vector3 camEye = camera->GetEye();
            engine::Vector3 camTarget = camera->GetTargetPoint();
            engine::Vector3 dir = { camTarget.x - camEye.x, camTarget.y - camEye.y, camTarget.z - camEye.z };
            float length = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
            engine::Vector3 forward = { 0, 0, 1 };

            if (length > 0.0001f) {
                forward = { dir.x / length, dir.y / length, dir.z / length };
            }

            // プレビュー距離はワールド単位。正規化した視線の先へ一時コピーの発生位置を置く。
            tempConfig.emitPos = {
                camEye.x + forward.x * previewDistance_,
                camEye.y + forward.y * previewDistance_,
                camEye.z + forward.z * previewDistance_
            };
        }
    }

    GpuParticleManager::GetInstance()->EmitFromConfig(tempConfig);
}

bool GpuParticleEditor::Save(const std::filesystem::path& path) {
    // プレビュー中の座標は保存せず、既存のプリセット形式へ変換する。
    std::ofstream file(path);
    file << GpuParticlePresetJsonCodec::Write(config_).dump(4);
    file.close();
    return file.good();
}

bool GpuParticleEditor::Load(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) return false;

    nlohmann::json j;
    file >> j;
    auto loaded = config_;
    {
        // 旧プリセットで省略された項目は、従来どおりEditor用の既定値へ戻す。
        const GpuParticleConfig defaults;
        loaded.spriteSheetColumns = defaults.spriteSheetColumns;
        loaded.spriteSheetRows = defaults.spriteSheetRows;
        loaded.spriteSheetFrameCount = defaults.spriteSheetFrameCount;
        loaded.spriteSheetFps = defaults.spriteSheetFps;
        loaded.spriteSheetLoop = defaults.spriteSheetLoop;
        loaded.spriteSheetRandomStart = defaults.spriteSheetRandomStart;
        loaded.alignToVelocity = defaults.alignToVelocity;
        loaded.velocityStretch = defaults.velocityStretch;
        loaded.particleType = defaults.particleType;
        loaded.trailLength = defaults.trailLength;
        loaded.receiveLighting = defaults.receiveLighting;
        loaded.lightDirection = defaults.lightDirection;
        loaded.lightColor = defaults.lightColor;
        loaded.lightingStrength = defaults.lightingStrength;
        loaded.fieldType = defaults.fieldType;
        loaded.fieldPosition = defaults.fieldPosition;
        loaded.fieldStrength = defaults.fieldStrength;
        loaded.fieldRadius = defaults.fieldRadius;
        loaded.fieldFalloff = defaults.fieldFalloff;
        loaded.maxParticles = defaults.maxParticles;
        loaded.lod = defaults.lod;

        GpuParticlePresetJsonCodec::Read(j, loaded);

    }
    // 読み込みが済んだ一時設定をまとめて適用する。ファイルを開けない段階では編集値に触れない。
    config_ = std::move(loaded);
    return true;
}
