#define NOMINMAX
#include "GimmickCopyMemoryStation.h"

#include "CollisionConfig.h"
#include "BaseScene.h"
#include "Player.h"
#include "game/actor/player/copy/PlayerCopyTypeCatalog.h"
#include "SceneManager.h"
#include "VFXSequencer.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace {
// 3つの見本の間に十分な選択余白を作り、意図しない能力の取得を防ぎます。
constexpr std::array<Vector3, 3> kCoreLocalPositions = {
    Vector3{-2.45f, 1.80f, -0.32f},
    Vector3{0.00f, 1.80f, 0.64f},
    Vector3{2.45f, 1.80f, -0.32f},
};
constexpr std::array<Vector3, 4> kFourCoreLocalPositions = {
    Vector3{-3.675f, 1.80f, -0.32f},
    Vector3{-1.225f, 1.80f, 0.64f},
    Vector3{1.225f, 1.80f, 0.64f},
    Vector3{3.675f, 1.80f, -0.32f},
};
constexpr float kCoreVisualScale = 0.92f;
constexpr float kSelectionCooldown = 0.28f;
constexpr float kPulseDuration = 0.85f;
// 記憶台の子モデルはシーンの特殊マテリアル一覧へ直接登録されないため、
// 通常PBRで確実に描画し、台座と3つの能力見本を一体として読めるようにします。
constexpr int kCorePreviewMaterialType = 0;
constexpr const char* kActivationCue = "copy_memory_station_activate_cue";
} // namespace

void GimmickCopyMemoryStation::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseGimmick::Initialize(common, modelName);

    // 台座だけをシーンへ登録し、能力見本は内部の描画物として再利用します。
    SetClassName("Gimmick");
    SetGimmickType("CopyMemoryStation");
    SetName("Gimmick_CopyMemoryStation");
    SetIsVisible(true);
    SetStatic(false);
    SetCastShadow(true);
    SetColor({1.0f, 1.0f, 1.0f, 1.0f});
    SetMaterialType(0);
    SetEmissive(0.50f);
    SetEnableEnvMap(true);
    SetEnvIntensity(0.35f);
    SetMetallic(0.18f);
    SetRoughness(0.34f);
    SetCollisionAttribute(CollisionCategories::Get("trigger"));
    SetCollisionMask(CollisionCategories::Get("player"));

    Object3d::ColliderConfig collider;
    collider.type = ColliderType::kOBB;
    collider.center = {0.0f, 0.95f, 0.0f};
    collider.size = {3.1f, 1.6f, 1.85f};
    SetColliderConfig(collider);

    if (!param_.has_value()) {
        param_.emplace();
    }
    param_->gimmickType = "CopyMemoryStation";
    param_->copyMemoryTypeA = "FireSlime";
    param_->copyMemoryTypeB = "ThunderSlime";
    param_->copyMemoryTypeC = "WindSlime";
    param_->copyMemoryActivationRadius = 0.9f;
    param_->copyMemoryUnlimitedDuration = true;
    param_->startActive = true;

    for (size_t index = 0; index < kCoreCount; ++index) {
        coreVisuals_[index] = std::make_unique<Object3d>();
        coreVisuals_[index]->Initialize(common);
        coreVisuals_[index]->SetName("CopyMemoryStation_Core_" + std::to_string(index + 1));
        coreVisuals_[index]->SetClassName("Object");
        coreVisuals_[index]->SetColliderType(ColliderType::kNone);
        coreVisuals_[index]->SetCollisionAttribute(0);
        coreVisuals_[index]->SetCollisionMask(0);
        coreVisuals_[index]->SetMaterialType(kCorePreviewMaterialType);
        // 通常のステージ照明を使い、暗い影に沈まず能力色を読める状態にします。
        coreVisuals_[index]->SetSelectedLighting(2);
        coreVisuals_[index]->SetEnableLighting(true);
        coreVisuals_[index]->SetEnableEnvMap(true);
        coreVisuals_[index]->SetEnvIntensity(0.55f);
        coreVisuals_[index]->SetCastShadow(true);
        coreVisuals_[index]->SetMetallic(0.0f);
        coreVisuals_[index]->SetRoughness(0.46f);
        coreVisuals_[index]->SetEmissive(0.65f);
        coreVisuals_[index]->SetIsVisible(false);
        displayedTypes_[index].clear();
        // 常駐する小数の描画物を使い回し、取得を繰り返しても粒を増殖させません。
        for (size_t part = 0; part < 4; ++part) {
            auto& effect = effects_[index * 4 + part];
            effect = std::make_unique<Object3d>();
            effect->Initialize(common);
            effect->SetModel(part == 3 ? "Gimmicks/copy_essence_halo" : "Gimmicks/copy_essence_spark");
            effect->SetColliderType(ColliderType::kNone);
            effect->SetCollisionAttribute(0);
            effect->SetCollisionMask(0);
            effect->SetEnableLighting(false);
            effect->SetEnableEnvMap(false);
            effect->SetCastShadow(false);
            effect->SetBlendMode(BlendMode::kAdd);
            effect->SetEmissive(1.0f);
            effect->SetIsVisible(false);
        }
    }
    UpdateCoreVisuals(0.0f);
}

void GimmickCopyMemoryStation::Update(float deltaTime) {
    // 接触が途切れた時に選択ラッチを外し、再入場で同じ能力を選び直せます。
    SceneManager* sceneManager = SceneManager::GetInstance();
    const bool isPlaying = sceneManager && sceneManager->IsPlaying();
    if (isPlaying) {
        if (!initializedForPlay_) {
            BeginPlayState();
        }
        selectionCooldown_ = (std::max)(0.0f, selectionCooldown_ - (std::max)(0.0f, deltaTime));
        if (!collisionObserved_) {
            latchedCore_ = -1;
        }
        collisionObserved_ = false;
    } else {
        ResetEditorState();
    }

    BaseGimmick::Update(deltaTime);
    UpdateCoreVisuals(deltaTime);
}

void GimmickCopyMemoryStation::Draw(ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    // 台座が隠れている間は子の見本や演出だけが浮かないよう描画を止めます。
    BaseGimmick::Draw(pointLightResource, spotLightResource);
    if (!GetIsVisible()) {
        return;
    }
    for (const std::unique_ptr<Object3d>& core : coreVisuals_) {
        if (core && core->GetIsVisible()) {
            core->Draw(pointLightResource, spotLightResource);
        }
    }
    for (const auto& effect : effects_) {
        if (effect && effect->GetIsVisible())
            effect->Draw(pointLightResource, spotLightResource);
    }
}

bool GimmickCopyMemoryStation::OnCollision(Object3d* other) {
    // コアごとの接触を調べ、同じ接触が続く間の連続取得を防ぎます。
    auto* player = dynamic_cast<Player*>(other);
    SceneManager* sceneManager = SceneManager::GetInstance();
    if (!player || !sceneManager || !sceneManager->IsPlaying() || !runtimeEnabled_) {
        return false;
    }

    collisionObserved_ = true;
    const int touchedCore = FindTouchedCore(player->GetWorldPosition());
    if (touchedCore < 0) {
        latchedCore_ = -1;
        return false;
    }
    if (selectionCooldown_ <= 0.0f && touchedCore != latchedCore_) {
        ActivateCore(*player, static_cast<size_t>(touchedCore));
    }
    return false;
}

void GimmickCopyMemoryStation::OnTrigger() {
    // 単発トリガーは台座を有効化するイベントへ変換します。
    OnSwitchEvent(true);
}

void GimmickCopyMemoryStation::OnSwitchEvent(bool active) {
    // 無効化時は直前に触れていたコアと取得パルスを消します。
    runtimeEnabled_ = active;
    if (!active) {
        latchedCore_ = -1;
        collisionObserved_ = false;
        corePulseTimers_.fill(0.0f);
    }
}

std::unique_ptr<Object3d> GimmickCopyMemoryStation::Clone() const {
    // 内部見本は初期化で作り直し、設定された能力種別だけコピーします。
    auto clone = std::make_unique<GimmickCopyMemoryStation>();
    assert(common_ != nullptr);
    clone->Initialize(common_, GetModelName());
    clone->CopyFrom(this);
    return clone;
}

void GimmickCopyMemoryStation::BeginPlayState() {
    // 再生開始時は編集設定を有効状態の初期値へ反映します。
    initializedForPlay_ = true;
    runtimeEnabled_ = param_.has_value() ? param_->startActive : true;
    selectionCooldown_ = 0.0f;
    latchedCore_ = -1;
    collisionObserved_ = false;
}

void GimmickCopyMemoryStation::ResetEditorState() {
    // 前回再生での選択とアニメーションを編集画面へ持ち越しません。
    initializedForPlay_ = false;
    corePulseTimers_.fill(0.0f);
    runtimeEnabled_ = param_ ? param_->startActive : true;
    selectionCooldown_ = 0.0f;
    latchedCore_ = -1;
    collisionObserved_ = false;
}

void GimmickCopyMemoryStation::UpdateCoreVisuals(float deltaTime) {
    // 無効な時間差を除外し、登録済みの見本モデルを配置と設定に追従させます。
    deltaTime = std::isfinite(deltaTime) ? std::clamp(deltaTime, 0.0f, 0.1f) : 0.0f;
    visualTimer_ = std::fmod(visualTimer_ + deltaTime, 600.0f);
    SceneManager* manager = SceneManager::GetInstance();
    Player* player = manager && manager->IsPlaying() && manager->GetCurrentScene()
                         ? manager->GetCurrentScene()->GetPlayer()
                         : nullptr;
    if (player && (player->isDead || player->IsCinematicLocked()))
        player = nullptr;
    const float size = param_ ? std::clamp(param_->copyMemoryEssenceScale, 0.25f, 2.0f) : 1.0f;
    const float hover = param_ ? std::clamp(param_->copyMemoryHoverAmount, 0.0f, 0.35f) : 0.10f;
    const float glow = param_ ? std::clamp(param_->copyMemoryGlow, 0.0f, 2.0f) : 0.45f;
    const bool four = param_ && !param_->copyMemoryTypeD.empty();
    const bool single = IsSingleCore();
    const std::string desiredModel = single ? "Gimmicks/copy_memory_station_single"
                                     : four ? "Gimmicks/copy_memory_station_four"
                                            : "Gimmicks/copy_memory_station";
    if ((GetModelName() == "Gimmicks/copy_memory_station" || GetModelName() == "Gimmicks/copy_memory_station_four" ||
         GetModelName() == "Gimmicks/copy_memory_station_single") &&
        GetModelName() != desiredModel) {
        SetModel(desiredModel);
    }
    // JSONに残る旧3台のトリガーも、新しい左右のコアまで届くよう更新します。
    auto trigger = GetColliderConfig();
    const float triggerWidth = single ? 1.3f : four ? 4.6f : 3.1f;
    if (trigger.size.x != triggerWidth) {
        trigger.size.x = triggerWidth;
        SetColliderConfig(trigger);
    }
    for (size_t index = 0; index < kCoreCount; ++index) {
        for (size_t part = 0; part < 4; ++part)
            effects_[index * 4 + part]->SetIsVisible(false);
        const std::string configuredType = GetConfiguredType(index);
        if (displayedTypes_[index] != configuredType) {
            ConfigureCore(index, configuredType);
        }

        Object3d* core = coreVisuals_[index].get();
        if (!core || !core->GetIsVisible()) {
            continue;
        }

        corePulseTimers_[index] = (std::max)(0.0f, corePulseTimers_[index] - (std::max)(0.0f, deltaTime));
        const float phase = visualTimer_ * 2.15f + static_cast<float>(index) * 2.0944f;
        const float bounce = std::sin(phase) * hover;
        Vector3 position = GetCoreWorldPosition(index);
        position.y += bounce * std::abs(GetScale().y);
        // 接近時は選んだ能力だけが少し伸び上がり、取得物であることを伝えます。
        float approach = 0.0f;
        if (player && runtimeEnabled_) {
            const Vector3 delta = player->GetWorldPosition() - position;
            const float range = 2.1f * (std::max)(0.1f, std::abs(GetScale().x));
            approach = std::clamp(1.0f - std::sqrt(delta.x * delta.x + delta.z * delta.z) / range, 0.0f, 1.0f);
        }
        position.y += approach * 0.16f;
        core->SetRotation({GetRotation().x, GetRotation().y + std::sin(phase * 0.45f) * 0.20f,
                           GetRotation().z + std::sin(phase) * 0.065f});

        const float pulseRate = corePulseTimers_[index] > 0.0f ? 1.0f - corePulseTimers_[index] / kPulseDuration : 1.0f;
        const float pulse = corePulseTimers_[index] > 0.0f ? 1.0f + std::sin(pulseRate * 3.14159265f) * 0.28f : 1.0f;
        const Vector3 rootScale = GetScale();
        // 台座の横幅を調整しても能力マークが潰れないよう、縦・奥行きから均一倍率を作ります。
        const float uniformRootScale = (std::max)(0.1f, (std::abs(rootScale.y) + std::abs(rootScale.z)) * 0.5f);
        const float visualScale = uniformRootScale * kCoreVisualScale * size;
        // 取得直後はコア自身が短い弧を描いて移り、縮んでから台座上に復帰します。
        float transferScale = 1.0f;
        if (corePulseTimers_[index] > 0.0f) {
            const float elapsed = kPulseDuration - corePulseTimers_[index];
            if (elapsed < 0.32f) {
                const float t = elapsed / 0.32f;
                const float eased = t * t * (3.0f - 2.0f * t);
                const Vector3 target =
                    player ? player->GetWorldPosition() + Vector3{0.0f, 0.5f, 0.0f} : transferTargets_[index];
                transferTargets_[index] = target;
                position = position * (1.0f - eased) + target * eased;
                position.y += std::sin(t * 3.14159265f) * 0.35f;
                transferScale = (std::max)(0.015f, 1.0f - eased);
            } else {
                transferScale = std::clamp((elapsed - 0.40f) / 0.35f, 0.015f, 1.0f);
            }
        }
        core->SetTranslate(position);
        core->SetScale({
            visualScale * pulse * transferScale,
            visualScale / pulse * transferScale,
            visualScale * pulse * transferScale,
        });
        core->SetEmissive(runtimeEnabled_ ? glow + approach * 0.35f + (corePulseTimers_[index] > 0.0f ? 0.3f : 0.0f)
                                          : 0.0f);
        core->SetColor(runtimeEnabled_ ? Vector4{1, 1, 1, 1} : Vector4{0.35f, 0.35f, 0.35f, 1});
        core->Update(deltaTime);
        // 見本モデルはObjectManagerへ登録しない内部描画物です。
        // Object3d::Updateは描画定数の確定をObjectManagerへ遅延するため、
        // ここで明示的に確定しないと初期位置へ重なり、カメラへ張り付いて見えます。
        core->UpdateWorldMatrix();
        UpdateCoreEffects(index, deltaTime, approach, visualScale);
    }
}

void GimmickCopyMemoryStation::UpdateCoreEffects(size_t index, float deltaTime, float approach, float scale) {
    // 各コアに固定数の粒と光輪を割り当て、毎フレーム生成しません。
    if (!runtimeEnabled_)
        return;
    const auto* descriptor = PlayerCopyTypeCatalog::Find(GetConfiguredType(index));
    if (!descriptor)
        return;
    Vector4 color = descriptor->displayColor;
    // ボムの光は黒い本体でも埋もれない、淡い紫にします。
    if (GetConfiguredType(index) == "Bomber")
        color = {0.75f, 0.53f, 1.0f, 1.0f};
    const bool active = corePulseTimers_[index] > 0.0f;
    const float elapsed = kPulseDuration - corePulseTimers_[index];
    const Vector3 origin = coreVisuals_[index]->GetWorldPosition();
    for (size_t part = 0; part < 4; ++part) {
        auto* effect = effects_[index * 4 + part].get();
        float alpha = 0.0f;
        float radius = 0.1f;
        Vector3 position = origin;
        Vector3 rotation = GetRotation();
        if (part < 3) {
            const float seed = static_cast<float>(part) * 2.094395f + static_cast<float>(index) * 0.71f;
            const float cycle = std::fmod(
                visualTimer_ * 0.60f + static_cast<float>(part) / 3.0f + static_cast<float>(index) * 0.137f, 1.0f);
            const float twinkle = cycle < 0.45f ? std::sin(cycle / 0.45f * 3.14159265f) : 0.0f;
            alpha = twinkle * (0.38f + approach * 0.35f);
            radius = (0.055f + twinkle * 0.045f) * scale;
            Vector3 offset{std::cos(seed + visualTimer_ * 0.3f) * 0.95f,
                           0.28f + std::sin(seed + visualTimer_ * 0.6f) * 0.40f,
                           std::sin(seed + visualTimer_ * 0.3f) * 0.35f};
            position = position + offset * scale;
            if (active && elapsed < 0.32f) {
                // 吸い込まれるコアへ粒を寄せます。
                alpha = 0.75f;
                position = origin + offset * (scale * (1.0f - elapsed / 0.32f));
            } else if (active && elapsed < 0.72f) {
                const float t = (elapsed - 0.32f) / 0.40f;
                position = transferTargets_[index] + Vector3{std::cos(seed) * (0.2f + t * 0.8f),
                                                             std::sin(seed) * (0.2f + t * 0.6f) + 0.2f,
                                                             std::sin(seed * 2.0f) * t * 0.3f};
                alpha = (1.0f - t) * 0.9f;
                radius = scale * 0.13f * (1.0f - t * 0.75f);
            }
            rotation.z += visualTimer_ * 0.7f + seed;
        } else {
            const Vector3 local = GetCoreLocalPosition(index);
            position = Math::Transform({local.x, 0.835f, local.z}, GetWorldMatrix());
            radius = scale * 0.72f;
            alpha = 0.12f + approach * 0.45f;
            if (active) {
                alpha = (1.0f - elapsed / kPulseDuration) * 0.65f;
                radius *= 1.0f + std::sin(elapsed / kPulseDuration * 3.14159265f) * 0.20f;
            }
        }
        alpha *= param_ ? std::clamp(param_->copyMemoryGlow * 2.0f, 0.0f, 1.5f) : 0.9f;
        effect->SetIsVisible(alpha > 0.015f);
        if (!effect->GetIsVisible())
            continue;
        effect->SetTranslate(position);
        effect->SetRotation(rotation);
        effect->SetScale({radius, radius, radius});
        effect->SetColor({color.x, color.y, color.z, alpha});
        effect->Update(deltaTime);
        effect->UpdateWorldMatrix();
    }
}

void GimmickCopyMemoryStation::ConfigureCore(size_t index, const std::string& enemyType) {
    // 能力カタログに登録された型だけ対応モデルを表示します。
    displayedTypes_[index] = enemyType;
    Object3d* core = coreVisuals_[index].get();
    const PlayerCopyTypeDescriptor* descriptor = PlayerCopyTypeCatalog::Find(enemyType);
    if (!core || !descriptor) {
        if (core) {
            core->SetIsVisible(false);
        }
        return;
    }

    core->SetModel(std::string(PlayerCopyTypeCatalog::GetEssenceModel(enemyType)));
    core->SetTexture("");
    // モデルの能力色と明るい記号を、そのまま読みやすく表示します。
    core->SetColor({1.0f, 1.0f, 1.0f, 1.0f});
    core->SetMaterialType(kCorePreviewMaterialType);
    core->SetIsVisible(true);
}

bool GimmickCopyMemoryStation::IsSingleCore() const {
    // 使用可能な能力が一つなら、見本と取得位置を中央へ集約します。
    size_t count = 0;
    for (size_t i = 0; i < kCoreCount; ++i)
        if (PlayerCopyTypeCatalog::IsSupported(GetConfiguredType(i)))
            ++count;
    return count == 1;
}

Vector3 GimmickCopyMemoryStation::GetCoreLocalPosition(size_t index) const {
    // 1能力だけなら空の台座を並べず、取得判定と光も中央へ揃える。
    if (IsSingleCore())
        return {0.0f, 1.8f, 0.0f};
    return param_ && !param_->copyMemoryTypeD.empty() ? kFourCoreLocalPositions[index]
                                                      : kCoreLocalPositions[(std::min)(index, size_t{2})];
}

Vector3 GimmickCopyMemoryStation::GetCoreWorldPosition(size_t index) const {
    // 台座の回転と拡大縮小も含めて、コアの接触位置を変換します。
    return Math::Transform(GetCoreLocalPosition(index), GetWorldMatrix());
}

std::string GimmickCopyMemoryStation::GetConfiguredType(size_t index) const {
    // 設定されたスロット番号を能力名へ変換します。
    if (!param_.has_value()) {
        return {};
    }
    switch (index) {
    case 0:
        return param_->copyMemoryTypeA;
    case 1:
        return param_->copyMemoryTypeB;
    case 2:
        return param_->copyMemoryTypeC;
    case 3:
        return param_->copyMemoryTypeD;
    default:
        return {};
    }
}

int GimmickCopyMemoryStation::FindTouchedCore(const Vector3& playerPosition) const {
    // 水平方向だけで最も近い有効コアを探し、見た目と取得先を一致させます。
    const float configuredRadius = param_.has_value() ? param_->copyMemoryActivationRadius : 0.9f;
    const float scaleRadius = (std::max)(std::abs(GetScale().x), std::abs(GetScale().z));
    const float radius = (std::max)(0.1f, configuredRadius) * scaleRadius;
    const float radiusSquared = radius * radius;
    int nearest = -1;
    float nearestDistanceSquared = radiusSquared;
    for (size_t index = 0; index < kCoreCount; ++index) {
        if (!PlayerCopyTypeCatalog::IsSupported(GetConfiguredType(index))) {
            continue;
        }
        const Vector3 corePosition = GetCoreWorldPosition(index);
        const float deltaX = playerPosition.x - corePosition.x;
        const float deltaZ = playerPosition.z - corePosition.z;
        const float distanceSquared = deltaX * deltaX + deltaZ * deltaZ;
        if (distanceSquared <= nearestDistanceSquared) {
            nearestDistanceSquared = distanceSquared;
            nearest = static_cast<int>(index);
        }
    }
    return nearest;
}

void GimmickCopyMemoryStation::ActivateCore(Player& player, size_t index) {
    // 能力適用に成功したときだけ再取得制限と吸い込み演出を始めます。
    if (index >= kCoreCount || player.isDead || player.IsCinematicLocked()) {
        return;
    }

    const std::string enemyType = GetConfiguredType(index);
    const bool unlimitedDuration = !param_.has_value() || param_->copyMemoryUnlimitedDuration;
    if (!player.ApplyStoredCopy(enemyType, unlimitedDuration)) {
        return;
    }

    latchedCore_ = static_cast<int>(index);
    selectionCooldown_ = kSelectionCooldown;
    corePulseTimers_[index] = kPulseDuration;
    transferTargets_[index] = player.GetWorldPosition() + Vector3{0.0f, 0.5f, 0.0f};
    VFXSequencer::PlayOneShot(kActivationCue, player.GetWorldPosition(), {0.65f, 0.65f, 0.65f});
}
