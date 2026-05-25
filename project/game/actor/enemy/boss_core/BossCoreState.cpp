#include "BossCoreShared.h"

void BossCore::ChangeState(State nextState) {
    state_ = nextState;

    uint32_t coreAttribute;
    uint32_t blockAttribute;

    // トドメ待ち状態の判定
    if (isWaitingForDeath_ || isWaitingForFinisher_) {
        coreAttribute = kEnemy;
        blockAttribute = 0;
    }
    else {
        if (state_ == State::Attack) {
            coreAttribute = kEnemy | kGround;
        }
        else if (state_ == State::Weak) {
            coreAttribute = kEnemy | kGround;
        }
        else {
            if (isBattleStarted_) {
                coreAttribute = kEnemy | kGround;
            }
            else {
                coreAttribute = kGround;
            }
        }
        blockAttribute = (state_ == State::Attack) ? (kEnemyAttack | kGround) : kGround;
    }

    SetCollisionAttribute(coreAttribute);
    for (Object3d* child : GetChildren()) {
        if (child) {
            child->SetCollisionAttribute(coreAttribute);
        }
    }

    if (state_ == State::Idle) {
        hasResetColorPreAttack_ = false;
        if (!isWaitingForDeath_) {
            SetColor(greenColor_);
            defaultColor_ = greenColor_;
        }
    }
    else if (state_ == State::Attack) {
        SetColor(originalColor_);
        defaultColor_ = originalColor_;
    }

    for (Object3d* block : armorBlocks_) {
        if (block) {
            block->SetCollisionAttribute(blockAttribute);
            if (state_ == State::Attack) {
                SetBlockColor(block, { 1.0f, 1.0f, 1.0f, 1.0f });
            }
            // ★ 追加：スタン(Weak)時のスケールを元に戻す
            if (state_ == State::Weak) {
                block->SetScale({ 1.0f, 1.0f, 1.0f });
                SetBlockColor(block, { 1.0f, 1.0f, 1.0f, 1.0f });

                // ★スタン時に子ブロック（Shard）の展開を強制収束（元のコンパクトな位置に戻す）
                for (auto* child : block->GetChildren()) {
                    if (child && child->GetName().find("Shard") != std::string::npos) {
                        Vector3 basePos = child->GetTranslate();
                        Vector3 dir = Math::Normalize(basePos);
                        if (Math::Length(dir) < 0.1f) dir = { 0.0f, 1.0f, 0.0f };

                        // 元の配置の基準距離（吸収したブロックなら0.25f、初期配置なら0.35f）に収束
                        float defaultOffset = 0.35f;
                        if (block->GetName().find("Enemy_Block") == std::string::npos) {
                            defaultOffset = 0.175f;
                        }
                        child->SetTranslate(dir * defaultOffset);
                    }
                }
            }
        }
    }

    if (state_ == State::Weak) {
        SetScale({ 1.0f, 1.0f, 1.0f }); // コア本体もリセット
    }

    switch (state_) {
    case State::Idle:
        animTimer_ = 0.0f;
        startIdlePos_ = GetTranslate(); // 遷移時の座標を保存
        break;

    case State::Attack: {
        static int lastAttack = 0;
        int totalWeight = 0;
        std::vector<::AttackWeight> candidates;

        // HP半分イベントがトリガーされたら第2形態のテーブル、それ以外なら第1形態のテーブルを使用
        const auto& attackPool = isHpHalfTriggered_ ? attackParams_.phase2Attacks : attackParams_.phase1Attacks;

        // 現在生きている（破壊されていない）装甲ブロックの数を数える
        int activeArmorCount = 0;
        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            if (!blockBroken_[i]) {
                activeArmorCount++;
            }
        }

        // 残りの壊れていない装甲がしきい値以下、かつ満タンでない場合に発動
        bool isLowArmorTriggered = (activeArmorCount <= attackParams_.lowArmorThreshold) && !IsArmorFull();

        int nextAttack = 1;
        bool isForcedAbsorb = false;

        if (isLowArmorTriggered) {
            // 0〜99の乱数を取得し、設定された確率（％）未満なら「吸収攻撃 (ID: 7)」を確定させる！
            int roll = std::rand() % 100;
            if (roll < attackParams_.lowArmorAbsorbRate) {
                nextAttack = 7;
                isForcedAbsorb = true;
            }
        }

        if (!isForcedAbsorb) {
            // 通常通りの重み付き抽選を行う
            for (const auto& a : attackPool) {
                // 他に候補がある場合は、連続して同じ攻撃を出すのを防ぐ
                if (attackPool.size() > 1 && a.id == lastAttack) continue;
                // 通常抽選からは、装甲満タン時の吸収攻撃のみ除外
                if (a.id == 7 && IsArmorFull()) continue;

                candidates.push_back(a);
                totalWeight += a.weight;
            }

            // 連続制限などで候補が空になってしまった場合のセーフティ
            if (candidates.empty() && !attackPool.empty()) {
                for (const auto& a : attackPool) {
                    if (a.id == 7 && IsArmorFull()) continue;
                    candidates.push_back(a);
                    totalWeight += a.weight;
                }
            }

            if (totalWeight > 0) {
                int randomVal = std::rand() % totalWeight;
                int currentSum = 0;
                for (const auto& c : candidates) {
                    currentSum += c.weight;
                    if (randomVal < currentSum) {
                        nextAttack = c.id;
                        break;
                    }
                }
            }
            else if (!attackPool.empty()) {
                // 重みが設定されていない場合のセーフティ
                nextAttack = attackPool[std::rand() % attackPool.size()].id;
            }
        }

        lastAttack = nextAttack;

        if (s_debugForceAttack != 0) {
            nextAttack = s_debugForceAttack;
            s_debugForceAttack = 0;
        }

        if (isFinalPhase_) {
            nextAttack = 8;
        }

        animTimer_ = 0.0f;

        if (nextAttack == 1) currentAttack_ = std::make_unique<BossAttack1_Rush>();
        else if (nextAttack == 2) currentAttack_ = std::make_unique<BossAttack2_Shoot>();
        else if (nextAttack == 3) currentAttack_ = std::make_unique<BossAttack3_Hammer>();
        else if (nextAttack == 4) currentAttack_ = std::make_unique<BossAttack4_Wall>();
        else if (nextAttack == 5) currentAttack_ = std::make_unique<BossAttack5_Humanoid>();
        else if (nextAttack == 6) currentAttack_ = std::make_unique<BossAttack6_Laser>();
        else if (nextAttack == 7) currentAttack_ = std::make_unique<BossAttack7_Absorb>();
        else if (nextAttack == 8) currentAttack_ = std::make_unique<BossAttack8_Final>();
        else if (nextAttack == 9) currentAttack_ = std::make_unique<BossAttack9_Funnels>();
        else if (nextAttack == 10) currentAttack_ = std::make_unique<BossAttack9_Spawn>();

        if (currentAttack_) {
            currentAttack_->Initialize(this);
        }
        break;
    }

    case State::Weak:
        animTimer_ = 0.0f;
        break;
    }
}

void BossCore::UpdateIdle(float deltaTime) {
    if (isWaitingForFinisher_) {
        // 周りのブロックを念のため消去（通常は消えているはず）
        for (Object3d* block : armorBlocks_) {
            if (block) {
                block->SetScale({ 0.0f, 0.0f, 0.0f });
                block->SetCollisionAttribute(0);
            }
        }

        if (isFinisherFalling_) {
            // 落下物理
            float gravity = 40.0f;
            finisherFallVelocity_ -= gravity * deltaTime;

            Vector3 pos = GetTranslate();
            pos.y += finisherFallVelocity_ * deltaTime;

            float groundY = 0.8f;
            if (pos.y <= groundY) {
                pos.y = groundY;

                // バウンド処理
                if (finisherBounceCount_ < 2) {
                    finisherFallVelocity_ = -finisherFallVelocity_ * 0.4f; // 反発係数0.4
                    finisherBounceCount_++;

                    // 着地時の土煙エフェクト
                    GPUParticleManager::GetInstance()->Emit("BossHitSpark", pos, Math::MakeIdentity4x4());
                } else {
                    pos.y = groundY;
                    finisherFallVelocity_ = 0.0f;
                    isFinisherFalling_ = false; // 落下終了
                    DebugConsole::GetInstance()->AddLog("【トドメ待ち】 ボスが着地した！プレイヤーよ、とどめを刺せ！");
                }
            }
            SetTranslate(pos);

            // 落下中の回転（ぐるぐる回りながら落ちる）
            Vector3 rot = GetRotation();
            rot.y += 10.0f * deltaTime;
            rot.x += 5.0f * deltaTime;
            SetRotation(rot);
        }
        else {
            // 着地後の弱々しい待機モーション（ピクピク動く、またはゆっくり明滅する）
            float time = s_globalIdleTimer * 2.0f;
            float hover = 0.8f + std::sin(time * 3.0f) * 0.1f; // 低い位置でピクピク
            Vector3 pos = GetTranslate();
            pos.y = hover;
            pos.x = 0.0f;
            pos.z = 0.0f; // 中央固定
            SetTranslate(pos);

            // 弱々しい回転
            SetRotation({ 0.3f, std::sin(time * 0.5f) * 0.5f, 0.0f });

            // 赤と黒の弱々しい明滅
            float pulse = (std::sin(time * 4.0f) + 1.0f) * 0.5f;
            SetColor({ 1.0f, pulse * 0.2f, pulse * 0.2f, 1.0f });
        }
        return;
    }

    if (isWaitingForDeath_) {
        // (トドメ待ちのボロボロ処理…そのまま)
        return;
    }



    // ====================================================
    // フェーズ1（咆哮開始）になってから、初めて合体タイマーを進める
    // ====================================================
    if (appearancePhase_ == 1 || (!isBattleStarted_ && assemblyTimer_ > 0.0f)) {
        assemblyTimer_ += deltaTime;
    }
    else if (isBattleStarted_) {
        assemblyTimer_ = 3.0f; // 戦闘中はMAX(3秒)にしておく
    }

    // ====================================================
    // コア本体の待機モーション（鼓動と浮遊）
    // ====================================================
    if (isBattleStarted_) {
        float actionDelta = deltaTime * kBaseSpeedMultiplier;
        // 1. 鼓動
        float t = std::fmod(s_globalIdleTimer, 2.0f);
        float pulse = 0.0f;
        if (t < 0.15f) {
            pulse = std::sin((t / 0.15f) * std::numbers::pi_v<float>);
        } else if (t > 0.25f && t < 0.4f) {
            pulse = std::sin(((t - 0.25f) / 0.15f) * std::numbers::pi_v<float>) * 0.7f;
        }
        
        float scaleVal = 1.0f + pulse * 0.2f;
        SetScale({ scaleVal, scaleVal, scaleVal });

        // 2. 浮遊 (登場ムービー終了時や攻撃終了時の瞬間移動を防止するため、最初の1.5秒間は前の位置からスムーズにLerp)
        float hoverY = 4.0f + std::sin(s_globalIdleTimer * 1.5f) * 0.3f;
        Vector3 targetPos = { 0.0f, hoverY, 0.0f }; // 待機状態では常に中央を目標にする

        if (animTimer_ < 1.5f) {
            float transitionT = std::min(animTimer_ / 1.5f, 1.0f);
            float easeTransition = 1.0f - std::pow(1.0f - transitionT, 3.0f); // OutCubic
            Vector3 currentPos = Math::Lerp(startIdlePos_, targetPos, easeTransition);
            SetTranslate(currentPos);
        }
        else {
            SetTranslate(targetPos);
        }
    }

    // ====================================================
    // ★ ここが圧倒的カッコよさの秘密！
    // 1.8秒（咆哮が終わって元のサイズに戻る時間）までは 0% で完全待機。
    // 1.8秒を過ぎたら、0.7秒間かけて一気にシュバッ！と集める！
    // ====================================================
    float t = 0.0f;
    if (assemblyTimer_ > 1.8f) {
        t = std::min((assemblyTimer_ - 1.8f) / 0.7f, 1.0f);
    }

    // カッコいいイージング計算（3乗アウト）：最初は早く、ボスに近づくにつれてゆっくり！
    float easeT = 1.0f - std::pow(1.0f - t, 3.0f);
    Vector3 coreScale = GetScale();

    float actionDelta = deltaTime * kBaseSpeedMultiplier;

    for (size_t i = 0; i < armorBlocks_.size(); ++i) {
        OrbitData orbit = GetIdleOrbit(i);
        Vector3 localPos = orbit.pos;
        localPos.x /= coreScale.x;
        localPos.y /= coreScale.y;
        localPos.z /= coreScale.z;

        Vector3 localScale = orbit.scale;
        localScale.x /= coreScale.x;
        localScale.y /= coreScale.y;
        localScale.z /= coreScale.z;

        if (i < blockStartPos_.size()) {
            Vector3 pos = Math::Lerp(blockStartPos_[i], localPos, easeT);
            armorBlocks_[i]->SetTranslate(pos);
        }
        else {
            armorBlocks_[i]->SetTranslate(localPos);
        }

        armorBlocks_[i]->SetScale(localScale);
        armorBlocks_[i]->SetRotation(orbit.rot);
        armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
    }

    // ==========================================
    // 戦闘開始フラグがONの時だけ、攻撃へのタイマーを進める
    // ==========================================
    if (isBattleStarted_) {

        // ====================================================
        // ★ 修正：4, 5, 6, 7, 8秒のどれかをピタリ選ぶ
        // ====================================================
        static float targetIdleTime = 5.0f;
        if (animTimer_ == 0.0f) {
            // rand() % 5 は「0, 1, 2, 3, 4」のどれかになるので、それに4を足すと「4, 5, 6, 7, 8」になります！
            int randomSeconds = 4 + (std::rand() % 5);
            targetIdleTime = static_cast<float>(randomSeconds);

            DebugConsole::GetInstance()->AddLog("[AI] 次の攻撃まで " + std::to_string(randomSeconds) + " 秒待機します");
        }

        animTimer_ += deltaTime;

        // 攻撃の1秒前に色を水色（青っぽい色）に戻す！
        if (animTimer_ >= targetIdleTime - 1.5f && !hasResetColorPreAttack_) {
            SetColor(originalColor_);
            defaultColor_ = originalColor_;
            hasResetColorPreAttack_ = true;
        }

        // タイマーが「今回決めた目標時間」を超えたら攻撃へ！
        if (animTimer_ >= targetIdleTime) {
            ChangeState(State::Attack);
        }
    }
}

void BossCore::UpdateWeak(float deltaTime) {
    float actionDelta = deltaTime * kBaseSpeedMultiplier;
    float duration = std::max(0.0f, attackParams_.stunDuration - 2.0f); // Reduce stun time by 2 seconds
    float wakeUpStart = duration > 2.0f ? duration - 2.0f : duration * 0.666f;
    float wakeUpDuration = duration - wakeUpStart;

    if (isCrashStun_) {
        // 自爆スタンの場合はバリアHPを変動させず、専用タイマーを進める
        crashStunTimer_ += deltaTime;
        if (crashStunTimer_ > duration) {
            crashStunTimer_ = duration;
        }
        animTimer_ = crashStunTimer_;
    }
    else {
        // スタンゲージ（barrierHp_）を duration 秒かけて0からmaxBarrierHp_まで回復（実時間で回復）
        barrierHp_ += (maxBarrierHp_ / duration) * deltaTime;
        if (barrierHp_ > maxBarrierHp_) {
            barrierHp_ = maxBarrierHp_;
        }
        // ゲージの回復進捗に合わせてアニメーションタイマー(0.0s〜duration)を逆算同期
        animTimer_ = (barrierHp_ / maxBarrierHp_) * duration;
    }

    // --- コア本体の落下・転がり（コロン）・復帰 ---
    Vector3 bossPos = GetTranslate();
    float fallDuration = 1.2f; // 0.5s -> 1.2sへ（重々しく倒れる）
    float rollAngle = 0.0f;

    // 1. 落下と転がり
    if (animTimer_ <= fallDuration) {
        float t = animTimer_ / fallDuration;
        float easeT = std::pow(t, 2.0f);
        bossPos.y = Math::Lerp(4.0f, 0.5f, easeT);
        rollAngle = Math::Lerp(0.0f, 90.0f * (std::numbers::pi_v<float> / 180.0f), easeT);
        SetTranslate(bossPos);
    }
    // 2. 起き上がり（最後の wakeUpDuration 秒）
    else if (animTimer_ > wakeUpStart) {
        float wakeUpT = (animTimer_ - wakeUpStart) / wakeUpDuration;
        float easeT = 1.0f - std::pow(1.0f - wakeUpT, 3.0f); // ふわりと浮くEaseOut
        float targetHoverY = 4.0f + std::sin(s_globalIdleTimer * 1.5f) * 0.3f; // 待機浮遊アニメのY座標にブレンド
        bossPos.y = Math::Lerp(0.5f, targetHoverY, easeT);
        rollAngle = Math::Lerp(90.0f * (std::numbers::pi_v<float> / 180.0f), 0.0f, easeT);
        SetTranslate(bossPos);
    }
    // 3. 地面でダウン中
    else {
        bossPos.y = 0.5f;
        rollAngle = 90.0f * (std::numbers::pi_v<float> / 180.0f);
        SetTranslate(bossPos);
    }

    SetRotation({ rollAngle, GetRotation().y, 0.0f });

    // --- フリッカー（点滅）エフェクト ---
    int flicker = static_cast<int>(animTimer_ * 15.0f) % 4; // ランダムっぽくチカチカさせる
    bool isLightOn = (flicker == 0 && animTimer_ < wakeUpStart);

    if (animTimer_ > wakeUpStart) {
        // 起き上がり中は徐々に元の色に戻す
        float wakeUpT = (animTimer_ - wakeUpStart) / wakeUpDuration;
        Vector4 color;
        color.x = Math::Lerp(0.3f, greenColor_.x, wakeUpT);
        color.y = Math::Lerp(0.3f, greenColor_.y, wakeUpT);
        color.z = Math::Lerp(0.3f, greenColor_.z, wakeUpT);
        color.w = 1.0f;
        SetColor(color);
    }
    else {
        SetColor(isLightOn ? greenColor_ : Vector4{ 0.3f, 0.3f, 0.3f, 1.0f });
    }

    // --- 装甲ブロックの動き ---
    float scatterDuration = 0.8f;
    float scatterT = std::min(animTimer_ / scatterDuration, 1.0f);
    float easeT = 1.0f - std::pow(1.0f - scatterT, 3.0f); // OutCubic

    for (size_t i = 0; i < armorBlocks_.size(); ++i) {
        if (i < blockStartPos_.size() && i < blockTargetPos_.size()) {
            // 現在の補間位置（ワールド空間）
            Vector3 currentPos = Math::Lerp(blockStartPos_[i], blockTargetPos_[i], easeT);

            // 落下中のバウンドエフェクト（ワールド空間のY座標に加算するため、完全に真上に跳ねる！）
            if (scatterT < 1.0f) {
                float bounce = std::abs(std::sin(scatterT * std::numbers::pi_v<float> * 2.0f)) * (1.0f - scatterT) * 4.0f;
                currentPos.y += bounce;
            }

            Vector3 currentRot = Math::Lerp(blockStartRot_[i], blockTargetRot_[i], easeT);
            // 落下中はワールド空間で追加の回転スピンを与える
            if (scatterT < 1.0f) {
                currentRot.x += 5.0f * animTimer_;
                currentRot.y += 3.0f * animTimer_;
            }

            // 起き上がり中のブロック補間（コアへスムーズに戻る）
            if (animTimer_ > wakeUpStart) {
                float wakeUpT = (animTimer_ - wakeUpStart) / wakeUpDuration;
                float returnEaseT = 1.0f - std::pow(1.0f - wakeUpT, 3.0f); // OutCubic
                
                OrbitData orbit = GetIdleOrbit(i);
                // 戻り先のターゲットワールド座標をボスの現在のワールド行列から計算
                Vector3 targetWorldPos = Math::Transform(orbit.pos, GetWorldMatrix());
                currentPos = Math::Lerp(currentPos, targetWorldPos, returnEaseT);

                // スケールも1.0fから orbit.scale へスムーズにイージング補間
                Vector3 currentScale = Math::Lerp({ 1.0f, 1.0f, 1.0f }, orbit.scale, returnEaseT);
                armorBlocks_[i]->SetScale(currentScale);

                // 戻り先のターゲットワールド回転（ボスの回転＋軌道自体の回転）
                Vector3 targetWorldRot = {
                    GetRotation().x + orbit.rot.x,
                    GetRotation().y + orbit.rot.y,
                    GetRotation().z + orbit.rot.z
                };

                auto LerpAngle = [](float a, float b, float t) {
                    float diff = b - a;
                    while (diff < -std::numbers::pi_v<float>) diff += 2.0f * std::numbers::pi_v<float>;
                    while (diff > std::numbers::pi_v<float>) diff -= 2.0f * std::numbers::pi_v<float>;
                    return a + diff * t;
                };
                currentRot.x = LerpAngle(currentRot.x, targetWorldRot.x, returnEaseT);
                currentRot.y = LerpAngle(currentRot.y, targetWorldRot.y, returnEaseT);
                currentRot.z = LerpAngle(currentRot.z, targetWorldRot.z, returnEaseT);
            }

            armorBlocks_[i]->SetTranslate(currentPos);
            armorBlocks_[i]->SetRotation(currentRot);
            armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;

            // ブロックも点滅
            if (animTimer_ > 4.0f) {
                float wakeUpT = (animTimer_ - 4.0f) / 2.0f;
                SetBlockColor(armorBlocks_[i], { 0.3f + wakeUpT * 0.7f, 0.3f + wakeUpT * 0.7f, 0.3f + wakeUpT * 0.7f, 1.0f });
            }
            else {
                SetBlockColor(armorBlocks_[i], isLightOn ? Vector4{ 0.8f, 0.8f, 0.8f, 1.0f } : Vector4{ 0.3f, 0.3f, 0.3f, 1.0f });
            }
        }
    }

    // --- duration 秒経過でステート復帰 ---
    if (animTimer_ >= duration) {
        animTimer_ = 0.0f;
        isCrashStun_ = false;
        crashStunTimer_ = 0.0f;
        SetRotation({ 0.0f, GetRotation().y, 0.0f });
        SetColor(greenColor_);
        defaultColor_ = greenColor_;
        Vector3 finalPos = GetTranslate();
        finalPos.y = 4.0f + std::sin(s_globalIdleTimer * 1.5f) * 0.3f; // 完全に待機モーションの浮遊位置に同期
        SetTranslate(finalPos);

        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            if (armorBlocks_[i]) {
                armorBlocks_[i]->SetParent(this);
                SetBlockColor(armorBlocks_[i], { 1.0f, 1.0f, 1.0f, 1.0f });

                // 復帰時に完全に元のスケール・回転・位置に揃える
                OrbitData orbit = GetIdleOrbit(i);
                armorBlocks_[i]->SetTranslate(orbit.pos);
                armorBlocks_[i]->SetRotation(orbit.rot);
                armorBlocks_[i]->SetScale(orbit.scale);
                armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
            }
        }

        ChangeState(State::Idle);
    }
}

void BossCore::UpdateFlyingBlocks(float deltaTime) {
    float actionDelta = deltaTime * kBaseSpeedMultiplier;
    int landedCount = 0;
    static Math math;

    for (auto& fb : flyingBlocks_) {
        if (!fb.block) continue;

        if (fb.mode == 4) {
            Vector3 bossPos = GetTranslate();
            Vector3 headPos = { bossPos.x, bossPos.y + 4.0f, bossPos.z };
            Vector3 currentPos = fb.block->GetTranslate();

            Vector3 dir = { headPos.x - currentPos.x, headPos.y - currentPos.y, headPos.z - currentPos.z };
            float distance = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);

            if (distance < 0.5f) {
                fb.block->SetTranslate(headPos);

                // ターゲット（プレイヤー）への追従
                if (IsTargetValid()) {
                    Vector3 targetPos = target_->GetWorldPosition();
                    targetPos.y = 0.0f;

                    Vector3 toPlayer = math.Normalize(targetPos - headPos);
                    float bulletSpeed = 60.0f;
                    fb.velocity = { toPlayer.x * bulletSpeed, toPlayer.y * bulletSpeed, toPlayer.z * bulletSpeed };

                    float angleY = std::atan2(toPlayer.x, toPlayer.z) + (std::numbers::pi_v<float> / 2.0f);
                    fb.currentRot = { 0.0f, angleY, 0.0f };
                }
                fb.mode = 0;
            }
            else {
                dir.x /= distance; dir.y /= distance; dir.z /= distance;
                float gatherSpeed = 30.0f;
                currentPos.x += dir.x * gatherSpeed * actionDelta;
                currentPos.y += dir.y * gatherSpeed * actionDelta;
                currentPos.z += dir.z * gatherSpeed * actionDelta;
                fb.block->SetTranslate(currentPos);

                fb.currentRot.x += 15.0f * actionDelta;
                fb.currentRot.y += 30.0f * actionDelta;
                fb.block->SetRotation(fb.currentRot);
            }
            fb.block->GetTransform()->isQuaternionMaster = false;
        }
        else if (fb.mode == 0) {
            Vector3 pos = fb.block->GetTranslate();
            pos.x += fb.velocity.x * actionDelta;
            pos.y += fb.velocity.y * actionDelta;
            pos.z += fb.velocity.z * actionDelta;

            if (pos.y <= 0.0f) {
                pos.y = 0.0f;
                fb.velocity = { 0.0f, 0.0f, 0.0f };
                fb.mode = 1;
            }
            fb.block->SetTranslate(pos);

            Vector3 spinSpeed = { 30.0f, 45.0f, 60.0f };
            fb.currentRot.x += spinSpeed.x * actionDelta;
            fb.currentRot.y += spinSpeed.y * actionDelta;
            fb.currentRot.z += spinSpeed.z * actionDelta;
            fb.block->SetRotation(fb.currentRot);
            fb.block->GetTransform()->isQuaternionMaster = false;

        }
        else if (fb.mode == 1) {
            landedCount++;
        }
        else if (fb.mode == 2) {
            Vector3 bossPos = GetTranslate();
            Vector3 blockPos = fb.block->GetTranslate();

            Vector3 dir = { bossPos.x - blockPos.x, bossPos.y - blockPos.y, bossPos.z - blockPos.z };
            float distance = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);

            if (distance < 2.0f) {
                fb.mode = 3;
            }
            else {
                dir.x /= distance; dir.y /= distance; dir.z /= distance;
                float returnSpeed = 60.0f;
                blockPos.x += dir.x * returnSpeed * actionDelta;
                blockPos.y += dir.y * returnSpeed * actionDelta;
                blockPos.z += dir.z * returnSpeed * actionDelta;
                fb.block->SetTranslate(blockPos);

                Vector3 spinSpeed = { 60.0f, 60.0f, 60.0f };
                fb.currentRot.x += spinSpeed.x * actionDelta;
                fb.currentRot.y += spinSpeed.y * actionDelta;
                fb.currentRot.z += spinSpeed.z * actionDelta;
                fb.block->SetRotation(fb.currentRot);
                fb.block->GetTransform()->isQuaternionMaster = false;
            }
        }
    }

    if (!flyingBlocks_.empty() && landedCount == flyingBlocks_.size() && flyingBlocks_.size() == armorBlocks_.size()) {
        returnDelayTimer_ += deltaTime;
        if (returnDelayTimer_ >= 5.0f) {
            for (auto& fb : flyingBlocks_) {
                fb.mode = 2;
                if (fb.block) {
                    fb.block->SetCollisionAttribute(kGround); // 戻るときに攻撃判定をなくす
                }
            }
            returnDelayTimer_ = 0.0f;
        }
    }
    else {
        returnDelayTimer_ = 0.0f;
    }

    for (auto it = flyingBlocks_.begin(); it != flyingBlocks_.end(); ) {
        if (it->mode == 3) {
            it->block->SetParent(this);
            int idx = it->originalIndex;

            OrbitData orbit = GetIdleOrbit(idx);
            it->block->SetTranslate(orbit.pos);
            it->block->SetScale(orbit.scale);
            it->block->SetRotation(orbit.rot);
            it->block->GetTransform()->isQuaternionMaster = false;

            it = flyingBlocks_.erase(it);
        }
        else {
            ++it;
        }
    }
}


