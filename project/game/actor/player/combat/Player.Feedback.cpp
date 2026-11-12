#include "game/actor/player/Player.Internal.h"
#include <numbers>
#include <string_view>
#include "ModelManager.h"
#include "engine/graphics/3d/MeshRenderer.h"

using namespace PlayerImplementation;

namespace {

constexpr float kPlayerHurtFaceSeconds = 0.18f;

struct PlayerHurtFaceModel {
    std::string_view baseName;
    std::string_view fileName;
    const char* hurtModel;
    bool includesBand;
};

// 通常モデル名とフルファイル名のどちらでも、同じ被弾素材へ対応づけます。
constexpr PlayerHurtFaceModel kPlayerHurtFaceModels[] = {
    {"Characters/slime", "Characters/slime/slime.gltf", "Characters/slime/slime_hurt.gltf", false},
    {"Characters/slime_pink", "Characters/slime_pink/slime_pink.gltf", "Characters/slime_pink/player_copy_pink_hurt.gltf", true},
    {"Characters/slime_red", "Characters/slime_red/slime_red.gltf", "Characters/slime_red/player_copy_fire_hurt.gltf", true},
    {"Characters/slime_yellow", "Characters/slime_yellow/slime_yellow.gltf", "Characters/slime_yellow/player_copy_thunder_hurt.gltf", true},
    {"Characters/slime_wind", "Characters/slime_wind/slime_wind.gltf", "Characters/slime_wind/player_copy_wind_hurt.gltf", true},
    {"Characters/slime_black", "Characters/slime_black/slime_black.gltf", "Characters/slime_black/player_copy_bomb_hurt.gltf", true},
    {"Characters/slime_crown", "Characters/slime_crown/slime_crown.gltf", "Characters/slime_crown/slime_crown_hurt.gltf", false},
    {"Characters/slime_crown/slime_crown_body.gltf", "Characters/slime_crown/slime_crown_body.gltf",
     "Characters/slime_crown/slime_crown_body_hurt.gltf", false},
};

} // namespace

void Player::PrepareHurtFaceModel() {
    Model* baseModel = GetModel();
    if (hurtFaceBaseModel_ == baseModel) {
        return;
    }

    // コピー解除などで本体が変わった場合は、前の形態の被弾顔を持ち越さない。
    ClearHurtFace();
    hurtFaceBaseModel_ = baseModel;
    hurtFaceModel_ = nullptr;
    hurtFaceIncludesBand_ = false;
    const std::string modelName = GetModelName();
    for (const auto& model : kPlayerHurtFaceModels) {
        if (modelName == model.baseName || modelName == model.fileName) {
            hurtFaceModel_ = ModelManager::GetInstance()->LoadModel(model.hurtModel);
            hurtFaceIncludesBand_ = model.includesBand;
            break;
        }
    }
}

void Player::StartHurtFace() {
    PrepareHurtFaceModel();
    if (!hurtFaceModel_ || GetHp() <= 0.0f || IsDead() || isCinematicLocked_ || enemyMorphReleaseActive_) {
        ClearHurtFace();
        return;
    }

    hurtFaceRemaining_ = kPlayerHurtFaceSeconds;
    UpdateHurtFace(0.0f);
}

void Player::UpdateHurtFace(float deltaTime) {
    PrepareHurtFaceModel();
    if (hurtFaceRemaining_ <= 0.0f) {
        return;
    }
    hurtFaceRemaining_ -= (std::max)(0.0f, deltaTime);
    if (hurtFaceRemaining_ <= 0.0f || GetHp() <= 0.0f || IsDead() || isCinematicLocked_ ||
        enemyMorphReleaseActive_ || gateReturnAnimation_.IsActive()) {
        ClearHurtFace();
        return;
    }

    // ムチ変形などが表示枠を使っている間は、終了後の更新まで差し替えを待つ。
    if (auto* renderer = GetMeshRenderer(); renderer && !renderer->GetVisualModelOverride()) {
        renderer->SetVisualModelOverride(hurtFaceModel_);
    }
}

void Player::ClearHurtFace() {
    hurtFaceRemaining_ = 0.0f;
    if (auto* renderer = GetMeshRenderer(); hurtFaceModel_ && renderer &&
        renderer->GetVisualModelOverride() == hurtFaceModel_) {
        renderer->SetVisualModelOverride(nullptr);
    }
}

// 元の更新順序と判定を維持して、担当処理をまとめています。
void Player::StartDamageFeedback(const engine::Vector3& knockbackDirection, float invincibleDuration) {
    // 被弾で通常攻撃を中断し、重なった無敵時間は短くしない。
    if (tutorialSafetyEnabled_ || IsDead() || isCinematicLocked_ || electricShockControlLocked_) {
        return;
    }

    if (baseCombatController_) {
        baseCombatController_->Cancel(*this, false);
    }

    // 近接が確保した操作権を戻してから、被弾側が操作不能を引き受けます。
    if (copyAbilityController_)
        copyAbilityController_->NotifyDamageStarted(*this);
    damageCooldownTimer_ = (std::max)(damageCooldownTimer_, (std::max)(0.0f, invincibleDuration));
    SetDamageInvincible(damageCooldownTimer_ > 0.0f);
    ChangeState(std::make_unique<PlayerStateDamage>(knockbackDirection));
}

void Player::StartElectricShockFeedback(float duration, float invincibleDuration) {
    // 感電開始時の位置・姿勢・操作状態を退避し、終了時に戻せるようにする。
    if (tutorialSafetyEnabled_) {
        return;
    }

    duration = (std::max)(0.0f, duration);
    electricShockPendingInvincibleDuration_ =
        (std::max)(electricShockPendingInvincibleDuration_, (std::max)(0.0f, invincibleDuration));

    if (duration <= 0.0f) {
        EndElectricShockFeedback();
        return;
    }

    if (copyAbilityController_)
        copyAbilityController_->NotifyDamageStarted(*this);
    RestoreDamageBlinkVisibility();

    if (!electricShockControlLocked_) {
        electricShockWasControlActive_ = isControlActive_;
        electricShockLockedPosition_ = GetTranslate();
        electricShockBaseScale_ = GetScale();
        electricShockBaseRotation_ = GetRotation();
        electricShockControlLocked_ = true;
    }

    electricShockFeedbackTimer_ = (std::max)(electricShockFeedbackTimer_, duration);
    electricShockFeedbackTotalDuration_ = (std::max)(electricShockFeedbackTotalDuration_, electricShockFeedbackTimer_);
    electricShockFeedbackEmitTimer_ = 0.0f;

    SetIsControlActive(false);
    SetVelocity({0.0f, 0.0f, 0.0f});
    SetTranslate(electricShockLockedPosition_);
    UpdateElectricShockAuraEffect(1.0f / 60.0f);
}

void Player::UpdateElectricShockFeedback(float deltaTime) {
    // 感電中は固定位置の周囲で小さく揺らし、残り時間に応じて発光を更新する。
    if (electricShockFeedbackTimer_ <= 0.0f || deltaTime <= 0.0f) {
        return;
    }

    const float previousShockTimer = electricShockFeedbackTimer_;
    electricShockFeedbackTimer_ = (std::max)(0.0f, electricShockFeedbackTimer_ - deltaTime);
    electricShockFeedbackEmitTimer_ -= deltaTime;

    const float totalDuration = (std::max)(electricShockFeedbackTotalDuration_, 0.001f);
    const float elapsed = (std::max)(0.0f, totalDuration - electricShockFeedbackTimer_);
    const float flicker = std::sin(elapsed * 74.0f);
    const engine::Vector4 base = isEnemyMorphed_ ? GetEnemyMorphTint(enemyMorphType_) : engine::Vector4{1.0f, 1.0f, 1.0f, 1.0f};
    SetColor({(std::clamp)(base.x + 0.24f * std::max(0.0f, flicker), 0.0f, 1.0f),
              (std::clamp)(base.y + 0.18f, 0.0f, 1.0f),
              (std::clamp)(base.z + 0.42f * std::max(0.0f, -flicker), 0.0f, 1.0f), base.w});

    if (electricShockControlLocked_) {
        engine::Vector3 shockPosition = electricShockLockedPosition_;
        shockPosition.x += std::sin(elapsed * 94.0f) * kElectricShockShakeAmount;
        shockPosition.z += std::cos(elapsed * 83.0f) * kElectricShockShakeAmount * 0.75f;
        SetTranslate(shockPosition);
        SetVelocity({0.0f, 0.0f, 0.0f});

        const float squash = 0.055f + std::abs(std::sin(elapsed * 46.0f)) * 0.075f;
        SetScale({electricShockBaseScale_.x * (1.0f + squash), electricShockBaseScale_.y * (1.0f - squash * 0.65f),
                  electricShockBaseScale_.z * (1.0f + squash * 0.9f)});

        engine::Vector3 shockRotation = electricShockBaseRotation_;
        shockRotation.x += std::sin(elapsed * 67.0f) * 0.045f;
        shockRotation.z += std::sin(elapsed * 89.0f) * 0.075f;
        SetRotation(shockRotation);
    }

    UpdateElectricShockAuraEffect(deltaTime);

    if (electricShockFeedbackEmitTimer_ <= 0.0f) {
        EmitOuterThunderParticles(GetWorldPosition(), GetScale(), elapsed * 3.4f, 5);
        electricShockFeedbackEmitTimer_ = 0.045f;
    }

    if (previousShockTimer > 0.0f && electricShockFeedbackTimer_ <= 0.0f) {
        EndElectricShockFeedback();
    }
}

void Player::EndElectricShockFeedback() {
    // 元の操作と姿勢を戻し、感電後の無敵時間をここで適用する。
    if (electricShockControlLocked_) {
        SetTranslate(electricShockLockedPosition_);
        SetVelocity({0.0f, 0.0f, 0.0f});
        SetScale(electricShockBaseScale_);
        SetRotation(electricShockBaseRotation_);
        SetIsControlActive(!IsDead() && electricShockWasControlActive_);
        electricShockControlLocked_ = false;
    }

    electricShockFeedbackTimer_ = 0.0f;
    electricShockFeedbackEmitTimer_ = 0.0f;
    electricShockFeedbackTotalDuration_ = 0.0f;
    HideElectricShockAuraEffect();
    UpdateColor();

    const float invincibleDuration = electricShockPendingInvincibleDuration_;
    electricShockPendingInvincibleDuration_ = 0.0f;

    if (invincibleDuration > 0.0f && !IsDead()) {
        damageCooldownTimer_ = (std::max)(damageCooldownTimer_, invincibleDuration);
        invincibleBlinkTimer_ = 0.0f;
        SetDamageInvincible(true);
    }
}

void Player::InitializeElectricShockAuraEffect() {
    // エフェクトJSONの読込に失敗した場合は、半端なObjectを保持しない。
    if (!GetObjectCommon() || electricShockAuraEffect_) {
        return;
    }

    electricShockAuraEffect_ = std::make_unique<EffectObject3D>();
    electricShockAuraEffect_->Initialize(GetObjectCommon());
    electricShockAuraEffect_->SetName("Player_ElectricShockAura");

    if (!electricShockAuraEffect_->LoadFromJson(kElectricShockAuraEffectPath)) {
        electricShockAuraEffect_.reset();
        return;
    }

    electricShockAuraEffect_->SetIsVisible(false);
    electricShockAuraEffect_->Play(99999.0f);
}

void Player::UpdateElectricShockAuraEffect(float deltaTime) {
    // Playerの大きさへ合わせた後、可能ならカメラへ向けてオーラを表示する。
    if (!electricShockAuraEffect_) {
        InitializeElectricShockAuraEffect();
    }

    if (!electricShockAuraEffect_) {
        return;
    }

    engine::Vector3 auraPos{};
    float horizontalDiameter = 1.0f;
    float verticalDiameter = 1.0f;
    CalculateElectricShockAuraShape(auraPos, horizontalDiameter, verticalDiameter);

    float yaw = GetRotation().y;
    Camera* camera = CameraManager::GetInstance() ? CameraManager::GetInstance()->GetActiveCamera() : nullptr;

    if (camera) {
        const engine::Vector3 toCamera = camera->GetEye() - auraPos;

        if (std::abs(toCamera.x) + std::abs(toCamera.z) > 0.001f) {
            yaw = std::atan2(toCamera.x, toCamera.z);
        }
    }

    electricShockAuraEffect_->SetIsVisible(true);
    electricShockAuraEffect_->Update(deltaTime);
    electricShockAuraEffect_->SetTranslate(auraPos);
    electricShockAuraEffect_->SetRotation({(std::numbers::pi_v<float> * 0.5f), yaw, 0.0f});
    electricShockAuraEffect_->SetScale({horizontalDiameter, 1.0f, verticalDiameter});
    electricShockAuraEffect_->UpdateLocalMatrix();
    electricShockAuraEffect_->UpdateWorldMatrix();
}

void Player::HideElectricShockAuraEffect() {
    // 感電終了時にObjectを再生成せず、表示だけを止める。
    if (electricShockAuraEffect_) {
        electricShockAuraEffect_->SetIsVisible(false);
    }
}

void Player::CalculateElectricShockAuraShape(engine::Vector3& center, float& horizontalDiameter,
                                             float& verticalDiameter) const {
    // 変身中の拡縮を反映しつつ、極小サイズでもオーラが消えない下限を使う。
    const engine::Vector3 scale = GetScale();
    const float maxXZ = (std::max)({std::abs(scale.x), std::abs(scale.z), 1.0f});
    const float yScale = (std::max)(std::abs(scale.y), 0.18f);

    horizontalDiameter = (std::max)(2.55f, maxXZ * 1.35f);
    verticalDiameter = (std::max)(0.46f, yScale * 1.28f);

    center = GetWorldPosition();
    center.y += (std::max)(0.22f, yScale * 0.43f);
}

Player::EnemyMorphType Player::ResolveEnemyMorphType(const std::string& enemyType) const {
    if (enemyType == "Crown")
        return EnemyMorphType::Crown;
    if (enemyType == "Bomber")
        return EnemyMorphType::Bomber;

    // 巨大スライムはフックで分裂して小型化するため、直接コピー対象にはしません。
    if (enemyType == "GiantSlime")
        return EnemyMorphType::None;
    if (enemyType == "FireSlime")
        return EnemyMorphType::FireSlime;
    if (enemyType == "ThunderSlime")
        return EnemyMorphType::ThunderSlime;
    if (enemyType == "WindSlime")
        return EnemyMorphType::WindSlime;
    if (enemyType == "Slime")
        return EnemyMorphType::Slime;
    return EnemyMorphType::None;
}

engine::Vector4 Player::GetEnemyMorphTint(EnemyMorphType type) const {
    // 感電点滅の基準色を能力ごとに変え、解除後の外観と一致させる。
    switch (type) {
    case EnemyMorphType::Crown:
        return {0.58f, 0.89f, 1.0f, 1.0f};

    case EnemyMorphType::Bomber:
        return {1.0f, 0.45f, 0.22f, 1.0f};

    case EnemyMorphType::FireSlime:
        return {1.0f, 0.24f, 0.16f, 1.0f};

    case EnemyMorphType::ThunderSlime:
        return {1.0f, 0.92f, 0.18f, 1.0f};

    case EnemyMorphType::WindSlime:
        return {0.42f, 1.0f, 0.82f, 1.0f};

    case EnemyMorphType::Slime:
        return {1.0f, 0.46f, 0.86f, 1.0f};

    default:
        return {1.0f, 1.0f, 1.0f, 1.0f};
    }
}
