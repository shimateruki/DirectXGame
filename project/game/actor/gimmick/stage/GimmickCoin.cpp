#include "GimmickCoin.h"
#include "CollisionConfig.h"
#include "GameDataManager.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "GroundEffectLocator.h"
#include <DebugConsole.h>
#include <algorithm>
#include <cmath>
#include <string>

namespace {
constexpr float kPi = 3.1415926535f;
constexpr float kCollectAnimationDuration = 0.58f;
constexpr float kDropGravity = 22.0f;
constexpr float kDropBounceRate = 0.42f;
constexpr float kDropGroundFriction = 0.86f;
constexpr float kDropStopSpeed = 0.45f;

json Vector3ToReplayJson(const engine::Vector3& value) {
    // Replay の保存形式を三要素配列に揃えます。
    return json::array({value.x, value.y, value.z});
}

engine::Vector3 ReplayJsonToVector3(const json& value, const engine::Vector3& fallback) {
    // 旧記録や欠損データでは現在の値を維持します。
    if (!value.is_array() || value.size() < 3) {
        return fallback;
    }
    return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
}
} // namespace

void GimmickCoin::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    // コインはプレイヤーだけが拾える球トリガーとして初期化します。
    SetCollisionAttribute(CollisionCategories::Get("trigger"));
    SetCollisionMask(CollisionCategories::Get("player"));
    SetColliderType(ColliderType::kSphere);
    SetCollisionRadius(1.0f);
    SetColor({1.0f, 0.9f, 0.0f, 1.0f});
    SetScale({0.6f, 0.6f, 0.15f});

    isCollected_ = false;
    isTemporaryDrop_ = false;
    collectAnimationTimer_ = 0.0f;
    dropAge_ = 0.0f;
    dropLifetime_ = 0.0f;
    dropBlinkStartTime_ = 0.0f;
    dropGroundY_ = GetTranslate().y;
    dropGroundOffset_ = 0.0f;
    dropSettleTimer_ = 0.0f;
    dropVelocity_ = {0.0f, 0.0f, 0.0f};
    collectStartPosition_ = GetTranslate();
    collectStartScale_ = GetScale();

    SetClassName("Gimmick");
    SetGimmickType("Coin");
}

void GimmickCoin::Update(float deltaTime) {
    // 取得時の跳ね上がりと、一時ドロップの落下・寿命は通常の回転より優先します。
    engine::Vector3 rotate = GetRotation();

    if (isCollected_) {
        collectAnimationTimer_ += deltaTime;
        const float t = std::clamp(collectAnimationTimer_ / kCollectAnimationDuration, 0.0f, 1.0f);
        const float easeOut = 1.0f - (1.0f - t) * (1.0f - t);
        const float hop = std::sin(t * kPi);

        engine::Vector3 position = collectStartPosition_;
        position.y += 1.25f * easeOut + 0.25f * hop;
        SetTranslate(position);

        rotate.y += (rotationSpeed_ + 18.0f) * deltaTime;
        rotate.z += 10.0f * deltaTime;
        SetRotation(rotate);

        const float popScale = 1.0f + 0.18f * hop;
        const float vanish = 1.0f - std::clamp((t - 0.55f) / 0.45f, 0.0f, 1.0f);
        SetScale({collectStartScale_.x * popScale * vanish, collectStartScale_.y * popScale * vanish,
                  collectStartScale_.z * popScale * vanish});
        SetColor({1.0f, 0.92f, 0.18f, vanish});

        if (t >= 1.0f) {
            SetIsVisible(false);
            isDead = true;
            RequestSelfRemove();
        }

        BaseGimmick::Update(deltaTime);
        return;
    }

    if (isTemporaryDrop_) {
        UpdateTemporaryDrop(deltaTime);
        if (isDead) {
            BaseGimmick::Update(deltaTime);
            return;
        }
    }

    rotate.y += rotationSpeed_ * deltaTime;
    if (rotate.y > kPi * 2.0f) {
        rotate.y -= kPi * 2.0f;
    }
    SetRotation(rotate);

    BaseGimmick::Update(deltaTime);
}

void GimmickCoin::ConfigureTemporaryDrop(const engine::Vector3& initialVelocity, float lifetime, float blinkStartTime,
                                         float groundY) {
    // モデルの底面と地面を揃え、落下中の見た目が地形へ埋まらないようにします。
    isTemporaryDrop_ = true;
    dropVelocity_ = initialVelocity;
    dropLifetime_ = (std::max)(0.2f, lifetime);
    dropBlinkStartTime_ = std::clamp(blinkStartTime, 0.0f, dropLifetime_);
    dropAge_ = 0.0f;
    UpdateLocalMatrix();
    UpdateWorldMatrix();
    const AABB bounds = GetModelWorldAABB();
    dropGroundOffset_ = (std::max)(0.03f, GetTranslate().y - bounds.min.y + 0.03f);
    dropGroundY_ = groundY + dropGroundOffset_;
    dropSettleTimer_ = 0.0f;
    SetColor({1.0f, 0.92f, 0.18f, 1.0f});
    SetIsVisible(true);
}

void GimmickCoin::UpdateTemporaryDrop(float deltaTime) {
    // 落下中のコインは地面への接触後に減速し、期限直前だけ点滅させます。
    dropAge_ += deltaTime;

    if (dropAge_ >= dropLifetime_) {
        SetCollisionAttribute(0);
        SetCollisionMask(0);
        SetIsVisible(false);
        isDead = true;
        RequestSelfRemove();
        return;
    }

    engine::Vector3 position = GetTranslate();
    const engine::Vector3 groundSample = {position.x, dropGroundY_ - dropGroundOffset_, position.z};
    dropGroundY_ = GroundEffectLocator::ResolveGroundPosition(groundSample).y + dropGroundOffset_;
    if (dropSettleTimer_ <= 0.0f) {
        dropVelocity_.y -= kDropGravity * deltaTime;
        position += dropVelocity_ * deltaTime;

        if (position.y <= dropGroundY_) {
            position.y = dropGroundY_;
            if (std::abs(dropVelocity_.y) > kDropStopSpeed) {
                dropVelocity_.y = -dropVelocity_.y * kDropBounceRate;
                dropVelocity_.x *= kDropGroundFriction;
                dropVelocity_.z *= kDropGroundFriction;
            } else {
                dropVelocity_.y = 0.0f;
                dropSettleTimer_ = 0.01f;
            }
        }
    } else {
        dropSettleTimer_ += deltaTime;
        dropVelocity_.x *= std::pow(kDropGroundFriction, deltaTime * 18.0f);
        dropVelocity_.z *= std::pow(kDropGroundFriction, deltaTime * 18.0f);
        position.x += dropVelocity_.x * deltaTime;
        position.z += dropVelocity_.z * deltaTime;
    }
    SetTranslate(position);

    const float remaining = dropLifetime_ - dropAge_;
    if (dropBlinkStartTime_ > 0.0f && remaining <= dropBlinkStartTime_) {
        const float blinkProgress = 1.0f - std::clamp(remaining / dropBlinkStartTime_, 0.0f, 1.0f);
        const float blinkSpeed = 8.0f + blinkProgress * 18.0f;
        const bool visible = std::sin(dropAge_ * blinkSpeed) >= -0.25f;
        SetIsVisible(visible);
        const float alpha = visible ? (0.45f + 0.55f * (1.0f - blinkProgress * 0.35f)) : 0.0f;
        SetColor({1.0f, 0.92f, 0.18f, alpha});
    } else {
        SetIsVisible(true);
        SetColor({1.0f, 0.92f, 0.18f, 1.0f});
    }
}

bool GimmickCoin::OnCollision(engine::Object3d* other) {
    // 取得済みなら同じ接触から枚数を二度加算しません。
    if (isCollected_) {
        return false;
    }

    if (other->GetCollisionAttribute() & CollisionCategories::Get("player")) {
        Collect();
        return true;
    }

    return false;
}

void GimmickCoin::Collect() {
    // 取得演出を開始してからセーブ側の枚数を更新します。
    if (isCollected_) {
        return;
    }

    isCollected_ = true;
    collectAnimationTimer_ = 0.0f;
    collectStartPosition_ = GetTranslate();
    collectStartScale_ = GetScale();
    SetCollisionAttribute(0);
    SetCollisionMask(0);

    GameDataManager::GetInstance()->AddCoin(1);

    const int currentCoins = GameDataManager::GetInstance()->GetCoins();
    const int currentLives = GameDataManager::GetInstance()->GetLives();
    DebugConsole::GetInstance()->AddLog("Coin Collected! (" + std::to_string(currentCoins) +
                                        "/100) Lives: " + std::to_string(currentLives));
}

void GimmickCoin::RequestSelfRemove() {
    // 描画・衝突の走査中に直接破棄せず、シーンの遅延削除へ依頼します。
    BaseScene* scene = SceneManager::GetInstance() ? SceneManager::GetInstance()->GetCurrentScene() : nullptr;
    if (scene) {
        scene->RequestRemoveObject(this);
    }
}

void GimmickCoin::CaptureReplayCustomState(json& state) const {
    // 取得演出と一時ドロップの進行を保存し、Replay 再開後も同じ位置から続けます。
    BaseGimmick::CaptureReplayCustomState(state);
    state["coinCollected"] = isCollected_;
    state["coinTemporaryDrop"] = isTemporaryDrop_;
    state["coinRotationSpeed"] = rotationSpeed_;
    state["coinCollectAnimationTimer"] = collectAnimationTimer_;
    state["coinDropAge"] = dropAge_;
    state["coinDropLifetime"] = dropLifetime_;
    state["coinDropBlinkStartTime"] = dropBlinkStartTime_;
    state["coinDropGroundY"] = dropGroundY_;
    state["coinDropGroundOffset"] = dropGroundOffset_;
    state["coinDropSettleTimer"] = dropSettleTimer_;
    state["coinDropVelocity"] = Vector3ToReplayJson(dropVelocity_);
    state["coinCollectStartPosition"] = Vector3ToReplayJson(collectStartPosition_);
    state["coinCollectStartScale"] = Vector3ToReplayJson(collectStartScale_);
}

void GimmickCoin::RestoreReplayCustomState(const json& state) {
    // 欠けたキーは既定値のままにして、古い Replay も読めるようにします。
    BaseGimmick::RestoreReplayCustomState(state);
    isCollected_ = state.value("coinCollected", isCollected_);
    isTemporaryDrop_ = state.value("coinTemporaryDrop", isTemporaryDrop_);
    rotationSpeed_ = state.value("coinRotationSpeed", rotationSpeed_);
    collectAnimationTimer_ = state.value("coinCollectAnimationTimer", collectAnimationTimer_);
    dropAge_ = state.value("coinDropAge", dropAge_);
    dropLifetime_ = state.value("coinDropLifetime", dropLifetime_);
    dropBlinkStartTime_ = state.value("coinDropBlinkStartTime", dropBlinkStartTime_);
    dropGroundY_ = state.value("coinDropGroundY", dropGroundY_);
    dropGroundOffset_ = state.value("coinDropGroundOffset", dropGroundOffset_);
    dropSettleTimer_ = state.value("coinDropSettleTimer", dropSettleTimer_);
    if (state.contains("coinDropVelocity")) {
        dropVelocity_ = ReplayJsonToVector3(state["coinDropVelocity"], dropVelocity_);
    }
    if (state.contains("coinCollectStartPosition")) {
        collectStartPosition_ = ReplayJsonToVector3(state["coinCollectStartPosition"], collectStartPosition_);
    }
    if (state.contains("coinCollectStartScale")) {
        collectStartScale_ = ReplayJsonToVector3(state["coinCollectStartScale"], collectStartScale_);
    }
}

std::unique_ptr<engine::Object3d> GimmickCoin::Clone() const {
    // 複製先の描画資源を初期化した上で、設定されたコインの見た目を引き継ぎます。
    auto newObj = std::make_unique<GimmickCoin>();
    assert(common_ != nullptr);
    newObj->Initialize(common_, GetModelName());
    newObj->CopyFrom(this);
    return newObj;
}
