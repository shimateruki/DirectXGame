#include "GimmickSinkingFloor.h"
#include "CollisionConfig.h"
#include "MovingFloorSupport.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "Player.h"
#include <algorithm>

void GimmickSinkingFloor::Initialize(Object3dCommon* common, const std::string& modelName) {
    // プレイヤーが乗れる床として衝突属性を設定する。
    BaseGimmick::Initialize(common, modelName);

    SetClassName("Gimmick");
    SetGimmickType("SinkingFloor");
    SetName("Gimmick_SinkingFloor");
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
    // 予告・下降・滞在・上昇を段階で管理し、再生停止時は元の高さへ戻す。
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
            if (actor && IsStandingOnMovingFloor(*this, *actor))
                riders.push_back(actor);
        }
    }
    const float dt = (std::max)(0.0f, deltaTime);
    const float depth = (std::max)(0.1f, param_->moveAmount);
    const float speed = (std::max)(0.1f, param_->speed);
    const engine::Vector3 before = GetTranslate();
    engine::Vector3 next = before;
    if (submergePhase_ == 0 && !riders.empty()) {
        submergePhase_ = 1;
        submergeTimer_ = 0.0f;
    }
    if (submergePhase_ == 1) {
        submergeTimer_ += dt;
        if (submergeTimer_ >= (std::max)(0.0f, param_->shakeDuration))
            submergePhase_ = 2;
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
