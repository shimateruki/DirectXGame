#include "boss_core/BossCoreShared.h"

BossCore::OrbitData BossCore::GetIdleOrbit(size_t index) {
    BossCore::OrbitData data;

    static std::vector<Vector3> randomScales;
    if (randomScales.empty()) {
        for (int i = 0; i < 10; ++i) {
            float randomVal = 0.5f + (static_cast<float>(rand()) / RAND_MAX) * 0.5f;
            randomScales.push_back({ randomVal, randomVal, randomVal });
        }
    }

    Vector3 basePos;
    switch (index % 10) {
    case 0: basePos = { 2.0f,  2.0f,  0.5f }; break;
    case 1: basePos = { -1.5f, -1.5f,  1.8f }; break;
    case 2: basePos = { 0.6f,  2.5f, -1.8f }; break;
    case 3: basePos = { -2.0f,  0.5f, -1.2f }; break;
    case 4: basePos = { 1.5f, -2.0f,  1.2f }; break;
    case 5: basePos = { -0.8f, -0.8f, -2.0f }; break;
    case 6: basePos = { 2.5f, -0.5f, -1.5f }; break;
    case 7: basePos = { -2.5f,  1.0f,  1.5f }; break;
    case 8: basePos = { 0.0f,  3.0f,  1.5f }; break;
    case 9: basePos = { 0.0f, -3.0f,  -1.5f }; break;
    }

    float angle = s_globalIdleTimer * 0.8f;
    float cosY = std::cos(angle);
    float sinY = std::sin(angle);

    Vector3 rotatedPos = {
        basePos.x * cosY - basePos.z * sinY,
        basePos.y,
        basePos.x * sinY + basePos.z * cosY
    };

    float hover = std::sin(s_globalIdleTimer * 1.5f) * 0.3f;
    rotatedPos.y += hover;

    data.pos = rotatedPos;

    float rotY = std::atan2(-data.pos.x, -data.pos.z);
    float xzLen = std::sqrt(data.pos.x * data.pos.x + data.pos.z * data.pos.z);
    float rotX = std::atan2(data.pos.y, xzLen);

    data.rot = { rotX, rotY, 0.0f };
    data.scale = randomScales[index % 10];

    return data;
}

// =================================================================
// 初期化・更新
// =================================================================

void BossCore::Initialize(Object3dCommon* common, const std::string& modelName) {
    BaseEnemy::Initialize(common, modelName);
    SetClassName("BossCore");

    LoadAttackParams();
    SetAttackDamage(attackParams_.damageRush); // 突進攻撃力を標準接触ダメージとしてセット

    barrierHp_ = attackParams_.maxBarrierHp;
    maxBarrierHp_ = attackParams_.maxBarrierHp;

    // 現在登録されているブロックのHPを読み込んだパラメータに初期化
    for (size_t i = 0; i < blockHps_.size(); ++i) {
        blockHps_[i] = attackParams_.maxArmorBlockHp;
    }

    // パラメータが未初期化ならデフォルト値で初期化（アクセス違反防止）
    if (!param_.has_value()) {
        param_ = EntityParameter();
        param_->hp = 1000.0f; // デフォルトHP
        param_->maxHp = 1000.0f;
    }

    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    director_ = std::make_unique<GhostDirector>();
    if (sceneManager_) {
        director_->Initialize(sceneManager_);
    }
    // 登場演出用のアニメーション(JSON)を読み込む
    if (director_) {
        director_->LoadScenario("EntranceAnimation");
    }

    // 色の初期設定 (緑色で統一)
    originalColor_ = { 0.2f, 0.8f, 1.0f, 1.0f };
    SetColor(greenColor_);
    defaultColor_ = greenColor_;

    // --- 1. 紫 ---
    auto BossParticle1 = std::make_unique<GPUParticleEmitter>();
    BossParticle1->Initialize("Boss1", this);
    BossParticle1->Play();

    // --- 2. 黄 ---
    auto BossParticle2 = std::make_unique<GPUParticleEmitter>();
    BossParticle2->Initialize("Boss2", this);
    BossParticle2->Play();
    particleEmitters_.push_back(std::move(BossParticle2));

    // --- 3. 水色 ---
    auto BossParticle3 = std::make_unique<GPUParticleEmitter>();
    BossParticle3->Initialize("Boss3", this);
    BossParticle3->Play();
    particleEmitters_.push_back(std::move(BossParticle3));

    // --- 4. 赤 ---
    auto BossParticle4 = std::make_unique<GPUParticleEmitter>();
    BossParticle4->Initialize("Boss4", this);
    BossParticle4->Play();

    // --- 5. 黄色 ---
    auto BossParticle5 = std::make_unique<GPUParticleEmitter>();
    BossParticle5->Initialize("Boss5", this);
    BossParticle5->Play();

    // --- 6. 緑色 ---
    auto BossParticle6 = std::make_unique<GPUParticleEmitter>();
    BossParticle6->Initialize("Boss6", this);
    BossParticle6->Play();

    isFinalPhase_ = false;
    isWaitingForDeath_ = false;
    isWaitingForFinisher_ = false;
    isFinisherFalling_ = false;
    finisherFallVelocity_ = 0.0f;
    finisherBounceCount_ = 0;
    deathPhase_ = 0;
    isCompletelyDead_ = false;
    isShardSpawnRequested_ = false;

    isHpHalfTriggered_ = false;
    isHpHalfEventActive_ = false;
    hpHalfPhase_ = HpHalfEventPhase::None;
    hpHalfEffectTimer_ = 0.0f;
}

void BossCore::Update(float deltaTime) {
    // JSON/ImGuiで動的に設定した最大バリアHPを同期
    maxBarrierHp_ = attackParams_.maxBarrierHp;

    // アクション速度（移動や回転など）用の補正DeltaTime
    float actionDelta = deltaTime * kBaseSpeedMultiplier;

    InputManager* input = InputManager::GetInstance();

#ifdef USE_IMGUI
    if (SceneManager::GetInstance()->IsPlaying()) {
        // 0キーで時間停止
        if (input->IsKeyTriggered(DIK_0)) {
            s_isTimeStopped_ = !s_isTimeStopped_;
            if (s_isTimeStopped_) {
                DebugConsole::GetInstance()->AddLog("[TIME STOP] ボスの時間が止まった…！");
            }
            else {
                DebugConsole::GetInstance()->AddLog("[TIME RESUME] 時が動き出す！");
            }
        }

        int triggerAttack = 0;
        if (input->IsKeyTriggered(DIK_1)) triggerAttack = 1;
        if (input->IsKeyTriggered(DIK_2)) triggerAttack = 2;
        if (input->IsKeyTriggered(DIK_3)) triggerAttack = 3;
        if (input->IsKeyTriggered(DIK_4)) triggerAttack = 4;
        if (input->IsKeyTriggered(DIK_5)) triggerAttack = 5;
        if (input->IsKeyTriggered(DIK_6)) triggerAttack = 6;
        if (input->IsKeyTriggered(DIK_7)) triggerAttack = 7;
        if (input->IsKeyTriggered(DIK_8)) {
            if (param_.has_value()) {
                param_->hp = 1.0f; // 8キーを押したらHPを1にする
            }
        }
        if (input->IsKeyTriggered(DIK_Y)) triggerAttack = 9; // Yキーでファンネル攻撃

        // 9キーで即座にボスを爆散させるデバッグ機能
        if (input->IsKeyTriggered(DIK_9)) {
            if (!isCoreBroken_) {
                DebugConsole::GetInstance()->AddLog("【DEBUG】 9キー入力：ボスを強制爆散させます！！！💥");
                param_->hp = 0.0f;
                StartDeathSequence();
            }
        }

        // HキーでHP半分時の演出を強制発動させるデバッグ機能
        if (input->IsKeyTriggered(DIK_H)) {
            DebugConsole::GetInstance()->AddLog("【DEBUG】 Hキー入力：HP半減演出を強制発動します！");
            
            // 強制的にフラグをリセットしてダメージを与えることで正規のルートで演出を開始する
            isHpHalfTriggered_ = false;
            isHpHalfEventActive_ = false;
            hpHalfPhase_ = HpHalfEventPhase::None;
            
            if (param_.has_value()) {
                param_->hp = param_->maxHp; // 一旦満タンにして確実に発動条件を満たす
                TakeBodyDamage(param_->maxHp * 0.5f); 
            }
        }

        if (triggerAttack != 0) {
            DebugConsole::GetInstance()->AddLog("【DEBUG】 攻撃 " + std::to_string(triggerAttack) + " を予約！待機に戻ります！");
            s_debugForceAttack = triggerAttack;

            if (currentAttack_) {
                currentAttack_->Finalize();
                currentAttack_.reset();
            }
            ChangeState(State::Idle);
            animTimer_ = 0.0f;

            SetTranslate({ 0.0f, 4.0f, 0.0f });
            SetRotation({ 0.0f, 0.0f, 0.0f });
            SetColor(originalColor_);

            flyingBlocks_.clear();
            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                if (armorBlocks_[i]) {
                    armorBlocks_[i]->SetParent(this);
                    armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
                }
            }

            if (warningArea_) {
                warningArea_->SetScale({ 0.0f, 0.0f, 0.0f });
                warningArea_->SetCollisionAttribute(0);
                if (warningArea_->GetParent() == nullptr) {
                    warningArea_->SetParent(this);
                }
            }
        }
    }
#endif

    if (s_isTimeStopped_) {
        deltaTime = 0.0f;
        actionDelta = 0.0f;
    }

    float preTimer = colorResetTimer_;

    // ベースクラスの更新
    BaseEnemy::Update(actionDelta);

    // ==========================================
    // HPが1以下の時の最終奥義発動チェック
    // ==========================================
    if (param_.has_value() && param_->hp <= 1.0f) {
        if (!isFinalPhase_) {
            param_->hp = 1.0f;
            isFinalPhase_ = true;
            DebugConsole::GetInstance()->AddLog("[LAST STAND] ボスが最後の大技を準備している…！！");

            if (currentAttack_) {
                currentAttack_->Finalize();
                currentAttack_.reset();
            }
            ChangeState(State::Idle);   // 一度Idle状態にしてパラメータ等をリセット
            ChangeState(State::Attack); // 自動で最終奥義(ID: 8)が選ばれる
        }
        else if (!isWaitingForFinisher_ && deathPhase_ == 0) {
            // 大技の最中などは絶対に死なないHP1を維持
            param_->hp = 1.0f;
        }
    }

    // ==========================================
    // HP半分時の演出更新 (崩壊・復帰・強化シークエンス)
    // ==========================================
    if (isHpHalfEventActive_) {
        hpHalfEffectTimer_ += deltaTime; // 演出タイマーは実時間

        // カメラ演出の終了を監視し、終わった瞬間に向きをボスに合わせる
        if (!isPlayerRotated_) {
            if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
                // カメラの再生が終わり、補間も含めてプレイヤーに完全に位置が戻った瞬間 (Weightが0)
                // ※戻り中に SetRotation しても Camera::Update 内で逆算上書きされるため、0になるのを待つ
                if (!camera->IsOverridden() && camera->GetOverrideWeight() <= 0.001f) {
                    if (target_) {
                        Vector3 playerPos = target_->GetWorldPosition();
                        Vector3 bossPos = this->GetWorldPosition();
                        Vector3 toBoss = bossPos - playerPos;
                        float distXZ = std::sqrt(toBoss.x * toBoss.x + toBoss.z * toBoss.z);

                        // 角度の計算
                        float angleY = std::atan2(toBoss.x, toBoss.z);
                        float angleX = std::atan2(-toBoss.y, distXZ);

                        // プレイヤーとカメラの向きを強制同期
                        target_->SetRotation({ 0.0f, angleY, 0.0f });
                        camera->SetRotation({ angleX, angleY, 0.0f });
                        
                        isPlayerRotated_ = true; // 一度だけ実行
                        DebugConsole::GetInstance()->AddLog("【EVENT】 カメラの復帰を確認。視点をボスに固定しました。");
                    }
                }
            }
        }

        // ブロックの落下・散乱物理（Falling〜Pulsingの間、常に更新し続ける）
        if (hpHalfPhase_ >= HpHalfEventPhase::Falling && hpHalfPhase_ < HpHalfEventPhase::Reassembling) {
            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                if (armorBlocks_[i] && i < fallingBlockVelocities_.size()) {
                    Vector3 bPos = armorBlocks_[i]->GetTranslate();

                    // Pulsing フェーズ（ボスの鼓動）中は、地面にいたブロックを浮かび上がらせる
                    if (hpHalfPhase_ == HpHalfEventPhase::Pulsing) {
                        float riseSpeed = 3.0f; // 上昇速度
                        bPos.y += riseSpeed * actionDelta;
                        
                        // 浮かび上がりながらゆっくり回転させる
                        Vector3 rot = armorBlocks_[i]->GetRotation();
                        rot.x += 2.0f * actionDelta;
                        rot.y += 1.5f * actionDelta;
                        armorBlocks_[i]->SetRotation(rot);

                        armorBlocks_[i]->SetTranslate(bPos);
                    }
                    // 落下中、または空中にいる場合の物理
                    else if (fallingBlockVelocities_[i].x != 0.0f || fallingBlockVelocities_[i].y != 0.0f || fallingBlockVelocities_[i].z != 0.0f || bPos.y > 0.5f) {
                        fallingBlockVelocities_[i].y -= 25.0f * actionDelta; // 重力
                        bPos += fallingBlockVelocities_[i] * actionDelta;
                        if (bPos.y <= 0.5f) {
                            bPos.y = 0.5f;
                            fallingBlockVelocities_[i] = { 0,0,0 }; // 地面についたら停止
                        }
                        armorBlocks_[i]->SetTranslate(bPos);

                        // 空中にいる間は回転させる
                        if (bPos.y > 0.5f) {
                            Vector3 rot = armorBlocks_[i]->GetRotation();
                            rot.x += 5.0f * actionDelta;
                            rot.y += 3.0f * actionDelta;
                            armorBlocks_[i]->SetRotation(rot);
                        }
                    }
                }
            }
        }

        switch (hpHalfPhase_) {
        case HpHalfEventPhase::WaitIdle:
            // いきなり落ちるのではなく、1.0秒間空中で「おや？」と思わせる溜めを作る
            if (hpHalfEffectTimer_ >= 1.0f) {
                originalCoreRotation_ = GetRotation();
                originalCorePosition_ = GetTranslate();
                this->UpdateWorldMatrix();

                fallingBlockVelocities_.clear();
                for (Object3d* block : armorBlocks_) {
                    if (!block) {
                        fallingBlockVelocities_.push_back({ 0,0,0 });
                        continue;
                    }

                    block->UpdateWorldMatrix();
                    Vector3 worldPos = block->GetWorldPosition();
                    block->SetParent(nullptr);
                    block->SetTranslate(worldPos);

                    // ボス中心から外側に向かって弾け飛ぶ速度を計算
                    Vector3 dir = { worldPos.x - originalCorePosition_.x, 0.0f, worldPos.z - originalCorePosition_.z };
                    float len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
                    if (len > 0.001f) {
                        dir.x /= len;
                        dir.z /= len;
                    } else {
                        float angle = (static_cast<float>(rand()) / RAND_MAX) * 3.14159f * 2.0f;
                        dir.x = std::cos(angle);
                        dir.z = std::sin(angle);
                    }

                    float horizontalSpeed = 7.0f + (rand() % 30) * 0.1f; // 15〜20 から半分程度に減少
                    float verticalSpeed = 7.0f + (rand() % 30) * 0.1f;   // 15〜20 から半分程度に減少

                    fallingBlockVelocities_.push_back({
                        dir.x * horizontalSpeed,
                        verticalSpeed,
                        dir.z * horizontalSpeed
                    });
                }

                basePostEffectParams_ = *PostEffect::GetInstance()->GetParams();
                hpHalfPhase_ = HpHalfEventPhase::Falling;
                hpHalfEffectTimer_ = 0.0f;
            }
            break;

        case HpHalfEventPhase::Falling:
        {
            // 急落するのではなく、1.5秒かけてゆっくり（かつ加速しながら）地面へ
            float duration = 1.5f;
            float t = std::min(hpHalfEffectTimer_ / duration, 1.0f);
            float easeT = t * t; // 加速して落ちる感じ

            Vector3 pos = GetTranslate();
            pos.y = Math::Lerp(4.0f, 0.8f, easeT);
            SetTranslate(pos);

            if (t >= 1.0f) {
                hpHalfPhase_ = HpHalfEventPhase::Lying;
                hpHalfEffectTimer_ = 0.0f;
            }

            Vector3 rot = GetRotation();
            rot.x = Math::Lerp(rot.x, 1.4f, t); // 落下時間に合わせて倒れ込む
            SetRotation(rot);
        }
        break;

        case HpHalfEventPhase::Lying:
            if (hpHalfEffectTimer_ >= 1.0f) {
                hpHalfPhase_ = HpHalfEventPhase::Recovery;
                hpHalfEffectTimer_ = 0.0f;
            }
            break;

        case HpHalfEventPhase::Recovery:
        {
            Vector3 rot = GetRotation();
            rot.x = Math::Lerp(rot.x, originalCoreRotation_.x, 2.0f * actionDelta);
            rot.y = originalCoreRotation_.y + std::sin(hpHalfEffectTimer_ * 15.0f) * 0.4f;
            SetRotation(rot);

            Vector3 pos = GetTranslate();
            pos.y = Math::Lerp(pos.y, originalCorePosition_.y, 2.0f * actionDelta);
            SetTranslate(pos);

            if (hpHalfEffectTimer_ >= 1.5f) {
                hpHalfPhase_ = HpHalfEventPhase::Pulsing;
                hpHalfEffectTimer_ = 0.0f;
            }
        }
        break;

        case HpHalfEventPhase::Pulsing:
        {
            float targetScale = 1.0f;
            Vector3 rot = GetRotation();

            if (hpHalfEffectTimer_ < 0.5f) {
                // 最初の0.5秒：小さくなってピタッと止まる演出（溜め）
                // 0.3秒で 0.6 倍まで縮み、残り0.2秒は完全に静止する
                float shrinkT = std::min(hpHalfEffectTimer_ / 0.3f, 1.0f);
                targetScale = Math::Lerp(1.0f, 0.6f, shrinkT);
                
                // この間は震えず、ただ斜めに傾くのみ
                float targetRotX = originalCoreRotation_.x + 0.6f;
                rot.x = Math::Lerp(rot.x, targetRotX, 2.5f * actionDelta);
            } else {
                // 0.5秒以降：縮んだ状態をベースにパルスし、震え始める
                float pulseTime = hpHalfEffectTimer_ - 0.5f;
                targetScale = 0.6f + std::sin(pulseTime * 40.0f) * 0.1f;

                float targetRotX = originalCoreRotation_.x + 0.6f;
                rot.x = Math::Lerp(rot.x, targetRotX, 2.5f * actionDelta);
                // 小刻みな震え
                rot.y = originalCoreRotation_.y + std::sin(pulseTime * 50.0f) * 0.05f;
            }

            SetScale({ targetScale, targetScale, targetScale });
            SetColor({ 1.0f, 0.1f, 0.1f, 1.0f });
            rot.z = originalCoreRotation_.z;
            SetRotation(rot);

            // 溜め時間を0.5秒使ったため、全体の演出時間を 1.5 -> 2.0秒 に延長
            if (hpHalfEffectTimer_ >= 2.0f) {
                hpHalfPhase_ = HpHalfEventPhase::Reassembling;
                hpHalfEffectTimer_ = 0.0f;
            }
        }
        break;

        case HpHalfEventPhase::Reassembling:
        {
            // アニメーションが終わって元に戻る時も、線形補間で滑らかに戻す
            Vector3 coreRot = GetRotation();
            coreRot.x = Math::Lerp(coreRot.x, originalCoreRotation_.x, 4.0f * actionDelta);
            coreRot.y = Math::Lerp(coreRot.y, originalCoreRotation_.y, 4.0f * actionDelta);
            coreRot.z = Math::Lerp(coreRot.z, originalCoreRotation_.z, 4.0f * actionDelta);
            SetRotation(coreRot);

            Vector3 coreScale = GetScale();
            coreScale.x = Math::Lerp(coreScale.x, 1.0f, 4.0f * actionDelta);
            coreScale.y = Math::Lerp(coreScale.y, 1.0f, 4.0f * actionDelta);
            coreScale.z = Math::Lerp(coreScale.z, 1.0f, 4.0f * actionDelta);
            SetScale(coreScale);

            bool allDone = true;
            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                if (armorBlocks_[i]) {
                    // ブロックを親子関係に戻しつつ、元の軌道位置へLerp
                    if (armorBlocks_[i]->GetParent() == nullptr) {
                        armorBlocks_[i]->SetParent(this);
                    }

                    OrbitData orbit = GetIdleOrbit(i);
                    Vector3 currentPos = armorBlocks_[i]->GetTranslate();
                    Vector3 targetPos = orbit.pos;
                    Vector3 nextPos = Math::Lerp(currentPos, targetPos, 4.0f * actionDelta);
                    armorBlocks_[i]->SetTranslate(nextPos);

                    if (Math::Length(nextPos - targetPos) > 0.1f) allDone = false;
                }
            }

            // すべての復帰が終わった、またはタイムアウト
            if (allDone || hpHalfEffectTimer_ >= 3.0f) {
                // カメラの演出（GhostRecorder等）が完全に終わるまで待機
                if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
                    // オーバーライドが終了し、かつ補間（戻り）も完全に終わっているかチェック
                    if (!camera->IsOverridden() && camera->GetOverrideWeight() <= 0.01f) {
                        hpHalfPhase_ = HpHalfEventPhase::Finishing;
                        hpHalfEffectTimer_ = 0.0f;
                    }
                } else {
                    hpHalfPhase_ = HpHalfEventPhase::Finishing;
                    hpHalfEffectTimer_ = 0.0f;
                }
            }
        }
        break;

        case HpHalfEventPhase::Finishing:
            isHpHalfEventActive_ = false;
            hpHalfPhase_ = HpHalfEventPhase::None;
            isPlayerRotated_ = false; // 次回のためにリセット

            if (target_) {
                if (auto player = dynamic_cast<Player*>(target_)) {
                    player->SetIsControlActive(true);
                }
            }
            SetColor(greenColor_);
            defaultColor_ = greenColor_;
            SetScale({ 1,1,1 });
            SetRotation(originalCoreRotation_);
            *PostEffect::GetInstance()->GetParams() = basePostEffectParams_;

            DebugConsole::GetInstance()->AddLog("【EVENT】 フェーズ移行完了！ボスの猛攻に備えろ！");
            break;
            break;
        }

        // ポストエフェクト演出：各フェーズに合わせてエフェクトを動的に変化させる
        auto params = PostEffect::GetInstance()->GetParams();

        if (hpHalfPhase_ == HpHalfEventPhase::Falling) {
            // --- 落下フェーズ：画面が一瞬光る（ブルーム強化） ---
            float glowT = std::sin(std::min(hpHalfEffectTimer_ * 3.1415f, 3.1415f));
            params->threshold = Math::Lerp(basePostEffectParams_.threshold, 0.0f, glowT);
            params->bloomIntensity = Math::Lerp(basePostEffectParams_.bloomIntensity, 0.7f, glowT);
            params->spread = Math::Lerp(basePostEffectParams_.spread, 1.2f, glowT);
            params->enableToneMapping = (glowT > 0.1f) ? 1 : basePostEffectParams_.enableToneMapping;
        }
        else if (hpHalfPhase_ == HpHalfEventPhase::Recovery) {
            // --- 起き上がりフェーズ：徐々に不穏な雰囲気を出す ---
            float recoveryT = std::min(hpHalfEffectTimer_ / 1.5f, 1.0f); // 0→1 で徐々に
            params->chromaticAberration = Math::Lerp(basePostEffectParams_.chromaticAberration, 0.00f, recoveryT);
            params->vignetteIntensity = Math::Lerp(basePostEffectParams_.vignetteIntensity, 0.0f, recoveryT);
            params->filmGrainIntensity = Math::Lerp(basePostEffectParams_.filmGrainIntensity, 0.08f, recoveryT);
        }
        else if (hpHalfPhase_ == HpHalfEventPhase::Pulsing) {
            // --- パルスフェーズ：ボスの大小アニメーションに同期したポストエフェクト ---
            if (hpHalfEffectTimer_ < 0.5f) {
                // 溜め段階（0〜0.5秒）：じわじわとエフェクトを強くする
                float chargeT = std::min(hpHalfEffectTimer_ / 0.5f, 1.0f);
                float easeCharge = chargeT * chargeT; // EaseIn で加速感

                params->vignetteIntensity = Math::Lerp(basePostEffectParams_.vignetteIntensity, 0.0f, easeCharge);
                params->radialIntensity = Math::Lerp(basePostEffectParams_.radialIntensity, 0.005f, easeCharge);
                params->filmGrainIntensity = Math::Lerp(basePostEffectParams_.filmGrainIntensity, 0.04f, easeCharge);
                params->threshold = Math::Lerp(basePostEffectParams_.threshold, 0.8f, easeCharge);
                params->bloomIntensity = Math::Lerp(basePostEffectParams_.bloomIntensity, 1.2f, easeCharge);
            } else {
                // パルス段階（0.5秒〜）：スケールの脈動に連動してエフェクトが波打つ
                float pulseTime = hpHalfEffectTimer_ - 0.5f;
                float pulseWave = std::sin(pulseTime * 40.0f); // スケールと同じ周波数
                float pulseNorm = (pulseWave + 1.0f) * 0.5f;   // 0〜1 に正規化

                // 時間経過で全体の強度を徐々に上げる（クライマックス感）
                float progressT = std::min(pulseTime / 1.5f, 1.0f);

                // ビネット：控えめな暗がり
                params->vignetteIntensity = Math::Lerp(0.5f, 1.2f, pulseNorm * progressT);

                // 放射ブラー：中心に軽く力が集まる程度の弱いボケ
                params->radialCenterX = 0.5f;
                params->radialCenterY = 0.5f;
                params->radialIntensity = Math::Lerp(0.005f, 0.015f, pulseNorm * progressT);

                // フィルムグレイン：ごく僅かなノイズ
                params->filmGrainIntensity = Math::Lerp(0.04f, 0.06f, progressT);

                // 画面揺れ：ごく僅かな震え
                params->wobbleIntensity = Math::Lerp(0.0f, 0.007f, progressT * progressT);
            }
            params->enableToneMapping = 1;
        }
        else if (hpHalfPhase_ == HpHalfEventPhase::Reassembling) {
            // --- 再集結フェーズ：エフェクトをスムーズに元に戻す ---
            float fadeT = std::min(hpHalfEffectTimer_ / 1.5f, 1.0f); // 1.5秒かけて戻す
            float easeFade = 1.0f - std::pow(1.0f - fadeT, 2.0f); // EaseOut

            params->chromaticAberration = Math::Lerp(params->chromaticAberration, basePostEffectParams_.chromaticAberration, easeFade);
            params->vignetteIntensity = Math::Lerp(params->vignetteIntensity, basePostEffectParams_.vignetteIntensity, easeFade);
            params->radialIntensity = Math::Lerp(params->radialIntensity, basePostEffectParams_.radialIntensity, easeFade);
            params->filmGrainIntensity = Math::Lerp(params->filmGrainIntensity, basePostEffectParams_.filmGrainIntensity, easeFade);
            params->threshold = Math::Lerp(params->threshold, basePostEffectParams_.threshold, easeFade);
            params->bloomIntensity = Math::Lerp(params->bloomIntensity, basePostEffectParams_.bloomIntensity, easeFade);
            params->spread = Math::Lerp(params->spread, basePostEffectParams_.spread, easeFade);
            params->wobbleIntensity = Math::Lerp(params->wobbleIntensity, basePostEffectParams_.wobbleIntensity, easeFade);
            params->enableToneMapping = basePostEffectParams_.enableToneMapping;
        }



        // 演出中はここで return してしまうため、カメラアニメーション(GhostDirector)の更新もここで行う
        if (director_) {
            director_->Update(actionDelta);
        }

        return; // 演出中は以降の通常更新をスキップ
    }

    // ==========================================
    // 死亡演出の進行ロジック
    // ==========================================
    if (deathPhase_ == 1 || deathPhase_ == 2) {
        sequenceTimer_ -= deltaTime; // 死亡演出のカウントダウンは実時間

        // 1秒経つごとに次のフェーズへ進む
        if (sequenceTimer_ <= 0.0f) {

            if (deathPhase_ == 1) {
                // 1秒経過 -> 「亀裂フェーズ」へ移行し、さらに1秒待つ
                deathPhase_ = 2;
                sequenceTimer_ = 1.0f;
                ShowCrackedCore();
            }
            else if (deathPhase_ == 2) {
                // さらに1秒経過 -> ついに「粉砕フェーズ」へ
                deathPhase_ = 3;
                BreakCore();
            }
        }
    }

    if (director_) {
        director_->Update(actionDelta);

        // ゴーストディレクターのアニメーション終了を待っている場合
        if (isWaitingForDirector_ && director_->IsFinished()) {
            isWaitingForDirector_ = false;

            // アニメーションが終わったら、"Center_Collision_Box" の当たり判定を完全に消す
            if (sceneManager_ && sceneManager_->GetCurrentScene()) {
                for (auto& obj : sceneManager_->GetCurrentScene()->GetObjects()) {
                    if (Object3d* found = FindObjectByNameRecursive(obj.get(), "Center_Collision_Box")) {
                        found->SetCollisionAttribute(0);
                        found->SetCollisionMask(0); // マスクも念のため消しておく
                        found->SetScale({ 0.0f, 0.0f, 0.0f }); // 見た目も消す
                        break;
                    }
                }
            }
        }
    }

    // ==========================================
    // 登場演出中なら、それを更新する
    // ==========================================
    if (isAppearing_) {
        UpdateAppearance(deltaTime); // 内部で使い分け
    }

    // ==========================================
    // パーティクルの自動追従の更新
    // ==========================================
    for (auto& emitter : particleEmitters_) {
        if (emitter) {
            emitter->Update(actionDelta);
        }
    }

    // バリアへのダメージ処理
    if (IsTargetValid() && damageCooldownTimer_ <= 0.0f && state_ != State::Weak &&
        !isFinalPhase_ && !isWaitingForFinisher_ && deathPhase_ == 0) {
        Object3d* weapon = FindWeaponRecursive(target_);
        bool hitFound = false;

        if (weapon && weapon->GetCollisionMask() != 0) {
            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                Object3d* block = armorBlocks_[i];
                if (!block || blockBroken_[i]) continue;

                if (block->CheckCollision(weapon).isColliding) {
                    float dmg = weapon->GetAttackDamage();
                    TakeBarrierDamage(dmg, block);
                    GPUParticleManager::GetInstance()->Emit("BossHitSpark", block->GetWorldPosition(), Math::MakeIdentity4x4());

                    blockHps_[i] -= dmg;
                    if (blockHps_[i] <= 0.0f) {
                        blockBroken_[i] = true;
                        GPUParticleManager::GetInstance()->Emit("BossHitSpark", block->GetWorldPosition(), Math::MakeIdentity4x4());
                        DebugConsole::GetInstance()->AddLog("[BREAK] ブロック " + std::to_string(i) + " が破壊された！！！");
                    }
                    hitFound = true;
                    break;
                }
            }
        }

        // 2. その他のプレイヤー攻撃（エフェクトや弾丸など）をマネージャ経由でチェック
        if (!hitFound) {
            // エフェクトのチェック
            for (const auto& effect : MeshEffectManager::GetInstance()->GetActiveEffects()) {
                if (!effect || effect->isDead) continue;
                for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                    Object3d* block = armorBlocks_[i];
                    if (!block || blockBroken_[i]) continue;

                    if (effect->CanHit(block) && block->CheckCollision(effect.get()).isColliding) {
                        float dmg = effect->GetAttackDamage();
                        TakeBarrierDamage(dmg, block);
                        effect->AddHitObject(block); // ヒットリストに追加
                        GPUParticleManager::GetInstance()->Emit("BossHitSpark", block->GetWorldPosition(), Math::MakeIdentity4x4());

                        blockHps_[i] -= dmg;
                        if (blockHps_[i] <= 0.0f) {
                            blockBroken_[i] = true;
                            GPUParticleManager::GetInstance()->Emit("BossHitSpark", block->GetWorldPosition(), Math::MakeIdentity4x4());
                            DebugConsole::GetInstance()->AddLog("[BREAK] ブロック " + std::to_string(i) + " が破壊された！！！");
                        }
                        hitFound = true;
                        break;
                    }
                }
                if (hitFound) break;
            }
        }

        if (!hitFound) {
            // 弾丸のチェック
            for (const auto& bullet : BulletManager::GetInstance()->GetBullets()) {
                if (!bullet || bullet->IsDead()) continue;
                for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                    Object3d* block = armorBlocks_[i];
                    if (!block || blockBroken_[i]) continue;

                    if (block->CheckCollision(bullet.get()).isColliding) {
                        float dmg = bullet->GetAttackDamage();
                        TakeBarrierDamage(dmg, block);
                        GPUParticleManager::GetInstance()->Emit("BossHitSpark", block->GetWorldPosition(), Math::MakeIdentity4x4());

                        blockHps_[i] -= dmg;
                        if (blockHps_[i] <= 0.0f) {
                            blockBroken_[i] = true;
                            GPUParticleManager::GetInstance()->Emit("BossHitSpark", block->GetWorldPosition(), Math::MakeIdentity4x4());
                            DebugConsole::GetInstance()->AddLog("[BREAK] ブロック " + std::to_string(i) + " が破壊された！！！");
                        }
                        hitFound = true;
                        break;
                    }
                }
                if (hitFound) break;
            }
        }
    }

    if (preTimer > 0.0f && colorResetTimer_ <= 0.0f) {
        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            if (armorBlocks_[i] && i < savedBlockColors_.size()) {
                SetBlockColor(armorBlocks_[i], savedBlockColors_[i]);
            }
        }
    }

    // ====================================================
     // 登場演出中か、戦闘開始後のみアニメーションタイマーを進める
     // ====================================================
    if (SceneManager::GetInstance()->IsPlaying()) {
        if (isAppearing_ || isBattleStarted_) {
            s_globalIdleTimer += actionDelta; // アイドルアニメは倍速
        }
    }

    UpdateFlyingBlocks(actionDelta);

    // ==========================================
    // 4. 通常のステート更新
    // ==========================================
    if (SceneManager::GetInstance()->IsPlaying()) {
        if (isFirstFrame_) {
            originalColor_ = GetColor();
            for (Object3d* child : GetChildren()) {
                if (child->GetName() == "WarningArea") {
                    warningArea_ = child;
                    warningArea_->SetParent(nullptr);
                    warningArea_->SetScale({ 0.0f, 0.0f, 0.0f });
                    warningArea_->SetCollisionAttribute(0);
                    warningArea_->SetCollisionMask(0);
                    warningArea_->SetRotation({ 0.0f, 0.0f, 0.0f });
                    warningArea_->GetTransform()->isQuaternionMaster = false;

                    // ==========================================
                    // ArmorBlocks_ だけでなく、HPやフラグのリストからも確実に消す
                    // ==========================================
                    for (size_t i = 0; i < armorBlocks_.size(); ) {
                        if (armorBlocks_[i] == warningArea_) {
                            armorBlocks_.erase(armorBlocks_.begin() + i);
                            if (i < blockHps_.size()) blockHps_.erase(blockHps_.begin() + i);
                            if (i < blockBroken_.size()) blockBroken_.erase(blockBroken_.begin() + i);
                        }
                        else {
                            ++i;
                        }
                    }
                    break;
                }
            }

            ChangeState(State::Idle);
            isFirstFrame_ = false;

            // ====================================================
            // 最初のフレームで、装甲ブロックをランダムに散らかす
            // ====================================================
            blockStartPos_.clear(); // armorManager_ ではなく、BossCoreが直接持っている変数を使います
            Vector3 bossPos = GetTranslate();


            for (size_t i = 0; i < armorBlocks_.size(); ++i) {
                // ボスを中心に、半径15～30の距離に散らす
                float angle = (static_cast<float>(rand()) / RAND_MAX) * 2.0f * std::numbers::pi_v<float>;
                float distance = 15.0f + (static_cast<float>(rand()) / RAND_MAX) * 15.0f;

                // ====================================================
                // ブロックは「ボスの子供（ローカル座標）」なので計算を変えます
                // ボスがどんな高さにいても、(0.5f - ボスの高さ) にすることで
                // ワールド空間での高さを強制的に 0.5f (地面) に揃えることができます
                // ====================================================
                Vector3 scatterPos = {
                    std::cos(angle) * distance, // X: 子オブジェクトなので bossPos.x を足さなくてOK
                    0.5f - bossPos.y,           // Y: 地面の高さ(0.5f) - ボスの高さ
                    std::sin(angle) * distance  // Z: 子オブジェクトなので bossPos.z を足さなくてOK
                };
                blockStartPos_.push_back(scatterPos);

                // 初期位置にブロックをワープさせておく
                if (armorBlocks_[i]) {
                    armorBlocks_[i]->SetTranslate(scatterPos);

                    // ただの瓦礫感を出すため、初期角度をめちゃくちゃにする
                    float rX = (static_cast<float>(rand()) / RAND_MAX) * 3.1415f;
                    float rY = (static_cast<float>(rand()) / RAND_MAX) * 3.1415f;
                    float rZ = (static_cast<float>(rand()) / RAND_MAX) * 3.1415f;
                    armorBlocks_[i]->SetRotation({ rX, rY, rZ });

                    armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
                }
            }
        }

        switch (state_) {
        case State::Idle:
            UpdateIdle(deltaTime); // 内部で使い分け
            break;
        case State::Attack:
            if (currentAttack_) {
                currentAttack_->Update(this, actionDelta); // 攻撃モーションは倍速

                // TriggerCrashStun() 等でステートが Weak に変わった場合、
                //  currentAttack_ がリセット済みの可能性があるため再チェック
                if (currentAttack_ && currentAttack_->IsFinished()) {
                    currentAttack_->Finalize();
                    currentAttack_.reset();

                    if (isFinalPhase_) {
                        ChangeState(State::Idle); 
                    }
                    else {
                        ChangeState(State::Idle);
                    }
                }
            }
            break;
        case State::Weak:
            UpdateWeak(deltaTime); // 内部で使い分け
            break;
        }
    }
    // ==========================================
    // 魔法の処理：破壊されたブロックの強制消去
    // ==========================================
    for (size_t i = 0; i < armorBlocks_.size(); ++i) {
        if (blockBroken_[i] && armorBlocks_[i]) {
            armorBlocks_[i]->SetScale({ 0.0f, 0.0f, 0.0f });
            armorBlocks_[i]->SetCollisionAttribute(0);
        }
    }

    // ==========================================
    // 破片の物理計算と退場タイマーを進める
    // ==========================================
    UpdateCorePieces(deltaTime);

    // コアとブロックを繋ぐエネルギー結線の更新
    // ====================================================
    // ブロックの移動トレース（残像）エフェクト
    // ====================================================
    if (prevBlockPositions_.size() != armorBlocks_.size()) {
        prevBlockPositions_.resize(armorBlocks_.size());
        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            if (armorBlocks_[i]) prevBlockPositions_[i] = armorBlocks_[i]->GetWorldPosition();
        }
    }

    for (size_t i = 0; i < armorBlocks_.size(); ++i) {
        if (!armorBlocks_[i] || blockBroken_[i]) continue;

        Vector3 currentPos = armorBlocks_[i]->GetWorldPosition();
        Vector3 prevPos = prevBlockPositions_[i];

        // 移動距離を計算
        Vector3 diff = currentPos - prevPos;
        float moveDist = std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);

        // 一定以上動いていたら（高速移動中なら）トレースを出す
        if (moveDist > 0.1f) {
            // 放出量をさらに抑制 (最大2個)
            int count = static_cast<int>(moveDist * 5.0f);
            if (count < 1) count = 1;
            if (count > 2) count = 2;

            for (int c = 0; c < count; ++c) {
                float t = static_cast<float>(c) / static_cast<float>(count);
                Vector3 emitPos = Math::Lerp(prevPos, currentPos, t);
                GPUParticleManager::GetInstance()->Emit("BossBlockTrail", emitPos);
            }
        }

        prevBlockPositions_[i] = currentPos;
    }

    // UpdateTethers(deltaTime);
}

// =================================================================
// ステート状態管理
