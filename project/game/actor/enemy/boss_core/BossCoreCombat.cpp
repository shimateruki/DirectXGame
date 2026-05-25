#include "BossCoreShared.h"

void BossCore::TakeBodyDamage(float damage) {
    // 既に爆散演出中、またはHP半分演出中、またはトドメ落下中は無敵
    if (deathPhase_ != 0 || isHpHalfEventActive_ || isFinisherFalling_) return;

    // 被弾前の色を保存
    SaveOriginalColors();

    // 赤色演出（ダメージフィードバック）
    SetColor({ 1.0f, 0.0f, 0.0f, 1.0f });
    colorResetTimer_ = 0.15f;

    // パラメータが設定されていなければ初期化する (安全策)
    if (!param_.has_value()) {
        param_ = EntityParameter();
        param_->hp = 1000.0f;
        param_->maxHp = 1000.0f;
    }

    float halfHp = param_->maxHp * 0.5f;
    float nextHp = param_->hp - damage;

    // ====================================================
    // ★ 追加：HPが50%を下回る瞬間に演出を開始し、HPを50%で止める
    // ====================================================
    if (!isHpHalfTriggered_ && nextHp <= halfHp) {
        param_->hp = halfHp;
        isHpHalfTriggered_ = true;

        // ★追加：ムービーに入るため、プレイヤーのロックオンを強制解除する
        if (target_) {
            if (auto player = dynamic_cast<Player*>(target_)) {
                player->RequestClearLockOn();
            }
        }

        // 強制的に待機状態へリセット
        if (currentAttack_) {
            currentAttack_->Finalize();
            currentAttack_.reset();
        }
        ChangeState(State::Idle);
        animTimer_ = 0.0f;

        SetColor(greenColor_);
        defaultColor_ = greenColor_;
        SetTranslate({ 0.0f, 4.0f, 0.0f }); // 演出開始時に強制的に真ん中へ移動(T)
        SetScale({ 1.0f, 1.0f, 1.0f });     // スケールをリセット(S)
        SetRotation({ 0.0f, 0.0f, 0.0f });  // 回転をリセット(R)
        flyingBlocks_.clear();
        FullyRecoverBarrierAndArmor();

        // HPが半分になった時、ステージの地面にまばらに20個のブロックを設置する
        if (auto currentScene = SceneManager::GetInstance()->GetCurrentScene()) {
            if (common_) {
                struct BlockSpawnInfo {
                    int x;
                    float y;
                    int z;
                    float scale;
                    float rotY;
                    float rotZ;
                };
                std::vector<BlockSpawnInfo> spawnInfos;
                spawnInfos.reserve(20);

                // 動的にまばらな配置を生成（最小距離を保証したランダム配置）
                for (int i = 0; i < 20; ++i) {
                    float x = 0.0f, z = 0.0f;
                    bool farEnough = false;
                    int attempts = 0;
                    while (!farEnough && attempts < 100) {
                        // 半径 15.0f 〜 65.0f の範囲に散布
                        float radius = 15.0f + (static_cast<float>(rand()) / RAND_MAX) * 50.0f;
                        float angle = (static_cast<float>(rand()) / RAND_MAX) * 3.14159265f * 2.0f;
                        x = std::cos(angle) * radius;
                        z = std::sin(angle) * radius;

                        // 他のブロックと十分に離れているかチェック（最小距離 15.0f）
                        farEnough = true;
                        for (const auto& existing : spawnInfos) {
                            float dx = x - static_cast<float>(existing.x);
                            float dz = z - static_cast<float>(existing.z);
                            float distSq = dx * dx + dz * dz;
                            if (distSq < 15.0f * 15.0f) {
                                farEnough = false;
                                break;
                            }
                        }
                        attempts++;
                    }

                    float scale = 1.0f + (static_cast<float>(rand()) / RAND_MAX) * 0.5f;
                    // Y軸、Z軸ともに完全にランダムな回転角を適用
                    float rotY = (static_cast<float>(rand()) / RAND_MAX) * 3.14159265f * 2.0f;
                    float rotZ = (static_cast<float>(rand()) / RAND_MAX) * 3.14159265f * 2.0f;

                    // 浮動小数点数は整数値（例：1.0f, -2.0f）となるように丸める
                    // 埋まり具合を表現するため、y座標は 0.7f に下げる
                    spawnInfos.push_back({
                        static_cast<int>(std::round(x)),
                        0.7f,
                        static_cast<int>(std::round(z)),
                        scale,
                        rotY,
                        rotZ
                    });
                }

                int blockIdx = 0;
                for (const auto& info : spawnInfos) {
                    auto mapBlock = std::make_unique<MapBlock>();
                    mapBlock->Initialize(common_);

                    // JSON設定と同一の設定を適用
                    mapBlock->SetName("HP_Half_Placed_Block_" + std::to_string(blockIdx++));
                    mapBlock->SetClassName("MapBlock");
                    mapBlock->SetModel("block");
                    
                    // スケールを1〜1.5の範囲でまばらに適用
                    mapBlock->SetScale({ info.scale, info.scale, info.scale });
                    
                    // 回転（Y軸とZ軸の両方）をまばらに適用
                    mapBlock->SetRotation({ 0.0f, info.rotY, info.rotZ });
                    
                    mapBlock->SetTranslate({ static_cast<float>(info.x), static_cast<float>(info.y), static_cast<float>(info.z) });

                    // 衝突属性とマスク (kGround | kMapBlock / kPlayer | kEnemy)
                    mapBlock->SetCollisionAttribute(kGround | kMapBlock);
                    mapBlock->SetCollisionMask(kPlayer | kEnemy);

                    // グラフィックス/マテリアル
                    mapBlock->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
                    mapBlock->SetEnableOutline(true);
                    mapBlock->SetEmissive(1.0f);
                    mapBlock->SetEnableEnvMap(true);
                    mapBlock->SetEnvIntensity(0.1f);
                    mapBlock->SetMetallic(0.6f);
                    mapBlock->SetRoughness(0.8f);
                    mapBlock->SetEnableNormalMap(false);
                    mapBlock->SetNormalMap("Resources/sprite/b.png");

                    // コライダー (OBB, size 1x1x1, center 0,0,0)
                    Object3d::ColliderConfig config = mapBlock->GetColliderConfig();
                    config.type = ColliderType::kOBB;
                    config.center = { 0.0f, 0.0f, 0.0f };
                    config.size = { 1.0f, 1.0f, 1.0f };
                    config.rotation = { 0.0f, 0.0f, 0.0f };
                    mapBlock->SetColliderConfig(config);

                    // 行列更新
                    mapBlock->UpdateLocalMatrix();
                    mapBlock->UpdateWorldMatrix();

                    // 登録
                    CollisionManager::GetInstance()->AddObject(mapBlock.get());
                    currentScene->AddObject(std::move(mapBlock));
                }
            }
        }

        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            if (armorBlocks_[i]) {
                armorBlocks_[i]->SetParent(this);
                armorBlocks_[i]->GetTransform()->isQuaternionMaster = false;
                OrbitData orbit = GetIdleOrbit(i);
                armorBlocks_[i]->SetTranslate(orbit.pos);
                armorBlocks_[i]->SetRotation(orbit.rot);
                armorBlocks_[i]->SetScale(orbit.scale);
                armorBlocks_[i]->SetCollisionAttribute(kGround);
            }
        }

        isHpHalfEventActive_ = true;
        hpHalfPhase_ = HpHalfEventPhase::WaitIdle;
        hpHalfEffectTimer_ = 0.0f;

        if (target_) {
            if (auto player = dynamic_cast<Player*>(target_)) {
                player->SetIsControlActive(false);
                player->SetVelocity({ 0.0f, 0.0f, 0.0f });
                player->SetTranslate({ 0.0f, 1.231f, -60.0f });
                player->UpdateWorldMatrix();
            }
        }

        // ★ 追加：作成いただいたカメラアニメーション（JSON）を再生する
        // ゴーストレーダー（GhostRecorder）で作成されたアニメーションを直接再生（橋が落ちる処理と同じ方式）
        bool isCameraFound = false;
        if (sceneManager_ && sceneManager_->GetCurrentScene()) {
            auto& objects = sceneManager_->GetCurrentScene()->GetObjects();
            for (auto& obj : objects) {
                // ★ 修正：ユーザーが配置した専用のカメラオブジェクト「EnemyHP50_Animation」を探す！
                if (obj->GetName() == "EnemyHP50_Animation") {
                    isCameraFound = true;
                    if (obj->recorder_) {
                        obj->recorder_->Play("EnemyHP50_Animation", false, false, true);
                        DebugConsole::GetInstance()->AddLog("【EVENT】 " + obj->GetName() + " を代用して EnemyHP50_Animation を再生指示！");
                    } else {
                        DebugConsole::GetInstance()->AddLog("【エラー】 " + obj->GetName() + " に GhostRecorder がアタッチされていません！");
                    }
                    break;
                }
            }
        } else {
            DebugConsole::GetInstance()->AddLog("【エラー】 sceneManager_ または GetCurrentScene() が nullptr です！");
        }
        
        if (!isCameraFound && sceneManager_ && sceneManager_->GetCurrentScene()) {
            DebugConsole::GetInstance()->AddLog("【エラー】 シネマティックカメラがシーン内に見つかりません！");
        }

        DebugConsole::GetInstance()->AddLog("【EVENT】 ボスHPが50%に到達！演出開始。");
        return;
    }

    // ====================================================
    // ★ 追加：最終奥義発動前、または大技中でトドメ待ちでないならHP1で踏みとどまる！
    // ====================================================
    if (!isFinalPhase_ && nextHp <= 1.0f) {
        nextHp = 1.0f;
    } else if (isFinalPhase_ && !isWaitingForFinisher_ && nextHp <= 1.0f) {
        nextHp = 1.0f;
    }

    param_->hp = nextHp;

    // HPが0以下になったら死亡演出を開始
    if (param_->hp <= 0.0f) {
        param_->hp = 0.0f;
        StartDeathSequence();
    }
}

// =================================================================
// 各ステートの個別更新処理
// =================================================================

void BossCore::SetBlockColor(Object3d* block, const Vector4& color) {
    if (!block) return;
    block->SetColor(color);
    for (Object3d* child : block->GetChildren()) {
        if (child) {
            child->SetColor(color);
        }
    }
}

void BossCore::SaveOriginalColors() {
    if (colorResetTimer_ <= 0.0f) {
        defaultColor_ = GetColor();
        savedBlockColors_.resize(armorBlocks_.size());
        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            if (armorBlocks_[i]) {
                savedBlockColors_[i] = armorBlocks_[i]->GetColor();
            } else {
                savedBlockColors_[i] = { 1.0f, 1.0f, 1.0f, 1.0f };
            }
        }
    }
}

bool BossCore::OnCollision(Object3d* other) {
    uint32_t attribute = other->GetCollisionAttribute();

    if (attribute & kPlayerAttack) {
        // --- 連続ヒット防止：クールダウン中なら無視する ---
        if (damageCooldownTimer_ > 0.0f) {
            return true;
        }

        CollisionInfo info = CheckCollision(other);
        if (!info.isColliding) {
            return false;
        }

        TakeBodyDamage(10.0f);
        // ★ エフェクトからの被弾を確定させてヒットリストに記録
        if (EffectObject3d* effect = dynamic_cast<EffectObject3d*>(other)) {
            effect->AddHitObject(this);
        }
        damageCooldownTimer_ = 0.5f; // 💥 追加：連続ヒット防止クールダウンを設定！
        return true;
    }

    return BaseEnemy::OnCollision(other);
}

// ==========================================
// ★ 追加：戦闘開始の合図を受け取る！

