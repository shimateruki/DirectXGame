#define NOMINMAX
#include "game/scene/select/GameSelectScene.h"
#include "DebugConsole.h"
#include "StageManager.h"
#include "GPUParticleManager.h"
#include "MeshEffectManager.h"
#include "GameAudioSettings.h"
#include "CollisionConfig.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <sstream>
#include "game/scene/select/GameSelectScene.Internal.h"
using namespace StageSelectDetail;

// 島の装飾と王冠報酬の表示。進行データはGameDataManager、表示状態はシーンが所有する。
void GameSelectScene::UpdateStageSelectDecorations(float deltaTime) {
    // 島は常に地形として残し、解放状態の差をゲート・橋・台座の発光で示す。
    const auto& stages = StageManager::GetInstance()->GetStages();
    float pulse = (std::sin(stageSelectTime_ * 5.0f) + 1.0f) * 0.5f;

    Object3d* crown = FindObjectByName("StageSelect_CrownCore");
    if (crown && unlockingStageIndex_ < 0) {
        crown->SetEulerY(crown->GetRotation().y + deltaTime * 0.55f);
        crown->SetQuaternionMaster(false);
        crown->SetColor({1.0f, 0.72f, 0.18f, 1.0f});
        crown->SetEmissive(1.5f + pulse * 0.45f);
        crown->SetGPUParticleName(kCrownIdleParticlePreset);
        crown->SetMeshEffect1Name(kCrownAuraEffectPath);
        crown->SetMeshEffect2Name(kCrownRayEffectPath);
    }

    for (int stageIndex = 0; stageIndex < static_cast<int>(stages.size()); ++stageIndex) {
        bool unlocked = IsStageUnlocked(stageIndex);
        bool unlocking = stageIndex == unlockingStageIndex_;
        bool selected = stageIndex == selectedStageIndex_;

        Object3d* island = FindObjectByName("StageIsland_" + std::to_string(stageIndex));
        if (island) {
            island->SetIsVisible(true);
            // ロック中も島そのものは地形として残し、ゲートだけで進行を制限する。
            // 地形まで無効化すると、未解放の島へ移動した際にプレイヤーが落下してしまう。
            island->SetCollisionAttribute(CollisionCategories::Get("ground"));
            island->SetCollisionMask(kStageSelectSolidMask);
            // 新しいPBR地形は日照色を維持する。解放・選択表示はゲートと台座が担当する。
            const bool gardenTerrain = island->GetMaterialType() == 34;
            island->SetColor(gardenTerrain ? Vector4{1, 1, 1, 1}
                                           : GetStageIslandColor(stageIndex, unlocked, selected, unlocking, pulse));
            island->SetEmissive(gardenTerrain ? 1.0f : GetStageIslandEmissive(unlocked, selected, unlocking, pulse));
        }

        // 楕円の輪郭に沿う短冊Colliderを重ね、外周や短冊間の隙間から落下しないようにする。
        for (int stripIndex = 0; stripIndex < kStageIslandCollisionStripCount; ++stripIndex) {
            Object3d* islandCollision = FindObjectByName("StageIslandCollision_" + std::to_string(stageIndex) + "_" +
                                                         std::to_string(stripIndex));
            if (!islandCollision) {
                continue;
            }
            islandCollision->SetIsVisible(false);
            islandCollision->SetCollisionAttribute(CollisionCategories::Get("ground"));
            islandCollision->SetCollisionMask(kStageSelectSolidMask);
        }

        // 旧形式のSceneを開いた場合も、従来の十字Colliderを地形として維持する。
        for (const char* suffix : {"_X", "_Z"}) {
            Object3d* legacyCollision = FindObjectByName("StageIslandCollision_" + std::to_string(stageIndex) + suffix);
            if (legacyCollision) {
                legacyCollision->SetIsVisible(false);
                legacyCollision->SetCollisionAttribute(CollisionCategories::Get("ground"));
                legacyCollision->SetCollisionMask(kStageSelectSolidMask);
            }
        }

        Object3d* bridge = FindObjectByName("StageSelect_Bridge_" + std::to_string(stageIndex));
        if (bridge) {
            bridge->SetIsVisible(true);
            bridge->SetCollisionAttribute(CollisionCategories::Get("ground"));
            bridge->SetCollisionMask(kStageSelectSolidMask);
            if (unlocking) {
                bridge->SetColor({1.0f, 0.78f + pulse * 0.16f, 0.28f, 1.0f});
                bridge->SetEmissive(1.55f + pulse * 0.75f);
            } else if (unlocked) {
                bridge->SetColor({1.0f, 1.0f, 1.0f, 1.0f});
                bridge->SetEmissive(1.0f);
            } else {
                bridge->SetColor({0.32f, 0.34f, 0.38f, 1.0f});
                bridge->SetEmissive(0.22f);
            }
        }

        Object3d* markerPad = FindObjectByName("StageMarkerPad_" + std::to_string(stageIndex));
        if (markerPad) {
            markerPad->SetIsVisible(true);
            if (unlocking) {
                markerPad->SetColor({1.0f, 0.82f + pulse * 0.12f, 0.28f, 1.0f});
                markerPad->SetEmissive(2.4f + pulse * 1.1f);
            } else if (unlocked) {
                markerPad->SetColor(selected ? Vector4{0.86f, 0.92f, 1.0f, 1.0f} : Vector4{0.45f, 0.66f, 0.88f, 1.0f});
                markerPad->SetEmissive(selected ? 1.8f + pulse * 0.45f : 0.95f);
            } else {
                markerPad->SetColor({0.22f, 0.24f, 0.30f, 1.0f});
                markerPad->SetEmissive(0.18f);
            }
        }

        Object3d* flag = FindObjectByName("StageFlag_" + std::to_string(stageIndex));
        if (flag) {
            flag->SetIsVisible(true);
            if (unlocking) {
                flag->SetColor({1.0f, 0.88f, 0.36f, 1.0f});
                flag->SetEmissive(2.0f + pulse * 1.0f);
            } else if (unlocked) {
                flag->SetColor(selected ? Vector4{0.95f, 1.0f, 1.0f, 1.0f} : Vector4{0.74f, 0.90f, 1.0f, 1.0f});
                flag->SetEmissive(selected ? 1.65f + pulse * 0.35f : 1.05f);
            } else {
                flag->SetColor({0.32f, 0.36f, 0.46f, 1.0f});
                flag->SetEmissive(0.2f);
            }
        }

        UpdatePathDisplay(stageIndex, unlocked, unlocking, pulse);
    }

    UpdateStarCoinDisplays(deltaTime);
    UpdateStageClearCrownDisplays(deltaTime);
}

void GameSelectScene::UpdatePathDisplay(int stageIndex, bool active, bool unlocking, float pulse) {
    // 未解放の経路は見た目だけでなく接触も無効化して進行を制限する。
    for (int segmentIndex = 0; segmentIndex < 12; ++segmentIndex) {
        std::ostringstream name;
        name << "StagePath_" << stageIndex << "_" << segmentIndex;
        Object3d* path = FindObjectByName(name.str());
        if (!path) {
            continue;
        }

        bool visible = active || unlocking;
        path->SetIsVisible(visible);
        path->SetCollisionAttribute(active ? CollisionCategories::Get("ground") : 0);
        path->SetCollisionMask(active ? kStageSelectSolidMask : 0);

        if (unlocking) {
            path->SetColor({1.0f, 0.78f + pulse * 0.16f, 0.28f, 1.0f});
            path->SetEmissive(2.2f + pulse * 1.4f);
        } else if (active) {
            path->SetColor({0.66f, 0.84f, 1.0f, 1.0f});
            path->SetEmissive(1.05f);
        } else {
            path->SetColor({0.12f, 0.14f, 0.18f, 0.35f});
            path->SetEmissive(0.1f);
        }
    }
}

void GameSelectScene::UpdateStarCoinDisplays(float deltaTime) {
    // ステージごとの取得記録を、島上の三つのスター表示へ反映する。
    (void)deltaTime;
    const auto& stages = StageManager::GetInstance()->GetStages();
    auto* save = GameDataManager::GetInstance();

    for (int stageIndex = 0; stageIndex < static_cast<int>(stages.size()); ++stageIndex) {
        bool unlocked = IsStageUnlocked(stageIndex);
        for (int coinIndex = 0; coinIndex < 3; ++coinIndex) {
            std::ostringstream name;
            name << "StageCoin_" << stageIndex << "_" << coinIndex;
            Object3d* coin = FindObjectByName(name.str());
            if (!coin) {
                continue;
            }

            coin->SetIsVisible(unlocked);
            coin->SetCollisionAttribute(0);
            coin->SetCollisionMask(0);
            if (!unlocked) {
                continue;
            }

            if (save->IsStarCoinCollected(stageIndex, coinIndex)) {
                coin->SetColor({1.0f, 0.85f, 0.12f, 1.0f});
                coin->SetEmissive(2.2f);
            } else {
                coin->SetColor({0.32f, 0.34f, 0.38f, 0.78f});
                coin->SetEmissive(0.35f);
            }
        }
    }
}

Object3d* GameSelectScene::EnsureStageClearCrown(int stageIndex) {
    // 保存レイアウトに王冠がない場合だけ演出用モデルを生成し、重複を避ける。
    if (!objectManager_ || !object3dCommon_) {
        return nullptr;
    }

    const std::string name = MakeStageClearCrownName(stageIndex);
    if (Object3d* existing = FindObjectByName(name)) {
        return existing;
    }

    auto crown = std::make_unique<Object3d>();
    crown->Initialize(object3dCommon_.get());
    crown->SetModel(kStageClearCrownModel);
    crown->SetName(name);
    crown->SetClassName("EditorOnly");
    crown->SetSaveCategory("Object");
    crown->SetIsLocked(true);
    crown->SetTargetID(stageIndex);
    crown->SetColliderType(ColliderType::kNone);
    crown->SetCollisionAttribute(0);
    crown->SetCollisionMask(0);
    crown->SetScale({kStageClearCrownBaseScale, kStageClearCrownBaseScale, kStageClearCrownBaseScale});
    crown->SetColor({0.58f, 0.62f, 0.70f, 1.0f});
    crown->SetEmissive(0.45f);
    crown->SetGPUParticleName(kCrownIdleParticlePreset);
    crown->SetMeshEffect1Name("");
    crown->SetMeshEffect2Name("");
    crown->SetTranslate(GetStageClearCrownPosition(stageIndex));
    crown->UpdateLocalMatrix();
    crown->UpdateWorldMatrix();

    Object3d* raw = crown.get();
    AddObjectImmediate(std::move(crown));
    return raw;
}

void GameSelectScene::UpdateStageClearCrownDisplays(float deltaTime) {
    // 通常時は回転・上下動、クリア直後は専用の落下演出へ切り替える。
    const auto& stages = StageManager::GetInstance()->GetStages();
    auto* save = GameDataManager::GetInstance();
    const float pulse = (std::sin(stageSelectTime_ * 4.2f) + 1.0f) * 0.5f;

    for (int stageIndex = 0; stageIndex < static_cast<int>(stages.size()); ++stageIndex) {
        Object3d* crown = EnsureStageClearCrown(stageIndex);
        if (!crown) {
            continue;
        }

        const bool cleared = save->IsStageCleared(stageIndex);
        crown->SetIsVisible(cleared);
        crown->SetCollisionAttribute(0);
        crown->SetCollisionMask(0);
        if (!cleared) {
            continue;
        }

        if (crownCountPresentationActive_ && stageIndex == crownCountPresentationStageIndex_) {
            UpdateStageClearRewardCrown(crown, stageIndex, deltaTime);
            continue;
        }

        const float bob = std::sin(stageSelectTime_ * 2.6f + static_cast<float>(stageIndex) * 0.75f) * 0.10f;
        Vector3 targetPos = GetStageClearCrownPosition(stageIndex);
        targetPos.y += bob;
        crown->SetTranslate(targetPos);
        crown->SetEulerY(crown->GetRotation().y + deltaTime * 1.15f);
        crown->SetEulerZ(std::sin(stageSelectTime_ * 2.0f + static_cast<float>(stageIndex)) * 0.08f);
        crown->SetQuaternionMaster(false);

        const float scale = kStageClearCrownBaseScale * (1.0f + pulse * 0.08f);
        crown->SetScale({scale, scale, scale});
        crown->SetColor({0.58f + pulse * 0.06f, 0.62f + pulse * 0.06f, 0.70f + pulse * 0.07f, 1.0f});
        crown->SetEmissive(0.38f + pulse * 0.16f);
        crown->SetGPUParticleName(kCrownIdleParticlePreset);
        crown->SetMeshEffect1Name("");
        crown->SetMeshEffect2Name("");
        crown->UpdateLocalMatrix();
        crown->UpdateWorldMatrix();
    }
}

// クリア後の王冠数変化を表示対象ステージに固定して演出を開始します。
void GameSelectScene::StartStageClearRewardPresentation(const GameDataManager::StageClearRewardPresentation& request) {
    // 保存済みの報酬要求が有効なステージを指す場合だけ演出を開始する。
    const auto& stages = StageManager::GetInstance()->GetStages();
    if (!request.active || request.stageIndex < 0 || request.stageIndex >= static_cast<int>(stages.size())) {
        return;
    }

    crownCountPresentationActive_ = true;
    crownCountPresentationImpactDone_ = false;
    crownCountPresentationStageIndex_ = request.stageIndex;
    crownCountPresentationFrom_ = std::clamp(request.previousCrownCount, 0, 999);
    crownCountPresentationTo_ = std::clamp(request.newCrownCount, 0, 999);
    crownCountPresentationTimer_ = 0.0f;
    crownCountPresentationParticleTimer_ = 0.0f;
    selectedStageIndex_ = crownCountPresentationStageIndex_;

    DebugConsole::GetInstance()->AddLog("Stage Select: crown count presentation start.");
}

// 王冠の着地を境に粒子と音を切り替え、終了後に次のステージ解放演出へつなぎます。
void GameSelectScene::UpdateStageClearRewardPresentation(float deltaTime) {
    // 王冠数の表示を進め、演出終了まで通常の選択表示との重なりを管理する。
    if (!crownCountPresentationActive_) {
        return;
    }

    crownCountPresentationTimer_ += deltaTime;
    crownCountPresentationParticleTimer_ -= deltaTime;
    selectedStageIndex_ = crownCountPresentationStageIndex_;

    Object3d* crown = EnsureStageClearCrown(crownCountPresentationStageIndex_);
    const Vector3 targetPos = GetStageClearCrownPosition(crownCountPresentationStageIndex_);
    const Vector3 crownPos = crown ? crown->GetWorldPosition() : targetPos;
    const Vector3 burstPos =
        (crownCountPresentationTimer_ < kCrownCountImpactTime ? crownPos : targetPos) + Vector3{0.0f, 1.05f, 0.0f};

    if (crownCountPresentationParticleTimer_ <= 0.0f) {
        crownCountPresentationParticleTimer_ = kCrownCountParticleInterval;
        GPUParticleManager::GetInstance()->Emit(kCrownUnlockFountainParticlePreset, burstPos);
        if (crownCountPresentationTimer_ >= kCrownCountImpactTime) {
            GPUParticleManager::GetInstance()->Emit(kCrownUnlockAfterglowParticlePreset, burstPos);
        }
    }

    if (!crownCountPresentationImpactDone_ && crownCountPresentationTimer_ >= kCrownCountImpactTime) {
        crownCountPresentationImpactDone_ = true;
        MeshEffectManager::GetInstance()->SpawnEffectAt(kCrownUnlockFlashRingEffectPath,
                                                        targetPos + Vector3{0.0f, 0.45f, 0.0f}, {0.0f, 0.0f, 0.0f},
                                                        {1.15f, 1.0f, 1.15f});
        MeshEffectManager::GetInstance()->SpawnEffectAt(kCrownUnlockRayFlashEffectPath, burstPos, {0.0f, 0.0f, 0.0f},
                                                        {1.05f, 1.05f, 1.05f});
        GPUParticleManager::GetInstance()->Emit(kCrownUnlockParticlePreset, burstPos);
        GPUParticleManager::GetInstance()->Emit(kCrownUnlockRayParticlePreset, burstPos);
        GPUParticleManager::GetInstance()->Emit(kCrownUnlockAfterglowParticlePreset, burstPos);

        if (particleSystem_) {
            Vector3 up = {0.0f, 1.0f, 0.0f};
            particleSystem_->SpawnParticles(burstPos, 12, 6.0f, &up, 1.35f, {1.0f, 0.88f, 0.24f, 1.0f},
                                            {1.0f, 0.98f, 0.58f, 0.0f}, 0.34f, 0.82f, 0.58f, 0.04f);
        }
    }

    if (crownCountPresentationTimer_ >= kCrownCountPresentationDuration) {
        crownCountPresentationActive_ = false;
        crownCountPresentationStageIndex_ = -1;
        crownCountPresentationTimer_ = 0.0f;
        crownCountPresentationParticleTimer_ = 0.0f;
        crownCountPresentationImpactDone_ = false;

        unlockingStageIndex_ = FindPendingUnlockStage();
        if (unlockingStageIndex_ >= 0) {
            selectedStageIndex_ = unlockingStageIndex_;
            unlockPresentationTimer_ = 0.0f;
            unlockParticleTimer_ = 0.0f;
            DebugConsole::GetInstance()->AddLog("Stage Select: crown unlock presentation start.");
        }
    }
}

void GameSelectScene::UpdateStageClearRewardCrown(Object3d* crown, int stageIndex, float deltaTime) {
    // 獲得王冠が落下して跳ね返る時刻を、HUD の数字更新と揃える。
    if (!crown) {
        return;
    }

    const Vector3 targetPos = GetStageClearCrownPosition(stageIndex);

    Vector3 position = targetPos;
    float scale = kStageClearCrownBaseScale;
    float emissive = 3.1f;
    float silverFlash = 0.0f;
    const float t = crownCountPresentationTimer_;

    if (t < kCrownCountImpactTime) {
        const float p = SelectClamp01(t / kCrownCountImpactTime);
        const float e = SelectEaseOutCubic(p);
        const Vector3 startPos = targetPos + Vector3{0.0f, 4.35f, 0.0f};
        position = SelectLerp(startPos, targetPos, e);
        position.y += std::sin(p * std::numbers::pi_v<float>) * 0.62f;
        scale = kStageClearCrownBaseScale * (0.18f + SelectEaseOutBack(p) * 0.92f);
        emissive = 2.6f + p * 2.8f;
        silverFlash = p;
        crown->SetEulerY(crown->GetRotation().y + deltaTime * (8.0f + (1.0f - p) * 7.5f));
        crown->SetEulerZ(std::sin(p * std::numbers::pi_v<float>) * 0.42f);
    } else {
        const float p = t - kCrownCountImpactTime;
        const float decay = std::max(0.0f, 1.0f - p / 1.35f);
        const float bounce = std::abs(std::sin(p * 11.0f)) * 0.24f * decay;
        const float pop = std::sin(SelectClamp01(p / 0.42f) * std::numbers::pi_v<float>);
        position.y += bounce;
        scale = kStageClearCrownBaseScale * (1.0f + pop * 0.34f + std::sin(p * 14.0f) * 0.08f * decay);
        emissive = 2.55f + pop * 2.2f + decay * 0.9f;
        silverFlash = std::max(pop, decay * 0.45f);
        crown->SetEulerY(crown->GetRotation().y + deltaTime * (2.0f + decay * 4.5f));
        crown->SetEulerZ(std::sin(stageSelectTime_ * 5.0f) * 0.12f * std::max(0.25f, decay));
    }

    crown->SetTranslate(position);
    crown->SetScale({scale, scale, scale});
    crown->SetQuaternionMaster(false);
    crown->SetIsVisible(true);
    silverFlash = SelectClamp01(silverFlash);
    crown->SetColor({0.58f + silverFlash * 0.30f, 0.62f + silverFlash * 0.28f, 0.70f + silverFlash * 0.24f, 1.0f});
    crown->SetEmissive(emissive);
    crown->SetGPUParticleName(kCrownIdleParticlePreset);
    crown->SetMeshEffect1Name("");
    crown->SetMeshEffect2Name("");
    crown->UpdateLocalMatrix();
    crown->UpdateWorldMatrix();
}

void GameSelectScene::UpdateStageSelectCrownHudReward() {
    // 着地前は控えめな予告、着地後は王冠アイコンと数字を一斉に弾ませる。
    auto applySprite = [](StageSelectHudSprite& state, float scale, const Vector4& color, float rotation) {
        // 毎回基準配置から変形して、演出後に位置や大きさを持ち越さない。
        if (!state.sprite) {
            return;
        }

        state.sprite->SetPosition(state.basePosition);
        state.sprite->SetSize({state.baseSize.x * scale, state.baseSize.y * scale});
        state.sprite->SetColor(color);
        state.sprite->SetRotation(rotation);
        state.sprite->Update();
    };

    if (!crownCountPresentationActive_) {
        applySprite(stageSelectCrownIcon_, 1.0f, stageSelectCrownIcon_.baseColor, 0.0f);
        applySprite(stageSelectCrownXIcon_, 1.0f, stageSelectCrownXIcon_.baseColor, 0.0f);
        for (StageSelectHudSprite& digit : stageSelectCrownDigits_) {
            applySprite(digit, 1.0f, digit.baseColor, 0.0f);
        }
        return;
    }

    const float t = crownCountPresentationTimer_;
    const float impactAge = std::max(0.0f, t - kCrownCountImpactTime);
    const float prePulse = std::sin(stageSelectTime_ * 11.0f) * 0.035f;
    const float pop = crownCountPresentationImpactDone_
                          ? std::sin(SelectClamp01(impactAge / 0.52f) * std::numbers::pi_v<float>) * 0.42f
                          : 0.0f;
    const float glow = crownCountPresentationImpactDone_ ? std::max(0.0f, 1.0f - impactAge / 1.15f) : 0.0f;
    const Vector4 hotCrown = {1.0f, 0.88f + glow * 0.10f, 0.22f + glow * 0.34f, 1.0f};
    const Vector4 hotNumber = {1.0f, 0.92f + glow * 0.08f, 0.36f + glow * 0.32f, 1.0f};

    applySprite(stageSelectCrownIcon_, 1.0f + prePulse + pop * 0.62f, hotCrown,
                std::sin(stageSelectTime_ * 7.0f) * 0.06f * (1.0f + glow));
    applySprite(stageSelectCrownXIcon_, 1.0f + pop * 0.24f, hotNumber, 0.0f);
    for (StageSelectHudSprite& digit : stageSelectCrownDigits_) {
        applySprite(digit, 1.0f + pop, hotNumber, 0.0f);
    }
}

int GameSelectScene::GetDisplayedCrownCount() const {
    // 着地の瞬間まで旧個数を見せ、演出と数字の変化を一致させる。
    if (crownCountPresentationActive_ && !crownCountPresentationImpactDone_ &&
        crownCountPresentationTimer_ < kCrownCountImpactTime) {
        return crownCountPresentationFrom_;
    }
    return crownCountPresentationActive_ ? crownCountPresentationTo_
                                         : GameDataManager::GetInstance()->GetClearedStageCount();
}

Vector3 GameSelectScene::GetStageClearCrownPosition(int stageIndex) const {
    // ゲートモデルの上端を優先し、AABB が無効ならノード基準の高さへ戻す。
    Vector3 fallback = GetStageNodePosition(stageIndex) + Vector3{0.0f, 3.0f, 0.0f};
    if (!objectManager_) {
        return fallback;
    }

    for (const auto& object : objectManager_->GetObjects()) {
        if (!object || !IsStageGateObject(object.get()) || GetStageGateIndex(object.get()) != stageIndex) {
            continue;
        }

        AABB gateAabb = object->GetModelWorldAABB();
        const bool validAabb = std::isfinite(gateAabb.min.x) && std::isfinite(gateAabb.min.y) &&
                               std::isfinite(gateAabb.min.z) && std::isfinite(gateAabb.max.x) &&
                               std::isfinite(gateAabb.max.y) && std::isfinite(gateAabb.max.z) &&
                               gateAabb.max.y > gateAabb.min.y;
        if (!validAabb) {
            return fallback;
        }

        return {(gateAabb.min.x + gateAabb.max.x) * 0.5f, gateAabb.max.y + kStageClearCrownHeightOffset,
                (gateAabb.min.z + gateAabb.max.z) * 0.5f};
    }

    return fallback;
}
