#include "GimmickMovingFloor.h"
#include <algorithm>
#include <cmath>
#include "CollisionConfig.h"
#include "RidingPlatformController.h"
#include "MovingFloorSupport.h"
#include "GhostRecorder.h"
#include "BaseScene.h"
#include "SceneManager.h"
#include "Player.h"
#include "Math.h"
#include "game/actor/gimmick/combat/GimmickMagmaGeyser.h"

GimmickMovingFloor::GimmickMovingFloor() = default;
GimmickMovingFloor::~GimmickMovingFloor() = default;

bool GimmickMovingFloor::IsRidePlatform() const {
    // 乗車タイムラインがある床は手続き移動と別の制御経路にする。
    const auto* path = GetPathMoverComponent();
    return path && !path->GetRideSequence().empty();
}

namespace {
constexpr float kLegacyMoveRange = 3.0f;
constexpr float kMaxFloatAmplitude = 2.0f;
constexpr float kFloatPitchScale = 0.025f;
constexpr float kFloatRollScale = 0.035f;
constexpr float kTwoPi = 6.28318530718f;

bool HasCurvedSupportBelow(engine::Object3d& floor, const Player& player) {
    // 互換モーターは接地相手を保持しないため、曲面の実形状を足元だけ照会する。
    // AABB最高点との比較では、円柱が少し回っただけで乗車が途切れてしまう。
    if (auto* collider = floor.GetCollider(); collider && collider->HasModelVolume() &&
        (floor.GetCollisionAttribute() & CollisionCategories::Get("ground"))) {
        // 足の中心だけのレイでは、箱形の足の端が曲面に乗る接触を見落とす。
        // 実際の身体の幅を使い、押し戻し直後の隙間だけ下へ補う。
        auto bounds = player.GetAABB();
        bounds.min.y -= 0.08f;
        Transform queryTransform;
        queryTransform.translate = (bounds.min + bounds.max) * 0.5f;
        queryTransform.UpdateMatrix();
        Collider query(&queryTransform);
        ColliderConfig config;
        config.type = ColliderType::kAABB;
        config.size = (bounds.max - bounds.min) * 0.5f;
        query.SetConfig(config);
        const auto contact = query.CheckCollision(collider);
        if (contact.isColliding && contact.normal.y > 0.5f)
            return true;
    }
    for (auto* child : floor.GetChildren()) {
        if (child && HasCurvedSupportBelow(*child, player)) return true;
    }
    return false;
}
} // namespace

void GimmickMovingFloor::Initialize(engine::Object3dCommon* common, const std::string& modelName) {
    // 床として接地判定できる属性を与え、旧データの速度欠落を補う。
    BaseGimmick::Initialize(common, modelName);
    SetClassName("Gimmick");
    SetGimmickType("MovingFloor");

    if (!param_.has_value())
        param_.emplace();
    if (param_->speed == 0.0f)
        param_->speed = 2.0f; // デフォルト速度

    // 当たり判定設定 (地面として扱う)
    SetCollisionAttribute(CollisionCategories::Get("ground"));
}

void GimmickMovingFloor::Update(float deltaTime) {
    if (!SceneManager::GetInstance()->IsPlaying()) {
        // 編集中の配置を次回再生の原点にする。手続き移動でInspectorの座標を上書きしない。
        startPosition_ = GetTranslate();
        startRotation_ = GetRotation();
        hasCapturedStartTransform_ = true;
        time_ = 0.0f;
        frameDelta_ = {};
        if (param_ && param_->actionMode == 5)
            SetEmissive(0.10f);
        BaseGimmick::Update(deltaTime);
        return;
    }

    if (IsRidePlatform()) {
        if (!ride_)
            ride_ = std::make_unique<RidingPlatformController>();
        ride_->Update(*this, deltaTime);
        BaseGimmick::Update(deltaTime);
        return;
    }

    std::vector<Character*> passengers;
    bool playerRidingPath = false;
    auto* scene = SceneManager::GetInstance()->GetCurrentScene();

    if (scene && SceneManager::GetInstance()->IsPlaying() && deltaTime > 0.0f) {
        for (auto& object : scene->GetObjects()) {
            auto* actor = object && !object->IsReplayRemoved() ? dynamic_cast<Character*>(object.get()) : nullptr;
            const bool standing = actor && IsStandingOnMovingFloor(*this, *actor);
            if (standing) passengers.push_back(actor);
            // 既存の乗員走査で通知も集め、Pathごとの追加衝突照会を避ける。
            auto* player = dynamic_cast<Player*>(actor);
            if (player && !player->isDead && player->IsGrounded() && player->GetVelocity().y <= 0.1f) {
                const auto& ground = player->GetCharacterMotor().GetResult();
                if (ground.grounded) {
                    for (auto* support = ground.groundObject; support; support = support->GetParent())
                        if (support == this) playerRidingPath = true;
                }
                // 旧モーターで接地相手が得られない場合だけ上端照会へ戻す。
                if (!ground.groundObject && (standing || HasCurvedSupportBelow(*this, *player)))
                    playerRidingPath = true;
                // 円柱の斜面はAABBの最高点と一致しない。Motorで接地が確定した乗員も運ぶ。
                if (playerRidingPath && !standing) passengers.push_back(player);
            }
        }
    }

    const auto carryPassengers = [&]() {
        // 押し戻し後の再判定には依存せず、移動前に接地していた乗員を一度だけ運ぶ。
        for (auto* actor : passengers) {
            actor->SetTranslate(actor->GetTranslate() + frameDelta_);
            actor->UpdateLocalMatrix();
            actor->UpdateWorldMatrix(false);
            if (auto* player = dynamic_cast<Player*>(actor))
                player->SetPlatformMotion(frameDelta_ * (1.0f / deltaTime));
        }
    };

    // Ghost RecorderのPathが割り当てられている床は、記録済みの軌道を唯一の移動元にする。
    // 手続き的な往復移動を先に重ねると、Editorで調整したPathと乗り移り判定がずれる。
    if (!GetRecordPathName().empty()) {
        // 丸い床や傾斜でも、AABB上端ではなくMotorの接地相手で再生を判断する。
        if (recorder_) recorder_->SetPlayerRiding(playerRidingPath);
        const engine::Vector3 previousPosition = GetTransform()->translate;
        UpdateLocalMatrix();
        UpdateWorldMatrix(false);
        const auto previousWorld = GetWorldMatrix();
        const auto previousInverse = engine::Math::Inverse(previousWorld);
        BaseGimmick::Update(deltaTime);
        frameDelta_ = GetTransform()->translate - previousPosition;

        // RecorderはObject更新の終盤で座標を変える。乗員の接地照会より前に反映する。
        UpdateWorldMatrix(false);
        hasCapturedStartTransform_ = true;
        // 足の接触点を旧床座標から新床座標へ移す。円柱の回転でも乗員を置き去りにしない。
        // キャラクター自身を回転させず、ジャンプで離れた後はこの処理の対象外にする。
        for (auto* actor : passengers) {
            const auto worldPosition = actor->GetWorldPosition();
            const engine::Vector3 contact = {worldPosition.x, actor->GetAABB().min.y, worldPosition.z};
            const auto localContact = engine::Math::Transform(contact, previousInverse);
            const auto displacement = engine::Math::Transform(localContact, GetWorldMatrix()) - contact;
            const auto nextWorld = worldPosition + displacement;
            actor->SetTranslate(actor->GetParent()
                ? engine::Math::Transform(nextWorld, engine::Math::Inverse(actor->GetParent()->GetWorldMatrix()))
                : nextWorld);
            actor->UpdateLocalMatrix();
            actor->UpdateWorldMatrix(false);
            if (auto* player = dynamic_cast<Player*>(actor); player && deltaTime > 0.0f)
                player->SetPlatformMotion(displacement * (1.0f / deltaTime));
        }
        return;
    }

    // JSONのTransformはInitialize後に反映されるため、最初のUpdateで実際の配置を記録する。
    if (!hasCapturedStartTransform_) {
        startPosition_ = GetTransform()->translate;
        startRotation_ = GetTransform()->rotate;
        phaseOffset_ = std::fmod(std::abs(startPosition_.x * 0.037f + startPosition_.z * 0.061f), kTwoPi);
        hasCapturedStartTransform_ = true;
    }

    const engine::Vector3 previousPosition = GetTransform()->translate;
    time_ += deltaTime;

    const float speed = param_.has_value() ? (std::max)(param_->speed, 0.0f) : 0.0f;
    const int actionMode = param_.has_value() ? param_->actionMode : 0;

    // 配置位置を往復運動の中心として扱う。位相差は自然な浮遊モードだけに適用する。
    const float waveTime = time_ * speed + (actionMode == 1 ? phaseOffset_ : 0.0f);
    Transform transformValue = *GetTransform();
    Transform* transform = &transformValue;
    transform->translate = startPosition_;
    transform->rotate = startRotation_;

    if (actionMode == 5) {
        // Target IDで噴出口に接続する。未接続なら原点で待機する。
        if (scene && GetTargetID() >= 0 && SceneManager::GetInstance()->IsPlaying()) {
            for (const auto& object : scene->GetObjects()) {
                auto* geyser = dynamic_cast<GimmickMagmaGeyser*>(object.get());

                if (geyser && geyser->GetEventID() == GetTargetID()) {
                    SetEmissive(0.10f + geyser->GetWarningProgress() * 0.65f);
                    const float desiredY = startPosition_.y + geyser->GetLiftOffset();
                    const float maxStep = 12.0f * (std::max)(0.0f, deltaTime);
                    transform->translate.y =
                        std::clamp(desiredY, previousPosition.y - maxStep, previousPosition.y + maxStep);
                    break;
                }
            }
        }
    } else if (actionMode == 1) {
        // 溶岩に浮く足場用。移動床の位置を保ったまま、緩やかな浮き沈みと傾きだけを加える。
        const float amplitude = (std::clamp)(std::abs(param_->moveAmount), 0.0f, kMaxFloatAmplitude);
        const float tiltWeight = (std::min)(amplitude, 1.0f);
        transform->translate.y += std::sin(waveTime) * amplitude;
        transform->rotate.x += std::sin(waveTime * 0.73f + 1.2f) * kFloatPitchScale * tiltWeight;
        transform->rotate.z += std::sin(waveTime * 0.91f) * kFloatRollScale * tiltWeight;
        transform->isQuaternionMaster = false;
    } else if (actionMode >= 2 && actionMode <= 4) {
        // ステージ制作用の往復移動。振幅と速度はInspectorから調整できる。
        const float travel = (std::max)(0.0f, std::abs(param_->moveAmount));
        const float offset = std::sin(waveTime) * travel;
        if (actionMode == 2)
            transform->translate.x += offset;
        if (actionMode == 3)
            transform->translate.z += offset;
        if (actionMode == 4)
            transform->translate.y += offset;
    } else {
        // 従来ステージとの互換性を保つ通常の上下移動。
        transform->translate.y += std::sin(waveTime) * kLegacyMoveRange;
    }

    frameDelta_ = hasCapturedStartTransform_ ? transform->translate - previousPosition : engine::Vector3{};
    SetLocalTransform(transformValue);

    // 移動後のTransformを同じフレームで行列と当たり判定へ反映する。
    BaseGimmick::Update(deltaTime);
    carryPassengers();
}

bool GimmickMovingFloor::OnCollision(engine::Object3d* other) {
    // 全移動床で更新前に乗員を運ぶ。衝突処理は押し戻しだけにする。
    (void)other;
    return true;
}

std::unique_ptr<engine::Object3d> GimmickMovingFloor::Clone() const {
    // 複製時は配置と設定だけを引き継ぎ、乗員・周期・原点は再生時に再取得する。
    auto clone = std::make_unique<GimmickMovingFloor>();
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    return clone;
}

void GimmickMovingFloor::ResetEscapeCycle() {
    // 脱出床を保存済み原点へ戻し、前フレームの乗員移動量も消す。
    time_ = 0.0f;
    frameDelta_ = {};

    if (hasCapturedStartTransform_) {
        SetTranslate(startPosition_);
        UpdateLocalMatrix();
        UpdateWorldMatrix();
    }
}
