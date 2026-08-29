#include "DebrisEffectEditor.h"

#include "CameraManager.h"
#include "DebugConsole.h"
#include "EffectPreviewStage.h"
#include "EditorManager.h"
#include "IconsFontAwesome5.h"
#include "ModelManager.h"
#include "SceneManager.h"
#include "imgui.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace {
constexpr const char* kDebrisPresetDirectory = "Resources/json/debris/";

constexpr const char* kMaterialTypeNames[] = {
    "通常 (Standard)",
    "ガラス (Glass)",
    "氷・宝石 (Ice/Crystal)",
    "ホログラム (Hologram)",
    "消滅 (Dissolve)",
    "旧マグマ (Emissive)",
    "トゥーン調 (Cel Shaded)",
    "ローカルフォグ (Local Fog)",
    "水 (Water)",
    "新マグマ (Magma)",
    "分厚い氷 (Ice)",
    "炎 (Fire)",
    "レーザー (Laser)",
    "スライムジェル (Slime Gel)",
    "地面衝撃波 (Shockwave)",
    "水/マグマ接触 (Liquid Contact)",
    "ダメージ亀裂 (Damage Crack)",
    "上昇気流 (Updraft)",
    "スタン拘束 (Stun Bind)",
    "王冠解放 (Crown Unlock)",
    "毒胞子 (Poison Spore)",
    "雲 (Cloud)",
    "ゲートポータル (Gate Portal)",
    "アニメ調地形 (Stylized Terrain)",
    "ダッシュパネル (Dash Panel)",
    "スライム補正 (Slime Soft)",
    "風弾 (Wind Orb)",
    "プリズム結晶 (Prism Crystal)"
};

void CopyToBuffer(std::array<char, 128>& buffer, const std::string& text) {
    std::fill(buffer.begin(), buffer.end(), '\0');
    strncpy_s(buffer.data(), buffer.size(), text.c_str(), _TRUNCATE);
}

void CopyToBuffer(char* buffer, size_t bufferSize, const std::string& text) {
    if (!buffer || bufferSize == 0) {
        return;
    }
    buffer[0] = '\0';
    strncpy_s(buffer, bufferSize, text.c_str(), _TRUNCATE);
}

std::string GetBufferText(const std::array<char, 128>& buffer) {
    return std::string(buffer.data());
}

void DrawModelCombo(const char* label, std::array<char, 128>& buffer, const std::vector<std::string>& modelList) {
#ifdef USE_IMGUI
    int currentIndex = -1;
    std::string current = GetBufferText(buffer);
    for (int i = 0; i < static_cast<int>(modelList.size()); ++i) {
        if (modelList[i] == current) {
            currentIndex = i;
            break;
        }
    }

    std::vector<const char*> names;
    names.reserve(modelList.size());
    for (const auto& modelName : modelList) {
        names.push_back(modelName.c_str());
    }

    if (!names.empty()) {
        if (ImGui::Combo(label, &currentIndex, names.data(), static_cast<int>(names.size()))) {
            if (currentIndex >= 0 && currentIndex < static_cast<int>(modelList.size())) {
                CopyToBuffer(buffer, modelList[currentIndex]);
            }
        }
    }
    ImGui::InputText((std::string(label) + " 直接入力").c_str(), buffer.data(), buffer.size());
#else
    (void)label;
    (void)buffer;
    (void)modelList;
#endif
}

void DrawMaterialTypeCombo(int& materialType) {
#ifdef USE_IMGUI
    constexpr int materialTypeCount = static_cast<int>(std::size(kMaterialTypeNames));
    int currentMaterialType = materialType;
    if (currentMaterialType < 0 || currentMaterialType >= materialTypeCount) {
        currentMaterialType = 0;
    }

    if (ImGui::Combo("マテリアルタイプ", &currentMaterialType, kMaterialTypeNames, materialTypeCount)) {
        materialType = currentMaterialType;
    }

    if (materialType < 0 || materialType >= materialTypeCount) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), "未知のマテリアルタイプ: %d", materialType);
        ImGui::DragInt("数値を直接調整", &materialType, 1, 0, 32);
    } else {
        ImGui::TextDisabled("現在: %d / %s", materialType, kMaterialTypeNames[materialType]);
    }
#else
    (void)materialType;
#endif
}
}

void DebrisEffectEditor::Initialize(SceneManager* sceneManager) {
    sceneManager_ = sceneManager;
    RefreshLists();
    ApplyQuickPresetRock();
    SyncModelBuffersFromConfig();
}

void DebrisEffectEditor::Update(float deltaTime) {
    EffectPreviewStage* previewStage = EffectPreviewStage::GetInstance();
    DebrisEffectManager* manager = DebrisEffectManager::GetInstance();
    const bool isThisEditorSelected = EditorManager::GetInstance()->GetSelectedObject() == this;
    const bool usePreviewStage = previewStage && previewStage->IsEnabled() && isThisEditorSelected;
    if (!usePreviewStage) {
        manager->SetTimeScale(1.0f);
        return;
    }

    const float previewDuration = (std::max)(config_.lifetimeMax, 0.1f);
    auto restartPreviewAt = [&](float seekTime) {
        manager->ResetEditorPreview();
        manager->SpawnFromConfig(config_, GetPreviewPosition());
        previewTime_ = 0.0f;
        loopTimer_ = 0.0f;

        const float targetTime = std::clamp(seekTime, 0.0f, previewDuration);
        const float previousScale = manager->GetTimeScale();
        manager->SetTimeScale(1.0f);
        while (previewTime_ + (1.0f / 60.0f) < targetTime) {
            manager->Update(1.0f / 60.0f);
            previewTime_ += 1.0f / 60.0f;
        }
        if (targetTime > previewTime_) {
            manager->Update(targetTime - previewTime_);
            previewTime_ = targetTime;
        }
        manager->SetTimeScale(previousScale);
    };

    if (previewStage->GetPlayRequestSerial() != lastStagePlayRequestSerial_) {
        lastStagePlayRequestSerial_ = previewStage->GetPlayRequestSerial();
        restartPreviewAt(0.0f);
    }
    if (previewStage->GetStopRequestSerial() != lastStageStopRequestSerial_) {
        lastStageStopRequestSerial_ = previewStage->GetStopRequestSerial();
        manager->ResetEditorPreview();
        previewTime_ = 0.0f;
        loopTimer_ = 0.0f;
    }
    if (previewStage->GetSeekRequestSerial() != lastStageSeekRequestSerial_) {
        lastStageSeekRequestSerial_ = previewStage->GetSeekRequestSerial();
        restartPreviewAt(previewStage->GetSeekTargetTime());
    }

    manager->SetTimeScale(previewStage->GetPlaybackSpeed());
    const float scaledDelta = deltaTime * manager->GetTimeScale();
    if (loopPreview_ && scaledDelta > 0.0f) {
        loopTimer_ += scaledDelta;
        if (loopTimer_ >= loopInterval_) {
            loopTimer_ = 0.0f;
            restartPreviewAt(0.0f);
        }
    }

    if (scaledDelta > 0.0f) {
        previewTime_ += scaledDelta;
        if (previewTime_ >= previewDuration) {
            if (previewStage->IsLoopEnabled()) {
                restartPreviewAt(0.0f);
            } else {
                previewTime_ = previewDuration;
            }
        }
    }

    std::vector<EffectPreviewStage::TimelineEvent> events;
    events.push_back({ "Burst", 0.0f, 0.06f, Vector4{ 1.0f, 0.65f, 0.25f, 1.0f } });
    events.push_back({ "Debris lifetime", 0.0f, previewDuration, Vector4{ 0.75f, 0.55f, 0.35f, 1.0f } });
    events.push_back({ "Fade", previewDuration * std::clamp(config_.fadeStartRatio, 0.0f, 1.0f), previewDuration, Vector4{ 0.9f, 0.45f, 0.25f, 1.0f } });
    previewStage->ReportToolState(
        EffectPreviewStage::ToolKind::Debris,
        "Debris",
        previewTime_,
        previewDuration,
        previewStage->IsTransportPlaying(),
        manager->GetActivePieceCount(),
        events);
}

void DebrisEffectEditor::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::Text("3D破片エフェクト Editor");
    ImGui::TextDisabled("爆発、破壊、着地衝撃などで飛び散る3D破片を調整します。");

    if (ImGui::Button(ICON_FA_SYNC " 一覧更新")) {
        RefreshLists();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_BROOM " 再生中の破片を消す")) {
        DebrisEffectManager::GetInstance()->Clear();
    }

    ImGui::Separator();

    if (ImGui::CollapsingHeader(ICON_FA_BOLT " クイックプリセット", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("岩の爆発", ImVec2(120.0f, 28.0f))) {
            ApplyQuickPresetRock();
            SyncModelBuffersFromConfig();
        }
        ImGui::SameLine();
        if (ImGui::Button("木片の飛散", ImVec2(120.0f, 28.0f))) {
            ApplyQuickPresetWood();
            SyncModelBuffersFromConfig();
        }
        ImGui::SameLine();
        if (ImGui::Button("小石の散り", ImVec2(120.0f, 28.0f))) {
            ApplyQuickPresetPebble();
            SyncModelBuffersFromConfig();
        }
        if (ImGui::Button("プリズム破砕", ImVec2(120.0f, 28.0f))) {
            Load("prism_crystal_shatter");
            CopyToBuffer(presetNameBuffer_, sizeof(presetNameBuffer_), "prism_crystal_shatter");
        }
    }

    if (ImGui::CollapsingHeader(ICON_FA_CUBE " 破片モデル", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawModelCombo("モデル 1", modelBuffer0_, modelList_);
        DrawModelCombo("モデル 2", modelBuffer1_, modelList_);
        DrawModelCombo("モデル 3", modelBuffer2_, modelList_);
        DrawModelCombo("モデル 4", modelBuffer3_, modelList_);
        ImGui::TextDisabled("空欄のモデル枠は使いません。複数入れるとランダムで混ざります。");
    }

    if (ImGui::CollapsingHeader(ICON_FA_RANDOM " 発生と速度", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragInt("発生数", &config_.spawnCount, 1, 1, 256);
        ImGui::DragFloat3("発生オフセット", &config_.spawnOffset.x, 0.05f);
        ImGui::DragFloat3("基準方向", &config_.baseDirection.x, 0.05f);
        ImGui::DragFloat("横方向の散らばり", &config_.horizontalSpread, 0.02f, 0.0f, 4.0f);
        ImGui::DragFloat("上方向 最小", &config_.verticalMin, 0.02f, -1.0f, 3.0f);
        ImGui::DragFloat("上方向 最大", &config_.verticalMax, 0.02f, -1.0f, 4.0f);
        ImGui::DragFloat("初速 最小", &config_.speedMin, 0.1f, 0.0f, 80.0f);
        ImGui::DragFloat("初速 最大", &config_.speedMax, 0.1f, 0.0f, 80.0f);
        ImGui::DragFloat("回転速度 最小", &config_.angularSpeedMin, 0.1f, 0.0f, 50.0f);
        ImGui::DragFloat("回転速度 最大", &config_.angularSpeedMax, 0.1f, 0.0f, 80.0f);
    }

    if (ImGui::CollapsingHeader(ICON_FA_WEIGHT_HANGING " 疑似物理", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat("重力", &config_.gravity, 0.2f, -50.0f, 120.0f);
        ImGui::DragFloat("空気抵抗", &config_.airDrag, 0.005f, 0.0f, 4.0f);
        ImGui::Checkbox("地面で跳ねる", &config_.collideGround);
        ImGui::DragFloat("地面Y", &config_.groundY, 0.05f, -200.0f, 200.0f);
        ImGui::DragFloat("跳ね返り", &config_.restitution, 0.02f, 0.0f, 1.0f);
        ImGui::DragFloat("地面摩擦", &config_.friction, 0.02f, 0.0f, 1.0f);
    }

    if (ImGui::CollapsingHeader(ICON_FA_PALETTE " 見た目と寿命", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat("スケール 最小", &config_.scaleMin, 0.01f, 0.01f, 10.0f);
        ImGui::DragFloat("スケール 最大", &config_.scaleMax, 0.01f, 0.01f, 10.0f);
        ImGui::DragFloat("寿命 最小", &config_.lifetimeMin, 0.05f, 0.1f, 20.0f);
        ImGui::DragFloat("寿命 最大", &config_.lifetimeMax, 0.05f, 0.1f, 20.0f);
        ImGui::Checkbox("終盤で縮小して消す", &config_.shrinkOnFade);
        ImGui::DragFloat("縮小開始比率", &config_.fadeStartRatio, 0.01f, 0.0f, 0.98f);
        ImGui::ColorEdit4("色", &config_.color.x);
        DrawMaterialTypeCombo(config_.materialType);
        ImGui::DragFloat("粗さ", &config_.roughness, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("金属度", &config_.metallic, 0.01f, 0.0f, 1.0f);
        ImGui::Checkbox("環境反射を使う", &config_.enableEnvMap);
        if (config_.enableEnvMap) {
            ImGui::DragFloat("環境反射の強さ", &config_.envIntensity, 0.02f, 0.0f, 4.0f);
        }
        ImGui::DragFloat("発光", &config_.emissive, 0.05f, 0.0f, 20.0f);
    }

    if (ImGui::CollapsingHeader(ICON_FA_PLAY " プレビュー", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat("カメラ前のプレビュー距離", &previewDistance_, 0.1f, 1.0f, 80.0f);
        ImGui::Checkbox("再生前に前回の破片を消す", &clearBeforePreview_);
        ImGui::Checkbox("ループ再生", &loopPreview_);
        if (loopPreview_) {
            ImGui::DragFloat("ループ間隔", &loopInterval_, 0.05f, 0.2f, 10.0f);
        }
        if (ImGui::Button(ICON_FA_PLAY " 1回再生", ImVec2(ImGui::GetContentRegionAvail().x, 30.0f))) {
            Preview();
        }
    }

    if (ImGui::CollapsingHeader(ICON_FA_SAVE " 保存と読み込み", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!presetList_.empty()) {
            std::vector<const char*> presetNames;
            for (const auto& name : presetList_) {
                presetNames.push_back(name.c_str());
            }
            if (selectedPresetIndex_ < 0 || selectedPresetIndex_ >= static_cast<int>(presetList_.size())) {
                selectedPresetIndex_ = 0;
            }
            if (ImGui::Combo("既存プリセット", &selectedPresetIndex_, presetNames.data(), static_cast<int>(presetNames.size()))) {
                CopyToBuffer(presetNameBuffer_, sizeof(presetNameBuffer_), presetList_[selectedPresetIndex_]);
            }
        } else {
            ImGui::TextDisabled("まだDebrisプリセットがありません。");
        }

        ImGui::InputText("保存名", presetNameBuffer_, sizeof(presetNameBuffer_));
        if (ImGui::Button(ICON_FA_DOWNLOAD " 保存", ImVec2(120.0f, 0.0f))) {
            Save(presetNameBuffer_);
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_UPLOAD " 読み込み", ImVec2(120.0f, 0.0f))) {
            Load(presetNameBuffer_);
        }
    }
#endif
}

void DebrisEffectEditor::RefreshLists() {
    modelList_ = ModelManager::GetInstance()->GetAvailableModelNames();
    std::sort(modelList_.begin(), modelList_.end());

    presetList_.clear();
    namespace fs = std::filesystem;
    if (fs::exists(kDebrisPresetDirectory)) {
        for (const auto& entry : fs::directory_iterator(kDebrisPresetDirectory)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                presetList_.push_back(entry.path().stem().string());
            }
        }
    }
    std::sort(presetList_.begin(), presetList_.end());
    DebrisEffectManager::GetInstance()->LoadAllPresets(kDebrisPresetDirectory);
}

void DebrisEffectEditor::Save(const std::string& presetName) {
    SyncConfigFromModelBuffers();
    config_.name = presetName.empty() ? "debris" : presetName;
    std::string filePath = std::string(kDebrisPresetDirectory) + config_.name + ".json";
    if (DebrisEffectManager::GetInstance()->SaveConfig(filePath, config_)) {
        DebrisEffectManager::GetInstance()->RegisterPreset(config_.name, config_);
        DebugConsole::GetInstance()->AddLog("Saved Debris Preset: " + config_.name);
        RefreshLists();
    }
}

void DebrisEffectEditor::Load(const std::string& presetName) {
    if (presetName.empty()) {
        return;
    }
    DebrisEffectConfig loaded;
    std::string filePath = std::string(kDebrisPresetDirectory) + presetName + ".json";
    if (DebrisEffectManager::GetInstance()->LoadConfig(filePath, loaded)) {
        config_ = loaded;
        SyncModelBuffersFromConfig();
        DebugConsole::GetInstance()->AddLog("Loaded Debris Preset: " + presetName);
    }
}

void DebrisEffectEditor::Preview() {
    SyncConfigFromModelBuffers();
    if (clearBeforePreview_) {
        DebrisEffectManager::GetInstance()->Clear();
    }
    DebrisEffectManager::GetInstance()->SpawnFromConfig(config_, GetPreviewPosition());
}

Vector3 DebrisEffectEditor::GetPreviewPosition() const {
    EffectPreviewStage* previewStage = EffectPreviewStage::GetInstance();
    if (previewStage && previewStage->IsEnabled()) {
        return previewStage->GetPreviewPosition();
    }

    const Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
    if (!camera) {
        return { 0.0f, 2.0f, 0.0f };
    }

    Vector3 eye = camera->GetEye();
    Vector3 target = camera->GetTargetPoint();
    Vector3 forward = Math::Normalize(target - eye);
    if (Math::Length(forward) < 0.001f) {
        forward = { 0.0f, 0.0f, 1.0f };
    }
    return eye + forward * previewDistance_;
}

void DebrisEffectEditor::SyncModelBuffersFromConfig() {
    std::array<std::array<char, 128>*, 4> buffers = {
        &modelBuffer0_, &modelBuffer1_, &modelBuffer2_, &modelBuffer3_
    };
    for (auto* buffer : buffers) {
        CopyToBuffer(*buffer, "");
    }
    for (size_t i = 0; i < config_.modelNames.size() && i < buffers.size(); ++i) {
        CopyToBuffer(*buffers[i], config_.modelNames[i]);
    }
}

void DebrisEffectEditor::SyncConfigFromModelBuffers() {
    config_.modelNames.clear();
    std::array<std::array<char, 128>*, 4> buffers = {
        &modelBuffer0_, &modelBuffer1_, &modelBuffer2_, &modelBuffer3_
    };
    for (const auto* buffer : buffers) {
        std::string modelName = GetBufferText(*buffer);
        if (!modelName.empty()) {
            config_.modelNames.push_back(modelName);
        }
    }
    if (config_.modelNames.empty()) {
        config_.modelNames.push_back("Primitives/cube");
        CopyToBuffer(modelBuffer0_, "Primitives/cube");
    }
}

void DebrisEffectEditor::ApplyQuickPresetRock() {
    config_ = DebrisEffectConfig{};
    config_.name = "rock_burst";
    config_.modelNames = { "Primitives/cube", "Primitives/sphere" };
    config_.spawnCount = 22;
    config_.spawnOffset = { 0.0f, 0.35f, 0.0f };
    config_.horizontalSpread = 1.2f;
    config_.verticalMin = 0.35f;
    config_.verticalMax = 1.05f;
    config_.speedMin = 8.0f;
    config_.speedMax = 18.0f;
    config_.scaleMin = 0.12f;
    config_.scaleMax = 0.38f;
    config_.lifetimeMin = 1.1f;
    config_.lifetimeMax = 1.9f;
    config_.gravity = 30.0f;
    config_.restitution = 0.38f;
    config_.friction = 0.72f;
    config_.color = { 0.62f, 0.56f, 0.49f, 1.0f };
    CopyToBuffer(presetNameBuffer_, sizeof(presetNameBuffer_), "rock_burst");
}

void DebrisEffectEditor::ApplyQuickPresetWood() {
    config_ = DebrisEffectConfig{};
    config_.name = "wood_splinter_burst";
    config_.modelNames = { "Primitives/cylinder", "Primitives/cube" };
    config_.spawnCount = 16;
    config_.horizontalSpread = 0.9f;
    config_.verticalMin = 0.25f;
    config_.verticalMax = 0.85f;
    config_.speedMin = 6.0f;
    config_.speedMax = 14.0f;
    config_.scaleMin = 0.08f;
    config_.scaleMax = 0.28f;
    config_.lifetimeMin = 0.9f;
    config_.lifetimeMax = 1.5f;
    config_.gravity = 24.0f;
    config_.restitution = 0.25f;
    config_.friction = 0.82f;
    config_.color = { 0.55f, 0.34f, 0.18f, 1.0f };
    CopyToBuffer(presetNameBuffer_, sizeof(presetNameBuffer_), "wood_splinter_burst");
}

void DebrisEffectEditor::ApplyQuickPresetPebble() {
    config_ = DebrisEffectConfig{};
    config_.name = "small_pebble_scatter";
    config_.modelNames = { "Primitives/sphere", "Primitives/cube" };
    config_.spawnCount = 34;
    config_.horizontalSpread = 1.5f;
    config_.verticalMin = 0.1f;
    config_.verticalMax = 0.55f;
    config_.speedMin = 3.5f;
    config_.speedMax = 10.0f;
    config_.scaleMin = 0.05f;
    config_.scaleMax = 0.16f;
    config_.lifetimeMin = 0.8f;
    config_.lifetimeMax = 1.35f;
    config_.gravity = 22.0f;
    config_.restitution = 0.42f;
    config_.friction = 0.7f;
    config_.color = { 0.58f, 0.58f, 0.54f, 1.0f };
    CopyToBuffer(presetNameBuffer_, sizeof(presetNameBuffer_), "small_pebble_scatter");
}
