#include "MaterialPreviewBoard.h"

#include "BaseScene.h"
#include "DebugConsole.h"
#include "DebugEditor.h"
#include "EffectPreviewStage.h"
#include "EditorAssetDragPayload.h"
#include "IconsFontAwesome5.h"
#include "ModelManager.h"
#include "Object3d.h"
#include "SceneManager.h"
#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <set>

namespace {
constexpr const char* kPreviewPrefix = "__Editor_MaterialPreview_";
constexpr const char* kModelRoot = "Resources/3DModel";

bool IsPreviewObject(const Object3d* object) {
    return object && object->GetName().rfind(kPreviewPrefix, 0) == 0;
}

bool IsModelFolder(const std::filesystem::path& folder) {
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) return false;

    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;

        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (ext == ".obj" || ext == ".gltf" || ext == ".glb") {
            return true;
        }
    }
    return false;
}

bool IsSpecialMaterialType(int materialType) {
    return (materialType >= 8 && materialType <= 22) || materialType == 26;
}

std::string ToModelName(const std::filesystem::path& folder) {
    std::error_code ec;
    std::filesystem::path relative = std::filesystem::relative(folder, kModelRoot, ec);
    if (ec) return {};
    return relative.generic_string();
}

const char* GetPreviewNote(int materialType) {
    if (materialType == 13 || materialType == 16) {
        return "モデル形状に重ねて確認";
    }
    return IsSpecialMaterialType(materialType) ? "専用描画パスで確認" : "通常描画パスで確認";
}
}

void MaterialPreviewBoard::Initialize(SceneManager* sceneManager, DebugEditor* editor) {
    sceneManager_ = sceneManager;
    editor_ = editor;
    RefreshModelCandidates();
}

void MaterialPreviewBoard::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::Text(ICON_FA_TH_LARGE " Material Preview Board");
    ImGui::Separator();

    DrawModelSelector();

    ImGui::DragFloat("間隔", &spacing_, 0.1f, 1.0f, 10.0f);
    ImGui::SliderInt("列数", &columns_, 1, 8);
    ImGui::Checkbox("形状モードも展開する", &expandModeVariants_);
    ImGui::Checkbox("専用シェーダーだけ表示", &showOnlySpecialMaterials_);
    ImGui::Checkbox("Effect Preview Stage内に生成", &useEffectPreviewStage_);
    ImGui::Checkbox("選択中オブジェクトの近くに生成", &placeNearSelected_);

    if (useEffectPreviewStage_) {
        EffectPreviewStage* stage = EffectPreviewStage::GetInstance();
        if (stage && stage->IsEnabled()) {
            ImGui::TextDisabled("Effect Preview Stageの隔離空間に生成します。");
        } else {
            ImGui::TextDisabled("Effect Preview Stageが無効な時は通常配置になります。");
        }
    }

    const std::vector<MaterialPreviewEntry> entries = GetEntries();
    ImGui::TextDisabled("現在のPreview数: %d / 次回生成数: %d", CountBoardObjects(), static_cast<int>(entries.size()));

    if (ImGui::Button(ICON_FA_MAGIC " Preview Boardを生成", ImVec2(-1, 0))) {
        CreateBoard();
    }

    if (ImGui::Button(ICON_FA_TRASH_ALT " Preview Boardを削除", ImVec2(-1, 0))) {
        RemoveBoard();
    }

    ImGui::Separator();
    ImGui::TextWrapped("生成物は保存対象から除外されます。確認用なので、不要になったら削除してください。");

    if (ImGui::BeginTable("MaterialPreviewTypes", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 48.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 165.0f);
        ImGui::TableSetupColumn("Mode", ImGuiTableColumnFlags_WidthFixed, 135.0f);
        ImGui::TableSetupColumn("Note", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (const MaterialPreviewEntry& entry : entries) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", entry.materialType);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(entry.label.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled("%s", entry.modeLabel.empty() ? "-" : entry.modeLabel.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextDisabled("%s", GetPreviewNote(entry.materialType));
        }

        ImGui::EndTable();
    }
#endif
}

void MaterialPreviewBoard::DrawModelSelector() {
#ifdef USE_IMGUI
    if (modelCandidates_.empty()) {
        RefreshModelCandidates();
    }

    ImGui::Text(ICON_FA_CUBE " 表示モデル");

    const char* previewText = modelNameBuffer_[0] != '\0' ? modelNameBuffer_ : "Primitives/sphere";
    if (ImGui::BeginCombo("モデルリスト", previewText)) {
        for (int i = 0; i < static_cast<int>(modelCandidates_.size()); ++i) {
            const bool selected = (i == selectedModelIndex_);
            if (ImGui::Selectable(modelCandidates_[i].c_str(), selected)) {
                selectedModelIndex_ = i;
                SetPreviewModel(modelCandidates_[i]);
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_SYNC_ALT " 更新")) {
        RefreshModelCandidates();
    }

    ImGui::Button(ICON_FA_BOX_OPEN " Projectからモデルをドロップ", ImVec2(-1, 30));
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODEL_ASSET")) {
            const std::string modelName = ReadEditorAssetDragPath(payload->Data, payload->DataSize);
            if (!modelName.empty()) {
                SetPreviewModel(modelName);
                RefreshModelCandidates();
                DebugConsole::GetInstance()->AddLog("Material preview model: " + modelName);
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::InputText("直接指定", modelNameBuffer_, sizeof(modelNameBuffer_));
#endif
}

void MaterialPreviewBoard::CreateBoard() {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;

    BaseScene* scene = sceneManager_->GetCurrentScene();
    Object3dCommon* common = scene->GetObject3dCommon();
    if (!common) return;

    RemoveBoard();

    std::string modelName = modelNameBuffer_;
    if (modelName.empty()) modelName = "Primitives/sphere";
    ModelManager::GetInstance()->LoadModel(modelName);

    std::vector<MaterialPreviewEntry> entries = GetEntries();
    int columnCount = (std::max)(1, columns_);

    Vector3 origin = { 0.0f, 2.0f, 0.0f };
    EffectPreviewStage* previewStage = EffectPreviewStage::GetInstance();
    bool placedInEffectStage = useEffectPreviewStage_ && previewStage && previewStage->IsEnabled();
    if (placedInEffectStage) {
        origin = previewStage->GetGroundPosition();
        int visibleColumns = (std::min)(columnCount, static_cast<int>(entries.size()));
        origin.x -= static_cast<float>(visibleColumns - 1) * spacing_ * 0.5f;
        previewStage->RequestCameraRecenter();
    } else if (placeNearSelected_) {
        if (Object3d* selected = editor_ ? editor_->GetSelectedObject() : nullptr) {
            origin = selected->GetTranslate();
            origin.x += spacing_ * 1.5f;
        }
    }

    for (size_t i = 0; i < entries.size(); ++i) {
        const MaterialPreviewEntry& entry = entries[i];
        auto object = std::make_unique<Object3d>();
        object->Initialize(common);
        object->SetModel(modelName);
        object->SetName(std::string(kPreviewPrefix) + std::to_string(entry.materialType) + "_" + entry.shortLabel);
        object->SetClassName("EditorOnly");
        object->SetSaveCategory("Object");
        object->SetIsLocked(true);
        object->SetCollisionAttribute(0);
        object->SetCollisionMask(0);
        ApplyPreviewDefaults(object.get(), entry);

        int col = static_cast<int>(i) % columnCount;
        int row = static_cast<int>(i) / columnCount;
        Vector3 previewPosition = {
            origin.x + static_cast<float>(col) * spacing_,
            origin.y,
            origin.z + static_cast<float>(row) * spacing_
        };
        object->SetTranslate(previewPosition);
        object->SetScale({ 1.0f, 1.0f, 1.0f });
        object->UpdateLocalMatrix();
        object->UpdateWorldMatrix();
        if (placedInEffectStage) {
            const AABB bounds = object->GetModelWorldAABB();
            if (std::isfinite(bounds.min.y)) {
                constexpr float kGroundClearance = 0.02f;
                previewPosition.y += origin.y - bounds.min.y + kGroundClearance;
                object->SetTranslate(previewPosition);
                object->UpdateLocalMatrix();
                object->UpdateWorldMatrix();
            }
        }
        scene->AddObject(std::move(object));
    }

    DebugConsole::GetInstance()->AddLog(placedInEffectStage
        ? "Material Preview Board created in Effect Preview Stage."
        : "Material Preview Board created.");
}

void MaterialPreviewBoard::RemoveBoard() {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;

    BaseScene* scene = sceneManager_->GetCurrentScene();
    std::vector<Object3d*> targets;
    for (auto& object : scene->GetObjects()) {
        if (IsPreviewObject(object.get())) {
            targets.push_back(object.get());
        }
    }

    for (Object3d* object : targets) {
        scene->RequestRemoveObject(object);
    }

    if (!targets.empty()) {
        DebugConsole::GetInstance()->AddLog("Material Preview Board removed.");
    }
}

int MaterialPreviewBoard::CountBoardObjects() const {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return 0;

    int count = 0;
    for (const auto& object : sceneManager_->GetCurrentScene()->GetObjects()) {
        if (IsPreviewObject(object.get())) ++count;
    }
    return count;
}

std::vector<MaterialPreviewBoard::MaterialPreviewEntry> MaterialPreviewBoard::GetEntries() const {
    struct Mode {
        float value;
        const char* label;
        const char* shortLabel;
    };
    struct BaseEntry {
        int type;
        const char* label;
        const char* shortLabel;
        std::vector<Mode> modes;
    };

    const std::vector<BaseEntry> baseEntries = {
        { 0, "通常 (Standard)", "Standard", {} },
        { 1, "ガラス (Glass)", "Glass", {} },
        { 2, "氷・宝石 (Ice/Crystal)", "Crystal", {} },
        { 3, "ホログラム (Hologram)", "Hologram", {} },
        { 4, "消滅 (Dissolve)", "Dissolve", {} },
        { 5, "旧マグマ (Emissive)", "Emissive", {} },
        { 6, "トゥーン調 (Cel Shaded)", "Cel", {} },
        { 7, "ローカルフォグ (Local Fog)", "Fog", {} },
        { 8, "水 (Water)", "Water", {} },
        { 9, "新マグマ (Magma)", "Magma", {} },
        { 10, "分厚い氷 (Ice)", "ThickIce", {} },
        { 11, "炎 (Fire)", "Fire", {
            { 0.0f, "炎の形", "Flame" },
            { 1.0f, "炎の球", "Ball" },
            { 2.0f, "まとい炎", "Wrapped" },
            { 3.0f, "炎の流れ", "Stream" },
            { 4.0f, "立体かがり火", "Brazier" }
        } },
        { 12, "レーザー (Laser)", "Laser", {} },
        { 13, "スライムジェル (Slime Gel)", "SlimeGel", {} },
        { 14, "地面衝撃波 (Shockwave)", "Shockwave", {} },
        { 15, "水/マグマ接触 (Liquid Contact)", "LiquidContact", {{ 0.0f, "水の泡", "Foam" }, { 1.0f, "マグマ蒸気", "Steam" }} },
        { 16, "ダメージ亀裂 (Damage Crack)", "DamageCrack", {{ 0.0f, "ブロック亀裂", "Block" }, { 1.0f, "ガラス亀裂", "Glass" }, { 2.0f, "弱点コア亀裂", "Core" }} },
        { 17, "上昇気流 (Updraft)", "Updraft", {{ 0.0f, "上昇柱", "Column" }, { 1.0f, "渦リング", "Vortex" }, { 2.0f, "横風スラッシュ", "Slash" }} },
        { 18, "スタン拘束 (Stun Bind)", "StunBind", {{ 0.0f, "微弱リング", "Ring" }, { 1.0f, "小さな電撃", "Spark" }, { 2.0f, "薄い帯電", "Aura" }} },
        { 19, "王冠解放 (Crown Unlock)", "CrownUnlock", {{ 0.0f, "魔法陣", "Circle" }, { 1.0f, "王冠バースト", "Burst" }, { 2.0f, "解放ポータル", "Portal" }} },
        { 20, "毒胞子 (Poison Spore)", "PoisonSpore", {{ 0.0f, "毒霧", "Mist" }, { 1.0f, "胞子雲", "Cloud" }, { 2.0f, "毒リング", "Ring" }} },
        { 21, "雲 (Cloud)", "Cloud", {{ 0.0f, "雲の塊", "Puff" }, { 1.0f, "流れる雲", "Drift" }, { 2.0f, "足元の煙", "Ground" }} },
        { 22, "ゲートポータル (Gate Portal)", "GatePortal", {{ 0.0f, "渦ポータル", "Swirl" }, { 1.0f, "暖色ゲート", "Warm" }, { 2.0f, "封印ゲート", "Seal" }} },
        { 23, "アニメ調地形 (Stylized Terrain)", "StylizedTerrain", {} },
        { 24, "ダッシュパネル (Dash Panel)", "DashPanel", {} },
        { 25, "スライム補正 (Slime Soft)", "SlimeSoft", {} },
        { 26, "風弾 (Wind Orb)", "WindOrb", {{ 0.0f, "安定した風弾", "Stable" }, { 1.0f, "高速の渦", "Fast" }, { 2.0f, "圧縮した風", "Compressed" }} },
        { 27, "プリズム結晶 (Prism Crystal)", "PrismCrystal", {} },
    };

    std::vector<MaterialPreviewEntry> entries;
    for (const BaseEntry& base : baseEntries) {
        if (showOnlySpecialMaterials_ && !IsSpecialMaterialType(base.type)) continue;

        if (expandModeVariants_ && !base.modes.empty()) {
            for (const Mode& mode : base.modes) {
                entries.push_back({
                    base.type,
                    mode.value,
                    base.label,
                    std::string(base.shortLabel) + "_" + mode.shortLabel,
                    mode.label
                });
            }
        } else {
            float effectType = base.modes.empty() ? 0.0f : base.modes.front().value;
            std::string modeLabel = base.modes.empty() ? "" : base.modes.front().label;
            entries.push_back({ base.type, effectType, base.label, base.shortLabel, modeLabel });
        }
    }
    return entries;
}

void MaterialPreviewBoard::RefreshModelCandidates() {
    std::set<std::string> uniqueModels = {
        "Primitives/sphere",
        "Primitives/cube",
        "Primitives/cylinder",
        "Primitives/plane"
    };

    std::error_code ec;
    const std::filesystem::path root(kModelRoot);
    if (std::filesystem::exists(root, ec)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
            if (ec) break;
            if (!entry.is_directory(ec)) continue;
            if (!IsModelFolder(entry.path())) continue;

            std::string modelName = ToModelName(entry.path());
            if (!modelName.empty()) {
                uniqueModels.insert(modelName);
            }
        }
    }

    modelCandidates_.assign(uniqueModels.begin(), uniqueModels.end());
    selectedModelIndex_ = 0;
    for (int i = 0; i < static_cast<int>(modelCandidates_.size()); ++i) {
        if (modelCandidates_[i] == modelNameBuffer_) {
            selectedModelIndex_ = i;
            break;
        }
    }
}

void MaterialPreviewBoard::SetPreviewModel(const std::string& modelName) {
    if (modelName.empty()) return;
    strncpy_s(modelNameBuffer_, modelName.c_str(), _TRUNCATE);
    for (int i = 0; i < static_cast<int>(modelCandidates_.size()); ++i) {
        if (modelCandidates_[i] == modelName) {
            selectedModelIndex_ = i;
            return;
        }
    }
    modelCandidates_.push_back(modelName);
    selectedModelIndex_ = static_cast<int>(modelCandidates_.size()) - 1;
}

void MaterialPreviewBoard::ApplyPreviewDefaults(Object3d* object, const MaterialPreviewEntry& entry) const {
    if (!object) return;

    object->SetMaterialType(entry.materialType);
    object->SetBlendMode(entry.materialType == 1 || entry.materialType == 3 || IsSpecialMaterialType(entry.materialType) ? BlendMode::kNormal : BlendMode::kNone);
    object->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    object->SetEmissive(entry.materialType == 3 ? 2.5f : 1.0f);
    if (entry.materialType == 23) {
        object->SetRoughness(0.24f);
        object->SetMetallic(0.62f);
    }
    else if (entry.materialType == 24) {
        object->SetColor({ 0.25f, 0.95f, 1.0f, 1.0f });
        object->SetRoughness(0.62f);
        object->SetMetallic(0.56f);
        object->SetTextureTiling({ 1.0f, 1.0f });
        object->SetAutoTextureTiling(false);
    }
    else if (entry.materialType == 25) {
        object->SetRoughness(0.60f);
        object->SetMetallic(0.0f);
        object->SetTextureTiling({ 1.0f, 1.0f });
        object->SetAutoTextureTiling(false);
    }
    else if (entry.materialType == 27) {
        object->SetColor({ 0.46f, 0.82f, 0.96f, 1.0f });
        object->SetRoughness(0.18f);
        object->SetMetallic(0.72f);
        object->SetEnableEnvMap(true);
        object->SetEnvIntensity(1.15f);
    }

    MeshRenderer* renderer = object->GetMeshRenderer();
    auto* water = renderer ? renderer->GetWaterParamData() : nullptr;
    if (!water) return;

    water->effectType = entry.effectType;
    water->waveSpeed = 2.0f;
    water->waveHeight = 1.0f;
    water->waveFrequency = 4.0f;
    water->effectScale = 1.0f;
    water->effectSoftness = 0.55f;
    water->effectIntensity = 1.0f;
    water->billboardScale = 1.0f;
    water->flowSpeedX = 0.1f;
    water->flowSpeedY = 0.1f;

    switch (entry.materialType) {
    case 8:
        object->SetBlendMode(BlendMode::kNormal);
        water->effectType = 0.0f;
        water->waveSpeed = 1.05f;
        water->waveHeight = 0.42f;
        water->waveFrequency = 4.2f;
        water->flowSpeedX = 0.035f;
        water->flowSpeedY = 0.018f;
        water->effectScale = 0.82f;
        water->effectSoftness = 0.48f;
        water->effectIntensity = 1.15f;
        water->billboardScale = 0.50f;
        water->effectScaleX = 8.0f;
        water->effectScaleY = 0.35f;
        water->effectScaleZ = 1.15f;
        break;
    case 9:
        object->SetBlendMode(BlendMode::kNone);
        object->SetColor({ 1.0f, 0.12f, 0.02f, 1.0f });
        object->SetEmissive(1.35f);
        water->waveSpeed = 0.62f;
        water->waveHeight = 0.42f;
        water->waveFrequency = 2.4f;
        water->flowSpeedX = 0.08f;
        water->flowSpeedY = 0.03f;
        water->effectScale = 1.65f;
        water->effectSoftness = 0.62f;
        water->effectIntensity = 1.35f;
        break;
    case 11:
        object->SetBlendMode(BlendMode::kNormal);
        water->waveSpeed = 2.0f;
        water->waveFrequency = 4.6f;
        water->effectScale = 1.0f;
        water->billboardScale = 1.05f;
        water->effectIntensity = 1.2f;
        break;
    case 12:
        object->SetBlendMode(BlendMode::kAdd);
        object->SetEmissive(6.0f);
        water->waveSpeed = 7.0f;
        water->waveFrequency = 18.0f;
        water->effectScale = 1.6f;
        water->effectSoftness = 0.35f;
        water->effectIntensity = 3.2f;
        break;
    case 13:
        water->waveSpeed = 1.7f;
        water->waveHeight = 0.55f;
        water->waveFrequency = 9.0f;
        water->effectScale = 0.55f;
        water->effectIntensity = 0.75f;
        break;
    case 14:
        water->waveSpeed = 3.0f;
        water->waveFrequency = 12.0f;
        water->effectScale = 2.6f;
        water->effectIntensity = 1.6f;
        break;
    case 16:
        water->waveSpeed = 1.6f;
        water->waveHeight = 1.0f;
        water->waveFrequency = 18.0f;
        water->effectScale = 1.1f;
        water->effectSoftness = 0.38f;
        water->effectIntensity = 1.0f;
        break;
    case 17:
        water->waveSpeed = 2.4f;
        water->waveHeight = 1.0f;
        water->waveFrequency = 14.0f;
        water->effectScale = 1.2f;
        water->effectIntensity = 0.9f;
        break;
    case 18:
        object->SetBlendMode(BlendMode::kAdd);
        water->waveSpeed = 3.5f;
        water->waveFrequency = 14.0f;
        water->effectScale = 0.85f;
        water->effectSoftness = 0.35f;
        water->effectIntensity = 0.9f;
        break;
    case 19:
        object->SetBlendMode(BlendMode::kAdd);
        water->waveSpeed = 2.4f;
        water->waveFrequency = 12.0f;
        water->effectScale = 1.35f;
        water->effectIntensity = 1.6f;
        break;
    case 20:
        water->waveSpeed = 1.5f;
        water->waveHeight = 1.1f;
        water->waveFrequency = 9.0f;
        water->effectScale = 1.5f;
        water->effectIntensity = 1.15f;
        break;
    case 21:
        water->waveSpeed = 0.85f;
        water->waveHeight = 1.35f;
        water->waveFrequency = 7.5f;
        water->effectScale = 1.15f;
        water->effectSoftness = 0.72f;
        water->effectIntensity = 0.95f;
        water->billboardScale = 1.35f;
        break;
    case 22:
        object->SetBlendMode(BlendMode::kNormal);
        object->SetEmissive(3.2f);
        water->waveSpeed = 2.1f;
        water->waveHeight = 1.35f;
        water->waveFrequency = 14.0f;
        water->effectScale = 1.05f;
        water->effectSoftness = 0.46f;
        water->effectIntensity = 2.2f;
        water->billboardScale = 1.15f;
        break;
    case 26:
        object->SetBlendMode(BlendMode::kNormal);
        object->SetColor({ 0.42f, 1.0f, 0.80f, 0.96f });
        object->SetEmissive(1.9f);
        water->waveSpeed = 2.65f;
        water->waveHeight = 0.72f;
        water->waveFrequency = 12.0f;
        water->effectScale = 0.86f;
        water->effectSoftness = 0.32f;
        water->effectIntensity = 1.72f;
        water->billboardScale = 1.0f;
        break;
    default:
        break;
    }
}
