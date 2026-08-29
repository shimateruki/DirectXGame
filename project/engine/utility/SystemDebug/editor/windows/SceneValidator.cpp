#include "SceneValidator.h"

#include "BaseScene.h"
#include "AssetDatabase.h"
#include "EditorTransactionManager.h"
#include "Model.h"
#include "CaptureToolWindow.h"
#include "CollisionConfig.h"
#include "DebugEditor.h"
#include "IconsFontAwesome5.h"
#include "ModelManager.h"
#include "Object3d.h"
#include "PrimitiveDrawer.h"
#include "SceneManager.h"
#include "TextureManager.h"
#include "imgui.h"
#include <cctype>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <unordered_set>

namespace {
constexpr int kMaxKnownMaterialType = 27;
constexpr float kPi = 3.14159265358979323846f;

bool IsEditorOnlyObject(const Object3d* object) {
    if (!object) return false;
    const std::string& name = object->GetName();
    return object->IsEditorInternal() || name.rfind("__Editor_", 0) == 0 || object->GetClassName() == "EditorOnly";
}

std::string NormalizePath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

OBB MakeObbFromAabb(const AABB& bounds) {
    OBB result{};
    result.center = (bounds.min + bounds.max) * 0.5f;
    result.size = (bounds.max - bounds.min) * 0.5f;
    result.orientations[0] = { 1.0f, 0.0f, 0.0f };
    result.orientations[1] = { 0.0f, 1.0f, 0.0f };
    result.orientations[2] = { 0.0f, 0.0f, 1.0f };
    return result;
}

Matrix4x4 MakeObbMatrix(const OBB& bounds) {
    Matrix4x4 result = Math::MakeIdentity4x4();
    const Vector3 scaledX = bounds.orientations[0] * (std::abs(bounds.size.x) * 2.0f);
    const Vector3 scaledY = bounds.orientations[1] * (std::abs(bounds.size.y) * 2.0f);
    const Vector3 scaledZ = bounds.orientations[2] * (std::abs(bounds.size.z) * 2.0f);
    result.m[0][0] = scaledX.x; result.m[0][1] = scaledX.y; result.m[0][2] = scaledX.z;
    result.m[1][0] = scaledY.x; result.m[1][1] = scaledY.y; result.m[1][2] = scaledY.z;
    result.m[2][0] = scaledZ.x; result.m[2][1] = scaledZ.y; result.m[2][2] = scaledZ.z;
    result.m[3][0] = bounds.center.x;
    result.m[3][1] = bounds.center.y;
    result.m[3][2] = bounds.center.z;
    return result;
}

float GetObbTop(const OBB& bounds) {
    return bounds.center.y +
        std::abs(bounds.orientations[0].y * bounds.size.x) +
        std::abs(bounds.orientations[1].y * bounds.size.y) +
        std::abs(bounds.orientations[2].y * bounds.size.z);
}

bool ContainsAnyToken(const std::string& value, const std::initializer_list<const char*>& tokens) {
    return std::any_of(tokens.begin(), tokens.end(), [&value](const char* token) {
        return value.find(token) != std::string::npos;
    });
}

bool IsDescendantOf(const Object3d* child, const Object3d* possibleParent) {
    if (!child || !possibleParent) return false;
    const Object3d* current = child->GetParent();
    for (int depth = 0; current && depth < 64; ++depth) {
        if (current == possibleParent) return true;
        current = current->GetParent();
    }
    return false;
}

std::string ToLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string FileStemLower(const std::string& value) {
    return ToLowerCopy(std::filesystem::path(NormalizePath(value)).stem().string());
}

int CommonPrefixScore(const std::string& first, const std::string& second) {
    const std::size_t count = (std::min)(first.size(), second.size());
    std::size_t index = 0;
    while (index < count && first[index] == second[index]) {
        ++index;
    }
    return static_cast<int>(index);
}

std::string FormatVector3(const Vector3& value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3)
        << "(" << value.x << ", " << value.y << ", " << value.z << ")";
    return stream.str();
}

std::string FormatCollider(const ColliderConfig& collider) {
    return "center " + FormatVector3(collider.center) +
        " / half-size " + FormatVector3(collider.size);
}

Vector3 NormalizeEuler(const Vector3& rotation) {
    const float fullTurn = kPi * 2.0f;
    return {
        std::remainder(rotation.x, fullTurn),
        std::remainder(rotation.y, fullTurn),
        std::remainder(rotation.z, fullTurn),
    };
}

bool IsFiniteVector(const Vector3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

} // namespace

void SceneValidator::Initialize(SceneManager* sceneManager, DebugEditor* editor) {
    sceneManager_ = sceneManager;
    editor_ = editor;
}

std::string SceneValidator::GetName() {
    return "シーン視覚監査 (Scene Visual Audit)";
}

void SceneValidator::DrawImGui() {
#ifdef USE_IMGUI
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) {
        ImGui::TextDisabled("現在のシーンがありません");
        return;
    }

    ImGui::Text(ICON_FA_CHECK_CIRCLE " シーン視覚監査 (Scene Visual Audit)");
    ImGui::Separator();

    if (ImGui::Button(ICON_FA_SYNC " 再チェック")) {
        Refresh();
    }
    ImGui::SameLine();
    ImGui::Checkbox("自動チェック", &autoRefresh_);
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FILE_EXPORT " シーン視覚監査を出力")) {
        Refresh();
        ExportVisualAudit();
    }

    if (!auditStatusText_.empty()) {
        ImGui::TextWrapped("%s", auditStatusText_.c_str());
    }

    if (ImGui::CollapsingHeader(
        ICON_FA_PROJECT_DIAGRAM " Asset Dependency / Prewarm",
        ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextWrapped(
            "Scene Manifestと参照JSONを再帰走査し、Texture・Model・Audio・VFX・Animator・Cinematicを列挙します。");
        if (ImGui::Button(ICON_FA_SYNC " 依存関係を収集してPrewarm確認")) {
            BaseScene* scene = sceneManager_->GetCurrentScene();
            if (scene) {
                dependencyReport_ =
                    ScenePreloader::Inspect(scene->BuildAsyncLoadManifest());
                hasDependencyReport_ = true;
                dependencyStatusText_ =
                    dependencyReport_.missingPaths.empty()
                    ? "依存Assetを収集し、CPU側Prewarmを確認しました。"
                    : "欠損Assetがあります。下の一覧を確認してください。";
            }
        }

        if (!dependencyStatusText_.empty()) {
            ImGui::TextWrapped("%s", dependencyStatusText_.c_str());
        }
        if (hasDependencyReport_) {
            ImGui::Text(
                "JSON %zu / Model %zu / Texture %zu / Audio %zu / Other %zu",
                dependencyReport_.Count(SceneDependencyType::Json),
                dependencyReport_.Count(SceneDependencyType::Model),
                dependencyReport_.Count(SceneDependencyType::Texture),
                dependencyReport_.Count(SceneDependencyType::Audio),
                dependencyReport_.Count(SceneDependencyType::Other));
            if (dependencyReport_.missingPaths.empty()) {
                ImGui::TextColored(
                    ImVec4(0.45f, 1.0f, 0.55f, 1.0f),
                    "Missing: 0");
            }
            else {
                ImGui::TextColored(
                    ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                    "Missing: %zu",
                    dependencyReport_.missingPaths.size());
                ImGui::BeginChild(
                    "##MissingSceneDependencies",
                    ImVec2(0.0f, 110.0f),
                    true);
                for (const std::string& path :
                    dependencyReport_.missingPaths) {
                    ImGui::BulletText("%s", path.c_str());
                }
                ImGui::EndChild();
            }
        }
    }
    ImGui::Checkbox("床へのめり込み候補", &checkFloorPenetrations_);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("見た目用モデルの下面と、床用コライダーの重なりを検査します。");
    }
    if (checkFloorPenetrations_) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::DragFloat("検出深さ(m)", &floorPenetrationThreshold_, 0.01f, 0.05f, 2.0f, "%.2f");
    }

    ImGui::Checkbox("Object同士のOBB重なり", &checkObjectOverlaps_);
    if (checkObjectOverlaps_) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::DragFloat("重なり深さ(m)", &objectOverlapThreshold_, 0.01f, 0.05f, 5.0f, "%.2f");
    }

    ImGui::Checkbox("移動・回転ギミックの時間監査", &checkDynamicPaths_);
    if (checkDynamicPaths_) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        ImGui::DragInt("サンプル数", &dynamicSampleCount_, 1.0f, 4, 32);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        ImGui::DragFloat("動的深さ(m)", &dynamicOverlapThreshold_, 0.01f, 0.05f, 5.0f, "%.2f");
    }

    ImGui::Checkbox("スターなど収集物の向き", &checkCollectibleOrientations_);
    if (checkCollectibleOrientations_) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        ImGui::DragFloat("許容角度(deg)", &collectibleRotationToleranceDegrees_, 0.5f, 1.0f, 45.0f, "%.1f");
    }
    ImGui::Checkbox("モデル境界とコライダーのずれ", &checkColliderFits_);
    if (checkColliderFits_) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        ImGui::DragFloat("許容差率", &colliderFitTolerance_, 0.01f, 0.10f, 1.0f, "%.2f");
    }
    ImGui::Checkbox("過大な回転値を正規化", &checkRotationNormalization_);
    ImGui::Checkbox("修正前後をシーン上に表示", &showFixPreview_);

    const char* filters[] = { "すべて", "Errorのみ", "Warning以上", "Infoのみ" };
    ImGui::Combo("表示フィルタ", &selectedSeverityFilter_, filters, IM_ARRAYSIZE(filters));

    if (autoRefresh_) {
        autoRefreshTimer_ += ImGui::GetIO().DeltaTime;
        if (autoRefreshTimer_ >= 1.0f) {
            autoRefreshTimer_ = 0.0f;
            Refresh();
        }
    }

    int errorCount = 0;
    int warningCount = 0;
    int infoCount = 0;
    for (const Issue& issue : issues_) {
        if (issue.severity == Severity::Error) ++errorCount;
        else if (issue.severity == Severity::Warning) ++warningCount;
        else ++infoCount;
    }

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Error: %d", errorCount);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.25f, 1.0f), "Warning: %d", warningCount);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), "Info: %d", infoCount);

    int fixCount = 0;
    int selectedFixCount = 0;
    int riskyFixCount = 0;
    for (const Issue& issue : issues_) {
        if (!issue.hasFix) continue;
        ++fixCount;
        if (issue.fixSelected) {
            ++selectedFixCount;
            if (!issue.safeFix) ++riskyFixCount;
        }
    }
    ImGui::Text("修正候補: %d / 選択中: %d", fixCount, selectedFixCount);
    if (ImGui::Button("安全候補だけ選択")) {
        for (Issue& issue : issues_) {
            issue.fixSelected = issue.hasFix && issue.safeFix;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("すべて解除")) {
        for (Issue& issue : issues_) issue.fixSelected = false;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(selectedFixCount == 0);
    if (ImGui::Button(ICON_FA_MAGIC " 選択候補を適用")) {
        ImGui::OpenPopup("監査修正の確認");
    }
    ImGui::EndDisabled();

    if (ImGui::BeginPopupModal("監査修正の確認", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%d件の候補をUndo可能な一括操作として適用します。", selectedFixCount);
        if (riskyFixCount > 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.20f, 1.0f),
                "うち%d件は配置意図の確認が必要な候補です。", riskyFixCount);
        }
        if (ImGui::Button("適用", ImVec2(120.0f, 0.0f))) {
            ApplySelectedFixes();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("キャンセル", ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();

    if (selectedIssueIndex_ >= 0 && selectedIssueIndex_ < static_cast<int>(issues_.size())) {
        if (ImGui::Button(ICON_FA_CROSSHAIRS " 選択問題へ移動")) {
            SelectIssue(static_cast<std::size_t>(selectedIssueIndex_));
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_CAMERA " 選択問題を撮影")) {
            CaptureSelectedIssue();
        }
        ImGui::TextDisabled("赤: 主対象 / シアン: 重なり相手");
        DrawFixPreview(issues_[static_cast<std::size_t>(selectedIssueIndex_)]);
        ImGui::Separator();
    }

    if (issues_.empty()) {
        ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.55f, 1.0f), "問題は見つかっていません");
        return;
    }

    if (ImGui::BeginTable("SceneValidatorIssues", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("重要度", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("カテゴリ", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("対象", ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableSetupColumn("内容", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("候補", ImGuiTableColumnFlags_WidthFixed, 56.0f);
        ImGui::TableHeadersRow();

        for (std::size_t issueIndex = 0; issueIndex < issues_.size(); ++issueIndex) {
            Issue& issue = issues_[issueIndex];
            bool show = true;
            if (selectedSeverityFilter_ == 1) show = issue.severity == Severity::Error;
            if (selectedSeverityFilter_ == 2) show = issue.severity == Severity::Error || issue.severity == Severity::Warning;
            if (selectedSeverityFilter_ == 3) show = issue.severity == Severity::Info;
            if (!show) continue;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(GetSeverityColor(issue.severity)), "%s", GetSeverityLabel(issue.severity));
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(issue.category.c_str());
            ImGui::TableSetColumnIndex(2);
            std::string targetLabel = issue.objectName;
            if (!issue.secondaryObjectName.empty()) {
                targetLabel += "  <->  " + issue.secondaryObjectName;
            }
            ImGui::PushID(static_cast<int>(issueIndex));
            if (ImGui::Selectable(targetLabel.c_str(), selectedIssueIndex_ == static_cast<int>(issueIndex))) {
                SelectIssue(issueIndex);
            }
            ImGui::PopID();
            ImGui::TableSetColumnIndex(3);
            ImGui::TextWrapped("%s", issue.message.c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::PushID(static_cast<int>(issueIndex) + 200000);
            if (issue.hasFix) {
                ImGui::Checkbox("##FixCandidate", &issue.fixSelected);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", issue.safeFix ? "見た目を変えない安全候補" : "配置意図の確認が必要");
                }
            } else {
                ImGui::TextDisabled("-");
            }
        }

        ImGui::EndTable();
    }
#endif
}

void SceneValidator::Refresh() {
    issues_.clear();
    auto makeIssueKey = [](const Issue& issue) {
        return issue.objectGuid + "|" + issue.category + "|" +
            std::to_string(static_cast<int>(issue.fixKind)) + "|" + issue.fixTitle;
    };
    std::map<std::string, std::pair<bool, int>> previousFixSelections;
    std::string previousSelectedIssueKey;
    if (selectedIssueIndex_ >= 0 && selectedIssueIndex_ < static_cast<int>(issues_.size())) {
        previousSelectedIssueKey = makeIssueKey(issues_[static_cast<std::size_t>(selectedIssueIndex_)]);
    }
    for (const Issue& issue : issues_) {
        if (issue.hasFix) {
            previousFixSelections[makeIssueKey(issue)] = {
                issue.fixSelected, issue.selectedAssetCandidate
            };
        }
    }

    selectedIssueIndex_ = -1;
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;

    BaseScene* scene = sceneManager_->GetCurrentScene();
    auto& objects = scene->GetObjects();

    std::map<int, std::vector<const Object3d*>> eventObjects;
    std::set<int> eventIDs;

    for (const auto& objectPtr : objects) {
        const Object3d* object = objectPtr.get();
        if (!object || IsEditorOnlyObject(object)) continue;

        int eventID = object->GetEventID();
        if (eventID > 0) {
            eventIDs.insert(eventID);
            eventObjects[eventID].push_back(object);
        }
    }

    for (const auto& [eventID, list] : eventObjects) {
        if (list.size() <= 1) continue;
        std::string names;
        for (const Object3d* object : list) {
            if (!names.empty()) names += ", ";
            names += object->GetName();
        }
        AddIssue(Severity::Error, list.front(), "Event ID", "Event ID " + std::to_string(eventID) + " が重複しています: " + names);
    }

    int nextEventId = eventIDs.empty() ? 1 : (*eventIDs.rbegin() + 1);
    for (const auto& [eventId, list] : eventObjects) {
        if (list.size() <= 1) continue;
        for (std::size_t duplicateIndex = 1; duplicateIndex < list.size(); ++duplicateIndex) {
            while (eventIDs.count(nextEventId) != 0) ++nextEventId;
            const Object3d* duplicate = list[duplicateIndex];
            AddIssue(Severity::Error, duplicate, "Event ID 修正候補",
                "重複ID " + std::to_string(eventId) + " を未使用IDへ変更できます。Target側の意図は適用前に確認してください。");
            SetEventIdFix(issues_.back(), duplicate, nextEventId,
                "Event ID " + std::to_string(eventId) + " -> " + std::to_string(nextEventId), false);
            eventIDs.insert(nextEventId);
            ++nextEventId;
        }
    }

    for (const auto& objectPtr : objects) {
        const Object3d* object = objectPtr.get();
        if (!object || IsEditorOnlyObject(object)) continue;

        const std::string className = object->GetClassName();
        const std::string modelName = object->GetModelName();
        const std::string saveCategory = object->GetSaveCategory();
        const auto& collider = object->GetColliderConfig();
        const bool isInvisibleGameplayVolume =
            className == "Gimmick" && modelName.empty() && collider.type != ColliderType::kNone;
        const bool isInactiveGameplayCollider =
            className == "Gimmick" && object->param_.has_value() && !object->param_->startActive;

        if (object->GetTargetID() > 0 && eventIDs.count(object->GetTargetID()) == 0) {
            AddIssue(Severity::Error, object, "Target ID", "Target ID " + std::to_string(object->GetTargetID()) + " の接続先が見つかりません");
        }

        if (className != "InvisibleBox" && className != "MeshRoot" &&
            !object->IsCameraObject() && !isInvisibleGameplayVolume) {
            if (modelName.empty()) {
                AddIssue(Severity::Error, object, "Model", "modelName が空です");
                SetAssetFix(issues_.back(), object, FixKind::Model, modelName,
                    FindAssetCandidates(object->GetName(), true), "Missing Modelの修復");
            }
            else if (!DoesModelExist(modelName)) {
                AddIssue(Severity::Error, object, "Model", "モデルが見つかりません: " + modelName);
                SetAssetFix(issues_.back(), object, FixKind::Model, modelName,
                    FindAssetCandidates(modelName, true), "Missing Modelの修復");
            }
        }

        if (saveCategory != "Object" && saveCategory != "Player" && saveCategory != "Enemy" && saveCategory != "Camera") {
            AddIssue(Severity::Warning, object, "SaveCategory", "保存カテゴリが不明です: " + saveCategory);
        }

        int materialType = object->GetMaterialType();
        if (materialType < 0 || materialType > kMaxKnownMaterialType) {
            AddIssue(Severity::Error, object, "Material", "materialType が範囲外です: " + std::to_string(materialType));
        }

        int blendMode = static_cast<int>(object->GetBlendMode());
        if (blendMode < 0 || blendMode >= static_cast<int>(BlendMode::kCountOfBlendMode)) {
            AddIssue(Severity::Error, object, "Material", "blendMode が範囲外です: " + std::to_string(blendMode));
        }

        if (collider.type == ColliderType::kNone && (object->GetCollisionAttribute() != 0 || object->GetCollisionMask() != 0)) {
            AddIssue(Severity::Warning, object, "Collision", "形状タイプが None ですが、collisionAttribute または collisionMask が残っています");
            SetCollisionFilterFix(issues_.back(), object, 0, 0,
                "未使用の衝突属性とマスクをクリア", true);
        }

        if (collider.type != ColliderType::kNone &&
            (object->GetCollisionAttribute() == 0 || object->GetCollisionMask() == 0) &&
            !isInactiveGameplayCollider) {
            AddIssue(Severity::Info, object, "Collision", "コライダー形状がありますが、衝突属性かマスクが 0 です");
        }

        if (!object->GetTexturePath().empty() && !DoesFileExist(object->GetTexturePath())) {
            AddIssue(Severity::Warning, object, "Texture", "基本画像が見つかりません: " + object->GetTexturePath());
            SetAssetFix(issues_.back(), object, FixKind::Texture, object->GetTexturePath(),
                FindAssetCandidates(object->GetTexturePath(), false), "基本画像の参照修復");
        }

        if (!object->GetNormalMapPath().empty() && !DoesFileExist(object->GetNormalMapPath())) {
            AddIssue(Severity::Warning, object, "Texture", "Normal Map が見つかりません: " + object->GetNormalMapPath());
            SetAssetFix(issues_.back(), object, FixKind::NormalMap, object->GetNormalMapPath(),
                FindAssetCandidates(object->GetNormalMapPath(), false), "Normal Mapの参照修復");
        }

        if (!object->GetOrmMapPath().empty() && !DoesFileExist(object->GetOrmMapPath())) {
            AddIssue(Severity::Warning, object, "Texture", "ORM Map が見つかりません: " + object->GetOrmMapPath());
            SetAssetFix(issues_.back(), object, FixKind::OrmMap, object->GetOrmMapPath(),
                FindAssetCandidates(object->GetOrmMapPath(), false), "ORM Mapの参照修復");
        }

        if ((saveCategory == "Enemy" || !object->GetEnemyType().empty()) && !object->param_.has_value()) {
            AddIssue(Severity::Warning, object, "Parameter", "敵扱いのオブジェクトですが param がありません");
        }

        if (!object->GetGimmickType().empty() && !object->param_.has_value()) {
            AddIssue(Severity::Warning, object, "Parameter", "ギミック種別がありますが param がありません");
        }
    }

    if (checkFloorPenetrations_) {
        CheckFloorPenetrations();
    }
    if (checkObjectOverlaps_) {
        CheckObjectOverlaps();
    }
    if (checkDynamicPaths_) {
        CheckDynamicPathOverlaps();
    }
    if (checkCollectibleOrientations_) {
        CheckCollectibleOrientations();
    }
    if (checkColliderFits_) {
        CheckColliderFits();
    }
    if (checkRotationNormalization_) {
        CheckRotationNormalization();
    }
    for (std::size_t index = 0; index < issues_.size(); ++index) {
        Issue& issue = issues_[index];
        const std::string issueKey = makeIssueKey(issue);
        const auto previous = previousFixSelections.find(issueKey);
        if (previous != previousFixSelections.end()) {
            issue.fixSelected = previous->second.first;
            if (!issue.assetCandidates.empty()) {
                issue.selectedAssetCandidate = (std::clamp)(previous->second.second, 0,
                    static_cast<int>(issue.assetCandidates.size()) - 1);
                issue.afterFix.textValue = issue.assetCandidates[issue.selectedAssetCandidate];
                issue.afterText = issue.afterFix.textValue;
            }
        }
        if (!previousSelectedIssueKey.empty() && issueKey == previousSelectedIssueKey) {
            selectedIssueIndex_ = static_cast<int>(index);
        }
    }
}


void SceneValidator::CheckCollectibleOrientations() {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;

    const float toleranceRadians = (std::clamp)(collectibleRotationToleranceDegrees_, 1.0f, 45.0f) * kPi / 180.0f;
    for (const auto& objectPtr : sceneManager_->GetCurrentScene()->GetObjects()) {
        const Object3d* object = objectPtr.get();
        if (!object || IsEditorOnlyObject(object)) continue;

        const std::string& name = object->GetName();
        const bool isStarCoin = name.find("StarCoin") != std::string::npos;
        if (!isStarCoin) continue;

        const Vector3 rotation = object->GetRotation();
        const float tiltX = std::abs(std::remainder(rotation.x, kPi * 2.0f));
        const float tiltZ = std::abs(std::remainder(rotation.z, kPi * 2.0f));
        const float yaw = std::abs(std::remainder(rotation.y, kPi * 2.0f));
        if (tiltX <= toleranceRadians && tiltZ <= toleranceRadians && yaw <= toleranceRadians) {
            continue;
        }

        std::ostringstream message;
        message << std::fixed << std::setprecision(1)
            << "収集物の正面が基準方向から外れています。Rotation(deg) = ("
            << rotation.x * 180.0f / kPi << ", "
            << rotation.y * 180.0f / kPi << ", "
            << rotation.z * 180.0f / kPi
            << ")。正面を見せる配置なら (0, 0, 0) を基準にしてください。";
        AddIssue(Severity::Warning, object, "収集物の向き", message.str());
        SetRotationFix(issues_.back(), object, { 0.0f, 0.0f, 0.0f },
            "収集物を基準方向へ合わせる", false);
    }
}

void SceneValidator::CheckColliderFits() {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;

    const float tolerance = (std::clamp)(colliderFitTolerance_, 0.10f, 1.0f);
    for (const auto& objectPtr : sceneManager_->GetCurrentScene()->GetObjects()) {
        const Object3d* object = objectPtr.get();
        if (!object || IsEditorOnlyObject(object) || object->GetClassName() == "InvisibleBox") continue;
        if (object->GetSaveCategory() == "Player" || object->GetSaveCategory() == "Enemy") continue;
        if (object->GetColliderType() != ColliderType::kAABB &&
            object->GetColliderType() != ColliderType::kOBB) {
            continue;
        }

        Model* model = object->GetModel();
        if (!model) continue;
        const Vector3 localMin = model->GetLocalAabbMin();
        const Vector3 localMax = model->GetLocalAabbMax();
        const Vector3 expectedCenter = (localMin + localMax) * 0.5f;
        const Vector3 expectedSize = (localMax - localMin) * 0.5f;
        if (expectedSize.x <= 0.001f || expectedSize.y <= 0.001f || expectedSize.z <= 0.001f) continue;

        const ColliderConfig current = object->GetColliderConfig();
        const float centerError = Math::Length(current.center - expectedCenter);
        const float errorX = std::abs(current.size.x - expectedSize.x) / (std::max)(expectedSize.x, 0.05f);
        const float errorY = std::abs(current.size.y - expectedSize.y) / (std::max)(expectedSize.y, 0.05f);
        const float errorZ = std::abs(current.size.z - expectedSize.z) / (std::max)(expectedSize.z, 0.05f);
        const float sizeError = (std::max)({ errorX, errorY, errorZ });
        if (centerError <= 0.08f && sizeError <= tolerance) continue;

        std::ostringstream message;
        message << std::fixed << std::setprecision(2)
            << "モデル境界と箱コライダーがずれています。中心差 " << centerError
            << "m / 最大サイズ差率 " << sizeError << "。";
        AddIssue(Severity::Info, object, "Collider Fit", message.str());

        ColliderConfig fitted = current;
        fitted.center = expectedCenter;
        fitted.size = expectedSize;
        SetColliderFix(issues_.back(), object, fitted, "モデルのローカル境界へ合わせる", false);
    }
}

void SceneValidator::CheckRotationNormalization() {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;

    const float fullTurn = kPi * 2.0f;
    for (const auto& objectPtr : sceneManager_->GetCurrentScene()->GetObjects()) {
        const Object3d* object = objectPtr.get();
        if (!object || IsEditorOnlyObject(object)) continue;
        const Vector3 rotation = object->GetRotation();
        if (!IsFiniteVector(rotation)) continue;
        if (std::abs(rotation.x) <= fullTurn && std::abs(rotation.y) <= fullTurn &&
            std::abs(rotation.z) <= fullTurn) {
            continue;
        }

        const Vector3 normalized = NormalizeEuler(rotation);
        AddIssue(Severity::Info, object, "Rotation",
            "360度を超えるEuler角を、見た目を変えずに -180〜180度へ正規化できます。");
        SetRotationFix(issues_.back(), object, normalized, "過大な回転値を正規化", true);
    }
}

void SceneValidator::CheckFloorPenetrations() {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return;

    BaseScene* scene = sceneManager_->GetCurrentScene();
    const auto& objects = scene->GetObjects();
    std::vector<const Object3d*> floorColliders;

    for (const auto& objectPtr : objects) {
        const Object3d* object = objectPtr.get();
        if (!object || IsEditorOnlyObject(object) || object->GetClassName() != "InvisibleBox") continue;
        if (object->GetColliderConfig().type == ColliderType::kNone) continue;
        if ((object->GetCollisionAttribute() & kGround) == 0) continue;

        const std::string& name = object->GetName();
        if (name.find("WallCollision") != std::string::npos || name.find("Trigger") != std::string::npos) continue;
        floorColliders.push_back(object);
    }

    static const char* placementTokens[] = {
        "Bridge", "Decor", "Gate", "StarCoin", "Pillar", "Brazier",
        "Tree", "Sign", "Spike", "Coin", "Wall", "Block", "RingBurner",
    };

    for (const auto& objectPtr : objects) {
        const Object3d* object = objectPtr.get();
        if (!object || IsEditorOnlyObject(object) || !object->GetIsVisible()) continue;
        if (object->GetClassName() == "InvisibleBox" || object->GetModelName().empty()) continue;

        const std::string& name = object->GetName();
        const bool isPlacementObject = std::any_of(
            std::begin(placementTokens), std::end(placementTokens),
            [&name](const char* token) { return name.find(token) != std::string::npos; });
        if (!isPlacementObject) continue;

        const AABB visualBounds = object->GetModelWorldAABB();
        float deepestPenetration = 0.0f;
        const Object3d* deepestFloor = nullptr;

        for (const Object3d* floor : floorColliders) {
            const std::string& floorName = floor->GetName();
            // 見た目モデルと、それ専用に分離した透明判定は同一配置物として扱います。
            // 例: Stage3_HighCrown_BossGateFrame と [Collision] ..._Pillar_1。
            if (floorName.find(name) != std::string::npos) {
                continue;
            }
            if (name.find("Bridge") != std::string::npos && floorName.find("Bridge") != std::string::npos) {
                continue;
            }

            const AABB floorBounds = floor->GetAABB();
            const float overlapX = (std::min)(visualBounds.max.x, floorBounds.max.x) -
                (std::max)(visualBounds.min.x, floorBounds.min.x);
            const float overlapZ = (std::min)(visualBounds.max.z, floorBounds.max.z) -
                (std::max)(visualBounds.min.z, floorBounds.min.z);
            if (overlapX <= 0.25f || overlapZ <= 0.25f || visualBounds.max.y <= floorBounds.min.y) continue;

            const float penetration = floorBounds.max.y - visualBounds.min.y;
            if (penetration > floorPenetrationThreshold_ && penetration > deepestPenetration) {
                deepestPenetration = penetration;
                deepestFloor = floor;
            }
        }

        if (deepestFloor) {
            std::ostringstream message;
            message << std::fixed << std::setprecision(2)
                << "見た目の下面が床「" << deepestFloor->GetName() << "」へ約 "
                << deepestPenetration << "m 入っています。配置またはモデル原点を確認してください。";
            AddSpatialIssue(
                Severity::Warning, object, deepestFloor, "配置/めり込み", message.str(),
                MakeObbFromAabb(visualBounds), GetAuditBounds(deepestFloor), deepestPenetration);
            if (!object->GetParent()) {
                const Vector3 raised = object->GetTranslate() + Vector3{ 0.0f, deepestPenetration + 0.01f, 0.0f };
                SetTranslateFix(issues_.back(), object, raised, "床の上面まで持ち上げる", false);
            }
        }
    }
}

std::vector<SceneValidator::AuditCollider> SceneValidator::CollectAuditColliders() const {
    std::vector<AuditCollider> result;
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return result;

    const auto& objects = sceneManager_->GetCurrentScene()->GetObjects();
    result.reserve(objects.size());
    for (const auto& objectPtr : objects) {
        const Object3d* object = objectPtr.get();
        if (!object || IsEditorOnlyObject(object)) continue;
        if (!object->GetIsVisible() && object->GetClassName() != "InvisibleBox") continue;

        const ColliderType type = object->GetColliderType();
        if (type != ColliderType::kAABB && type != ColliderType::kOBB) continue;

        const uint32_t attribute = object->GetCollisionAttribute();
        const std::string gimmickType = object->GetGimmickType();
        const std::string name = object->GetName();
        const bool structuralGimmick = ContainsAnyToken(gimmickType, {
            "Floor", "Block", "Gate", "Barrier", "Platform", "Pillar", "Wall"
        });
        const bool structuralName = ContainsAnyToken(name, {
            "Floor", "Block", "Gate", "Barrier", "Platform", "Pillar", "Wall", "Bridge", "Glass"
        });
        if ((attribute & kGround) == 0 && object->GetClassName() != "InvisibleBox" &&
            !structuralGimmick && !structuralName) {
            continue;
        }

        AuditCollider collider;
        collider.object = object;
        collider.bounds = GetAuditBounds(object);
        collider.isGround = (attribute & kGround) != 0;
        collider.isTrigger = (attribute & kTrigger) != 0 || name.find("Trigger") != std::string::npos;
        collider.isDynamic = !object->GetRecordPathName().empty() ||
            gimmickType == "RotatingFloor" || gimmickType == "RotatingPillar";
        result.push_back(collider);
    }
    return result;
}

OBB SceneValidator::GetAuditBounds(const Object3d* object) const {
    if (!object) return {};
    if (object->GetColliderType() == ColliderType::kAABB) {
        return MakeObbFromAabb(object->GetAABB());
    }
    return object->GetOBB();
}

bool SceneValidator::ShouldIgnorePair(
    const AuditCollider& first, const AuditCollider& second, float penetration) const {
    if (!first.object || !second.object || first.object == second.object) return true;
    if (first.isTrigger || second.isTrigger) return true;
    if (IsDescendantOf(first.object, second.object) || IsDescendantOf(second.object, first.object)) return true;

    const std::string firstName = first.object->GetName();
    const std::string secondName = second.object->GetName();
    const bool firstInvisible = first.object->GetClassName() == "InvisibleBox";
    const bool secondInvisible = second.object->GetClassName() == "InvisibleBox";
    if (firstInvisible && secondInvisible) return true;

    // 静的モデルと透明判定の組み合わせは通常の見た目/当たり判定ペアなので、床監査へ任せる。
    if (firstInvisible != secondInvisible && !first.isDynamic && !second.isDynamic) return true;

    // 大地形モデル同士の意図的な接続は除外し、調整対象のギミックを優先する。
    if (!firstInvisible && !secondInvisible &&
        first.object->GetGimmickType().empty() && second.object->GetGimmickType().empty() &&
        first.object->IsStatic() && second.object->IsStatic()) {
        return true;
    }

    // 見た目モデルと、そのモデルを支える同名系の透明コライダーは意図した組み合わせとして除外する。
    if (firstInvisible != secondInvisible) {
        const std::string& visibleName = firstInvisible ? secondName : firstName;
        const std::string& invisibleName = firstInvisible ? firstName : secondName;
        if ((visibleName.find("Bridge") != std::string::npos && invisibleName.find("Bridge") != std::string::npos) ||
            (visibleName.find("Floor") != std::string::npos && invisibleName.find("Floor") != std::string::npos)) {
            return true;
        }
    }

    // 同じ高さの地形同士が継ぎ目でわずかに重なる配置は問題にしない。
    if (first.isGround && second.isGround && penetration < 0.35f &&
        std::abs(GetObbTop(first.bounds) - GetObbTop(second.bounds)) < 0.10f) {
        return true;
    }
    return false;
}

void SceneValidator::CheckObjectOverlaps() {
    const std::vector<AuditCollider> colliders = CollectAuditColliders();
    for (std::size_t firstIndex = 0; firstIndex < colliders.size(); ++firstIndex) {
        for (std::size_t secondIndex = firstIndex + 1; secondIndex < colliders.size(); ++secondIndex) {
            const AuditCollider& first = colliders[firstIndex];
            const AuditCollider& second = colliders[secondIndex];

            // 動くObjectは下の時間監査で扱い、現在フレームとの重複報告を避ける。
            if (first.isDynamic || second.isDynamic) continue;

            const CollisionInfo collision = CheckOBBCollision(first.bounds, second.bounds);
            if (!collision.isColliding || collision.penetration <= objectOverlapThreshold_) continue;
            if (ShouldIgnorePair(first, second, collision.penetration)) continue;

            std::ostringstream message;
            message << std::fixed << std::setprecision(2)
                << "回転を考慮したOBBが約 " << collision.penetration
                << "m 重なっています。位置・回転・Scaleを確認してください。";
            AddSpatialIssue(
                collision.penetration > 0.75f ? Severity::Error : Severity::Warning,
                first.object, second.object, "OBB重なり", message.str(),
                first.bounds, second.bounds, collision.penetration);
            if (!first.object->GetParent() && IsFiniteVector(collision.normal)) {
                const Vector3 separated = first.object->GetTranslate() +
                    collision.normal * (collision.penetration + 0.02f);
                SetTranslateFix(issues_.back(), first.object, separated, "最小分離方向へ移動", false);
            }
        }
    }
}

OBB SceneValidator::BuildSampledBounds(
    const Object3d* object, const Vector3& position, const Vector3& rotation, const Vector3& scale) const {
    if (!object) return {};

    const auto& config = object->GetColliderConfig();
    Math math;

    if (config.type == ColliderType::kAABB) {
        Matrix4x4 ownerMatrix = math.Multiply(
            math.MakeScaleMatrix(scale), math.MakeTranslateMatrix(position));
        if (object->GetParent()) {
            ownerMatrix = math.Multiply(ownerMatrix, object->GetParent()->GetWorldMatrix());
        }
        const Matrix4x4 centerMatrix = math.Multiply(math.MakeTranslateMatrix(config.center), ownerMatrix);
        OBB bounds{};
        bounds.center = { centerMatrix.m[3][0], centerMatrix.m[3][1], centerMatrix.m[3][2] };
        bounds.size = {
            std::abs(config.size.x * scale.x),
            std::abs(config.size.y * scale.y),
            std::abs(config.size.z * scale.z)
        };
        bounds.orientations[0] = { 1.0f, 0.0f, 0.0f };
        bounds.orientations[1] = { 0.0f, 1.0f, 0.0f };
        bounds.orientations[2] = { 0.0f, 0.0f, 1.0f };
        return bounds;
    }

    const Matrix4x4 colliderRotation = math.Multiply(
        math.MakeRotateZMatrix(config.rotation.z),
        math.Multiply(math.MakeRotateXMatrix(config.rotation.x), math.MakeRotateYMatrix(config.rotation.y)));
    const Matrix4x4 colliderLocal = math.Multiply(colliderRotation, math.MakeTranslateMatrix(config.center));
    const Matrix4x4 objectLocal = math.Multiply(
        math.Multiply(math.MakeScaleMatrix(scale),
            math.MakeRotateQuaternionMatrix(math.EulerToQuaternion(rotation))),
        math.MakeTranslateMatrix(position));
    Matrix4x4 objectWorld = objectLocal;
    if (object->GetParent()) {
        objectWorld = math.Multiply(objectWorld, object->GetParent()->GetWorldMatrix());
    }
    const Matrix4x4 finalMatrix = math.Multiply(colliderLocal, objectWorld);

    OBB bounds{};
    bounds.center = { finalMatrix.m[3][0], finalMatrix.m[3][1], finalMatrix.m[3][2] };
    bounds.orientations[0] = math.Normalize({ finalMatrix.m[0][0], finalMatrix.m[0][1], finalMatrix.m[0][2] });
    bounds.orientations[1] = math.Normalize({ finalMatrix.m[1][0], finalMatrix.m[1][1], finalMatrix.m[1][2] });
    bounds.orientations[2] = math.Normalize({ finalMatrix.m[2][0], finalMatrix.m[2][1], finalMatrix.m[2][2] });
    bounds.size = {
        std::abs(config.size.x * scale.x),
        std::abs(config.size.y * scale.y),
        std::abs(config.size.z * scale.z)
    };
    return bounds;
}

OBB SceneValidator::BuildBoundsForConfig(
    const Object3d* object, const ColliderConfig& config,
    const Vector3& position, const Vector3& rotation, const Vector3& scale) const {
    if (!object) return {};

    Math math;
    if (config.type == ColliderType::kAABB) {
        Matrix4x4 ownerMatrix = math.Multiply(
            math.MakeScaleMatrix(scale), math.MakeTranslateMatrix(position));
        if (object->GetParent()) {
            ownerMatrix = math.Multiply(ownerMatrix, object->GetParent()->GetWorldMatrix());
        }
        const Matrix4x4 centerMatrix = math.Multiply(math.MakeTranslateMatrix(config.center), ownerMatrix);
        OBB bounds{};
        bounds.center = { centerMatrix.m[3][0], centerMatrix.m[3][1], centerMatrix.m[3][2] };
        bounds.size = {
            std::abs(config.size.x * scale.x),
            std::abs(config.size.y * scale.y),
            std::abs(config.size.z * scale.z)
        };
        bounds.orientations[0] = { 1.0f, 0.0f, 0.0f };
        bounds.orientations[1] = { 0.0f, 1.0f, 0.0f };
        bounds.orientations[2] = { 0.0f, 0.0f, 1.0f };
        return bounds;
    }

    const Matrix4x4 colliderRotation = math.Multiply(
        math.MakeRotateZMatrix(config.rotation.z),
        math.Multiply(math.MakeRotateXMatrix(config.rotation.x), math.MakeRotateYMatrix(config.rotation.y)));
    const Matrix4x4 colliderLocal = math.Multiply(colliderRotation, math.MakeTranslateMatrix(config.center));
    const Matrix4x4 objectLocal = math.Multiply(
        math.Multiply(math.MakeScaleMatrix(scale),
            math.MakeRotateQuaternionMatrix(math.EulerToQuaternion(rotation))),
        math.MakeTranslateMatrix(position));
    Matrix4x4 objectWorld = objectLocal;
    if (object->GetParent()) {
        objectWorld = math.Multiply(objectWorld, object->GetParent()->GetWorldMatrix());
    }
    const Matrix4x4 finalMatrix = math.Multiply(colliderLocal, objectWorld);

    OBB bounds{};
    bounds.center = { finalMatrix.m[3][0], finalMatrix.m[3][1], finalMatrix.m[3][2] };
    bounds.orientations[0] = math.Normalize({ finalMatrix.m[0][0], finalMatrix.m[0][1], finalMatrix.m[0][2] });
    bounds.orientations[1] = math.Normalize({ finalMatrix.m[1][0], finalMatrix.m[1][1], finalMatrix.m[1][2] });
    bounds.orientations[2] = math.Normalize({ finalMatrix.m[2][0], finalMatrix.m[2][1], finalMatrix.m[2][2] });
    bounds.size = {
        std::abs(config.size.x * scale.x),
        std::abs(config.size.y * scale.y),
        std::abs(config.size.z * scale.z)
    };
    return bounds;
}

void SceneValidator::CheckDynamicPathOverlaps() {
    const std::vector<AuditCollider> colliders = CollectAuditColliders();
    const int sampleCount = std::clamp(dynamicSampleCount_, 4, 32);

    struct SamplePose {
        Vector3 position{};
        Vector3 rotation{};
        Vector3 scale{ 1.0f, 1.0f, 1.0f };
        float timeSeconds = 0.0f;
    };

    for (const AuditCollider& moving : colliders) {
        if (!moving.isDynamic || !moving.object) continue;

        const Object3d* object = moving.object;
        std::vector<SamplePose> samples;
        std::string pathName = object->GetRecordPathName();

        if (!pathName.empty()) {
            const std::filesystem::path path =
                std::filesystem::path("Resources/json/animation") / (pathName + ".json");
            std::ifstream file(path);
            json root;
            bool loaded = false;
            if (file) {
                try {
                    file >> root;
                    loaded = true;
                }
                catch (const json::exception&) {
                    loaded = false;
                }
            }
            if (!loaded || !root.contains("frames") || !root["frames"].is_array() || root["frames"].empty()) {
                AddIssue(Severity::Warning, object, "動的監査", "Ghost Pathを読み込めません: " + path.generic_string());
                continue;
            }

            const auto& frames = root["frames"];
            samples.reserve(sampleCount);
            for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
                const float normalized = sampleCount <= 1 ? 0.0f :
                    static_cast<float>(sampleIndex) / static_cast<float>(sampleCount - 1);
                const std::size_t frameIndex = static_cast<std::size_t>(
                    std::round(normalized * static_cast<float>(frames.size() - 1)));
                const json& frame = frames[frameIndex];
                if (!frame.contains("pos") || !frame.contains("rot")) continue;

                SamplePose pose;
                pose.position = {
                    frame["pos"][0].get<float>(), frame["pos"][1].get<float>(), frame["pos"][2].get<float>()
                };
                pose.rotation = {
                    frame["rot"][0].get<float>(), frame["rot"][1].get<float>(), frame["rot"][2].get<float>()
                };
                if (frame.contains("scale") && frame["scale"].is_array() && frame["scale"].size() >= 3) {
                    pose.scale = {
                        frame["scale"][0].get<float>(), frame["scale"][1].get<float>(), frame["scale"][2].get<float>()
                    };
                }
                if (object->IsRecordRelative()) {
                    pose.position = object->GetTranslate() + pose.position;
                    pose.rotation = object->GetRotation() + pose.rotation;
                }
                pose.timeSeconds = static_cast<float>(frameIndex) / 60.0f;
                samples.push_back(pose);
            }
        }
        else {
            pathName = "ProceduralRotation";
            samples.reserve(sampleCount);
            const int axisMode = object->param_.has_value() ? object->param_->actionMode : 1;
            const float degreesPerSecond = object->param_.has_value() ?
                std::abs(object->param_->speed) : 45.0f;
            for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
                const float normalized = static_cast<float>(sampleIndex) / static_cast<float>(sampleCount);
                const float angle = normalized * 2.0f * kPi;
                SamplePose pose;
                pose.position = object->GetTranslate();
                pose.rotation = object->GetRotation();
                pose.scale = object->GetScale();
                if (axisMode == 0) pose.rotation.x += angle;
                else if (axisMode == 2) pose.rotation.z += angle;
                else pose.rotation.y += angle;
                pose.timeSeconds = degreesPerSecond > 0.001f ?
                    (normalized * 360.0f) / degreesPerSecond : normalized;
                samples.push_back(pose);
            }
        }

        struct BestHit {
            const Object3d* other = nullptr;
            OBB movingBounds{};
            OBB otherBounds{};
            float penetration = 0.0f;
            float timeSeconds = 0.0f;
        };
        std::map<std::string, BestHit> bestHits;

        for (const SamplePose& pose : samples) {
            const OBB sampledBounds = BuildSampledBounds(
                object, pose.position, pose.rotation, pose.scale);
            AuditCollider sampledMoving = moving;
            sampledMoving.bounds = sampledBounds;

            for (const AuditCollider& other : colliders) {
                if (!other.object || other.object == object || other.isDynamic) continue;
                const CollisionInfo collision = CheckOBBCollision(sampledBounds, other.bounds);
                if (!collision.isColliding || collision.penetration <= dynamicOverlapThreshold_) continue;
                if (ShouldIgnorePair(sampledMoving, other, collision.penetration)) continue;

                BestHit& best = bestHits[other.object->GetName()];
                if (collision.penetration > best.penetration) {
                    best.other = other.object;
                    best.movingBounds = sampledBounds;
                    best.otherBounds = other.bounds;
                    best.penetration = collision.penetration;
                    best.timeSeconds = pose.timeSeconds;
                }
            }
        }

        for (const auto& [otherName, hit] : bestHits) {
            if (!hit.other) continue;
            std::ostringstream message;
            message << std::fixed << std::setprecision(2)
                << "動作開始から " << hit.timeSeconds << "秒付近で「" << otherName
                << "」へ約 " << hit.penetration << "m 入ります。Path・回転軸・周辺配置を確認してください。";
            AddSpatialIssue(
                hit.penetration > 0.75f ? Severity::Error : Severity::Warning,
                object, hit.other, "動的OBB重なり", message.str(),
                hit.movingBounds, hit.otherBounds, hit.penetration,
                hit.timeSeconds, pathName);
        }
    }
}

void SceneValidator::AddIssue(Severity severity, const Object3d* object, const std::string& category, const std::string& message) {
    Issue issue;
    issue.severity = severity;
    issue.objectName = object ? object->GetName() : "(Scene)";
    issue.objectGuid = object ? object->GetPersistentGuid() : "";
    issue.category = category;
    issue.message = message;
    issues_.push_back(std::move(issue));
}

void SceneValidator::AddSpatialIssue(
    Severity severity, const Object3d* primary, const Object3d* secondary,
    const std::string& category, const std::string& message,
    const OBB& primaryBounds, const OBB& secondaryBounds, float penetration,
    float sampleTimeSeconds, const std::string& pathName) {
    Issue issue;
    issue.severity = severity;
    issue.objectName = primary ? primary->GetName() : "(Scene)";
    issue.objectGuid = primary ? primary->GetPersistentGuid() : "";
    issue.secondaryObjectName = secondary ? secondary->GetName() : "";
    issue.secondaryObjectGuid = secondary ? secondary->GetPersistentGuid() : "";
    issue.category = category;
    issue.message = message;
    issue.primaryBounds = primaryBounds;
    issue.secondaryBounds = secondaryBounds;
    issue.hasPrimaryBounds = primary != nullptr;
    issue.hasSecondaryBounds = secondary != nullptr;
    issue.penetration = penetration;
    issue.sampleTimeSeconds = sampleTimeSeconds;
    issue.pathName = pathName;
    issues_.push_back(std::move(issue));
}

Object3d* SceneValidator::FindObjectByName(const std::string& name) const {
    if (name.empty() || !sceneManager_ || !sceneManager_->GetCurrentScene()) return nullptr;
    for (const auto& objectPtr : sceneManager_->GetCurrentScene()->GetObjects()) {
        if (objectPtr && objectPtr->GetName() == name) return objectPtr.get();
    }
    return nullptr;
}
Object3d* SceneValidator::FindObjectByGuid(const std::string& guid, const std::string& fallbackName) const {
    if (!sceneManager_ || !sceneManager_->GetCurrentScene()) return nullptr;
    if (!guid.empty()) {
        for (const auto& objectPtr : sceneManager_->GetCurrentScene()->GetObjects()) {
            if (objectPtr && objectPtr->GetPersistentGuid() == guid) return objectPtr.get();
        }
    }
    return FindObjectByName(fallbackName);
}


void SceneValidator::SelectIssue(std::size_t issueIndex) {
    if (issueIndex >= issues_.size()) return;
    selectedIssueIndex_ = static_cast<int>(issueIndex);
    if (!editor_) return;

    Object3d* primary = FindObjectByGuid(
        issues_[issueIndex].objectGuid, issues_[issueIndex].objectName);
    if (!primary) return;
    editor_->SetSelectedObject(primary);
    editor_->SyncObjectSelectionToInspector();
    editor_->FocusSceneObject(primary);
}

void SceneValidator::DrawDebug(
    PrimitiveDrawer& drawer, ID3D12GraphicsCommandList* commandList,
    int& instanceCount, int maxDrawLimit) const {
    if (selectedIssueIndex_ < 0 || selectedIssueIndex_ >= static_cast<int>(issues_.size())) return;
    const Issue& issue = issues_[selectedIssueIndex_];

    if (issue.hasPrimaryBounds && instanceCount < maxDrawLimit) {
        drawer.DrawWireCube(
            commandList, MakeObbMatrix(issue.primaryBounds), { 1.0f, 0.08f, 0.12f, 1.0f }, instanceCount++);
    }
    if (issue.hasSecondaryBounds && instanceCount < maxDrawLimit) {
        drawer.DrawWireCube(
            commandList, MakeObbMatrix(issue.secondaryBounds), { 0.05f, 0.95f, 1.0f, 1.0f }, instanceCount++);
    }
    if (showFixPreview_ && issue.hasProposedBounds && instanceCount < maxDrawLimit) {
        drawer.DrawWireCube(
            commandList, MakeObbMatrix(issue.proposedBounds), { 0.15f, 1.0f, 0.25f, 1.0f }, instanceCount++);
    }
}

void SceneValidator::DrawFixPreview(Issue& issue) {
#ifdef USE_IMGUI
    if (!issue.hasFix) {
        ImGui::TextDisabled("この問題には安全に提案できる修正候補がありません。");
        return;
    }

    ImGui::Spacing();
    ImGui::TextColored(
        issue.safeFix ? ImVec4(0.30f, 1.0f, 0.45f, 1.0f) : ImVec4(1.0f, 0.78f, 0.22f, 1.0f),
        "%s %s", ICON_FA_MAGIC, issue.fixTitle.c_str());
    ImGui::Checkbox("この候補を一括適用へ含める", &issue.fixSelected);
    ImGui::TextDisabled("修正前");
    ImGui::TextWrapped("%s", issue.beforeText.c_str());

    if (!issue.assetCandidates.empty()) {
        issue.selectedAssetCandidate = (std::clamp)(
            issue.selectedAssetCandidate, 0, static_cast<int>(issue.assetCandidates.size()) - 1);
        const char* preview = issue.assetCandidates[issue.selectedAssetCandidate].c_str();
        if (ImGui::BeginCombo("修正後候補", preview)) {
            for (int index = 0; index < static_cast<int>(issue.assetCandidates.size()); ++index) {
                const bool selected = index == issue.selectedAssetCandidate;
                if (ImGui::Selectable(issue.assetCandidates[index].c_str(), selected)) {
                    issue.selectedAssetCandidate = index;
                    issue.afterFix.textValue = issue.assetCandidates[index];
                    issue.afterText = issue.assetCandidates[index];
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    } else {
        ImGui::TextDisabled("修正後");
        ImGui::TextWrapped("%s", issue.afterText.c_str());
    }

    if (!issue.safeFix) {
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.20f, 1.0f),
            "要確認: ステージ設計や接続意図に影響するため、自動選択していません。");
    }
    if (issue.hasProposedBounds && showFixPreview_) {
        ImGui::TextDisabled("Scene表示: 赤=修正前 / 緑=修正後 / シアン=重なり相手");
    }
#else
    (void)issue;
#endif
}

void SceneValidator::ApplySelectedFixes() {
    std::vector<std::size_t> selected;
    for (std::size_t index = 0; index < issues_.size(); ++index) {
        if (issues_[index].hasFix && issues_[index].fixSelected) selected.push_back(index);
    }
    if (selected.empty()) return;

    EditorTransactionManager* transactions = EditorTransactionManager::GetInstance();
    transactions->BeginGroup("Scene Audit Fixes");
    int appliedCount = 0;
    for (std::size_t index : selected) {
        if (ApplyIssueFix(index)) ++appliedCount;
    }
    transactions->EndGroup();

    Refresh();
    auditStatusText_ = std::to_string(appliedCount) + "件の監査修正を適用しました。Ctrl+Zで一括して戻せます。";
}

bool SceneValidator::ApplyIssueFix(std::size_t issueIndex) {
    if (issueIndex >= issues_.size()) return false;
    Issue& issue = issues_[issueIndex];
    if (!issue.hasFix || issue.fixKind == FixKind::None) return false;

    if (!issue.assetCandidates.empty()) {
        issue.selectedAssetCandidate = (std::clamp)(
            issue.selectedAssetCandidate, 0, static_cast<int>(issue.assetCandidates.size()) - 1);
        issue.afterFix.textValue = issue.assetCandidates[issue.selectedAssetCandidate];
    }

    const FixKind kind = issue.fixKind;
    const FixState before = issue.beforeFix;
    const FixState after = issue.afterFix;
    const std::string objectGuid = issue.objectGuid;
    const std::string objectName = issue.objectName;
    const std::string label = "Scene Audit: " + issue.fixTitle;

    if (!ApplyFixState(objectGuid, objectName, kind, after)) return false;

    EditorTransaction transaction;
    transaction.label = label;
    transaction.undo = [this, objectGuid, objectName, kind, before]() {
        ApplyFixState(objectGuid, objectName, kind, before);
    };
    transaction.redo = [this, objectGuid, objectName, kind, after]() {
        ApplyFixState(objectGuid, objectName, kind, after);
    };
    EditorTransactionManager::GetInstance()->Register(std::move(transaction));
    return true;
}

bool SceneValidator::ApplyFixState(
    const std::string& objectGuid, const std::string& objectName,
    FixKind kind, const FixState& state) {
    Object3d* object = FindObjectByGuid(objectGuid, objectName);
    if (!object) return false;

    switch (kind) {
    case FixKind::Translate:
        object->SetTranslate(state.translate);
        break;
    case FixKind::Rotation:
        object->SetRotation(state.rotation);
        break;
    case FixKind::Collider:
        object->SetColliderConfig(state.collider);
        break;
    case FixKind::EventId:
        object->SetEventID(state.eventId);
        break;
    case FixKind::Model:
        object->SetModel(state.textValue);
        break;
    case FixKind::Texture:
        object->SetTexture(state.textValue);
        break;
    case FixKind::NormalMap:
        object->SetNormalMap(state.textValue);
        break;
    case FixKind::OrmMap:
        object->SetOrmMap(state.textValue);
        break;
    case FixKind::CollisionFilter:
        object->SetCollisionAttribute(state.collisionAttribute);
        object->SetCollisionMask(state.collisionMask);
        break;
    case FixKind::None:
    default:
        return false;
    }

    object->UpdateWorldMatrix();
    if (editor_) editor_->MarkDirtyForObject(object);
    return true;
}

std::vector<std::string> SceneValidator::FindAssetCandidates(
    const std::string& missingValue, bool modelAsset, std::size_t maxCount) const {
    const auto& assets = AssetDatabase::GetInstance()->GetAssets();
    const std::string wanted = FileStemLower(missingValue);
    struct ScoredCandidate {
        std::string value;
        int score = 0;
    };
    std::vector<ScoredCandidate> scored;
    std::unordered_set<std::string> seen;

    for (const EditorAssetRecord& asset : assets) {
        if (modelAsset && asset.type != EditorAssetType::Model) continue;
        if (!modelAsset && asset.type != EditorAssetType::Texture) continue;

        const std::string value = modelAsset ? MakeModelReference(asset.sourcePath) : NormalizePath(asset.sourcePath);
        if (value.empty() || !seen.insert(ToLowerCopy(value)).second) continue;

        const std::string stem = FileStemLower(asset.sourcePath);
        int score = CommonPrefixScore(wanted, stem) * 4;
        if (!wanted.empty() && (stem.find(wanted) != std::string::npos ||
            wanted.find(stem) != std::string::npos)) {
            score += 24;
        }
        const std::string lowerMissing = ToLowerCopy(NormalizePath(missingValue));
        const std::string lowerSource = ToLowerCopy(NormalizePath(asset.sourcePath));
        if (!lowerMissing.empty() && lowerSource.find(lowerMissing) != std::string::npos) score += 16;
        if (score > 5) scored.push_back({ value, score });
    }

    std::sort(scored.begin(), scored.end(), [](const ScoredCandidate& first, const ScoredCandidate& second) {
        if (first.score != second.score) return first.score > second.score;
        return first.value < second.value;
    });

    std::vector<std::string> result;
    for (std::size_t index = 0; index < (std::min)(maxCount, scored.size()); ++index) {
        result.push_back(scored[index].value);
    }
    return result;
}

std::string SceneValidator::MakeModelReference(const std::string& sourcePath) const {
    const std::string normalized = NormalizePath(sourcePath);
    const std::string lower = ToLowerCopy(normalized);
    constexpr const char* prefix = "resources/3dmodel/";
    if (lower.rfind(prefix, 0) != 0) return {};

    std::filesystem::path relative(normalized.substr(std::char_traits<char>::length(prefix)));
    const std::filesystem::path parent = relative.parent_path();
    if (!parent.empty()) return NormalizePath(parent.generic_string());
    return relative.stem().generic_string();
}

void SceneValidator::SetTranslateFix(
    Issue& issue, const Object3d* object, const Vector3& after,
    const std::string& title, bool safeFix) {
    if (!object || !IsFiniteVector(after)) return;
    issue.hasFix = true;
    issue.fixKind = FixKind::Translate;
    issue.fixTitle = title;
    issue.safeFix = safeFix;
    issue.fixSelected = safeFix;
    issue.beforeFix.translate = object->GetTranslate();
    issue.afterFix.translate = after;
    issue.beforeText = "Position " + FormatVector3(issue.beforeFix.translate);
    issue.afterText = "Position " + FormatVector3(after);
    if (!issue.hasPrimaryBounds) {
        issue.primaryBounds = object->GetModel() ?
            MakeObbFromAabb(object->GetModelWorldAABB()) : GetAuditBounds(object);
        issue.hasPrimaryBounds = true;
    }
    issue.proposedBounds = issue.primaryBounds;
    issue.proposedBounds.center += after - issue.beforeFix.translate;
    issue.hasProposedBounds = true;
}

void SceneValidator::SetRotationFix(
    Issue& issue, const Object3d* object, const Vector3& after,
    const std::string& title, bool safeFix) {
    if (!object || !IsFiniteVector(after)) return;
    issue.hasFix = true;
    issue.fixKind = FixKind::Rotation;
    issue.fixTitle = title;
    issue.safeFix = safeFix;
    issue.fixSelected = safeFix;
    issue.beforeFix.rotation = object->GetRotation();
    issue.afterFix.rotation = after;
    issue.beforeText = "Rotation(rad) " + FormatVector3(issue.beforeFix.rotation);
    issue.afterText = "Rotation(rad) " + FormatVector3(after);
    if (object->GetColliderType() == ColliderType::kAABB ||
        object->GetColliderType() == ColliderType::kOBB) {
        issue.primaryBounds = GetAuditBounds(object);
        issue.hasPrimaryBounds = true;
        issue.proposedBounds = BuildSampledBounds(
            object, object->GetTranslate(), after, object->GetScale());
        issue.hasProposedBounds = true;
    }
}

void SceneValidator::SetColliderFix(
    Issue& issue, const Object3d* object, const ColliderConfig& after,
    const std::string& title, bool safeFix) {
    if (!object || !IsFiniteVector(after.center) || !IsFiniteVector(after.size)) return;
    issue.hasFix = true;
    issue.fixKind = FixKind::Collider;
    issue.fixTitle = title;
    issue.safeFix = safeFix;
    issue.fixSelected = safeFix;
    issue.beforeFix.collider = object->GetColliderConfig();
    issue.afterFix.collider = after;
    issue.beforeText = FormatCollider(issue.beforeFix.collider);
    issue.afterText = FormatCollider(after);
    issue.primaryBounds = GetAuditBounds(object);
    issue.hasPrimaryBounds = true;
    issue.proposedBounds = BuildBoundsForConfig(
        object, after, object->GetTranslate(), object->GetRotation(), object->GetScale());
    issue.hasProposedBounds = true;
}

void SceneValidator::SetEventIdFix(
    Issue& issue, const Object3d* object, int after,
    const std::string& title, bool safeFix) {
    if (!object || after <= 0) return;
    issue.hasFix = true;
    issue.fixKind = FixKind::EventId;
    issue.fixTitle = title;
    issue.safeFix = safeFix;
    issue.fixSelected = safeFix;
    issue.beforeFix.eventId = object->GetEventID();
    issue.afterFix.eventId = after;
    issue.beforeText = "Event ID " + std::to_string(issue.beforeFix.eventId);
    issue.afterText = "Event ID " + std::to_string(after);
}

void SceneValidator::SetAssetFix(
    Issue& issue, const Object3d* object, FixKind kind,
    const std::string& before, const std::vector<std::string>& candidates,
    const std::string& title) {
    if (!object || candidates.empty()) return;
    issue.hasFix = true;
    issue.fixKind = kind;
    issue.fixTitle = title;
    issue.safeFix = false;
    issue.fixSelected = false;
    issue.assetCandidates = candidates;
    issue.selectedAssetCandidate = 0;
    issue.beforeFix.textValue = before;
    issue.afterFix.textValue = candidates.front();
    issue.beforeText = before.empty() ? "(empty)" : before;
    issue.afterText = candidates.front();
}

void SceneValidator::SetCollisionFilterFix(
    Issue& issue, const Object3d* object, uint32_t afterAttribute, uint32_t afterMask,
    const std::string& title, bool safeFix) {
    if (!object) return;
    issue.hasFix = true;
    issue.fixKind = FixKind::CollisionFilter;
    issue.fixTitle = title;
    issue.safeFix = safeFix;
    issue.fixSelected = safeFix;
    issue.beforeFix.collisionAttribute = object->GetCollisionAttribute();
    issue.beforeFix.collisionMask = object->GetCollisionMask();
    issue.afterFix.collisionAttribute = afterAttribute;
    issue.afterFix.collisionMask = afterMask;
    issue.beforeText = "attribute " + std::to_string(issue.beforeFix.collisionAttribute) +
        " / mask " + std::to_string(issue.beforeFix.collisionMask);
    issue.afterText = "attribute " + std::to_string(afterAttribute) +
        " / mask " + std::to_string(afterMask);
}

bool SceneValidator::DoesModelExist(const std::string& modelName) const {
    if (modelName.empty()) return false;

    std::vector<std::string> loadedNames = ModelManager::GetInstance()->GetLoadedModelNames();
    if (std::find(loadedNames.begin(), loadedNames.end(), modelName) != loadedNames.end()) return true;

    std::filesystem::path keyPath(modelName);
    std::filesystem::path directory = std::filesystem::path("Resources/3DModel") / keyPath;
    std::string stem = keyPath.filename().string();

    static const char* extensions[] = { ".obj", ".gltf", ".glb" };
    for (const char* extension : extensions) {
        if (std::filesystem::exists(directory / (stem + extension))) return true;
    }

    return false;
}

bool SceneValidator::DoesFileExist(const std::string& path) const {
    if (path.empty()) return true;
    std::string normalized = NormalizePath(path);
    if (std::filesystem::exists(normalized)) return true;

    uint32_t handle = TextureManager::GetInstance()->GetSrvHandle(normalized);
    return handle != 0;
}

const char* SceneValidator::GetSeverityLabel(Severity severity) const {
    switch (severity) {
    case Severity::Error:
        return "Error";
    case Severity::Warning:
        return "Warning";
    case Severity::Info:
    default:
        return "Info";
    }
}

unsigned int SceneValidator::GetSeverityColor(Severity severity) const {
    switch (severity) {
    case Severity::Error:
        return IM_COL32(255, 80, 80, 255);
    case Severity::Warning:
        return IM_COL32(255, 210, 70, 255);
    case Severity::Info:
    default:
        return IM_COL32(110, 180, 255, 255);
    }
}
