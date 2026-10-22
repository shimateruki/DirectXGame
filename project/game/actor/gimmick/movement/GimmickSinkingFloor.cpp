#include "GimmickSinkingFloor.h"
#include "engine/utility/SystemDebug/editor/core/ObjectPropertyDefinition.h"
#include "CollisionConfig.h"
#include "MovingFloorSupport.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "Player.h"
#include <algorithm>
#include <cmath>

namespace {
// 一つの沈む床を構成する子の上面も調べ、どの部品を踏んでも親を始動する。
bool HasRiderOnDeck(engine::Object3d& part, Character& actor) {
    if (const Collider* collider = part.GetCollider(); collider && part.GetCollisionAttribute() != 0) {
        if (!collider->HasModelVolume() && IsStandingOnMovingFloor(part, actor))
            return true;
        if (!actor.isDead && actor.IsGrounded() && actor.GetVelocity().y <= 0.1f &&
            collider->HasModelVolume()) {
            // 親の非一様拡縮で傾いた子床は、箱の上端でなく実際の凸形状の上面を測る。
            const AABB body = actor.GetAABB();
            const engine::Vector3 origin = {
                (body.min.x + body.max.x) * 0.5f, body.min.y + 0.35f,
                (body.min.z + body.max.z) * 0.5f
            };
            float distance = 0.0f;
            engine::Vector3 normal{};
            if (collider->RaycastModelVolume(origin, {0.0f, -1.0f, 0.0f}, 0.65f, distance, normal) &&
                normal.y > 0.5f && std::abs(origin.y - distance - body.min.y) <= 0.25f)
                return true;
        }
        // 傾いた子床ではAABB最高点が上面中央より高い。実際のOBB上面で足元を測る。
        if (!actor.isDead && actor.IsGrounded() && actor.GetVelocity().y <= 0.1f &&
            collider->GetType() == ColliderType::kOBB && !collider->HasModelVolume()) {
            const OBB box = collider->GetOBB();
            const engine::Vector3 normal = box.orientations[1];
            const auto body = actor.GetAABB();
            const auto bounds = part.GetAABB();
            if (normal.y > 0.5f && body.max.x > bounds.min.x && body.min.x < bounds.max.x &&
                body.max.z > bounds.min.z && body.min.z < bounds.max.z) {
                const float x = (body.min.x + body.max.x) * 0.5f;
                const float z = (body.min.z + body.max.z) * 0.5f;
                const engine::Vector3 top = box.center + normal * box.size.y;
                const float surfaceY = top.y - (normal.x * (x - top.x) + normal.z * (z - top.z)) / normal.y;
                if (std::abs(body.min.y - surfaceY) <= 0.25f &&
                    (collider->GetConfig().footprint.size() < 3 || collider->OverlapsFootprint(actor.GetCollider())))
                    return true;
            }
        }
    }
    for (engine::Object3d* child : part.GetChildren()) {
        if (child && !child->IsReplayRemoved() && HasRiderOnDeck(*child, actor))
            return true;
    }
    return false;
}
} // namespace

void GimmickSinkingFloor::Initialize(engine::Object3dCommon* common, const std::string& modelName) {
    // プレイヤーが乗れる床として衝突属性を設定する。
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("SinkingFloor");
    SetName("Gimmick_SinkingFloor");

    // 登録済みのクラスでは共通処理が二重登録を省きます。個体のアドレスは台帳へ渡しません。
    engine::ObjectPropertyDefinition<GimmickSinkingFloor> properties(*this, kTypeId, "沈む床");
    const auto shallow = +[](const GimmickSinkingFloor& floor) { return !floor.param_ || floor.param_->actionMode != 1; };
    const auto submerged = +[](const GimmickSinkingFloor& floor) { return floor.param_ && floor.param_->actionMode == 1; };
    using Parameter = engine::Object3d::EntityParameter;
    properties.Parameter("沈み方", "actionMode", &Parameter::actionMode, 0, 1, nullptr,
                         {"従来の浅い沈み込み", "踏むと水没して復帰"});
    properties.Expose("浅い沈み深さ", ENGINE_PROPERTY(sinkDepth_), 0.1f, 100.0f, shallow);
    properties.Expose("沈み込みの追従係数", ENGINE_PROPERTY(sinkSpeed_), 0.1f, 20.0f, shallow);
    properties.Parameter("沈む深さ", "moveAmount", &Parameter::moveAmount, 0.1f, 100.0f, submerged);
    properties.Parameter("昇降速度", "speed", &Parameter::speed, 0.1f, 20.0f, submerged);
    properties.Parameter("水没後の待機時間", "interval", &Parameter::interval, 0.1f, 10.0f, submerged);
    SetCollisionAttribute(CollisionCategories::Get("ground"));
    SetCollisionMask(CollisionCategories::GetDefaultGameplayMask());
}

void GimmickSinkingFloor::Update(float deltaTime) {
    // actionMode で単純な踏み込みと、段階的な水没演出を切り替える。
    if (param_ && param_->actionMode == 1) {
        UpdateSubmerging(deltaTime);
        return;
    }

    if (!initializedStart_) {
        startPos_ = GetTransform()->translate;
        initializedStart_ = true;
    }

    if (contactTimer_ > 0.0f) {
        contactTimer_ -= deltaTime;
        if (contactTimer_ < 0.0f)
            contactTimer_ = 0.0f;
    }

    const bool isStepped = contactTimer_ > 0.0f;
    float targetY = startPos_.y + (isStepped ? -sinkDepth_ : 0.0f);
    float t = 1.0f - std::exp(-sinkSpeed_ * deltaTime);

    engine::Vector3 pos = GetTransform()->translate;
    pos.y = engine::Math::Lerp(pos.y, targetY, t);
    SetTranslate(pos);

    BaseGimmick::Update(deltaTime);
}

bool GimmickSinkingFloor::OnCollision(engine::Object3d* other) {
    // プレイヤーが上面を踏んだ短い間だけ、目標の沈み位置を維持する。
    if (other && other->GetClassName() == "Player") {
        CollisionInfo info = CheckCollision(other);

        if (info.isColliding && info.normal.y < -0.5f) {
            contactTimer_ = 0.12f;
        }
    }

    return true;
}

std::unique_ptr<engine::Object3d> GimmickSinkingFloor::Clone() const {
    // 移動中の位相は複製せず、保存された配置と設定だけ引き継ぐ。
    auto newObj = std::make_unique<GimmickSinkingFloor>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}

void GimmickSinkingFloor::UpdateSubmerging(float deltaTime) {
    // 親子で組んだ床は親だけが沈下を計算し、子はローカル姿勢のまま追従する。
    if (dynamic_cast<GimmickSinkingFloor*>(GetParent())) {
        BaseGimmick::Update(deltaTime);
        return;
    }

    // 接地したフレームから下降し、最深部で滞在した後に元の高さへ戻す。
    auto* manager = SceneManager::GetInstance();

    if (!manager || !manager->IsPlaying()) {
        if (initializedStart_)
            SetTranslate(startPos_);
        initializedStart_ = false;
        submergePhase_ = 0;
        submergeTimer_ = 0.0f;
        contactTimer_ = 0.0f;
        BaseGimmick::Update(deltaTime);
        return;
    }

    if (!initializedStart_) {
        startPos_ = GetTranslate();
        initializedStart_ = true;
    }

    std::vector<Character*> riders;

    if (auto* scene = manager->GetCurrentScene()) {
        for (auto& object : scene->GetObjects()) {
            auto* actor = object && !object->IsReplayRemoved() ? dynamic_cast<Character*>(object.get()) : nullptr;
            bool standing = actor && HasRiderOnDeck(*this, *actor);
            if (auto* player = dynamic_cast<Player*>(actor); player && !standing && player->IsGrounded() &&
                player->GetVelocity().y <= 0.1f) {
                // Motorが子の斜面を接地相手と確定した場合は、その親まで乗車を伝える。
                const auto& ground = player->GetCharacterMotor().GetResult();
                if (ground.grounded) {
                    for (auto* support = ground.groundObject; support; support = support->GetParent()) {
                        if (support == this) { standing = true; break; }
                    }
                }
            }
            if (standing)
                riders.push_back(actor);
        }
    }

    const float dt = (std::max)(0.0f, deltaTime);
    const float depth = (std::max)(0.1f, param_->moveAmount);
    const float speed = (std::max)(0.1f, param_->speed);
    const engine::Vector3 before = GetTranslate();
    engine::Vector3 next = before;

    if (submergePhase_ == 0 && !riders.empty()) {
        submergePhase_ = 2;
        submergeTimer_ = 0.0f;
    }

    // 一度踏んだら最深部まで沈む。接地が一瞬切れても途中で浮き戻らない。
    if (submergePhase_ == 2) {
        next.y = (std::max)(startPos_.y - depth, before.y - speed * dt);

        if (next.y <= startPos_.y - depth) {
            submergePhase_ = 3;
            submergeTimer_ = 0.0f;
        }
    } else if (submergePhase_ == 3) {
        submergeTimer_ += dt;
        if (submergeTimer_ >= (std::max)(0.1f, param_->interval))
            submergePhase_ = 4;
    } else if (submergePhase_ == 4) {
        next.y = (std::min)(startPos_.y, before.y + speed * dt);
        if (next.y >= startPos_.y)
            submergePhase_ = 0;
    }

    SetTranslate(next);
    BaseGimmick::Update(deltaTime);

    // 移動床と同じ順序で乗員を運び、下降中にプレイヤーが宙に残るのを防ぐ。
    const engine::Vector3 displacement = next - before;

    for (auto* rider : riders) {
        rider->SetTranslate(rider->GetTranslate() + displacement);
        rider->UpdateLocalMatrix();
        rider->UpdateWorldMatrix(false);
        if (auto* player = dynamic_cast<Player*>(rider); player && dt > 0.0f)
            player->SetPlatformMotion(displacement * (1.0f / dt));
    }
}
