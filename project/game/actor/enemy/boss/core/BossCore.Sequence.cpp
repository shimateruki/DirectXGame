#include "game/actor/enemy/boss/core/BossCore.Internal.h"

using namespace BossCoreImplementation;

// 元の更新順序と判定を維持して、担当処理をまとめています。
void BossCore::UpdateAnimationSequence(float deltaTime) {

    // ==========================================
    // ゲームが再生中(Play)でなければ、この先のアニメーション・入力処理を一切行わない！
    // ==========================================
    if (!SceneManager::GetInstance()->IsPlaying()) {
        return; // 再生中でなければ操作を受け付けない
    }
    if (state_ == State::Weak) {
        return;
    }

    // ======================================
    // フェーズ0: 待機（Idleステート中）
    // ======================================
    if (animPhase_ == 0) {
        // 待機中は常にブロックをランダムスケールの周回軌道に乗せる！
        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            OrbitData orbit = GetIdleOrbit(i);
            armorBlocks_[i]->SetTranslate(orbit.pos);
            armorBlocks_[i]->SetScale(orbit.scale);
            armorBlocks_[i]->SetRotation(orbit.rot);
            armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
        }

        // 自動ループ化に伴い、キー入力による遷移を廃止
        // 攻撃のトリガーは ChangeState(State::Attack) が自動で行います。
        return;
    }

    // ======================================
    // 攻撃モード1：形態変化 ＆ 突進 (Phase 1 ~ 5)
    // ======================================
    if (attackMode_ == 1) {
        UpdateChargeSequence(deltaTime);
    }

    // ======================================
    // 攻撃モード2：移動 → 陣形変化 → ブロック射撃 (Phase 10 ~ 13)
    // ======================================
    else if (attackMode_ == 2) {
        UpdateBlockShotSequence(deltaTime);
    }
    // ======================================
    // 攻撃モード3：ハンマー合体 ＆ 目の前で叩き潰す！
    // ======================================
    else if (attackMode_ == 3) {
        UpdateHammerSequence(deltaTime);
    }
    // ======================================
    // 攻撃モード4：絶望の十字往復ギガ・ウォール！
    // ======================================
    else if (attackMode_ == 4) {
        UpdateWallSequence(deltaTime);
    } // ======================================
    // 攻撃モード5：人型になって倒れてくる！ (Phase 50 ~ 55)
    // ======================================
    else if (attackMode_ == 5) {
        UpdateCollapseSequence(deltaTime);
    }
    // ======================================
    // 攻撃モード6：中央移動 → 回転タメ → 回転変形 → 0.5秒停止 → レーザー！ (Phase 60 ~ 65)
    // ======================================
    else if (attackMode_ == 6) {
        UpdateLaserSequence(deltaTime);
    }
}

// 攻撃モード1の各フェーズを更新します。
void BossCore::UpdateChargeSequence(float deltaTime) {

    // Phase 1 開始時の初期化処理
    if (animPhase_ == 1 && animTimer_ == 0.0f) {
        blockStartPos_.clear();
        blockTargetPos_.clear();

        // 装甲の組立位置・大きさ・ラジアン回転。一連の部品を同じ設定表から動かす。
        struct BlockSetting {
            Vector3 translate;
            Vector3 scale;
            Vector3 rotation;
        };

        std::vector<BlockSetting> settings = {{{-3.3f, 0.0f, 0.0f}, {0.300f, 0.500f, 0.500f}, {0.0f, 0.0f, 0.0f}},
                                              {{-2.0f, 0.0f, 0.0f}, {1.035f, 1.000f, 1.500f}, {0.0f, 0.0f, 0.0f}},
                                              {{0.0f, 1.5f, 0.0f}, {2.000f, 0.506f, 1.500f}, {0.0f, 0.0f, 0.0f}},
                                              {{0.0f, -1.5f, 0.0f}, {2.000f, 0.511f, 1.500f}, {0.0f, 0.0f, 0.0f}},
                                              {{2.5f, 0.0f, 0.0f}, {0.500f, 3.000f, 1.500f}, {0.0f, 0.0f, 0.0f}},
                                              {{3.5f, 0.0f, 0.0f}, {0.500f, 1.000f, 0.500f}, {0.0f, 0.0f, 0.0f}}};

        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            blockStartPos_.push_back(armorBlocks_[i]->GetTranslate());

            if (i < settings.size()) {
                blockTargetPos_.push_back(settings[i].translate);
                armorBlocks_[i]->SetScale(settings[i].scale);
                armorBlocks_[i]->SetRotation(settings[i].rotation);
                armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
            } else {
                blockTargetPos_.push_back({0.0f, 0.0f, 0.0f});
            }
        }
    }

    // --- フェーズ1: 形態変化（ブロックがカシャッと合体する） ---
    if (animPhase_ == 1) {
        animTimer_ += deltaTime;
        float duration = 1.5f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::OutExpo(t);

        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            if (i < blockStartPos_.size() && i < blockTargetPos_.size()) {
                Vector3 pos = Math::Lerp(blockStartPos_[i], blockTargetPos_[i], easeT);
                armorBlocks_[i]->SetTranslate(pos);
            }
        }

        if (t >= 1.0f) {
            animPhase_ = 2;
            animTimer_ = 0.0f;
            animStartPos_ = GetTranslate();
        }
    }
    // --- フェーズ2: 移動 (x = -50) ---
    else if (animPhase_ == 2) {
        animTimer_ += deltaTime;
        float duration = 2.5f;
        float t = std::min(animTimer_ / duration, 1.0f);

        Vector3 pos = GetTranslate();
        pos.x = Math::Lerp(animStartPos_.x, -50.0f, Easing::OutExpo(t));
        SetTranslate(pos);

        if (t >= 1.0f) {
            animPhase_ = 3;
            animTimer_ = 0.0f;
            animStartPos_ = GetTranslate();
        }
    }
    // --- フェーズ3: シェイク & プレイヤー注視 ---
    else if (animPhase_ == 3) {
        animTimer_ += deltaTime;
        float duration = 3.0f;
        float t = std::min(animTimer_ / duration, 1.0f);

        if (target_) {
            Vector3 toPlayer = target_->GetWorldPosition() - GetWorldPosition();
            float angleY = std::atan2(toPlayer.x, toPlayer.z) + (std::numbers::pi_v<float> / 2.0f);
            SetRotation({GetRotation().x, angleY, GetRotation().z});
            GetTransform()->isQuaternionMaster = false;
        }

        Vector3 pos = animStartPos_;
        float shake = 0.3f;
        pos.x += ((float)rand() / RAND_MAX * 2.0f - 1.0f) * shake;
        pos.y += ((float)rand() / RAND_MAX * 2.0f - 1.0f) * shake;
        SetTranslate(pos);

        if (t >= 1.0f) {
            animPhase_ = 4;
            animTimer_ = 0.0f;
            animStartPos_ = GetTranslate();
            if (target_)
                animTargetPos_ = target_->GetWorldPosition();
        }
    }
    // --- フェーズ4: 加速突進 ---
    else if (animPhase_ == 4) {
        animTimer_ += deltaTime;
        float duration = 1.5f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easedT = std::pow(t, 4.0f);

        SetTranslate(Math::Lerp(animStartPos_, animTargetPos_, easedT));

        float totalRotation = std::numbers::pi_v<float> * 2.0f * 5.0f;
        SetRotation({easedT * totalRotation, GetRotation().y, GetRotation().z});
        GetTransform()->isQuaternionMaster = false;

        if (t >= 1.0f) {
            animPhase_ = 5;
            animTimer_ = 0.0f;
        }
    }
    // --- フェーズ5: 待機軌道に向かってゆっくり復帰する ---
    else if (animPhase_ == 5) {

        if (animTimer_ == 0.0f) {
            blockStartPos_.clear();
            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                blockStartPos_.push_back(armorBlocks_[i]->GetTranslate());
            }
        }

        animTimer_ += deltaTime;
        float duration = 3.0f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::OutExpo(t);

        SetRotation({0.0f, 0.0f, 0.0f});
        GetTransform()->isQuaternionMaster = false;

        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            if (i < blockStartPos_.size()) {
                OrbitData orbit = GetIdleOrbit(i);
                Vector3 pos = Math::Lerp(blockStartPos_[i], orbit.pos, easeT);
                armorBlocks_[i]->SetTranslate(pos);
                armorBlocks_[i]->SetScale(orbit.scale);
                armorBlocks_[i]->SetRotation(orbit.rot);
                armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
            }
        }

        // 完全に復帰したら攻撃終了！
        if (t >= 1.0f) {
            animPhase_ = 0;
            attackMode_ = 0;
            animTimer_ = 0.0f;
        }
    }
}

// 攻撃モード2の各フェーズを更新します。
void BossCore::UpdateBlockShotSequence(float deltaTime) {

    // --- Phase 10: X = 50.0f へ移動 ---
    if (animPhase_ == 10) {
        if (animTimer_ == 0.0f)
            animStartPos_ = GetTranslate();
        animTimer_ += deltaTime;
        float t = std::min(animTimer_ / 2.5f, 1.0f);

        Vector3 pos = GetTranslate();
        pos.x = Math::Lerp(animStartPos_.x, 50.0f, Easing::OutExpo(t));
        SetTranslate(pos);

        if (target_) {
            Vector3 toPlayer = target_->GetWorldPosition() - GetWorldPosition();
            float angleY = std::atan2(toPlayer.x, toPlayer.z) + (std::numbers::pi_v<float> / 2.0f);
            SetRotation({GetRotation().x, angleY, GetRotation().z});
            GetTransform()->isQuaternionMaster = false;
        }

        if (t >= 1.0f) {
            animPhase_ = 11;
            animTimer_ = 0.0f;

            blockStartPos_.clear();
            blockTargetPos_.clear();

            // 装甲ブロックの組立姿勢。回転はラジアンで、表示スケールと配置位置を一緒に指定する。
            struct BlockSetting {
                Vector3 translate;
                Vector3 scale;
                Vector3 rotation;
            };

            float turnY = std::numbers::pi_v<float> / 2.0f;

            std::vector<BlockSetting> settings = {{{-2.0f, 2.5f, 0.0f}, {0.5f, 0.5f, 0.5f}, {0.0f, turnY, 0.0f}},
                                                  {{-2.0f, 1.0f, -2.0f}, {0.5f, 0.5f, 0.5f}, {0.0f, turnY, 0.0f}},
                                                  {{-2.0f, 1.0f, 2.0f}, {0.5f, 0.5f, 0.5f}, {0.0f, turnY, 0.0f}},
                                                  {{-2.0f, -1.0f, -2.0f}, {0.5f, 0.5f, 0.5f}, {0.0f, turnY, 0.0f}},
                                                  {{-2.0f, -1.0f, 2.0f}, {0.5f, 0.5f, 0.5f}, {0.0f, turnY, 0.0f}},
                                                  {{-2.0f, -2.5f, 0.0f}, {0.5f, 0.5f, 0.5f}, {0.0f, turnY, 0.0f}}};

            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                blockStartPos_.push_back(armorBlocks_[i]->GetTranslate());

                if (i < settings.size()) {
                    blockTargetPos_.push_back(settings[i].translate);
                    armorBlocks_[i]->SetScale(settings[i].scale);
                    armorBlocks_[i]->SetRotation(settings[i].rotation);
                    armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
                } else {
                    blockTargetPos_.push_back({0.0f, 0.0f, 0.0f});
                }
            }
        }
    }
    // --- Phase 11: 射撃陣形へスライド移動 ---
    else if (animPhase_ == 11) {
        animTimer_ += deltaTime;
        float duration = 1.0f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::OutExpo(t);

        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            if (i < blockStartPos_.size() && i < blockTargetPos_.size()) {
                Vector3 pos = Math::Lerp(blockStartPos_[i], blockTargetPos_[i], easeT);
                armorBlocks_[i]->SetTranslate(pos);
            }
        }

        if (target_) {
            Vector3 toPlayer = target_->GetWorldPosition() - GetWorldPosition();
            float angleY = std::atan2(toPlayer.x, toPlayer.z) + (std::numbers::pi_v<float> / 2.0f);
            SetRotation({GetRotation().x, angleY, GetRotation().z});
            GetTransform()->isQuaternionMaster = false;
        }

        if (t >= 1.0f) {
            animPhase_ = 12;
            animTimer_ = 0.0f;
            shotCount_ = 0;
            shotInterval_ = 0.0f;
        }
    }
    // --- Phase 12: プレイヤーを向いて、1つずつ飛ばす ---
    else if (animPhase_ == 12) {
        animTimer_ += deltaTime;

        if (target_) {
            Vector3 toPlayer = target_->GetWorldPosition() - GetWorldPosition();
            float angleY = std::atan2(toPlayer.x, toPlayer.z) + (std::numbers::pi_v<float> / 2.0f);
            SetRotation({GetRotation().x, angleY, GetRotation().z});
            GetTransform()->isQuaternionMaster = false;
        }

        float nextShotTime = shotCount_ * 0.5f;

        if (animTimer_ >= nextShotTime) {
            int idx = (int)armorBlocks_.size() - 1 - shotCount_;
            if (idx >= 0 && idx < armorBlocks_.size()) {
                Object3d* block = armorBlocks_[idx];

                Vector3 bossPos = GetTranslate();
                float bossRotY = GetRotation().y;
                Vector3 localPos = block->GetTranslate();

                Vector3 worldPos;
                worldPos.x = bossPos.x + (localPos.x * std::cos(bossRotY) + localPos.z * std::sin(bossRotY));
                worldPos.y = bossPos.y + localPos.y;
                worldPos.z = bossPos.z + (-localPos.x * std::sin(bossRotY) + localPos.z * std::cos(bossRotY));

                block->SetParent(nullptr);
                block->SetTranslate(worldPos);

                Vector3 currentRot = block->GetRotation();
                block->GetTransform()->isQuaternionMaster = false;

                armorFlight_.Queue(block, currentRot, idx);
            }

            shotCount_++;

            if (shotCount_ >= armorBlocks_.size()) {
                animPhase_ = 13;
                animTimer_ = 0.0f;
            }
        }
    } else if (animPhase_ == 13) {
        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            const bool isFlying = armorFlight_.ContainsIndex(i);
            if (!isFlying) {
                OrbitData orbit = GetIdleOrbit(i);
                armorBlocks_[i]->SetTranslate(orbit.pos);
                armorBlocks_[i]->SetScale(orbit.scale);
                armorBlocks_[i]->SetRotation(orbit.rot);
                armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
            }
        }

        if (armorFlight_.Empty()) {
            animTimer_ += deltaTime;
            if (animTimer_ >= 1.0f) {
                animPhase_ = 0;
                attackMode_ = 0;
                animTimer_ = 0.0f;
            }
        }
    }
}

// 攻撃モード3の各フェーズを更新します。
void BossCore::UpdateHammerSequence(float deltaTime) {

    // --- Phase 20: 瞬時にハンマー形態へ変形 ---
    if (animPhase_ == 20) {
        if (animTimer_ == 0.0f) {
            // ... (ハンマー変形の hammerSettings 処理はそのまま) ...
            struct HammerSetting {
                Vector3 translate;
                Vector3 scale;
                Vector3 rotation;
            };
            std::vector<HammerSetting> hammerSettings = {
                {{0.000f, 4.000f, 0.000f}, {1.500f, 1.000f, 1.000f}, {0.0f, 0.0f, 0.0f}},
                {{2.000f, 4.000f, 0.000f}, {0.700f, 1.500f, 1.000f}, {0.0f, 0.0f, 0.0f}},
                {{0.000f, 1.000f, 0.000f}, {0.400f, 2.200f, 0.400f}, {0.0f, 0.0f, 0.0f}},
                {{-2.000f, 4.000f, 0.000f}, {0.700f, 1.500f, 1.000f}, {0.0f, 0.0f, 0.0f}},
                {{0.000f, -1.800f, -0.002f}, {0.500f, 0.500f, 0.800f}, {0.0f, 0.0f, 0.0f}},
                {{0.000f, 5.100f, 0.000f}, {0.500f, 0.250f, 0.500f}, {0.0f, 0.0f, 0.0f}}};
            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                if (i < hammerSettings.size()) {
                    armorBlocks_[i]->SetTranslate(hammerSettings[i].translate);
                    armorBlocks_[i]->SetScale(hammerSettings[i].scale);
                    armorBlocks_[i]->SetRotation(hammerSettings[i].rotation);
                    armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
                }
            }
        }

        animTimer_ += deltaTime;

        if (target_) {
            Vector3 toPlayer = target_->GetWorldPosition() - GetWorldPosition();
            float angleY = std::atan2(toPlayer.x, toPlayer.z) - (std::numbers::pi_v<float> / 2.0f);
            SetRotation({0.0f, angleY, 0.0f});
            GetTransform()->isQuaternionMaster = false;
        }

        if (animTimer_ >= 1.5f) {
            animPhase_ = 21;
            animTimer_ = 0.0f;

            if (target_) {
                animTargetPos_ = target_->GetWorldPosition();
            } else {
                animTargetPos_ = GetTranslate();
            }

            if (warningArea_) {
                // 高さをプレイヤーと同じ（2m）にし、位置を1m浮かせて地面に接地させる
                if (armorBlocks_.size() > 1 && armorBlocks_[1]) {
                    Vector3 block2Scale = armorBlocks_[1]->GetScale();
                    warningArea_->SetScale({block2Scale.x, 2.0f, block2Scale.z}); // 高さ2.0f
                }
                warningArea_->SetTranslate({animTargetPos_.x, 1.0f, animTargetPos_.z}); // 中心を1.0fにする

                float finalRotY = GetRotation().y + (std::numbers::pi_v<float> / 2.0f);
                warningArea_->SetRotation({0.0f, finalRotY, 0.0f});
                warningArea_->GetTransform()->isQuaternionMaster = false;

                // 透明度を薄く（0.3f）してスタート
                warningArea_->SetColor({1.0f, 1.0f, 0.0f, 0.9f});
            }
        }
    }
    // --- Phase 21: ロックオンした位置へ移動 ＆ 振りかぶる！ ---
    else if (animPhase_ == 21) {
        animTimer_ += deltaTime;

        float moveDuration = 4.5f;
        float moveT = std::min(animTimer_ / moveDuration, 1.0f);

        if (warningArea_) {
            // 予兆エリアの色を黄色から赤へ遷移
            float currentGreen = Math::Lerp(1.0f, 0.0f, moveT);
            warningArea_->SetColor({1.0f, currentGreen, 0.0f, 0.9f});
        }

        // ... (移動処理と回転処理は今のまま) ...
        Vector3 targetPos = animTargetPos_;
        Vector3 currentPos = GetTranslate();
        Vector3 toBoss = currentPos - targetPos;
        toBoss.y = 0.0f;
        float dist = std::sqrt(toBoss.x * toBoss.x + toBoss.z * toBoss.z);
        if (dist > 0.0f) {
            toBoss.x /= dist;
            toBoss.z /= dist;
        }
        Vector3 targetHoverPos = {targetPos.x + toBoss.x * 4.5f, targetPos.y + 1.0f, targetPos.z + toBoss.z * 4.5f};
        float easeT = moveT;
        currentPos.x = Math::Lerp(currentPos.x, targetHoverPos.x, easeT);
        currentPos.y = Math::Lerp(currentPos.y, targetHoverPos.y, easeT);
        currentPos.z = Math::Lerp(currentPos.z, targetHoverPos.z, easeT);
        SetTranslate(currentPos);
        float rotT = std::min(animTimer_ / 3.0f, 1.0f);
        Vector3 toPlayer = targetPos - currentPos;
        float angleY = std::atan2(toPlayer.x, toPlayer.z) - (std::numbers::pi_v<float> / 2.0f);
        float elasticT = 0.0f;
        if (rotT == 0.0f)
            elasticT = 0.0f;
        else if (rotT == 1.0f)
            elasticT = 1.0f;
        else {
            float c4 = (2.0f * std::numbers::pi_v<float>) / 3.0f;
            elasticT = -std::pow(2.0f, 10.0f * rotT - 10.0f) * std::sin((rotT * 10.0f - 10.75f) * c4);
        }
        float targetTilt = -70.0f * (std::numbers::pi_v<float> / 180.0f);
        float tiltBack = Math::Lerp(0.0f, targetTilt, elasticT);
        SetRotation({0.0f, angleY, tiltBack});
        GetTransform()->isQuaternionMaster = false;

        if (moveT >= 1.0f) {
            animPhase_ = 22;
            animTimer_ = 0.0f;
        }
    }
    // --- Phase 22: 一気に振り下ろして叩き潰す！ ---
    else if (animPhase_ == 22) {
        animTimer_ += deltaTime;
        float smashDuration = 0.15f;
        float t = std::min(animTimer_ / smashDuration, 1.0f);

        if (warningArea_) {
            // 振り下ろし中も赤色を表示し続ける
            warningArea_->SetColor({1.0f, 0.0f, 0.0f, 0.9f});
        }

        float startRotZ = -70.0f * (std::numbers::pi_v<float> / 180.0f);
        float endRotZ = 270.0f * (std::numbers::pi_v<float> / 180.0f);
        float currentRotZ = Math::Lerp(startRotZ, endRotZ, std::pow(t, 3.0f));
        SetRotation({0.0f, GetRotation().y, currentRotZ});

        if (t >= 1.0f) {
            SetRotation({0.0f, GetRotation().y, endRotZ});
            if (warningArea_) {
                warningArea_->SetScale({0.0f, 0.0f, 0.0f});
            }
            animPhase_ = 23;
            animTimer_ = 0.0f;
        }
    }
    // --- Phase 23: 地面に倒れたまま3秒待機 ---
    else if (animPhase_ == 23) {

        if (animTimer_ == 0.0f) {
            animStartPos_ = GetRotation();
        }

        animTimer_ += deltaTime;

        if (animTimer_ >= 3.0f) {
            animPhase_ = 24;
            animTimer_ = 0.0f;
        }
    }
    // --- Phase 24: 待機軌道に向かって復帰する ---
    else if (animPhase_ == 24) {
        // (ここは今までと全く同じなので省略せずにそのまま残します)
        if (animTimer_ == 0.0f) {
            blockStartPos_.clear();
            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                blockStartPos_.push_back(armorBlocks_[i]->GetTranslate());
            }
        }

        animTimer_ += deltaTime;
        float duration = 1.0f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::OutExpo(t);

        Vector3 bossPos = GetTranslate();
        bossPos.y = Math::Lerp(bossPos.y, 4.0f, easeT);
        SetTranslate(bossPos);

        Vector3 currentRot;
        currentRot.x = Math::Lerp(animStartPos_.x, 0.0f, easeT);
        currentRot.y = Math::Lerp(animStartPos_.y, 0.0f, easeT);
        currentRot.z = Math::Lerp(animStartPos_.z, 0.0f, easeT);
        SetRotation(currentRot);

        GetTransform()->isQuaternionMaster = false;

        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            if (i < blockStartPos_.size()) {
                OrbitData orbit = GetIdleOrbit(i);
                Vector3 pos = Math::Lerp(blockStartPos_[i], orbit.pos, easeT);
                armorBlocks_[i]->SetTranslate(pos);
                armorBlocks_[i]->SetScale(orbit.scale);
                armorBlocks_[i]->SetRotation(orbit.rot);
                armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
            }
        }

        if (t >= 1.0f) {
            animPhase_ = 0;
            attackMode_ = 0;
            animTimer_ = 0.0f;
        }
    }
}

// 攻撃モード4の各フェーズを更新します。
void BossCore::UpdateWallSequence(float deltaTime) {

    // --- Phase 39: コンボ数に応じた壁の配置計算 ---
    if (animPhase_ == 39) {
        blockStartPos_.clear();
        blockTargetPos_.clear();

        float blockWidth = 25.0f;

        Vector3 bossCurrentPos = GetTranslate();
        animStartPos_ = bossCurrentPos; // 移動のスタート地点を記憶

        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            if (shotCount_ == 0) {
                Vector3 localPos = armorBlocks_[i]->GetTranslate();
                float bossRotY = GetRotation().y;
                Vector3 worldPos;
                worldPos.x = bossCurrentPos.x + (localPos.x * std::cos(bossRotY) + localPos.z * std::sin(bossRotY));
                worldPos.y = bossCurrentPos.y + localPos.y;
                worldPos.z = bossCurrentPos.z + (-localPos.x * std::sin(bossRotY) + localPos.z * std::cos(bossRotY));

                armorBlocks_[i]->SetParent(nullptr);
                armorBlocks_[i]->SetTranslate(worldPos);
                blockStartPos_.push_back(worldPos);
            } else {
                blockStartPos_.push_back(armorBlocks_[i]->GetTranslate());
            }

            float offset = -(i - 2.5f) * blockWidth;
            Vector3 targetPos;

            if (shotCount_ == 0) {
                targetPos = {offset, 2.0f, 150.0f};
                armorBlocks_[i]->SetScale({blockWidth, 4.0f, 1.0f});
            } else if (shotCount_ == 1) {
                targetPos = {offset, 2.0f, -150.0f};
                armorBlocks_[i]->SetScale({blockWidth, 4.0f, 1.0f});
            } else if (shotCount_ == 2) {
                targetPos = {150.0f, 2.0f, offset};
                armorBlocks_[i]->SetScale({1.0f, 4.0f, blockWidth});
            } else if (shotCount_ == 3) {
                targetPos = {-150.0f, 2.0f, offset};
                armorBlocks_[i]->SetScale({1.0f, 4.0f, blockWidth});
            }

            blockTargetPos_.push_back(targetPos);
            armorBlocks_[i]->SetRotation({0.0f, 0.0f, 0.0f});
            armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
        }

        animPhase_ = 40;
        animTimer_ = 0.0f;
    }
    // --- Phase 40: ボスが上空へ先回り ＆ ブロックが壁を形成 ---
    else if (animPhase_ == 40) {
        animTimer_ += deltaTime;
        float duration = 1.5f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::OutExpo(t);

        Vector3 bossPos = GetTranslate();

        if (shotCount_ == 0) {
            bossPos.x = Math::Lerp(animStartPos_.x, 0.0f, easeT);
            bossPos.y = Math::Lerp(animStartPos_.y, 8.0f, easeT);
            bossPos.z = Math::Lerp(animStartPos_.z, 150.0f, easeT); // 奥
        } else if (shotCount_ == 1) {
            bossPos.x = Math::Lerp(animStartPos_.x, 0.0f, easeT);
            bossPos.y = Math::Lerp(animStartPos_.y, 8.0f, easeT);
            bossPos.z = Math::Lerp(animStartPos_.z, -150.0f, easeT); // 手前
        } else if (shotCount_ == 2) {
            bossPos.x = Math::Lerp(animStartPos_.x, 150.0f, easeT); // 右
            bossPos.y = Math::Lerp(animStartPos_.y, 8.0f, easeT);
            bossPos.z = Math::Lerp(animStartPos_.z, 0.0f, easeT);
        } else if (shotCount_ == 3) {
            bossPos.x = Math::Lerp(animStartPos_.x, -150.0f, easeT); // 左
            bossPos.y = Math::Lerp(animStartPos_.y, 8.0f, easeT);
            bossPos.z = Math::Lerp(animStartPos_.z, 0.0f, easeT);
        }
        SetTranslate(bossPos);

        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            if (i < blockStartPos_.size() && i < blockTargetPos_.size()) {
                Vector3 pos = Math::Lerp(blockStartPos_[i], blockTargetPos_[i], easeT);
                armorBlocks_[i]->SetTranslate(pos);
            }
        }

        SetRotation({0.0f, 0.0f, 0.0f});
        GetTransform()->isQuaternionMaster = false;

        if (t >= 1.0f) {
            animPhase_ = 41;
            animTimer_ = 0.0f;
        }
    }
    // --- Phase 41: 壁だけがステージを往復横断！ ---
    else if (animPhase_ == 41) {
        animTimer_ += deltaTime;
        float duration = 5.0f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = std::pow(t, 2.0f);

        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            Vector3 blockPos = armorBlocks_[i]->GetTranslate();

            if (shotCount_ == 0) {
                blockPos.z = Math::Lerp(150.0f, -150.0f, easeT);
            } else if (shotCount_ == 1) {
                blockPos.z = Math::Lerp(-150.0f, 150.0f, easeT);
            } else if (shotCount_ == 2) {
                blockPos.x = Math::Lerp(150.0f, -150.0f, easeT);
            } else if (shotCount_ == 3) {
                blockPos.x = Math::Lerp(-150.0f, 150.0f, easeT);
            }
            armorBlocks_[i]->SetTranslate(blockPos);
        }

        if (t >= 1.0f) {
            animPhase_ = 42;
            animTimer_ = 0.0f;
        }
    }
    // --- Phase 42: 攻撃後の猶予 ＆ 往復のループ判定！ ---
    else if (animPhase_ == 42) {
        animTimer_ += deltaTime;

        if (animTimer_ >= 0.5f) {
            shotCount_++;

            if (shotCount_ < 4) {
                animPhase_ = 39;
            } else {
                animPhase_ = 43;
            }
            animTimer_ = 0.0f;
        }
    }
    // --- Phase 43: 親子関係を復活させ、コアも元の定位置に戻る ---
    else if (animPhase_ == 43) {

        if (animTimer_ == 0.0f) {
            blockStartPos_.clear();
            Vector3 bossPos = GetTranslate();
            float bossRotY = GetRotation().y;

            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                Vector3 worldPos = armorBlocks_[i]->GetTranslate();
                Vector3 offset = {worldPos.x - bossPos.x, worldPos.y - bossPos.y, worldPos.z - bossPos.z};
                Vector3 localPos;
                localPos.x = offset.x * std::cos(-bossRotY) + offset.z * std::sin(-bossRotY);
                localPos.y = offset.y;
                localPos.z = -offset.x * std::sin(-bossRotY) + offset.z * std::cos(-bossRotY);

                armorBlocks_[i]->SetParent(this);
                armorBlocks_[i]->SetTranslate(localPos);
                blockStartPos_.push_back(localPos);
            }
            animStartPos_ = bossPos;
        }

        animTimer_ += deltaTime;
        float duration = 3.0f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::OutExpo(t);

        Vector3 bossPos = GetTranslate();
        bossPos.x = Math::Lerp(animStartPos_.x, 0.0f, easeT);
        bossPos.y = Math::Lerp(animStartPos_.y, 4.0f, easeT);
        bossPos.z = Math::Lerp(animStartPos_.z, 0.0f, easeT);
        SetTranslate(bossPos);

        SetRotation({0.0f, 0.0f, 0.0f});
        GetTransform()->isQuaternionMaster = false;

        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            if (i < blockStartPos_.size()) {
                OrbitData orbit = GetIdleOrbit(i);
                Vector3 pos = Math::Lerp(blockStartPos_[i], orbit.pos, easeT);
                armorBlocks_[i]->SetTranslate(pos);
                armorBlocks_[i]->SetScale(orbit.scale);
                armorBlocks_[i]->SetRotation(orbit.rot);
                armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
            }
        }

        if (t >= 1.0f) {
            animPhase_ = 0;
            attackMode_ = 0;
            animTimer_ = 0.0f;
        }
    }
}

// 攻撃モード5の各フェーズを更新します。
void BossCore::UpdateCollapseSequence(float deltaTime) {

    // --- Phase 50: ボスコアが画面の端（X = -50）まで行く ---
    if (animPhase_ == 50) {
        if (animTimer_ == 0.0f) {
            animStartPos_ = GetTranslate();
        }
        animTimer_ += deltaTime;
        float duration = 2.0f; // 2秒かけて移動
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::OutExpo(t);

        Vector3 pos = GetTranslate();
        pos.x = Math::Lerp(animStartPos_.x, -50.0f, easeT); // 左端へ

        pos.y = Math::Lerp(animStartPos_.y, 24.0f, easeT);
        SetTranslate(pos);

        if (target_) {
            // プレイヤーを向くように回転。背中合わせにならないよう調整
            Vector3 toPlayer = target_->GetWorldPosition() - GetWorldPosition();
            float angleY = std::atan2(toPlayer.x, toPlayer.z) + std::numbers::pi_v<float>;

            SetRotation({GetRotation().x, angleY, GetRotation().z});
            GetTransform()->isQuaternionMaster = false;
        }

        if (t >= 1.0f) {
            animPhase_ = 51;
            animTimer_ = 0.0f;
        }
    }
    // --- Phase 51: ボスのブロックが人型に変形する ---
    else if (animPhase_ == 51) {
        if (animTimer_ == 0.0f) {
            blockStartPos_.clear();
            blockTargetPos_.clear();
            blockStartScale_.clear();
            blockTargetScale_.clear(); // 初期化

            animStartRot_ = GetRotation();

            // Block4とBlock5の Z軸回転 を 90度（pi/2）に設定
            // ==========================================
            float rotZ90 = std::numbers::pi_v<float> / 2.0f; // 90度のラジアン値

            // 装甲の組立位置・大きさ・ラジアン回転。一連の部品を同じ設定表から動かす。
            struct BlockSetting {
                Vector3 translate;
                Vector3 scale;
                Vector3 rotation;
            };
            std::vector<BlockSetting> settings = {
                {{-5.0f, -22.5f, 6.0f}, {2.0f, 4.0f, 5.0f}, {0.0f, 0.0f, 0.0f}},  // Block1(足)
                {{0.0f, 21.5f, 6.0f}, {10.0f, 10.0f, 5.0f}, {0.0f, 0.0f, 0.0f}},  // Block2(頭)
                {{0.0f, -3.5f, 6.0f}, {8.0f, 15.0f, 5.0f}, {0.0f, 0.0f, 0.0f}},   // Block3(胴体)
                {{11.0f, 0.0f, 6.0f}, {7.0f, 3.0f, 5.0f}, {0.0f, 0.0f, rotZ90}},  // Block4(右腕)
                {{-11.0f, 0.0f, 6.0f}, {7.0f, 3.0f, 5.0f}, {0.0f, 0.0f, rotZ90}}, // Block5(左腕)
                {{5.0f, -22.5f, 6.0f}, {2.0f, 4.0f, 5.0f}, {0.0f, 0.0f, 0.0f}}    // Block6(足)
            };

            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                armorBlocks_[i]->SetParent(this);
                blockStartPos_.push_back(armorBlocks_[i]->GetTranslate());
                blockStartScale_.push_back(armorBlocks_[i]->GetScale()); // 開始時スケールを記憶

                if (i < settings.size()) {
                    blockTargetPos_.push_back(settings[i].translate);
                    blockTargetScale_.push_back(settings[i].scale); // 目標スケールを記憶
                    armorBlocks_[i]->SetRotation(settings[i].rotation);
                    armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
                } else {
                    blockTargetPos_.push_back({0.0f, 0.0f, 0.0f});
                    blockTargetScale_.push_back({0.0f, 0.0f, 0.0f});
                }
            }

            if (warningArea_) {
                warningArea_->SetTranslate({0.0f, -0.5f, 6.0f});
                warningArea_->SetScale({35.0f, 55.0f, 5.0f});
                warningArea_->SetRotation({0.0f, 0.0f, 0.0f});
                warningArea_->GetTransform()->isQuaternionMaster = false;
                warningArea_->SetColor({1.0f, 1.0f, 0.0f, 0.5f});
            }
        }

        animTimer_ += deltaTime;
        float duration = 1.5f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::OutExpo(t);

        // コアはそのままに、ブロック全体を 0.2倍 に圧縮
        // ==========================================
        float currentOverallScale = Math::Lerp(1.0f, 0.2f, easeT);

        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            if (i < blockStartPos_.size() && i < blockTargetPos_.size()) {
                // 目標の座標もスケールも、圧縮率(0.2倍)をかける！
                Vector3 targetP = {blockTargetPos_[i].x * 0.2f, blockTargetPos_[i].y * 0.2f,
                                   blockTargetPos_[i].z * 0.2f};
                Vector3 pos = Math::Lerp(blockStartPos_[i], targetP, easeT);
                armorBlocks_[i]->SetTranslate(pos);

                Vector3 targetS = {blockTargetScale_[i].x * 0.2f, blockTargetScale_[i].y * 0.2f,
                                   blockTargetScale_[i].z * 0.2f};
                Vector3 scale = Math::Lerp(blockStartScale_[i], targetS, easeT);
                armorBlocks_[i]->SetScale(scale);
            }
        }

        // コアの回転演出
        float spinCount = 3.0f;
        float totalAngle = spinCount * 2.0f * std::numbers::pi_v<float>;
        Vector3 rot = GetRotation();
        rot.y = animStartRot_.y - totalAngle * (1.0f - easeT);
        SetRotation(rot);
        GetTransform()->isQuaternionMaster = false;

        if (t >= 1.0f) {
            animPhase_ = 52;
            animTimer_ = 0.0f;
        }
    }
    // --- Phase 52: 倒れる前のタメ（予兆が赤くなる ＆ 巨大化！） ---
    else if (animPhase_ == 52) {
        animTimer_ += deltaTime;

        // ==========================================
        // 倒れるまでの「合計のタメ時間」をここで長くする！（例：2.5秒）
        // ==========================================
        float duration = 2.5f;
        float t = std::min(animTimer_ / duration, 1.0f);

        if (warningArea_) {
            float currentGreen = Math::Lerp(1.0f, 0.0f, t);
            warningArea_->SetColor({1.0f, currentGreen, 0.0f, 0.5f});
        }

        float currentOverallScale = 1.0f;

        // ==========================================
        // 't' ではなく 'animTimer_'（絶対時間）を使う！
        // ==========================================
        if (animTimer_ < 0.15f) {
            currentOverallScale = 0.2f; // 小さいまま力を溜める
        } else if (animTimer_ < 0.3f) {
            currentOverallScale = 1.2f; // バッ！と少し大きめに膨らむ
        } else if (animTimer_ < 0.45f) {
            currentOverallScale = 0.5f; // また縮む
        } else if (animTimer_ < 0.6f) {
            currentOverallScale = 1.1f; // また膨らむ
        } else if (animTimer_ < 0.75f) {
            currentOverallScale = 0.8f; // ちょい縮む
        } else {
            // 指定時間経過後、攻撃開始までスケールを固定して静止
            currentOverallScale = 1.0f;
        }

        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            if (i < blockTargetPos_.size()) {
                Vector3 pos = {blockTargetPos_[i].x * currentOverallScale, blockTargetPos_[i].y * currentOverallScale,
                               blockTargetPos_[i].z * currentOverallScale};
                armorBlocks_[i]->SetTranslate(pos);

                Vector3 scale = {blockTargetScale_[i].x * currentOverallScale,
                                 blockTargetScale_[i].y * currentOverallScale,
                                 blockTargetScale_[i].z * currentOverallScale};
                armorBlocks_[i]->SetScale(scale);
            }
        }

        if (t >= 1.0f) {
            animPhase_ = 53;
            animTimer_ = 0.0f;
            animStartRot_ = GetRotation();
            animStartPos_ = GetTranslate();
        }
    }
    // --- Phase 53: 前にぶっ倒れて叩き潰す！ ---
    else if (animPhase_ == 53) {
        animTimer_ += deltaTime;
        float duration = 2.0f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = std::pow(t, 3.0f);

        // ==========================================
        // 全体が1.0倍なので、つま先の距離(ピボット)も1.5倍！
        // ==========================================
        float overallScale = 1.0f;
        float pivotY = -24.5f * overallScale;
        float pivotZ = 8.5f * overallScale;

        float currentRotX = Math::Lerp(animStartRot_.x, -90.0f * (std::numbers::pi_v<float> / 180.0f), easeT);

        Vector3 rot = animStartRot_;
        rot.x = currentRotX;
        SetRotation(rot);
        GetTransform()->isQuaternionMaster = false;

        float dirX = std::sin(animStartRot_.y);
        float dirZ = std::cos(animStartRot_.y);

        Vector3 pivotWorldPos;
        pivotWorldPos.x = animStartPos_.x + dirX * pivotZ;
        pivotWorldPos.y = animStartPos_.y + pivotY;
        pivotWorldPos.z = animStartPos_.z + dirZ * pivotZ;

        float rotLocalY = pivotY * std::cos(currentRotX) - pivotZ * std::sin(currentRotX);
        float rotLocalZ = pivotY * std::sin(currentRotX) + pivotZ * std::cos(currentRotX);

        Vector3 newPos;
        newPos.x = pivotWorldPos.x - dirX * rotLocalZ;
        newPos.y = pivotWorldPos.y - rotLocalY;
        newPos.z = pivotWorldPos.z - dirZ * rotLocalZ;
        SetTranslate(newPos);

        // ==========================================
        // 右腕と左腕で「回転する方向」を逆にして、外側を通す！
        // ==========================================
        float startArmAngle = std::numbers::pi_v<float> / 2.0f; // 90度
        float armOffset45 = std::numbers::pi_v<float> / 4.0f;   // 45度のラジアン

        // ① 右腕（Block4）は 270度(真上)の手前、225度に向かって「反時計回り（外回り）」
        // 3pi/2 (270度) - pi/4 (45度) = 225度
        float targetArmAngleRight = (3.0f * std::numbers::pi_v<float> / 2.0f) - armOffset45;
        float currentArmAngleRight = Math::Lerp(startArmAngle, targetArmAngleRight, easeT);

        // ② 左腕（Block5）は -90度(真上)の手前、-45度に向かって「時計回り（外回り）」
        // -pi/2 (-90度) + pi/4 (45度) = -45度
        float targetArmAngleLeft = (-std::numbers::pi_v<float> / 2.0f) + armOffset45;
        float currentArmAngleLeft = Math::Lerp(startArmAngle, targetArmAngleLeft, easeT);

        float armHalfLength = 3.5f; // 腕の長さ(Scale.x = 7.0)の半分
        float shoulderY = 3.5f;     // 肩の高さ（中心0.0 + 半分3.5）

        // 右腕 (Block4: index 3)
        if (armorBlocks_.size() > 3 && armorBlocks_[3]) {
            Vector3 pos = armorBlocks_[3]->GetTranslate();
            // 右腕用の角度(currentArmAngleRight)で計算
            pos.x = 11.0f - armHalfLength * std::cos(currentArmAngleRight);
            pos.y = shoulderY - armHalfLength * std::sin(currentArmAngleRight);

            armorBlocks_[3]->SetTranslate(pos);
            armorBlocks_[3]->SetRotation({0.0f, 0.0f, currentArmAngleRight});
            armorBlocks_[3]->GetTransform()->isQuaternionMaster = false;
        }

        // 左腕 (Block5: index 4)
        if (armorBlocks_.size() > 4 && armorBlocks_[4]) {
            Vector3 pos = armorBlocks_[4]->GetTranslate();
            // 左腕用の角度(currentArmAngleLeft)で計算
            pos.x = -11.0f - armHalfLength * std::cos(currentArmAngleLeft);
            pos.y = shoulderY - armHalfLength * std::sin(currentArmAngleLeft);

            armorBlocks_[4]->SetTranslate(pos);
            armorBlocks_[4]->SetRotation({0.0f, 0.0f, currentArmAngleLeft});
            armorBlocks_[4]->GetTransform()->isQuaternionMaster = false;
        }

        if (warningArea_)
            warningArea_->SetColor({1.0f, 0.0f, 0.0f, 0.9f});

        if (t >= 1.0f) {
            if (warningArea_)
                warningArea_->SetScale({0.0f, 0.0f, 0.0f});
            animPhase_ = 54;
            animTimer_ = 0.0f;
        }
    }
    // --- Phase 54: 倒れたまま待機（攻撃チャンス） ---
    else if (animPhase_ == 54) {
        animTimer_ += deltaTime;
        if (animTimer_ >= 3.0f) {
            animPhase_ = 55;
            animTimer_ = 0.0f;
        }
    }
    // --- Phase 55: 起き上がって復帰 ---
    else if (animPhase_ == 55) {
        if (animTimer_ == 0.0f) {
            blockStartPos_.clear();
            blockStartScale_.clear();
            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                blockStartPos_.push_back(armorBlocks_[i]->GetTranslate());
                blockStartScale_.push_back(armorBlocks_[i]->GetScale());
            }
            animStartRot_ = GetRotation();
        }

        animTimer_ += deltaTime;
        float duration = 1.5f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::InOutSine(t);

        // 起き上がりのピボットも 1.0倍 で計算
        float overallScale = 1.0f;
        float pivotY = -24.5f * overallScale;
        float pivotZ = 8.5f * overallScale;
        float dirX = std::sin(animStartRot_.y);
        float dirZ = std::cos(animStartRot_.y);

        float startRotLocalY = pivotY * std::cos(animStartRot_.x) - pivotZ * std::sin(animStartRot_.x);
        float startRotLocalZ = pivotY * std::sin(animStartRot_.x) + pivotZ * std::cos(animStartRot_.x);
        Vector3 startToeWorld = {animStartPos_.x + dirX * startRotLocalZ, animStartPos_.y + startRotLocalY,
                                 animStartPos_.z + dirZ * startRotLocalZ};

        Vector3 targetToeWorld = {0.0f + dirX * pivotZ, 4.0f + pivotY, 0.0f + dirZ * pivotZ};

        Vector3 currentToeWorld = {Math::Lerp(startToeWorld.x, targetToeWorld.x, easeT),
                                   Math::Lerp(startToeWorld.y, targetToeWorld.y, easeT),
                                   Math::Lerp(startToeWorld.z, targetToeWorld.z, easeT)};

        float currentRotX = Math::Lerp(animStartRot_.x, 0.0f, easeT);
        Vector3 rot = animStartRot_;
        rot.x = currentRotX;
        SetRotation(rot);
        GetTransform()->isQuaternionMaster = false;

        float currentRotLocalY = pivotY * std::cos(currentRotX) - pivotZ * std::sin(currentRotX);
        float currentRotLocalZ = pivotY * std::sin(currentRotX) + pivotZ * std::cos(currentRotX);

        Vector3 newPos = {currentToeWorld.x - dirX * currentRotLocalZ, currentToeWorld.y - currentRotLocalY,
                          currentToeWorld.z - dirZ * currentRotLocalZ};
        SetTranslate(newPos);

        // ブロックを元の待機軌道に戻す（スケールも元通りに！）
        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            if (i < blockStartPos_.size()) {
                OrbitData orbit = GetIdleOrbit(i);
                Vector3 bPos = Math::Lerp(blockStartPos_[i], orbit.pos, easeT);
                armorBlocks_[i]->SetTranslate(bPos);

                Vector3 bScale = Math::Lerp(blockStartScale_[i], orbit.scale, easeT); // ★追加：スケールも戻す
                armorBlocks_[i]->SetScale(bScale);

                armorBlocks_[i]->SetRotation(orbit.rot);
                armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
            }
        }

        if (t >= 1.0f) {
            animPhase_ = 0;
            attackMode_ = 0;
            animTimer_ = 0.0f;
        }
    }
}

// 攻撃モード6の各フェーズを更新します。
void BossCore::UpdateLaserSequence(float deltaTime) {

    // 回転スピードの調整用変数
    // ==========================================
    float maxSpinSpeed = 8.0f;  // タメ・変形中の大回転トップスピード！
    float fireSpinSpeed = 0.3f; // レーザー発射中の少し落ち着いた回転スピード

    // --- Phase 60: まずはコアが中央(0,0)へスゥーッと移動する ---
    if (animPhase_ == 60) {
        if (animTimer_ == 0.0f) {
            animStartPos_ = GetTranslate(); // コアのスタート位置
        }

        animTimer_ += deltaTime;
        float duration = 2.5f; // 実時間で約1.6秒
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::InOutSine(t);

        // コア本体をステージ中央 (X=0, Y=4.0, Z=0) へ移動
        Vector3 corePos = GetTranslate();
        corePos.x = Math::Lerp(animStartPos_.x, 0.0f, easeT);
        corePos.y = Math::Lerp(animStartPos_.y, 2.0f, easeT);
        corePos.z = Math::Lerp(animStartPos_.z, 0.0f, easeT);
        SetTranslate(corePos);

        // 移動中、ブロックたちは普段の「フワフワ待機軌道」のまま追従
        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            OrbitData orbit = GetIdleOrbit(i);
            armorBlocks_[i]->SetTranslate(orbit.pos);
            armorBlocks_[i]->SetScale(orbit.scale);
            armorBlocks_[i]->SetRotation(orbit.rot);
            armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
        }

        if (t >= 1.0f) {
            animPhase_ = 61;
            animTimer_ = 0.0f;
        }
    }
    // --- Phase 61: 待機軌道のまま、コアがギュイィィンと回転し始める（回転だけの時間） ---
    else if (animPhase_ == 61) {
        if (animTimer_ == 0.0f) {
            // この瞬間のフワフワ軌道を「固定」してコアの子パーツにする！
            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                OrbitData orbit = GetIdleOrbit(i);
                armorBlocks_[i]->SetParent(this);
                armorBlocks_[i]->SetTranslate(orbit.pos);
                armorBlocks_[i]->SetScale(orbit.scale);
                armorBlocks_[i]->SetRotation(orbit.rot);
                armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
            }
        }

        animTimer_ += deltaTime;
        // 実時間で1.5秒間、回転だけのタメを作る (1.5 * 1.5 = 2.25f)
        float duration = 2.25f;
        float t = std::min(animTimer_ / duration, 1.0f);

        // コアの回転スピードを 0 から maxSpinSpeed(5.0f) まで徐々に上げる！（加速演出）
        float currentSpinSpeed = Math::Lerp(0.0f, maxSpinSpeed, Easing::InSine(t));

        Vector3 rot = GetRotation();
        rot.y += currentSpinSpeed * deltaTime;
        SetRotation(rot);
        GetTransform()->isQuaternionMaster = false;

        if (t >= 1.0f) {
            animPhase_ = 62;
            animTimer_ = 0.0f;
        }
    }
    // --- Phase 62: 大回転を維持したまま、遠心力で開くように砲台陣形へ変形！ ---
    else if (animPhase_ == 62) {
        if (animTimer_ == 0.0f) {
            blockStartPos_.clear();
            blockTargetPos_.clear();
            blockStartScale_.clear();
            blockTargetScale_.clear();
            attentionStartRot_.clear(); // ヘッダにある回転記憶用の変数を流用

            float radius = 12.0f; // 陣形の半径

            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                blockStartPos_.push_back(armorBlocks_[i]->GetTranslate());
                blockStartScale_.push_back(armorBlocks_[i]->GetScale());
                attentionStartRot_.push_back(armorBlocks_[i]->GetRotation()); // 現在の回転を記憶

                float angle = (i * 2.0f * std::numbers::pi_v<float>) / armorBlocks_.size();
                Vector3 targetPos = {std::cos(angle) * radius, 0.0f, std::sin(angle) * radius};
                blockTargetPos_.push_back(targetPos);
                blockTargetScale_.push_back({1.5f, 1.5f, 1.5f});
            }
        }

        animTimer_ += deltaTime;
        // 変形は実時間で2.0秒 (2.0 * 1.5 = 3.0f)
        float duration = 3.0f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::InOutSine(t);

        // コアは大回転（トップスピード 5.0f）をそのまま継続！
        Vector3 rot = GetRotation();
        rot.y += maxSpinSpeed * deltaTime;
        SetRotation(rot);
        GetTransform()->isQuaternionMaster = false;

        // ブロックの座標・スケール・角度を砲台の定位置へ補間
        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            if (i < blockStartPos_.size() && i < blockTargetPos_.size()) {
                Vector3 pos = Math::Lerp(blockStartPos_[i], blockTargetPos_[i], easeT);
                armorBlocks_[i]->SetTranslate(pos);

                Vector3 scale = Math::Lerp(blockStartScale_[i], blockTargetScale_[i], easeT);
                armorBlocks_[i]->SetScale(scale);

                // 角度も、待機軌道のナナメ向きから「外側を向く角度」へ補間
                float angle = (i * 2.0f * std::numbers::pi_v<float>) / armorBlocks_.size();
                Vector3 targetRot = {0.0f, -angle, 0.0f};

                Vector3 currentRot;
                currentRot.x = Math::Lerp(attentionStartRot_[i].x, targetRot.x, easeT);
                currentRot.y = Math::Lerp(attentionStartRot_[i].y, targetRot.y, easeT);
                currentRot.z = Math::Lerp(attentionStartRot_[i].z, targetRot.z, easeT);

                armorBlocks_[i]->SetRotation(currentRot);
                armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
            }
        }

        // 展開完了したら、完全停止フェーズへ！
        if (t >= 1.0f) {
            animPhase_ = 63;
            animTimer_ = 0.0f;
        }
    }
    // --- Phase 63: 陣形完了後、0.5秒間完全に沈黙する！（嵐の前の静けさ ＋ 予兆レーザー） ---
    else if (animPhase_ == 63) {
        if (animTimer_ == 0.0f) {
            for (Object3d* block : armorBlocks_) {
                if (!block)
                    continue;
                for (Object3d* child : block->GetChildren()) {
                    if (child->GetName().find("Beam_Cylinder") != std::string::npos) {

                        // Y軸を長さ(80.0f)に、XZを極細(0.1f)に設定
                        // ==========================================
                        child->SetScale({0.1f, 80.0f, 0.1f});

                        // 円柱をX軸に90度（pi/2）倒して、正面に向ける
                        // ==========================================
                        float rotX90 = std::numbers::pi_v<float> / 2.0f;
                        child->SetRotation({rotX90, 0.0f, 0.0f});
                        child->GetTransform()->isQuaternionMaster = false;

                        child->SetColor({1.0f, 0.0f, 0.0f, 0.5f}); // 半透明の赤
                        child->SetCollisionAttribute(0);           // 当たり判定なし
                    }
                }
            }
        }

        animTimer_ += deltaTime;

        // 実時間で 0.5秒間停止 (0.5 * 1.5 = 0.75f)
        float stopDuration = 0.75f;

        if (animTimer_ >= stopDuration) {
            animPhase_ = 64;
            animTimer_ = 0.0f;
        }
    }
    // --- Phase 64: 陣形を維持したまま回転し、ビームを撃つ！ ---
    else if (animPhase_ == 64) {
        animTimer_ += deltaTime;

        // 実時間で5.0秒間レーザーを撃ち続ける (5.0 * 1.5 = 7.5f)
        float spinDuration = 7.5f;

        // 回転スピードをレーザー用の速度（fireSpinSpeed: 0.5f）に切り替えて回し続ける
        Vector3 rot = GetRotation();
        rot.y += fireSpinSpeed * deltaTime;
        SetRotation(rot);
        GetTransform()->isQuaternionMaster = false;

        // ビームの太さを拡大し、当たり判定を付与
        // ==========================================
        float expandTime = 0.2f; // 0.2秒で極太になる
        float t = std::min(animTimer_ / expandTime, 1.0f);

        // 太さを 0.1 から 3.0 へ一気に膨張させる
        float beamThickness = Math::Lerp(0.1f, 1.0f, Easing::OutExpo(t));

        for (Object3d* block : armorBlocks_) {
            if (!block)
                continue;
            for (Object3d* child : block->GetChildren()) {
                if (child->GetName().find("Beam_Cylinder") != std::string::npos) {

                    // 長さ(Y軸)は80.0fに固定し、太さ(XZ)を拡大
                    // ==========================================
                    child->SetScale({beamThickness, 80.0f, beamThickness});

                    // ※回転（Rotation）はPhase 63で倒したままなので、ここでは設定しなくてOKです！

                    child->SetColor({1.0f, 0.0f, 0.0f, 1.0f});  // 濃い赤
                    child->SetCollisionAttribute(CollisionCategories::Get("enemy_attack")); // 攻撃属性を付与
                }
            }
        }

        // 撃ち終わったら消して復帰！
        if (animTimer_ >= spinDuration) {
            animPhase_ = 65; // 復帰フェーズへ
            animTimer_ = 0.0f;
            animStartRot_ = GetRotation();

            // ビームを非表示にし、当たり判定を削除
            // ==========================================
            for (Object3d* block : armorBlocks_) {
                if (!block)
                    continue;
                for (Object3d* child : block->GetChildren()) {
                    if (child->GetName().find("Beam_Cylinder") != std::string::npos) {
                        child->SetScale({0.0f, 0.0f, 0.0f}); // 見えなくする
                        child->SetCollisionAttribute(0);     // 当たり判定を消す
                    }
                }
            }

            blockStartPos_.clear();
            blockStartScale_.clear();
            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                blockStartPos_.push_back(armorBlocks_[i]->GetTranslate());
                blockStartScale_.push_back(armorBlocks_[i]->GetScale());
            }
        }
    }
    // --- Phase 65: 回転を止め、待機状態のバラバラ軌道へ復帰する ---
    else if (animPhase_ == 65) {
        animTimer_ += deltaTime;
        // 復帰は実時間で1.5秒 (1.5 * 1.5 = 2.25f)
        float duration = 2.25f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::InOutSine(t);

        // コアの回転をゆっくり 0 に戻す
        Vector3 rot = animStartRot_;
        rot.y = Math::Lerp(animStartRot_.y, 0.0f, easeT);
        SetRotation(rot);
        GetTransform()->isQuaternionMaster = false;

        // ブロックを元の待機軌道に戻す
        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            if (i < blockStartPos_.size()) {
                OrbitData orbit = GetIdleOrbit(i);
                Vector3 pos = Math::Lerp(blockStartPos_[i], orbit.pos, easeT);
                armorBlocks_[i]->SetTranslate(pos);

                Vector3 scale = Math::Lerp(blockStartScale_[i], orbit.scale, easeT);
                armorBlocks_[i]->SetScale(scale);

                armorBlocks_[i]->SetRotation(orbit.rot);
                armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
            }
        }

        if (t >= 1.0f) {
            animPhase_ = 0;
            attackMode_ = 0;
            animTimer_ = 0.0f;
        }
    }
}
