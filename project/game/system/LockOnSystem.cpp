#include "LockOnSystem.h"

#include "Camera.h"
#include "Player.h"
#include "InputManager.h"
#include "CollisionManager.h"
#include "CollisionConfig.h"
#include "BaseEnemy.h"
#include "SceneManager.h"
#include <algorithm>
#include <cmath>
namespace {
// ロックオンの照準点として、ワールドAABBの中心を返します。
engine::Vector3 AimCenter(engine::Object3d* object) {
    // 原点や固定の高さではなく、サイズ・オフセットを反映した判定中心を狙います。
    const AABB bounds = object->GetAABB();
    return (bounds.min + bounds.max) * 0.5f;
}

bool Available(engine::Object3d* object) {
    // 倒れた敵・運搬中の敵・不可視の敵は照準候補から外す。
    auto* enemy = dynamic_cast<BaseEnemy*>(object);
    return enemy && !enemy->isDead && enemy->GetIsVisible() && (enemy->GetCollisionAttribute() & CollisionCategories::Get("enemy")) &&
           !enemy->IsCarried() && !enemy->IsDefeatEffectPlaying();
}

bool Visible(Player* player, engine::Object3d* target) {
    // 照準中心間の遮蔽物を確認し、距離がほぼ0なら方向計算を避ける。
    const engine::Vector3 start = AimCenter(player);
    const engine::Vector3 delta = AimCenter(target) - start;
    const float distance = engine::Math::Length(delta);
    if (distance < 0.001f)
        return true;
    PhysicsQueryFilter filter;
    filter.mask = CollisionCategories::Get("ground");
    filter.ignoredObject = player;
    return !CollisionManager::GetInstance()->Raycast(start, delta / distance, distance, filter).isHit;
}
} // namespace
// カメラとプレイヤーから見える候補を評価し、ロック対象を選びます。対象消失時は照準を解除します。

LockOnSystem::LockOnSystem() = default;
LockOnSystem::~LockOnSystem() = default;
void LockOnSystem::Initialize(InputManager* inputManager) {
    // 入力の所有権は移さず、ロックオン操作の参照先として保持する。
    inputManager_ = inputManager;
}
void LockOnSystem::ResetLockOn(Camera* camera) {
    // 対象参照と見失い時計を同時に消し、カメラ側の追従も解除する。
    const bool active = isLockingOn_;
    isLockingOn_ = false;
    lockOnTarget_ = nullptr;
    lostSightTimer_ = 0;
    if (camera && active) {
        camera->SyncRotationToCurrentView();
        camera->SetLockOnTarget(nullptr);
        if (camera->GetFollowMode() == Camera::FollowMode::kLockOn)
            camera->SetFollowMode(Camera::FollowMode::kAimable);
    }
}

void LockOnSystem::Update(const std::vector<std::unique_ptr<engine::Object3d>>& objects, Camera* camera, Player* player,
                          float deltaTime) {
    if (!player || !camera || !inputManager_)
        return;
    player->SetLockOn(false);
    player->SetLockOnAimPoint({}, false);
    if (!SceneManager::GetInstance()->IsPlaying() || player->isDead || player->IsCinematicLocked() ||
        player->IsGateReturnAnimationActive()) {
        ResetLockOn(camera);
        return;
    }
    if (deltaTime <= 0 || !std::isfinite(deltaTime))
        return;
    if (lockOnTarget_) {
        // 対象を参照する前に、シーンに残っていることを確認します。
        const bool exists = std::any_of(objects.begin(), objects.end(),
                                        [&](const auto& object) { return object.get() == lockOnTarget_; });
        if (!exists || !Available(lockOnTarget_) ||
            engine::Math::Length(lockOnTarget_->GetWorldPosition() - player->GetWorldPosition()) >
                kMaxLockOnDistance_ * 1.25f) {
            ResetLockOn(camera);
            return;
        }
    }
    if (inputManager_->IsActionTriggered("LockOn")) {
        if (isLockingOn_) {
            ResetLockOn(camera);
            return;
        }
        lockOnTarget_ = FindBestTarget(objects, camera, player);
        isLockingOn_ = lockOnTarget_ != nullptr;
    } else if (isLockingOn_ && inputManager_->IsActionTriggered("LockOnNext")) {
        if (auto* next = FindBestTarget(objects, camera, player, true)) {
            lockOnTarget_ = next;
            lostSightTimer_ = 0;
        }
    }
    if (!isLockingOn_ || !lockOnTarget_)
        return;
    const bool visible = Visible(player, lockOnTarget_);
    // 一瞬の遮蔽ではカメラを切り替えず、0.5 秒続いたら解除する。
    lostSightTimer_ = visible ? 0 : lostSightTimer_ + deltaTime;
    if (lostSightTimer_ >= 0.5f) {
        ResetLockOn(camera);
        return;
    }
    camera->SetFollowMode(Camera::FollowMode::kLockOn);
    camera->SetLockOnTarget(lockOnTarget_);
    player->SetLockOn(true);
    // 遮蔽中の猶予はカメラだけに与え、攻撃補助は止めます。
    player->SetLockOnAimPoint(AimCenter(lockOnTarget_), visible);
}

engine::Object3d* LockOnSystem::FindBestTarget(const std::vector<std::unique_ptr<engine::Object3d>>& objects, Camera* camera,
                                       Player* player, bool next) {
    // ロックオン候補の借用参照と選択用スコア。候補を集めてから順序付けする。
    struct Candidate {
        engine::Object3d* object;
        float score;
    };
    std::vector<Candidate> candidates;
    engine::Vector3 forward = camera->GetTargetPoint() - camera->GetEye();
    const float length = engine::Math::Length(forward);
    forward = length > 0.001f ? forward / length : engine::Vector3{0, 0, 1};
    for (const auto& object : objects) {
        if (!Available(object.get()))
            continue;
        bool owned = false;
        for (auto* parent = object.get(); parent; parent = parent->GetParent())
            if (parent == player) {
                owned = true;
                break;
            }
        if (owned)
            continue;
        const float distance = engine::Math::Length(object->GetWorldPosition() - player->GetWorldPosition());
        if (distance > kMaxLockOnDistance_ || distance < 0.1f)
            continue;
        const engine::Vector3 delta = object->GetWorldPosition() - camera->GetEye();
        const float cameraDistance = engine::Math::Length(delta);
        if (cameraDistance < 0.001f)
            continue;
        const float dot = engine::Math::Dot(forward, delta / cameraDistance);
        if (dot < 0.5f || !Visible(player, object.get()))
            continue;
        candidates.push_back({object.get(), (1 - dot) * 2 + distance / kMaxLockOnDistance_});
    }
    if (candidates.empty())
        return nullptr;
    // 手動切替は安定したシーン順で巡回し、カメラ移動で同じ2体を往復させません。
    if (next) {
        for (size_t i = 0; i < candidates.size(); ++i)
            if (candidates[i].object == lockOnTarget_)
                return candidates[(i + 1) % candidates.size()].object;
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const auto& a, const auto& b) { return a.score < b.score; });
    // 正面に近く、かつプレイヤーから近い候補を優先する。
    return candidates.front().object;
}
