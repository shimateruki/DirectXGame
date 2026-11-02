#define NOMINMAX
#include "game/actor/player/state/PlayerState.h"
#include "Player.h"
#include "BaseEnemy.h"
#include "CollisionManager.h"
#include "CollisionConfig.h"
#include "GimmickHookPullBlock.h"
#include "game/system/combat/HitEffectDirector.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <unordered_set>
#include "ModelManager.h"
#include "MeshRenderer.h"
#include <cstring>
#ifdef USE_IMGUI
#include "imgui.h"
#endif

namespace {
constexpr const char* kSettingsPath = "Resources/json/player/whip.json";
void Sanitize(PlayerWhipSettings& settings) {
    const auto clamp = [](float value, float lo, float hi, float fallback) {
        return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
    };
    settings.maxDistance = clamp(settings.maxDistance, 1.0f, 200.0f, 30.0f);
    settings.shootSpeed = clamp(settings.shootSpeed, 30.0f, 1000.0f, 450.0f);
    settings.returnSpeed = clamp(settings.returnSpeed, 30.0f, 1500.0f, 650.0f);
    settings.hitRadius = clamp(settings.hitRadius, 0.1f, 3.0f, 0.65f);
    settings.windupDuration = clamp(settings.windupDuration, 0.02f, 0.4f, 0.08f);
    settings.sweepDuration = clamp(settings.sweepDuration, 0.06f, 0.8f, 0.18f);
    settings.recoverDuration = clamp(settings.recoverDuration, 0.04f, 0.6f, 0.14f);
    settings.sweepAngleDegrees = clamp(settings.sweepAngleDegrees, 30.0f, 170.0f, 140.0f);
    settings.bendDegrees = clamp(settings.bendDegrees, 0.0f, 45.0f, 28.0f);
    settings.bodyTwistDegrees = clamp(settings.bodyTwistDegrees, 0.0f, 65.0f, 32.0f);
    settings.bodySquash = clamp(settings.bodySquash, 0.0f, 0.35f, 0.18f);
    settings.thickness = clamp(settings.thickness, 0.1f, 1.5f, 0.65f);

}
}

engine::Vector3 Player::GetWhipOrigin() const {
    const AABB bounds = GetAABB();
    return (bounds.min + bounds.max) * 0.5f;
}
engine::Vector3 Player::GetWhipDirection() const {
    if (HasLockOnAimPoint()) {
        const engine::Vector3 delta = lockOnAimPoint_ - GetWhipOrigin();
        if (engine::Math::Length(delta) > 0.001f)
            return engine::Math::Normalize(delta);
    }
    return GetForwardDirection();
}
void Player::LoadWhipSettings() {
    // 読み込みだけで保存は行わず、ファイルがない場合は既定値で遊べます。
    whipSettings_ = {};
    try {
        std::ifstream stream(kSettingsPath);
        if (!stream) return;
        const auto json = nlohmann::json::parse(stream);
        whipSettings_.maxDistance = json.value("maxDistance", whipSettings_.maxDistance);
        whipSettings_.shootSpeed = json.value("shootSpeed", whipSettings_.shootSpeed);
        whipSettings_.returnSpeed = json.value("returnSpeed", whipSettings_.returnSpeed);
        whipSettings_.hitRadius = json.value("hitRadius", whipSettings_.hitRadius);
        whipSettings_.windupDuration = json.value("windupDuration", whipSettings_.windupDuration);
        whipSettings_.sweepDuration = json.value("sweepDuration", whipSettings_.sweepDuration);
        whipSettings_.recoverDuration = json.value("recoverDuration", whipSettings_.recoverDuration);
        whipSettings_.sweepAngleDegrees = json.value("sweepAngleDegrees", whipSettings_.sweepAngleDegrees);
        whipSettings_.bendDegrees = json.value("bendDegrees", whipSettings_.bendDegrees);
        whipSettings_.bodyTwistDegrees = json.value("bodyTwistDegrees", whipSettings_.bodyTwistDegrees);
        whipSettings_.bodySquash = json.value("bodySquash", whipSettings_.bodySquash);
        whipSettings_.thickness = json.value("thickness", whipSettings_.thickness);

    } catch (const nlohmann::json::exception&) {
        whipSettings_ = {};
    }
    Sanitize(whipSettings_);
}
bool Player::SaveWhipSettings() const {
    // 書き終えた一時ファイルを置き換え、保存失敗で前回の設定を空にしません。
    std::error_code error;
    const std::filesystem::path destination(kSettingsPath);
    std::filesystem::create_directories(destination.parent_path(), error);
    if (error) return false;
    const std::filesystem::path temporary = destination.wstring() + L".tmp." + std::to_wstring(GetCurrentProcessId());
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) return false;
        stream << nlohmann::json{{"maxDistance", whipSettings_.maxDistance},
            {"shootSpeed", whipSettings_.shootSpeed}, {"returnSpeed", whipSettings_.returnSpeed},
            {"hitRadius", whipSettings_.hitRadius},
            {"windupDuration", whipSettings_.windupDuration},
            {"sweepDuration", whipSettings_.sweepDuration},
            {"recoverDuration", whipSettings_.recoverDuration},
            {"sweepAngleDegrees", whipSettings_.sweepAngleDegrees},
            {"bendDegrees", whipSettings_.bendDegrees},
            {"bodyTwistDegrees", whipSettings_.bodyTwistDegrees},
            {"bodySquash", whipSettings_.bodySquash},
            {"thickness", whipSettings_.thickness}}.dump(2);
        stream.flush();
        if (!stream) { stream.close(); std::filesystem::remove(temporary, error); return false; }
    }
    if (MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    std::filesystem::remove(temporary, error);
    return false;
}
void Player::DrawWhipEditor() {
#ifdef USE_IMGUI
    if (!ImGui::CollapsingHeader("スライムウィップ（通常形態）")) return;
    ImGui::PushID(this);
    ImGui::DragFloat("射程", &whipSettings_.maxDistance, 0.5f, 1.0f, 200.0f);
    ImGui::TextUnformatted("右から左へ薙ぎ払い、最初に触れた対象を捕獲します。");
    ImGui::DragFloat("鞭の判定半径（太さ以上）", &whipSettings_.hitRadius, 0.02f, 0.1f, 3.0f);
    ImGui::DragFloat("振りかぶり時間（秒）", &whipSettings_.windupDuration, 0.01f, 0.02f, 0.4f);
    ImGui::DragFloat("薙ぎ払い時間（秒）", &whipSettings_.sweepDuration, 0.01f, 0.06f, 0.8f);
    ImGui::DragFloat("復帰時間（秒）", &whipSettings_.recoverDuration, 0.01f, 0.04f, 0.6f);
    ImGui::DragFloat("薙ぎ払い角度", &whipSettings_.sweepAngleDegrees, 0.5f, 30.0f, 170.0f);
    ImGui::DragFloat("しなり角度", &whipSettings_.bendDegrees, 0.5f, 0.0f, 45.0f);
    ImGui::DragFloat("体のひねり角度", &whipSettings_.bodyTwistDegrees, 0.5f, 0.0f, 65.0f);
    ImGui::DragFloat("体の潰れ量", &whipSettings_.bodySquash, 0.01f, 0.0f, 0.35f);
    ImGui::DragFloat("鞭の根元の太さ", &whipSettings_.thickness, 0.01f, 0.1f, 1.5f);
    Sanitize(whipSettings_);
    ImGui::TextWrapped("次の一振りから反映。保存すると次回生成するPlayerにも適用します。");
    if (ImGui::Button("鞭設定を保存")) {
        ImGui::OpenPopup(SaveWhipSettings() ? "保存完了" : "保存失敗");
    }
    ImGui::SameLine();
    if (ImGui::Button("鞭設定を再読込")) LoadWhipSettings();
    for (const char* popup : {"保存完了", "保存失敗"}) {
        if (ImGui::BeginPopup(popup)) { ImGui::TextUnformatted(popup); ImGui::EndPopup(); }
    }
    ImGui::TextUnformatted("保存先: Resources/json/player/whip.json");
    ImGui::PopID();
#endif
}

namespace {
constexpr float kPi = 3.14159265358979323846f;
float Smooth(float t) { t = std::clamp(t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }
engine::Vector3 SafeUnit(const engine::Vector3& v, const engine::Vector3& fallback) {
    const float length = engine::Math::Length(v);
    return length > 0.0001f ? v / length : fallback;
}
}

float PlayerWhipMotion::Duration(const PlayerWhipSettings& s) {
    return s.windupDuration + s.sweepDuration + s.recoverDuration;
}
PlayerWhipMotion::Curve PlayerWhipMotion::Evaluate(const PlayerWhipSettings& s, const engine::Vector3& forward, float time) {
    const auto f = SafeUnit(forward, {0, 0, 1});
    const auto right = SafeUnit({f.z, 0, -f.x}, {1, 0, 0});
    float angle = s.sweepAngleDegrees * 0.5f;
    float length = 0.0f;
    float bend = 0.0f;
    if (time < s.windupDuration) {
        const float u = Smooth(time / s.windupDuration);
        length = s.maxDistance * 0.25f * u;
        bend = s.bendDegrees * u;
    } else if (time < s.windupDuration + s.sweepDuration) {
        const float u = std::clamp((time - s.windupDuration) / s.sweepDuration, 0.0f, 1.0f);
        angle *= 1.0f - 2.0f * Smooth(u);
        length = s.maxDistance * (0.25f + 0.75f * Smooth(u / 0.25f));
        bend = s.bendDegrees * (1.0f - Smooth(u));
    } else {
        const float u = Smooth((time - s.windupDuration - s.sweepDuration) / s.recoverDuration);
        angle = -angle - 20.0f * std::sin(u * kPi);
        length = s.maxDistance * (1.0f - u);
        bend = -s.bendDegrees * std::sin(u * kPi);
    }
    Curve curve{};
    for (size_t i = 0; i <= kSegments; ++i) {
        const float v = static_cast<float>(i) / static_cast<float>(kSegments);
        // 根元が先行し、先端ほど遅れて振られる一本の曲線です。
        const float a = std::clamp(angle + bend * v * v * (1.0f - std::abs(angle) / 90.0f), -85.0f, 85.0f) * kPi / 180.0f;
        curve[i] = (f * std::cos(a) + right * std::sin(a)) * (length * v);
    }
    return curve;
}

void Player::ClearWhipBodyPose() {
    slimeAnimator_.ClearWhipPose();
    if (GetMeshRenderer()) GetMeshRenderer()->ResetVisualTransform();
}

void PlayerStateWhip::Enter(Player* player) {
    if (!player) return;
    settings_ = player->GetWhipSettings();
    Sanitize(settings_);
    origin_ = player->GetWhipOrigin();
    direction_ = SafeUnit(direction_, player->GetForwardDirection());
    player->SetIsControlActive(false);
    player->SetVelocity({0, 0, 0});
    player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::ShootHook);
    player->SetSlimeAnimationDirection(direction_);
    // 一本の連続したチューブを一振りにつき一度確保し、フレーム中は頂点だけを書き換えます。
    constexpr uint32_t sides = 10;
    vertices_.resize((PlayerWhipMotion::kSegments + 1) * sides);
    std::vector<uint32_t> indices;
    for (uint32_t i = 0; i < PlayerWhipMotion::kSegments; ++i) {
        for (uint32_t j = 0; j < sides; ++j) {
            const uint32_t a = i * sides + j, b = i * sides + (j + 1) % sides;
            indices.insert(indices.end(), {a, b, a + sides, b, b + sides, a + sides});
        }
    }
    for (uint32_t j = 1; j + 1 < sides; ++j) {
        indices.insert(indices.end(), {0, j + 1, j});
        const uint32_t end = static_cast<uint32_t>(PlayerWhipMotion::kSegments) * sides;
        indices.insert(indices.end(), {end, end + j, end + j + 1});
    }
    if (auto* marker = player->GetHookMarker()) {
        originalModel_ = marker->GetModel();
        mesh_ = std::make_unique<Model>();
        mesh_->CreateFromVertices(ModelManager::GetInstance()->GetModelCommon(), vertices_, indices);
        marker->SetModel(mesh_.get());
    }
    UpdateBodyPose(player);
    UpdateVisual(player);
}

void PlayerStateWhip::BeginRecovery() {
    blocked_ = true;
    blockedAt_ = elapsed_;
    blockedCurve_ = curve_;
}

bool PlayerStateWhip::HandleHit(Player* player, engine::Object3d* object, const engine::Vector3& point) {
    for (auto* target = object; target; target = target->GetParent()) {
        if (auto* block = dynamic_cast<GimmickHookPullBlock*>(target)) {
            player->ChangeState(std::make_unique<PlayerStatePullObject>(block, point, true));
            return true;
        }
        if (auto* enemy = dynamic_cast<BaseEnemy*>(target)) {
            if (!enemy->IsPullImmune() && !enemy->IsDead() && !enemy->IsReplayRemoved()) {
                player->ChangeState(std::make_unique<PlayerStatePullEnemy>(enemy, point, true));
                return true;
            }
            HitEffectDirector::SpawnPullBindHit(point);
            break;
        }
    }
    BeginRecovery();
    return false;
}

void PlayerStateWhip::CollectCandidates(Player* player) {
    queryCandidates_.clear();
    const auto mask = CollisionCategories::Get("ground") | CollisionCategories::Get("enemy") | CollisionCategories::Get("hook_anchor");
    // ワールド形状は一更新につき一度取得し、多数の曲線サンプル間で再利用します。
    for (const auto* object : CollisionManager::GetInstance()->GetObjects()) {
        if (!object || object == player || !(object->GetCollisionAttribute() & mask)) continue;
        const auto type = object->GetColliderType();
        bool special = type != ColliderType::kAABB && type != ColliderType::kOBB;
        AABB bounds = object->GetAABB();
        if (type == ColliderType::kSphere) {
            const auto center = object->GetCollider()->GetOBB().center;
            const float radius = std::abs(object->GetCollider()->GetRadius());
            const engine::Vector3 extent{radius, radius, radius};
            bounds = {center - extent, center + extent}; special = false;
        } else if (type == ColliderType::kCylinder) {
            const auto cylinder = object->GetCollider()->GetCylinder();
            const float half = std::abs(cylinder.height) * 0.5f;
            const float radius = std::abs(cylinder.radius);
            const engine::Vector3 extent{std::abs(cylinder.axis.x) * half + radius,
                std::abs(cylinder.axis.y) * half + radius, std::abs(cylinder.axis.z) * half + radius};
            bounds = {cylinder.center - extent, cylinder.center + extent}; special = false;
        }
        queryCandidates_.push_back({object, bounds, special});
    }
}

bool PlayerStateWhip::CheckCurveHit(Player* player, const PlayerWhipMotion::Curve& curve, const engine::Vector3& origin) {
    PhysicsQueryFilter filter;
    filter.mask = CollisionCategories::Get("ground") | CollisionCategories::Get("enemy") | CollisionCategories::Get("hook_anchor");
    filter.ignoredObject = player;
    filter.includeTriggers = true;
    filter.refineSphereCastHit = true;
    // 根元から順に調べ、壁を越えた曲線上の敵を捕まえません。
    for (size_t i = 0; i < PlayerWhipMotion::kSegments; ++i) {
        const auto start = origin + curve[i];
        const auto end = origin + curve[i + 1];
        const auto delta = end - start;
        const float distance = engine::Math::Length(delta);
        if (distance < 0.0001f) continue;
        const float r = (std::max)(settings_.hitRadius, settings_.thickness);
        const AABB sweep{{(std::min)(start.x, end.x) - r, (std::min)(start.y, end.y) - r, (std::min)(start.z, end.z) - r},
                         {(std::max)(start.x, end.x) + r, (std::max)(start.y, end.y) + r, (std::max)(start.z, end.z) + r}};
        std::unordered_set<const engine::Object3d*> candidates;
        for (const auto& candidate : queryCandidates_) {
            const auto* object = candidate.object;
            if (candidate.special) { candidates.insert(object); continue; }
            const auto& bounds = candidate.bounds;
            if (bounds.min.x <= sweep.max.x && bounds.max.x >= sweep.min.x &&
                bounds.min.y <= sweep.max.y && bounds.max.y >= sweep.min.y &&
                bounds.min.z <= sweep.max.z && bounds.max.z >= sweep.min.z) candidates.insert(object);
        }
        if (candidates.empty()) continue;
        filter.acceptsObject = [&candidates](const engine::Object3d* o) { return candidates.contains(o); };
        const auto hit = CollisionManager::GetInstance()->SphereCast(start, r, delta / distance, distance, filter);
        if (!hit.isHit || !hit.hitObject) continue;
        // 壁への命中点より先を切り、壁の向こうに鞭の残りを表示しません。
        const float contactIndex = static_cast<float>(i) + hit.distance / distance;
        for (size_t j = 0; j < curve_.size(); ++j) {
            const float sample = contactIndex * static_cast<float>(j) / static_cast<float>(PlayerWhipMotion::kSegments);
            const auto index = (std::min)(static_cast<size_t>(sample), PlayerWhipMotion::kSegments - 1);
            curve_[j] = curve[index] + (curve[index + 1] - curve[index]) * (sample - static_cast<float>(index));
        }
        return HandleHit(player, hit.hitObject, hit.hitPoint);
    }
    return false;
}

void PlayerStateWhip::Update(Player* player, float deltaTime) {
    if (!player || !std::isfinite(deltaTime) || deltaTime <= 0.0f) return;
    const float endTime = (std::min)(elapsed_ + deltaTime, PlayerWhipMotion::Duration(settings_));
    const auto currentOrigin = player->GetWhipOrigin();
    const float sweepEnd = settings_.windupDuration + settings_.sweepDuration;
    const float from = (std::max)(elapsed_, settings_.windupDuration);
    const float to = (std::min)(endTime, sweepEnd);
    if (!blocked_ && to >= from && endTime >= settings_.windupDuration) {
        CollectCandidates(player);
        // 半径の半分以内の先端移動に分割。低FPSでも曲線間を飛ばさず調べます。
        const float speedBound = settings_.maxDistance *
            (4.5f + 1.5f * (settings_.sweepAngleDegrees + settings_.bendDegrees) * kPi / 180.0f) / settings_.sweepDuration;
        const float movementSpeed = engine::Math::Length(currentOrigin - origin_) / deltaTime;
        const float step = settings_.hitRadius * 0.5f / (std::max)(speedBound + movementSpeed, 0.001f);
        const int count = (std::max)(1, static_cast<int>(std::ceil((to - from) / step)));
        for (int i = 0; i <= count; ++i) {
            const float t = from + (to - from) * static_cast<float>(i) / static_cast<float>(count);
            const auto queryOrigin = origin_ + (currentOrigin - origin_) * std::clamp((t - elapsed_) / deltaTime, 0.0f, 1.0f);
            const auto curve = PlayerWhipMotion::Evaluate(settings_, direction_, t);
            if (CheckCurveHit(player, curve, queryOrigin)) return; // 状態は既に破棄されています。
            if (blocked_) { blockedAt_ = t; break; }
        }
    }
    elapsed_ = endTime;
    origin_ = currentOrigin;
    const float recovery = blocked_ ? (elapsed_ - blockedAt_) / settings_.recoverDuration : 0.0f;
    if (elapsed_ >= PlayerWhipMotion::Duration(settings_) || (blocked_ && recovery >= 1.0f)) {
        player->ChangeState(std::make_unique<PlayerStateIdle>());
        return;
    }
    if (blocked_) {
        for (size_t i = 0; i < curve_.size(); ++i) curve_[i] = blockedCurve_[i] * (1.0f - Smooth(recovery));
    } else curve_ = PlayerWhipMotion::Evaluate(settings_, direction_, elapsed_);
    player->SetSlimeAnimationMode(PlayerSlimeAnimator::Mode::ShootHook);
    UpdateBodyPose(player);
    UpdateVisual(player);
}

void PlayerStateWhip::UpdateBodyPose(Player* player) {
    const float poseTime = blocked_ ? blockedAt_ : elapsed_;
    float twist = 0.0f, squash = 0.0f, lean = 0.0f;
    if (poseTime < settings_.windupDuration) {
        const float u = Smooth(poseTime / settings_.windupDuration);
        twist = u; squash = settings_.bodySquash * u; lean = -0.10f * u;
    } else if (poseTime < settings_.windupDuration + settings_.sweepDuration) {
        const float u = Smooth((poseTime - settings_.windupDuration) / settings_.sweepDuration);
        twist = 1.0f - 2.0f * u;
        squash = settings_.bodySquash * (1.0f - u) - 0.10f * std::sin(kPi * u);
        lean = 0.16f * std::sin(kPi * u);
    } else {
        const float u = Smooth((poseTime - settings_.windupDuration - settings_.sweepDuration) / settings_.recoverDuration);
        twist = -(1.0f - u); lean = 0.06f * std::sin(kPi * u);
    }
    if (blocked_) {
        const float weight = 1.0f - Smooth((elapsed_ - blockedAt_) / settings_.recoverDuration);
        twist *= weight; squash *= weight; lean *= weight;
    }
    player->SetWhipBodyPose({1.0f + squash * 0.55f, 1.0f - squash, 1.0f + squash * 0.4f},
                           {lean, twist * settings_.bodyTwistDegrees * kPi / 180.0f, -twist * 0.12f});
}

void PlayerStateWhip::UpdateVisual(Player* player) {
    auto* marker = player->GetHookMarker();
    if (!marker || !mesh_ || mesh_->GetModelData().meshes.empty()) return;
    constexpr size_t sides = 10;
    for (size_t i = 0; i < curve_.size(); ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(PlayerWhipMotion::kSegments);
        const auto tangent = SafeUnit(curve_[(std::min)(i + 1, curve_.size() - 1)] - curve_[i ? i - 1 : 0], direction_);
        const auto side = SafeUnit(engine::Math::Cross({0, 1, 0}, tangent), {1, 0, 0});
        const auto up = SafeUnit(engine::Math::Cross(tangent, side), {0, 1, 0});
        const float radius = settings_.thickness * (1.0f - 0.86f * u);
        for (size_t j = 0; j < sides; ++j) {
            const float a = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(sides);
            const auto normal = side * std::cos(a) + up * std::sin(a);
            const auto point = curve_[i] + normal * radius;
            auto& v = vertices_[i * sides + j];
            v.position = {point.x, point.y, point.z, 1.0f};
            v.normal = normal; v.tangent = tangent;
            v.texcoord = {static_cast<float>(j) / static_cast<float>(sides), u};
        }
    }
    // 現行レンダラーは毎フレーム末尾でGPUフェンスを待つため、次Updateで安全に再利用できます。
    const auto& resource = mesh_->GetModelData().meshes[0].vertexResource;
    void* mapped = nullptr;
    const D3D12_RANGE readRange{0, 0};
    if (resource && SUCCEEDED(resource->Map(0, &readRange, &mapped))) {
        std::memcpy(mapped, vertices_.data(), vertices_.size() * sizeof(Model::VertexData));
        resource->Unmap(0, nullptr);
    }
    marker->SetIsVisible(engine::Math::Length(curve_.back()) > 0.05f);
    marker->SetTranslate(player->GetWhipOrigin());
    marker->SetQuaternionMaster(false);
    marker->SetEulerAngles({0, 0, 0});
    marker->SetScale({1, 1, 1});
    marker->SetColor({0.10f, 0.82f, 1.0f, 1.0f});
    // 暗い背景でも細い先端を読めるよう、通常材質へ控えめな発光分を足します。
    marker->SetEmissive(1.35f);
    marker->UpdateLocalMatrix();
    marker->UpdateWorldMatrix();
}
void PlayerStateWhip::Exit(Player* player) {
    if (!player) return;
    player->SetIsControlActive(true);
    player->ClearWhipBodyPose();
    if (auto* marker = player->GetHookMarker()) {
        marker->SetIsVisible(false);
        marker->SetModel(originalModel_);
        // Player生成時のフック材質の既定値へ戻し、既存の引き寄せ演出へ持ち越しません。
        marker->SetEmissive(1.0f);
        marker->SetScale({1, 1, 1});
        marker->SetEulerAngles({0, 0, 0});
        marker->SetQuaternionMaster(true);
        marker->UpdateLocalMatrix();
        marker->UpdateWorldMatrix();
    }
}
