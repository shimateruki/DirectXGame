#include "MaterialPreviewBoard.h"

#include "BaseScene.h"
#include "DebugConsole.h"
#include "DebugEditor.h"
#include "EffectPreviewStage.h"
#include "IconsFontAwesome5.h"
#include "ModelManager.h"
#include "Object3d.h"
#include "SceneManager.h"
#include "imgui.h"

#include <algorithm>
#include <memory>

namespace {
constexpr const char* kPreviewPrefix = "__Editor_MaterialPreview_";

bool IsPreviewObject(const Object3d* object) {
    return object && object->GetName().rfind(kPreviewPrefix, 0) == 0;
}
}

void MaterialPreviewBoard::Initialize(SceneManager* sceneManager, DebugEditor* editor) {
    sceneManager_ = sceneManager;
    editor_ = editor;
}

void MaterialPreviewBoard::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::Text(ICON_FA_TH_LARGE " Material Preview Board");
    ImGui::Separator();

    ImGui::InputText("表示モデル", modelNameBuffer_, sizeof(modelNameBuffer_));
    ImGui::DragFloat("間隔", &spacing_, 0.1f, 1.0f, 10.0f);
    ImGui::SliderInt("列数", &columns_, 1, 8);
    ImGui::Checkbox("Effect Preview Stage内に生成", &useEffectPreviewStage_);
    ImGui::Checkbox("選択中オブジェクトの近くに生成", &placeNearSelected_);

    if (useEffectPreviewStage_) {
        EffectPreviewStage* stage = EffectPreviewStage::GetInstance();
        if (stage && stage->IsEnabled()) {
            ImGui::TextDisabled("Effect Preview Stageの隔離空間に生成します。");
        }
        else {
            ImGui::TextDisabled("Effect Preview Stageが無効な時は通常配置になります。");
        }
    }

    ImGui::TextDisabled("現在のPreview数: %d", CountBoardObjects());

    if (ImGui::Button(ICON_FA_MAGIC " Preview Boardを生成", ImVec2(-1, 0))) {
        CreateBoard();
    }

    if (ImGui::Button(ICON_FA_TRASH_ALT " Preview Boardを削除", ImVec2(-1, 0))) {
        RemoveBoard();
    }

    ImGui::Separator();
    ImGui::TextWrapped("生成物は保存対象から除外されます。見た目確認用なので、不要になったら削除してください。");

    if (ImGui::BeginTable("MaterialPreviewTypes", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableSetupColumn("Note", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (const MaterialPreviewEntry& entry : GetEntries()) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", entry.materialType);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(entry.label);
            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled(entry.materialType >= 8 ? "専用描画パスで確認" : "通常描画パスで確認");
        }

        ImGui::EndTable();
    }
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
        origin = previewStage->GetPreviewPosition();
        int visibleColumns = (std::min)(columnCount, static_cast<int>(entries.size()));
        origin.x -= static_cast<float>(visibleColumns - 1) * spacing_ * 0.5f;
        previewStage->RequestCameraRecenter();
    }
    else if (placeNearSelected_) {
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
        object->SetMaterialType(entry.materialType);
        object->SetBlendMode(entry.materialType == 1 || entry.materialType == 3 || entry.materialType >= 8 ? BlendMode::kNormal : BlendMode::kNone);
        object->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        object->SetEmissive(entry.materialType == 3 ? 2.5f : 1.0f);

        int col = static_cast<int>(i) % columnCount;
        int row = static_cast<int>(i) / columnCount;
        object->SetTranslate({
            origin.x + static_cast<float>(col) * spacing_,
            origin.y,
            origin.z + static_cast<float>(row) * spacing_
        });
        object->SetScale({ 1.0f, 1.0f, 1.0f });
        object->UpdateLocalMatrix();
        object->UpdateWorldMatrix();
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
    return {
        { 0, "通常 (Standard)", "Standard" },
        { 1, "ガラス (Glass)", "Glass" },
        { 2, "氷・宝石 (Ice/Crystal)", "Crystal" },
        { 3, "ホログラム (Hologram)", "Hologram" },
        { 4, "消滅 (Dissolve)", "Dissolve" },
        { 5, "旧マグマ (Emissive)", "Emissive" },
        { 6, "トゥーン調 (Cel Shaded)", "Cel" },
        { 7, "ローカルフォグ (Local Fog)", "Fog" },
        { 8, "水 (Water)", "Water" },
        { 9, "新マグマ (Magma)", "Magma" },
        { 10, "分厚い氷 (Ice)", "Ice" },
        { 11, "炎 (Fire)", "Fire" },
        { 12, "レーザー (Laser)", "Laser" },
        { 13, "スライムジェル (Slime Gel)", "SlimeGel" },
        { 14, "地面衝撃波 (Shockwave)", "Shockwave" },
        { 15, "水/マグマ接触 (Liquid Contact)", "LiquidContact" }
    };
}
