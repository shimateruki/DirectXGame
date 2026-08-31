#define NOMINMAX
#include "LevelDesignLabWindow.h"

#include "BaseScene.h"
#include "CollisionConfig.h"
#include "DebugEditor.h"
#include "ModelManager.h"
#include "Object3d.h"
#include "SceneManager.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <queue>
#include <set>
#include <sstream>
#include <unordered_set>

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kOverlapTolerance = 0.35f;
constexpr float kEpsilon = 0.0001f;

float Distance(const Vector3& first, const Vector3& second) {
    const float x = first.x - second.x;
    const float y = first.y - second.y;
    const float z = first.z - second.z;
    return std::sqrt(x * x + y * y + z * z);
}

std::string FormatFloat(float value) {
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(2);
    stream << value;
    return stream.str();
}
}

void LevelDesignLabWindow::Initialize(SceneManager* sceneManager, DebugEditor* editor) {
    sceneManager_ = sceneManager;
    editor_ = editor;
    LoadCatalog();
    LoadPlan();
}

bool LevelDesignLabWindow::ReadVector3(const nlohmann::json& value, Vector3& output) const {
    if (!value.is_array() || value.size() < 3 ||
        !value[0].is_number() || !value[1].is_number() || !value[2].is_number()) {
        return false;
    }
    output = { value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
    return true;
}

bool LevelDesignLabWindow::ReadVector4(const nlohmann::json& value, Vector4& output) const {
    if (!value.is_array() || value.size() < 4 ||
        !value[0].is_number() || !value[1].is_number() ||
        !value[2].is_number() || !value[3].is_number()) {
        return false;
    }
    output = {
        value[0].get<float>(), value[1].get<float>(),
        value[2].get<float>(), value[3].get<float>()
    };
    return true;
}

bool LevelDesignLabWindow::LoadCatalog() {
    chunks_.clear();
    catalogLoaded_ = false;

    std::ifstream input(catalogPath_, std::ios::binary);
    if (!input.is_open()) {
        statusText_ = std::string("チャンクカタログを開けません: ") + catalogPath_;
        return false;
    }

    try {
        nlohmann::json root;
        input >> root;
        if (!root.contains("chunks") || !root["chunks"].is_array()) {
            statusText_ = "チャンクカタログに chunks 配列がありません。";
            return false;
        }

        for (const auto& source : root["chunks"]) {
            if (!source.is_object()) continue;
            ChunkDefinition chunk;
            chunk.id = source.value("id", "");
            chunk.displayName = source.value("displayName", chunk.id);
            chunk.modelName = source.value("modelName", "");
            chunk.role = source.value("role", "structure");
            chunk.walkable = source.value("walkable", true);
            chunk.blocksSight = source.value("blocksSight", true);
            if (source.contains("halfExtents")) ReadVector3(source["halfExtents"], chunk.halfExtents);
            if (source.contains("colliderSize")) ReadVector3(source["colliderSize"], chunk.colliderSize);
            if (source.contains("color")) ReadVector4(source["color"], chunk.color);
            if (source.contains("entry")) ReadVector3(source["entry"], chunk.entry.position);
            if (source.contains("exit")) ReadVector3(source["exit"], chunk.exit.position);
            if (source.contains("sightBounds") && source["sightBounds"].is_array()) {
                for (const auto& boundSource : source["sightBounds"]) {
                    if (!boundSource.is_object()) continue;
                    SightBound bound;
                    if (boundSource.contains("center")) ReadVector3(boundSource["center"], bound.center);
                    if (boundSource.contains("halfExtents")) ReadVector3(boundSource["halfExtents"], bound.halfExtents);
                    chunk.sightBounds.push_back(bound);
                }
            }
            if (chunk.id.empty() || chunk.modelName.empty()) continue;
            chunks_[chunk.id] = std::move(chunk);
        }
    }
    catch (const std::exception& exception) {
        statusText_ = std::string("チャンクカタログの解析に失敗しました: ") + exception.what();
        return false;
    }

    catalogLoaded_ = !chunks_.empty();
    statusText_ = catalogLoaded_
        ? "チャンクカタログを読み込みました。"
        : "有効なLevel Chunkがありません。";
    return catalogLoaded_;
}

void LevelDesignLabWindow::ResetLoadedPlan() {
    operations_.clear();
    links_.clear();
    issues_.clear();
    planJson_.clear();
    planName_.clear();
    startOperation_.clear();
    goalOperation_.clear();
    planLoaded_ = false;
    validationComplete_ = false;
}

bool LevelDesignLabWindow::LoadPlan() {
    ResetLoadedPlan();
    if (!catalogLoaded_ && !LoadCatalog()) return false;

    std::ifstream input(planPath_, std::ios::binary);
    if (!input.is_open()) {
        statusText_ = std::string("配置Planを開けません: ") + planPath_;
        return false;
    }

    try {
        input >> planJson_;
        planName_ = planJson_.value("name", "Level Plan");
        if (!planJson_.contains("operations") || !planJson_["operations"].is_array()) {
            statusText_ = "配置Planに operations 配列がありません。";
            return false;
        }

        for (const auto& source : planJson_["operations"]) {
            if (!source.is_object()) continue;
            Operation operation;
            operation.name = source.value("name", "");
            operation.chunkId = source.value("chunk", "");
            operation.rotationY = source.value("rotationY", 0.0f);
            operation.allowOverlap = source.value("allowOverlap", false);
            if (source.contains("position")) ReadVector3(source["position"], operation.position);
            if (source.contains("scale")) ReadVector3(source["scale"], operation.scale);
            if (!operation.name.empty() && !operation.chunkId.empty()) {
                operations_.push_back(std::move(operation));
            }
        }

        if (planJson_.contains("mainPath") && planJson_["mainPath"].is_object()) {
            const auto& mainPath = planJson_["mainPath"];
            startOperation_ = mainPath.value("start", "");
            goalOperation_ = mainPath.value("goal", "");
            if (mainPath.contains("links") && mainPath["links"].is_array()) {
                for (const auto& source : mainPath["links"]) {
                    if (!source.is_object()) continue;
                    Link link;
                    link.from = source.value("from", "");
                    link.to = source.value("to", "");
                    link.ability = source.value("ability", "");
                    link.maxGap = source.value("maxGap", 2.0f);
                    if (!link.from.empty() && !link.to.empty()) links_.push_back(std::move(link));
                }
            }
        }
    }
    catch (const std::exception& exception) {
        statusText_ = std::string("配置Planの解析に失敗しました: ") + exception.what();
        return false;
    }

    planLoaded_ = !operations_.empty();
    if (!planLoaded_) {
        statusText_ = "配置Planに有効な操作がありません。";
        return false;
    }

    ValidatePlan();
    statusText_ = "配置Planを読み込み、検証しました。";
    return true;
}

const LevelDesignLabWindow::Operation* LevelDesignLabWindow::FindOperation(const std::string& name) const {
    const auto found = std::find_if(operations_.begin(), operations_.end(), [&name](const Operation& operation) {
        return operation.name == name;
    });
    return found != operations_.end() ? &*found : nullptr;
}

const LevelDesignLabWindow::ChunkDefinition* LevelDesignLabWindow::FindChunk(const Operation& operation) const {
    const auto found = chunks_.find(operation.chunkId);
    return found != chunks_.end() ? &found->second : nullptr;
}

Vector3 LevelDesignLabWindow::TransformLocalPoint(
    const Operation& operation, const Vector3& localPoint) const {
    const float radians = operation.rotationY * kPi / 180.0f;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    const float x = localPoint.x * operation.scale.x;
    const float y = localPoint.y * operation.scale.y;
    const float z = localPoint.z * operation.scale.z;
    return {
        operation.position.x + x * cosine + z * sine,
        operation.position.y + y,
        operation.position.z - x * sine + z * cosine,
    };
}

LevelDesignLabWindow::Bounds LevelDesignLabWindow::BuildBounds(
    const Operation& operation, const ChunkDefinition& chunk) const {
    return BuildBounds(operation, {}, chunk.halfExtents);
}

LevelDesignLabWindow::Bounds LevelDesignLabWindow::BuildBounds(
    const Operation& operation, const Vector3& localCenter, const Vector3& localHalfExtents) const {
    const float radians = operation.rotationY * kPi / 180.0f;
    const float cosine = std::abs(std::cos(radians));
    const float sine = std::abs(std::sin(radians));
    const float localX = std::abs(localHalfExtents.x * operation.scale.x);
    const float localY = std::abs(localHalfExtents.y * operation.scale.y);
    const float localZ = std::abs(localHalfExtents.z * operation.scale.z);
    const Vector3 extent{
        localX * cosine + localZ * sine,
        localY,
        localX * sine + localZ * cosine,
    };
    const Vector3 center = TransformLocalPoint(operation, localCenter);
    return {
        { center.x - extent.x, center.y - extent.y, center.z - extent.z },
        { center.x + extent.x, center.y + extent.y, center.z + extent.z },
    };
}

bool LevelDesignLabWindow::SegmentIntersectsBounds(
    const Vector3& start, const Vector3& end, const Bounds& bounds) const {
    float minimum = 0.0f;
    float maximum = 1.0f;
    const std::array<float, 3> startValues{ start.x, start.y, start.z };
    const std::array<float, 3> directionValues{
        end.x - start.x, end.y - start.y, end.z - start.z
    };
    const std::array<float, 3> minimumValues{ bounds.min.x, bounds.min.y, bounds.min.z };
    const std::array<float, 3> maximumValues{ bounds.max.x, bounds.max.y, bounds.max.z };

    for (size_t axis = 0; axis < 3; ++axis) {
        if (std::abs(directionValues[axis]) < kEpsilon) {
            if (startValues[axis] < minimumValues[axis] || startValues[axis] > maximumValues[axis]) {
                return false;
            }
            continue;
        }
        const float inverse = 1.0f / directionValues[axis];
        float nearValue = (minimumValues[axis] - startValues[axis]) * inverse;
        float farValue = (maximumValues[axis] - startValues[axis]) * inverse;
        if (nearValue > farValue) std::swap(nearValue, farValue);
        minimum = std::max(minimum, nearValue);
        maximum = std::min(maximum, farValue);
        if (minimum > maximum) return false;
    }
    return maximum > kEpsilon && minimum < 1.0f - kEpsilon;
}

void LevelDesignLabWindow::AddIssue(
    Severity severity, const std::string& category, const std::string& message) {
    issues_.push_back({ severity, category, message });
}

bool LevelDesignLabWindow::HasErrors() const {
    return std::any_of(issues_.begin(), issues_.end(), [](const ValidationIssue& issue) {
        return issue.severity == Severity::Error;
    });
}

void LevelDesignLabWindow::ValidatePlan() {
    issues_.clear();
    validationComplete_ = false;
    if (!planLoaded_) return;

    std::unordered_set<std::string> operationNames;
    for (const Operation& operation : operations_) {
        if (!operationNames.insert(operation.name).second) {
            AddIssue(Severity::Error, "構造", "同じ操作名が重複しています: " + operation.name);
        }
        if (!FindChunk(operation)) {
            AddIssue(Severity::Error, "チャンク", operation.name + " が未登録チャンクを参照しています: " + operation.chunkId);
        }
    }

    for (size_t firstIndex = 0; firstIndex < operations_.size(); ++firstIndex) {
        const Operation& first = operations_[firstIndex];
        const ChunkDefinition* firstChunk = FindChunk(first);
        if (!firstChunk || first.allowOverlap) continue;
        const Bounds firstBounds = BuildBounds(first, *firstChunk);
        for (size_t secondIndex = firstIndex + 1; secondIndex < operations_.size(); ++secondIndex) {
            const Operation& second = operations_[secondIndex];
            const ChunkDefinition* secondChunk = FindChunk(second);
            if (!secondChunk || second.allowOverlap) continue;
            const Bounds secondBounds = BuildBounds(second, *secondChunk);
            const float penetrationX = std::min(firstBounds.max.x, secondBounds.max.x) -
                std::max(firstBounds.min.x, secondBounds.min.x);
            const float penetrationY = std::min(firstBounds.max.y, secondBounds.max.y) -
                std::max(firstBounds.min.y, secondBounds.min.y);
            const float penetrationZ = std::min(firstBounds.max.z, secondBounds.max.z) -
                std::max(firstBounds.min.z, secondBounds.min.z);
            if (penetrationX > kOverlapTolerance && penetrationY > kOverlapTolerance &&
                penetrationZ > kOverlapTolerance) {
                AddIssue(Severity::Warning, "重なり",
                    first.name + " と " + second.name + " が深く重なっています。");
            }
        }
    }

    std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> adjacency;
    for (const Link& link : links_) {
        const Operation* from = FindOperation(link.from);
        const Operation* to = FindOperation(link.to);
        if (!from || !to) {
            AddIssue(Severity::Error, "接続", link.from + " -> " + link.to + " の対象が存在しません。");
            continue;
        }
        const ChunkDefinition* fromChunk = FindChunk(*from);
        const ChunkDefinition* toChunk = FindChunk(*to);
        if (!fromChunk || !toChunk) continue;
        const Vector3 exitPoint = TransformLocalPoint(*from, fromChunk->exit.position);
        const Vector3 entryPoint = TransformLocalPoint(*to, toChunk->entry.position);
        const float gap = Distance(exitPoint, entryPoint);
        if (gap > link.maxGap) {
            AddIssue(Severity::Error, "接続",
                link.from + " -> " + link.to + " の接続が " + FormatFloat(gap) +
                " 離れています（許容 " + FormatFloat(link.maxGap) + "）。");
        }
        adjacency[link.from].push_back({ link.to, link.ability });
    }

    auto canReach = [&adjacency](const std::string& start, const std::string& target, const std::string& ability) {
        if (start.empty() || target.empty()) return false;
        std::queue<std::string> pending;
        std::unordered_set<std::string> visited;
        pending.push(start);
        visited.insert(start);
        while (!pending.empty()) {
            const std::string current = pending.front();
            pending.pop();
            if (current == target) return true;
            const auto found = adjacency.find(current);
            if (found == adjacency.end()) continue;
            for (const auto& edge : found->second) {
                if (!edge.second.empty() && edge.second != ability) continue;
                if (visited.insert(edge.first).second) pending.push(edge.first);
            }
        }
        return false;
    };

    if (!FindOperation(startOperation_) || !FindOperation(goalOperation_)) {
        AddIssue(Severity::Error, "到達性", "mainPath の start または goal が存在しません。");
    }
    else if (!canReach(startOperation_, goalOperation_, "")) {
        AddIssue(Severity::Error, "到達性", "基礎行動だけでスタートからゴールへ到達できません。");
    }
    else {
        AddIssue(Severity::Info, "到達性", "基礎行動だけでメインルートを完走できます。");
    }

    if (planJson_.contains("targets") && planJson_["targets"].is_array()) {
        for (const auto& target : planJson_["targets"]) {
            if (!target.is_object()) continue;
            const std::string operationName = target.value("operation", "");
            const std::string ability = target.value("ability", "");
            if (!canReach(startOperation_, operationName, ability)) {
                AddIssue(Severity::Error, "到達性",
                    operationName + " に能力「" + ability + "」を含むルートで到達できません。");
            }
            else {
                AddIssue(Severity::Info, "到達性",
                    operationName + " は能力「" + ability + "」で到達できます。");
            }
        }
    }

    if (planJson_.contains("visibilityChecks") && planJson_["visibilityChecks"].is_array()) {
        for (const auto& check : planJson_["visibilityChecks"]) {
            if (!check.is_object()) continue;
            const std::string fromName = check.value("from", "");
            const std::string targetName = check.value("target", "");
            const Operation* from = FindOperation(fromName);
            const Operation* target = FindOperation(targetName);
            if (!from || !target) {
                AddIssue(Severity::Error, "可視性", fromName + " -> " + targetName + " の対象が存在しません。");
                continue;
            }
            Vector3 fromOffset{};
            Vector3 targetOffset{};
            if (check.contains("fromOffset")) ReadVector3(check["fromOffset"], fromOffset);
            if (check.contains("targetOffset")) ReadVector3(check["targetOffset"], targetOffset);
            const Vector3 start{
                from->position.x + fromOffset.x,
                from->position.y + fromOffset.y,
                from->position.z + fromOffset.z,
            };
            const Vector3 end{
                target->position.x + targetOffset.x,
                target->position.y + targetOffset.y,
                target->position.z + targetOffset.z,
            };
            bool blocked = false;
            std::string blockerName;
            for (const Operation& operation : operations_) {
                if (operation.name == fromName || operation.name == targetName) continue;
                const ChunkDefinition* chunk = FindChunk(operation);
                if (!chunk || !chunk->blocksSight) continue;
                if (chunk->sightBounds.empty()) {
                    blocked = SegmentIntersectsBounds(start, end, BuildBounds(operation, *chunk));
                }
                else {
                    for (const SightBound& sightBound : chunk->sightBounds) {
                        if (SegmentIntersectsBounds(
                            start, end, BuildBounds(operation, sightBound.center, sightBound.halfExtents))) {
                            blocked = true;
                            break;
                        }
                    }
                }
                if (blocked) {
                    blockerName = operation.name;
                    break;
                }
            }
            const bool expectedVisible = check.value("expected", std::string("visible")) == "visible";
            const bool visible = !blocked;
            if (visible != expectedVisible) {
                AddIssue(Severity::Warning, "可視性",
                    fromName + " から " + targetName + " の見え方が期待と異なります" +
                    (blocked ? "（遮蔽物: " + blockerName + "）。" : "。"));
            }
            else {
                AddIssue(Severity::Info, "可視性",
                    fromName + " から " + targetName + " は期待どおり" +
                    (visible ? "見えます。" : "隠れます。"));
            }
        }
    }

    validationComplete_ = true;
}

void LevelDesignLabWindow::StartPlanPreview() {
    if (!editor_ || !sceneManager_ || !sceneManager_->GetCurrentScene() ||
        !validationComplete_ || HasErrors() || operations_.empty()) {
        statusText_ = "エラーのない検証済みPlanだけをプレビューできます。";
        return;
    }

    Object3dCommon* common = sceneManager_->GetCurrentScene()->GetObject3dCommon();
    if (!common) {
        statusText_ = "Object3dCommonを取得できません。";
        return;
    }

    const Vector3 rootPosition = operations_.front().position;
    std::vector<std::unique_ptr<Object3d>> objects;
    size_t previewObjectCount = operations_.size();
    for (const Operation& operation : operations_) {
        const ChunkDefinition* chunk = FindChunk(operation);
        if (chunk && chunk->walkable) previewObjectCount += chunk->sightBounds.size();
    }
    objects.reserve(previewObjectCount);
    Object3d* rootObject = nullptr;

    for (const Operation& operation : operations_) {
        const ChunkDefinition* chunk = FindChunk(operation);
        if (!chunk) continue;
        if (!ModelManager::GetInstance()->LoadModel(chunk->modelName)) {
            statusText_ = "モデルを読み込めません: " + chunk->modelName;
            return;
        }

        auto object = std::make_unique<Object3d>();
        object->Initialize(common);
        object->SetModel(chunk->modelName);
        object->SetName(planName_ + "_" + operation.name);
        object->SetClassName("Model");
        object->SetSaveCategory("Object");
        object->SetColor(chunk->color);
        object->SetScale(operation.scale);
        object->SetRotation({ 0.0f, operation.rotationY * kPi / 180.0f, 0.0f });
        object->SetTranslate({
            operation.position.x - rootPosition.x,
            operation.position.y - rootPosition.y,
            operation.position.z - rootPosition.z,
        });

        ColliderConfig collider;
        collider.type = ColliderType::kOBB;
        collider.center = { 0.0f, 0.0f, 0.0f };
        collider.size = chunk->colliderSize;
        object->SetColliderConfig(collider);
        object->SetCollisionAttribute(chunk->walkable ? kGround : 0u);
        object->SetCollisionMask(chunk->walkable ? 0xFFFFFFFFu : 0u);

        if (!rootObject) {
            rootObject = object.get();
        }
        else {
            object->SetParent(rootObject, false);
        }
        object->UpdateLocalMatrix();
        object->UpdateWorldMatrix();
        objects.push_back(std::move(object));

        if (!chunk->walkable) continue;
        for (size_t boundIndex = 0; boundIndex < chunk->sightBounds.size(); ++boundIndex) {
            const SightBound& sightBound = chunk->sightBounds[boundIndex];
            const Vector3 center = TransformLocalPoint(operation, sightBound.center);
            auto wallCollider = std::make_unique<Object3d>();
            wallCollider->Initialize(common);
            wallCollider->SetModel(nullptr);
            wallCollider->SetIsVisible(true);
            wallCollider->SetName(
                planName_ + "_" + operation.name + "_WallCollider_" + std::to_string(boundIndex + 1));
            wallCollider->SetClassName("InvisibleBox");
            wallCollider->SetSaveCategory("Object");
            wallCollider->SetRotation({ 0.0f, operation.rotationY * kPi / 180.0f, 0.0f });
            wallCollider->SetTranslate({
                center.x - rootPosition.x,
                center.y - rootPosition.y,
                center.z - rootPosition.z,
            });

            ColliderConfig wallConfig;
            wallConfig.type = ColliderType::kOBB;
            wallConfig.center = { 0.0f, 0.0f, 0.0f };
            wallConfig.size = {
                std::abs(sightBound.halfExtents.x * operation.scale.x),
                std::abs(sightBound.halfExtents.y * operation.scale.y),
                std::abs(sightBound.halfExtents.z * operation.scale.z),
            };
            wallCollider->SetColliderConfig(wallConfig);
            wallCollider->SetCollisionAttribute(kGround);
            wallCollider->SetCollisionMask(0xFFFFFFFFu);
            wallCollider->SetParent(rootObject, false);
            wallCollider->UpdateLocalMatrix();
            wallCollider->UpdateWorldMatrix();
            objects.push_back(std::move(wallCollider));
        }
    }

    if (objects.empty()) {
        statusText_ = "プレビュー対象を生成できませんでした。";
        return;
    }

    editor_->StartGameViewCreatePreview(std::move(objects), "Level Plan: " + planName_);
    statusText_ = "Game Viewで配置位置を決め、左クリックで確定してください。右クリックで破棄できます。";
}

const char* LevelDesignLabWindow::GetSeverityLabel(Severity severity) const {
    switch (severity) {
    case Severity::Error: return "ERROR";
    case Severity::Warning: return "WARN";
    default: return "INFO";
    }
}

unsigned int LevelDesignLabWindow::GetSeverityColor(Severity severity) const {
    switch (severity) {
    case Severity::Error: return IM_COL32(255, 92, 92, 255);
    case Severity::Warning: return IM_COL32(255, 196, 76, 255);
    default: return IM_COL32(110, 208, 255, 255);
    }
}

void LevelDesignLabWindow::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::TextWrapped("AIや外部ツールが生成した制限付きLevel Planを、Sceneへ直接書き込まず検証します。");
    ImGui::TextDisabled("赤い問題が0件のPlanだけ、Game Viewへ一括プレビューできます。");
    ImGui::Separator();

    ImGui::InputText("チャンクカタログ", catalogPath_, sizeof(catalogPath_));
    ImGui::InputText("配置Plan", planPath_, sizeof(planPath_));
    if (ImGui::Button("カタログを再読込")) LoadCatalog();
    ImGui::SameLine();
    if (ImGui::Button("Planを読込・検証")) LoadPlan();

    if (!statusText_.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", statusText_.c_str());
    }

    ImGui::Separator();
    ImGui::Text("登録チャンク: %d", static_cast<int>(chunks_.size()));
    ImGui::Text("Plan: %s", planName_.empty() ? "未読込" : planName_.c_str());
    ImGui::Text("配置操作: %d / 接続: %d", static_cast<int>(operations_.size()), static_cast<int>(links_.size()));

    int errorCount = 0;
    int warningCount = 0;
    int infoCount = 0;
    for (const ValidationIssue& issue : issues_) {
        if (issue.severity == Severity::Error) ++errorCount;
        else if (issue.severity == Severity::Warning) ++warningCount;
        else ++infoCount;
    }
    ImGui::TextColored(ImVec4(1.0f, 0.36f, 0.36f, 1.0f), "ERROR %d", errorCount);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.77f, 0.30f, 1.0f), "WARN %d", warningCount);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.43f, 0.82f, 1.0f, 1.0f), "INFO %d", infoCount);

    ImGui::BeginDisabled(!validationComplete_ || HasErrors());
    if (ImGui::Button("検証済みPlanをGame Viewでプレビュー", ImVec2(-1.0f, 34.0f))) {
        StartPlanPreview();
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("検証結果");
    if (issues_.empty()) {
        ImGui::TextDisabled("検証結果はありません。");
    }
    else if (ImGui::BeginChild("LevelDesignIssues", ImVec2(0.0f, 260.0f), true)) {
        for (const ValidationIssue& issue : issues_) {
            ImGui::PushStyleColor(ImGuiCol_Text, GetSeverityColor(issue.severity));
            ImGui::Text("[%s] %s", GetSeverityLabel(issue.severity), issue.category.c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextWrapped("%s", issue.message.c_str());
        }
        ImGui::EndChild();
    }
#endif
}
