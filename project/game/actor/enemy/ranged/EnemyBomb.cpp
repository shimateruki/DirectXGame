#define NOMINMAX
#include "EnemyBomb.h"
#include "game/actor/player/Player.h"
#include "PlayerState.h"
#include "Event.h"          
#include "EventManager.h" 
#include "CollisionConfig.h"
#include "CollisionManager.h"
#include "json.hpp"
#include "HitEffectDirector.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <MeshEffectManager.h>
#include <DebugConsole.h>

namespace {
// 辷・匱遽・峇縲∬ｻ｢縺後ｊ縲∝●豁｢蛻､螳壹・隱ｿ謨ｴ蛟､
constexpr const char* kExplosionEffectPath = "Resources/json/effect/effect_bakuhatu.json";
constexpr float kFallbackExplosionRadius = 3.0f;
constexpr float kGroundFriction = 0.985f;
constexpr float kStopSpeed = 0.02f;
constexpr float kBombVisualScale = 0.30f;
constexpr float kBombWorldCollisionRadius = 0.48f;
constexpr float kBombCollisionRadius = kBombWorldCollisionRadius / kBombVisualScale;
constexpr float kBombSweepSkin = 0.08f;
constexpr float kBombLandingBounce = 0.22f;
constexpr float kBombLandingFriction = 0.72f;
constexpr float kBombWallBounce = 0.35f;

float MaxScaleComponent(const nlohmann::json& values) {
    if (!values.is_array() || values.size() < 3) return 1.0f;
    return std::max({ values[0].get<float>(), values[1].get<float>(), values[2].get<float>() });
}

float LoadExplosionRadiusFromEffectJson() {
    std::ifstream file(kExplosionEffectPath);
    if (!file.is_open()) return kFallbackExplosionRadius;

    nlohmann::json effectJson;
    try {
        file >> effectJson;
    } catch (...) {
        return kFallbackExplosionRadius;
    }

    const float endScale = effectJson.contains("EndScale") ? MaxScaleComponent(effectJson["EndScale"]) : 1.0f;
    if (effectJson.contains("Collision") && effectJson["Collision"].is_object()) {
        const auto& collision = effectJson["Collision"];
        if (collision.value("HasCollision", false) && collision.value("Shape", -1) == 0 && collision.contains("Size")) {
            return collision["Size"][0].get<float>() * endScale;
        }
    }
    if (effectJson.contains("SphereRadius")) {
        return effectJson["SphereRadius"].get<float>() * endScale;
    }
    return kFallbackExplosionRadius;
}
}
void EnemyBomb::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);
    managedBaseScale_ = { kBombVisualScale, kBombVisualScale, kBombVisualScale };
    SetScale(managedBaseScale_);
    
    state_ = State::Chase;
    fuseTimer_ = 3.0f; // 辷・匱縺吶ｋ譎る俣繧貞ｰ代＠蟒ｶ髟ｷ・・遘抵ｼ・
    flashTimer_ = 0.0f;
    pulseTimer_ = 0.0f;
    flashState_ = false;
    isThrown_ = false;
    isAbilityExecuted_ = false;

    SetColliderType(ColliderType::kSphere);
    SetCollisionRadius(kBombCollisionRadius);
    // 蠖薙◆繧雁愛螳壹・蛻晄悄螻樊ｧ縺ｨ繝槭せ繧ｯ險ｭ螳夲ｼ磯㍽逕溘・謨ｵ螻樊ｧ・・
    SetCollisionAttribute(kEnemy);
    SetCollisionMask(kPlayer | kAllSolid | kPlayerAttack | kAttributePlayerBullet);
}

void EnemyBomb::ApplyManagedScale(const Vector3& scale) {
    managedBaseScale_ = scale;
    SetScale(scale);
}

// 霑ｽ霍｡縲∫せ轣ｫ縲∫・逋ｺ繧ｹ繝・・繝医ｒ縺ｾ縺ｨ繧√※譖ｴ譁ｰ縺吶ｋ
void EnemyBomb::Update(float deltaTime) {
    if (state_ == State::Exploded) {
        // 辷・匱螳御ｺ・ｾ後・蠖薙◆繧雁愛螳壹ｒ螳悟・縺ｫ辟｡蜉ｹ蛹悶＠縺ｦ髱櫁｡ｨ遉ｺ縺ｫ縺吶ｋ
        SetCollisionAttribute(0);
        SetCollisionMask(0);
        SetIsVisible(false);
        return;
    }

    const Vector3 previousPosition = GetTranslate();

    // 繝励Ξ繧､繝､繝ｼ縺ｫ謗ｴ縺ｾ繧後◆迸ｬ髢薙∝叉蠎ｧ縺ｫ轤ｹ轣ｫ・・gnited・臥憾諷九∈遘ｻ陦後☆繧・
    if (isCarried_ && state_ != State::Ignited) {
        state_ = State::Ignited;
        fuseTimer_ = 3.0f; // 辷・匱譎る俣繧・遘偵↓繝ｪ繧ｻ繝・ヨ
        pulseTimer_ = 0.0f;
        flashTimer_ = 0.0f;
        flashState_ = false;
    }

    if (isCarried_) {
        // 謗ｴ縺ｾ繧後※縺・ｋ髢薙・遘ｻ蜍輔ｄ驥榊鴨蜃ｦ逅・・陦後ｏ縺ｪ縺・′縲√き繧ｦ繝ｳ繝医ム繧ｦ繝ｳ縺ｨ貍泌・縺ｯ譖ｴ譁ｰ縺吶ｋ
        UpdateIgnited(deltaTime);
    } else if (IsThrownPhysics()) {
        UpdateIgnited(deltaTime);
        if (state_ != State::Exploded) {
            BaseEnemy::Update(deltaTime);
            ResolveSweptCollision(previousPosition);
        }
    } else {
        // 騾壼ｸｸ譎ゑｼ亥慍髱｢縺ｫ縺・ｋ縲√∪縺溘・謚輔￡繧峨ｌ縺溽憾諷具ｼ・
        // 騾溷ｺｦ縺ｫ驥榊鴨繧帝←逕ｨ縺吶ｋ
        if (param_.has_value()) {
            velocity_.y -= param_.value().gravity * deltaTime;
        } else {
            velocity_.y -= 50.0f * deltaTime; // 繝輔か繝ｼ繝ｫ繝舌ャ繧ｯ
        }

        // 謗･蝨ｰ縺励※縺・ｋ蝣ｴ蜷医°縺､縲∵兜縺偵ｉ繧後◆蠕後・縺ｿ縲√ヵ繝ｪ繧ｯ繧ｷ繝ｧ繝ｳ・亥慍髱｢縺ｮ鞫ｩ謫ｦ・峨ｒ驕ｩ逕ｨ縺励※蟆代＠霆｢縺後▲縺ｦ縺九ｉ閾ｪ辟ｶ縺ｫ豁｢縺ｾ繧九ｈ縺・↓縺吶ｋ
        // 窶ｻ驥守函縺ｮ霑ｽ蟆ｾ豁ｩ陦梧凾・・peed = 0.04f・峨・鞫ｩ謫ｦ縺ｧ縺九″豸医＆繧後↑縺・ｈ縺・↓縺励∪縺吶・
        if (isGrounded_ && isThrown_) {
            // 逹蝨ｰ蠕後↓蟆代＠髟ｷ繧√↓霆｢縺後☆
            velocity_.x *= kGroundFriction;
            velocity_.z *= kGroundFriction;

            if (std::abs(velocity_.x) < kStopSpeed) velocity_.x = 0.0f;
            if (std::abs(velocity_.z) < kStopSpeed) velocity_.z = 0.0f;

            // 螳悟・縺ｫ髱呎ｭ｢縺励◆繧画兜謫ｲ迥ｶ諷九ｒ隗｣髯､縺吶ｋ
            if (velocity_.x == 0.0f && velocity_.z == 0.0f) {
                isThrown_ = false;
            }

            // 蝨ｰ髱｢繧定ｻ｢縺後ｋ逅・ｽ薙・繝薙ず繝･繧｢繝ｫ蝗櫁ｻ｢貍泌・
            float speedXZ = std::sqrt(velocity_.x * velocity_.x + velocity_.z * velocity_.z);
            if (speedXZ > 0.05f) {
                // 騾ｲ陦梧婿蜷代・繝吶け繝医Ν繧貞叙蠕・
                float dirX = velocity_.x / speedXZ;
                float dirZ = velocity_.z / speedXZ;

                // 騾ｲ陦梧婿蜷代ｒ蜷代°縺帙ｋ (Y霆ｸ蝗櫁ｻ｢)
                SetRotationY(std::atan2(dirX, dirZ));

                // 霆｢縺後ｋ蝗櫁ｻ｢繧｢繝九Γ繝ｼ繧ｷ繝ｧ繝ｳ (X霆ｸ蝗櫁ｻ｢縺ｫ騾ｲ陦碁溷ｺｦ蛻・・蝗櫁ｻ｢繧貞刈邂・
                float rollDelta = speedXZ * deltaTime * 0.5f; // 逅・′繧ｴ繝ｭ繧ｴ繝ｭ霆｢縺後ｋ騾溘＆
                Vector3 rot = GetRotation();
                rot.x += rollDelta;
                SetRotation(rot);
            }
        }

        // 繧ｹ繝・・繝医・繧ｷ繝ｳ譖ｴ譁ｰ
        switch (state_) {
        case State::Chase:
            UpdateChase(deltaTime);
            break;
        case State::Ignited:
            UpdateIgnited(deltaTime);
            break;
        }

        // 遘ｻ蜍募宛髯舌ｄ蠎ｧ讓呎峩譁ｰ縺ｯ BaseEnemy::Update 縺瑚｡後▲縺ｦ縺上ｌ繧・
        BaseEnemy::Update(deltaTime);
        ResolveSweptCollision(previousPosition);
    }
}

// 轤ｹ轣ｫ蜑阪・繝励Ξ繧､繝､繝ｼ霑ｽ霍｡
void EnemyBomb::ResolveSweptCollision(const Vector3& previousPosition) {
    if (state_ == State::Exploded || isCarried_) {
        return;
    }

    CollisionManager* collisionManager = CollisionManager::GetInstance();
    if (!collisionManager) {
        return;
    }

    const Vector3 currentPosition = GetTranslate();
    Vector3 movement = {
        currentPosition.x - previousPosition.x,
        currentPosition.y - previousPosition.y,
        currentPosition.z - previousPosition.z
    };

    const float movementLength = std::sqrt(
        movement.x * movement.x +
        movement.y * movement.y +
        movement.z * movement.z
    );
    if (movementLength <= 0.001f) {
        return;
    }

    Vector3 direction = {
        movement.x / movementLength,
        movement.y / movementLength,
        movement.z / movementLength
    };

    const float sweepLength = movementLength + kBombWorldCollisionRadius + kBombSweepSkin;
    RaycastHit hit = collisionManager->Raycast(previousPosition, direction, sweepLength, kAllSolid);
    if (!hit.isHit) {
        return;
    }

    Vector3 normal = hit.normal;
    float normalLength = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    if (normalLength <= 0.001f) {
        if (direction.y < -0.15f) {
            normal = { 0.0f, 1.0f, 0.0f };
            normalLength = 1.0f;
        } else {
            normal = { -direction.x, 0.0f, -direction.z };
            normalLength = std::sqrt(normal.x * normal.x + normal.z * normal.z);
            if (normalLength <= 0.001f) {
                normal = { 0.0f, 1.0f, 0.0f };
                normalLength = 1.0f;
            }
        }
    }

    normal.x /= normalLength;
    normal.y /= normalLength;
    normal.z /= normalLength;

    const float separation = kBombWorldCollisionRadius + kBombSweepSkin;
    SetTranslate({
        hit.hitPoint.x + normal.x * separation,
        hit.hitPoint.y + normal.y * separation,
        hit.hitPoint.z + normal.z * separation
    });

    const float velocityAlongNormal =
        velocity_.x * normal.x +
        velocity_.y * normal.y +
        velocity_.z * normal.z;

    if (normal.y > 0.35f) {
        isGrounded_ = true;
        if (velocity_.y < 0.0f) {
            velocity_.y = -velocity_.y * kBombLandingBounce;
        }
        if (std::abs(velocity_.y) < 0.85f) {
            velocity_.y = 0.0f;
        }
        velocity_.x *= kBombLandingFriction;
        velocity_.z *= kBombLandingFriction;
    } else if (velocityAlongNormal < 0.0f) {
        const float reflectionScale = (1.0f + kBombWallBounce) * velocityAlongNormal;
        velocity_.x -= normal.x * reflectionScale;
        velocity_.y -= normal.y * reflectionScale;
        velocity_.z -= normal.z * reflectionScale;
    }
}
void EnemyBomb::UpdateChase(float deltaTime) {
    if (!target_) return;
    Vector3 playerPos = target_->GetTranslate();
    Vector3 myPos = GetTranslate();
    Vector3 toPlayer = playerPos - myPos;
    toPlayer.y = 0.0f; // 鬮倅ｽ主ｷｮ縺ｯ辟｡隕悶＠縺ｦ豌ｴ蟷ｳ遘ｻ蜍・

    float distance = std::sqrt(toPlayer.x * toPlayer.x + toPlayer.z * toPlayer.z);

    if (UpdateNoticeReaction(deltaTime, distance, detectionRange_, toPlayer)) {
        velocity_.x = 0.0f;
        velocity_.z = 0.0f;
        if (distance > 0.001f) {
            SetRotationY(std::atan2(toPlayer.x, toPlayer.z));
        }
        return;
    }

    if (distance > detectionRange_) {
        const float wanderSpeed = param_.has_value() ? (std::max)(0.35f, param_.value().speed * 4.0f) : 0.55f;
        Vector3 wanderVelocity = CalculateWanderVelocity(deltaTime, wanderSpeed, 0.65f);
        velocity_.x = wanderVelocity.x;
        velocity_.z = wanderVelocity.z;

        const float speedXZ = std::sqrt(velocity_.x * velocity_.x + velocity_.z * velocity_.z);
        if (speedXZ > 0.05f) {
            SetRotationY(std::atan2(velocity_.x, velocity_.z));
        }
        return;
    }

    // 荳螳夊ｷ晞屬蜀・↓蜈･縺｣縺溘ｉ轤ｹ轣ｫ・・gnited・峨↓遘ｻ陦・
    if (distance <= 4.0f) {
        state_ = State::Ignited;
        fuseTimer_ = 3.0f; // 辷・匱譎る俣繧・遘偵↓險ｭ螳・
        pulseTimer_ = 0.0f;
        flashTimer_ = 0.0f;
        flashState_ = false;
        velocity_.x = 0.0f;
        velocity_.z = 0.0f;
        return;
    }

    // 縺倥ｏ縺倥ｏ繝励Ξ繧､繝､繝ｼ縺ｸ霑代▼縺・
    if (distance > 0.1f && param_.has_value()) {
        toPlayer.x /= distance;
        toPlayer.z /= distance;
        velocity_.x = toPlayer.x * param_.value().speed;
        velocity_.z = toPlayer.z * param_.value().speed;

        // 騾ｲ陦梧婿蜷代ｒ蜷代￥
        SetRotationY(std::atan2(toPlayer.x, toPlayer.z));
    }
}

// 轤ｹ轣ｫ蠕後・繧ｫ繧ｦ繝ｳ繝医ム繧ｦ繝ｳ縺ｨ轤ｹ貊・・鮠灘虚貍泌・
void EnemyBomb::UpdateIgnited(float deltaTime) {
    // 騾溷ｺｦ繧貞ｾ舌・↓貂幄｡ｰ・育せ轣ｫ縺励◆繧臥ｫ九■豁｢縺ｾ繧九√∪縺溘・謚輔￡繧峨ｌ縺滓・諤ｧ縺ｧ貊代ｋ・・
    // 謗･蝨ｰ譎ゑｼ・sGrounded_ == true・峨・霆｢縺後ｊ繝輔Μ繧ｯ繧ｷ繝ｧ繝ｳ縺ｯ Update() 蛛ｴ縺ｧ繧医ｊ貊代ｉ縺九↓陦後≧縺溘ａ縲・
    // 縺薙％縺ｧ縺ｯ遨ｺ荳ｭ縺ｪ縺ｩ髱樊磁蝨ｰ譎ゅ°縺､譛ｪ謚墓憧譎ゅ・繝悶Ξ繝ｼ繧ｭ縺ｮ縺ｿ陦後＞縺ｾ縺吶・
    if (!isCarried_ && !isThrown_ && !isGrounded_) {
        velocity_.x *= 0.8f;
        velocity_.z *= 0.8f;
    }

    // 繧ｫ繧ｦ繝ｳ繝医ム繧ｦ繝ｳ騾ｲ陦・
    fuseTimer_ -= deltaTime;

    if (fuseTimer_ <= 0.0f) {
        Explode();
        return;
    }

    // --- 貍泌・驛ｨ 1: 蟆守↓邱夂せ轣ｫ繧定｡ｨ迴ｾ縺吶ｋ襍､轤ｹ貊・ｼ泌・・・.0遘偵せ繧ｱ繝ｼ繝ｫ蟇ｾ蠢懶ｼ・---
    flashTimer_ += deltaTime;
    // 辷・匱縺瑚ｿｫ繧九⊇縺ｩ轤ｹ貊・′騾溘￥縺ｪ繧・
    float flashLimit = (fuseTimer_ / 3.0f) * 0.2f + 0.03f;
    if (flashTimer_ >= flashLimit) {
        flashTimer_ = 0.0f;
        flashState_ = !flashState_;
        
        if (flashState_) {
            // 繝峨け繝ｳ縺ｨ蜈峨ｋ迸ｬ髢難ｼ亥ｼｷ縺・ｵ､濶ｲ縺ｫ繧ｪ繝ｼ繝舌・繝ｩ繧､繝茨ｼ・
            SetColor({2.0f, 0.2f, 0.2f, 1.0f});
        } else {
            // 騾壼ｸｸ縺ｮ濶ｲ縺ｫ謌ｻ縺・
            SetColor(defaultColor_);
        }
    }

    // --- 貍泌・驛ｨ 2: 辷・匱蟇ｸ蜑阪・繝峨け繝ｳ繝峨け繝ｳ縺ｨ豕｢謇薙▽鮠灘虚・・.0遘偵せ繧ｱ繝ｼ繝ｫ蟇ｾ蠢懶ｼ・---
    // 辷・匱縺ｫ霑代▼縺上↓縺､繧後※鮠灘虚蜻ｨ豕｢謨ｰ繧剃ｸ翫￡繧・
    float pulseSpeed = (3.4f - fuseTimer_) * 3.0f;
    pulseTimer_ += deltaTime * pulseSpeed;

    // 辷・匱逶ｴ蜑阪⊇縺ｩ蟆代＠縺縺題・繧峨・縲ょ､ｧ縺阪↑貎ｰ繧悟､牙ｽ｢縺ｯ霆｢縺後ｊ縺ｨ蟷ｲ貂峨＠縺ｦ荳崎・辟ｶ縺ｫ隕九∴繧九・
    float progress = 1.0f - (fuseTimer_ / 3.0f);
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    float amplitude = 0.015f + 0.045f * progress;
    float offset = std::sin(pulseTimer_) * amplitude;
    const float scaleRate = 1.0f + offset;
    
    // 逅・ｽ薙Δ繝・Ν縺瑚ｻ｢縺後ｋ蝗櫁ｻ｢縺ｨ蟷ｲ貂峨＠縺ｪ縺・ｈ縺・√せ繧ｱ繝ｼ繝ｫ・磯ｼ灘虚縺ｮ莨ｸ邵ｮ・峨・縺ｿ繧呈峩譁ｰ縺励∪縺・
    SetScale({
        managedBaseScale_.x * scaleRate,
        managedBaseScale_.y * scaleRate,
        managedBaseScale_.z * scaleRate
    });
}
void EnemyBomb::Explode() {
    state_ = State::Exploded;
    SetScale(managedBaseScale_);

    // 繝｢繝・Ν繧貞叉蠎ｧ縺ｫ髱櫁｡ｨ遉ｺ縺ｫ縺吶ｋ・域兜謫ｲ辷・匱蠕後ｄ閾ｪ辷・ｾ後↓繝｢繝・Ν縺後◎縺ｮ蝣ｴ縺ｫ谿九ｋ繝舌げ繧貞ｮ悟・縺ｫ隗｣豸茨ｼ・
    SetIsVisible(false);

    // --- 辷・｢ｨ縺ｫ繧医ｋ陦晉ｪ∝愛螳壹・逕滓・縺ｨ螻樊ｧ蛻・ｊ譖ｿ縺・---
    // 辷・匱縺励◆縺昴・迸ｬ髢薙・1繝輔Ξ繝ｼ繝縺縺大愛螳壹ｒ蠎・￡繧・
    SetColliderType(ColliderType::kSphere);
    SetCollisionRadius(LoadExplosionRadiusFromEffectJson());

    // 繝励Ξ繧､繝､繝ｼ・・Player・峨♀繧医・莉悶・謨ｵ・・Enemy・峨・蜿梧婿縺ｫ辷・匱縺悟ｽ薙◆繧九ｈ縺・↓螻樊ｧ繝ｻ繝槭せ繧ｯ繧堤┌蟾ｮ蛻･蛹厄ｼ・
    // 縺薙ｌ縺ｫ繧医ｊ縲∬・辷・〒繧よ雰縺ｫ蠖薙◆繧翫∵兜縺偵ｉ繧後◆辷・ｼｾ縺ｧ繧りｿ代☆縺弱ｋ縺ｨ繝励Ξ繧､繝､繝ｼ縺ｫ繝繝｡繝ｼ繧ｸ縺悟・繧九せ繝ｪ繝ｪ繝ｳ繧ｰ縺ｪ莉墓ｧ倥↓縺ｪ繧翫∪縺吶・
    SetCollisionAttribute(kPlayerAttack | kEnemyAttack);
    SetCollisionMask(kPlayer | kEnemy | kAllSolid);

    // --- 譁ｰ縺励＞鄒朱ｺ励↑縲檎・逋ｺ逕ｨ繧ｨ繝輔ぉ繧ｯ繝医阪・逋ｺ逕・---
    Vector3 myPos = GetTranslate();
    HitEffectDirector::SpawnBombExplosionHit(myPos);
    if (MeshEffectManager::GetInstance()) {
        MeshEffectManager::GetInstance()->SpawnEffectAt(
            kExplosionEffectPath,
            myPos,
            { 0.0f, 0.0f, 0.0f },
            { 1.0f, 1.0f, 1.0f }
        );
    }

    // 繝懊Β閾ｪ霄ｫ縺ｮHP繧・縺ｫ縺励※豸域ｻ・＆縺帙ｋ
    if (param_.has_value()) {
        param_.value().hp = 0.0f;
    }
    isDead = true; // 繝・ャ繝峨ヵ繝ｩ繧ｰ繧堤ｫ九※縺ｦ縲√ご繝ｼ繝繧ｷ繧ｹ繝・Β蛛ｴ縺ｧ縺ｮ繧ｯ繝ｪ繝ｼ繝ｳ繧｢繝・・繧剃ｿ・☆
}

void EnemyBomb::SetCarried(bool isCarried) {
    BaseEnemy::SetCarried(isCarried);

    if (isCarried) {
        // 繝励Ξ繧､繝､繝ｼ縺ｫ謗ｴ縺ｾ繧後◆迸ｬ髢薙∝ｼｷ蛻ｶ逧・↓轤ｹ轣ｫ迥ｶ諷九↓縺吶ｋ
        Ignite(3.0f);
        isThrown_ = false;
    } else {
        // 謚輔￡繧峨ｌ縺滂ｼ・sCarried_ 縺・false 縺ｫ縺ｪ繧翫√・繝ｬ繧､繝､繝ｼ縺九ｉ騾溷ｺｦ繧剃ｸ弱∴繧峨ｌ縺溽憾諷具ｼ・
        isThrown_ = true;
        throwRecoveryTimer_ = 0.0f;
        SetCollisionAttribute(kEnemy);
        SetCollisionMask(kPlayer | kAllSolid | kPlayerAttack | kAttributePlayerBullet);
    }
}

void EnemyBomb::Ignite(float fuseTime) {
    state_ = State::Ignited;
    fuseTimer_ = fuseTime;
    pulseTimer_ = 0.0f;
    flashTimer_ = 0.0f;
    flashState_ = false;
}
void EnemyBomb::ExecuteAbility(Player* player) {
    if (isAbilityExecuted_) return;
    isAbilityExecuted_ = true;

    // 鬆ｭ縺ｮ荳翫〒縺ｮ閾ｪ辷・・蜉幢ｼ壹・繝ｬ繧､繝､繝ｼ繧貞ｷｻ縺崎ｾｼ繧薙〒螟ｧ辷・匱縺吶ｋ
    Vector3 myPos = GetTranslate();
    Vector3 playerPos = player->GetTranslate();
    
    // 蜷ｹ縺埼｣帙・縺玲婿蜷代・險育ｮ暦ｼ亥ｰ代＠譁懊ａ荳翫↓鬟帙・縺呻ｼ・
    Vector3 throwBackDir = playerPos - myPos;
    throwBackDir.y = 0.0f;
    float dist = std::sqrt(throwBackDir.x * throwBackDir.x + throwBackDir.z * throwBackDir.z);
    if (dist > 0.001f) {
        throwBackDir.x /= dist;
        throwBackDir.z /= dist;
    } else {
        throwBackDir = {0.0f, 0.0f, -1.0f}; // 繝・ヵ繧ｩ繝ｫ繝域婿蜷・
    }
    throwBackDir.y = 0.7f; // 譁懊ａ荳翫↓繝弱ャ繧ｯ繝舌ャ繧ｯ

    // 繝励Ξ繧､繝､繝ｼ縺ｫ繝繝｡繝ｼ繧ｸ縺ｨ蜷ｹ縺埼｣帙・縺励せ繝・・繝医ｒ驕ｩ逕ｨ
    player->ChangeState(std::make_unique<PlayerStateDamage>(throwBackDir));
    
    // 辷・匱繧堤匱逕溘＆縺帙※閾ｪ霄ｫ縺ｯ豸域ｻ・
    Explode();
}

bool EnemyBomb::OnCollision(Object3d* other) {
    // 謚輔￡繧峨ｌ縺滓凾縺ｮ蜊ｳ襍ｷ辷・・蟒・ｭ｢縺励√き繧ｦ繝ｳ繝医ム繧ｦ繝ｳ縺ｫ繧医ｋ譎る剞辷・ｴ縺ｮ縺ｿ縺ｫ縺吶ｋ縺溘ａ縲∝叉辷・匱蜃ｦ逅・ｒ蜑企勁縲・
    // 邏皮ｲ九↓迚ｩ逅・噪縺ｪ螢√ｄ蠎翫∽ｻ悶・謨ｵ縺ｨ縺ｮ霍ｳ縺ｭ霑斐ｊ繧・款縺玲綾縺励・縺ｿ繧定｡後＞縺ｾ縺吶・
    return BaseEnemy::OnCollision(other);
}




