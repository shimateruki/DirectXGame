#include "BossCoreShared.h"

// ==========================================
// マップブロックを自分の装甲として取り込む（同化する）
// ==========================================
bool BossCore::AssimilateBlock(Object3d* newBlock) {
    for (Object3d* block : armorBlocks_) {
        if (block == newBlock) return true; // 既に同化済み
    }

    // ==========================================
    // 吸収した瞬間にファンネル仕様へアップグレード
    // ==========================================
    UpgradeToFunnel(newBlock);

    newBlock->SetParent(this);
    newBlock->GetTransform()->isQuaternionMaster = false;
    newBlock->SetIsVisible(false); // 内部パーツが表示されるため親は隠す

    for (size_t i = 0; i < armorBlocks_.size(); ++i) {
        if (blockBroken_[i]) {
            armorBlocks_[i] = newBlock;
            blockHps_[i] = attackParams_.maxArmorBlockHp;
            blockBroken_[i] = false;

            newBlock->SetCollisionAttribute(kEnemyAttack);
            newBlock->SetCollisionMask(kPlayer);
            newBlock->SetEnemyType("BossArmor");
            return true;
        }
    }

    if (armorBlocks_.size() < 10) {
        AddArmorBlock(newBlock);
        return true;
    }

    // ==========================================
    // 10個満タンの時は吸収しないので、親子関係を解除して元に戻す
    // ==========================================
    newBlock->SetParent(nullptr);
    return false;
}

// ==========================================
// 装甲が10個満タン（壊れてもいない）かどうかを判定
// ==========================================
bool BossCore::IsArmorFull() const {
    if (armorBlocks_.size() < 10) return false; // 10個未満ならまだ吸える

    for (bool broken : blockBroken_) {
        if (broken) return false; // 壊れている箇所があればまだ吸える
    }

    return true; // 10個あって、1つも壊れていないなら完全体
}

// ==========================================
// 10個満タンになるまで、あと何個ブロックが必要か計算する
// ==========================================
int BossCore::GetNeededBlockCount() const {
    int validCount = 0;
    for (size_t i = 0; i < armorBlocks_.size(); ++i) {
        if (!blockBroken_[i]) {
            validCount++; // 壊れていない装甲の数を数える
        }
    }
    int needed = 10 - validCount;
    return (needed > 0) ? needed : 0; // 必要な数を返す（満タンなら0）
}

void BossCore::TakeBarrierDamage(float damage, Object3d* hitBlock) {
    // 被弾前の色を保存
    SaveOriginalColors();

    barrierHp_ -= damage;

    DebugConsole::GetInstance()->AddLog("[HIT!] Barrier Damaged! 残りHP: " + std::to_string(barrierHp_) + " / " + std::to_string(maxBarrierHp_));

    damageCooldownTimer_ = 1.0f;
    colorResetTimer_ = 0.15f;

    // ==========================================
    // 全部ではなく、当たったブロックだけを赤くする
    // ==========================================
    if (hitBlock) {
        SetBlockColor(hitBlock, { 1.0f, 0.0f, 0.0f, 1.0f });
    }

    if (barrierHp_ <= 0.0f) {
        DebugConsole::GetInstance()->AddLog("★★★ Barrier BROKEN! ★★★");
        barrierHp_ = 0.0f; // スタン開始時はゲージを0にする（ここから全回復に向けて増加）

        if (currentAttack_) {
            currentAttack_->Finalize();
            currentAttack_.reset();
        } // ダウン時の攻撃を強制終了
        animTimer_ = 0.0f;
        flyingBlocks_.clear();

        blockStartPos_.clear();
        blockTargetPos_.clear();
        blockStartRot_.clear();
        blockTargetRot_.clear();

        Vector3 bossWorldPos = GetWorldPosition();

        for (size_t i = 0; i < armorBlocks_.size(); ++i) {
            Object3d* block = armorBlocks_[i];
            if (block) {
                // まずブロックの現在のワールド座標とワールド回転を取得
                Vector3 startWorldPos = block->GetWorldPosition();
                Vector3 startWorldRot = block->GetRotation(); // ボス本体がまだ回転していないため、GetRotation()がそのままワールド回転と一致します。

                // 親子関係を解除（ワールド空間へ移行）
                block->SetParent(nullptr);
                block->SetTranslate(startWorldPos);
                block->SetRotation(startWorldRot);
                block->UpdateWorldMatrix();

                blockStartPos_.push_back(startWorldPos);
                blockStartRot_.push_back(startWorldRot);

                // 散らばり先のターゲット座標（ワールド空間）を計算
                float angle = (static_cast<float>(rand()) / RAND_MAX) * 2.0f * std::numbers::pi_v<float>;
                float distance = 5.0f + (static_cast<float>(rand()) / RAND_MAX) * 8.0f;
                float height = 0.5f; // 地面の高さに平らに置く

                Vector3 scatterPos = {
                    bossWorldPos.x + std::cos(angle) * distance,
                    height,
                    bossWorldPos.z + std::sin(angle) * distance
                };
                blockTargetPos_.push_back(scatterPos);

                // 散らばり先のターゲット回転（ワールド空間）
                Vector3 scatterRot = {
                    0.0f,
                    (static_cast<float>(rand()) / RAND_MAX) * 2.0f * std::numbers::pi_v<float>,
                    0.0f
                };
                blockTargetRot_.push_back(scatterRot);
            }
        }

        SetRotation({ 0.0f, GetRotation().y, 0.0f });
        ChangeState(State::Weak);
    }
}

void BossCore::TriggerCrashStun() {
    DebugConsole::GetInstance()->AddLog("★★★ Boss CRASH STUN! ★★★");
    
    // バリアHP（barrierHp_）は変更せず、現在の値を維持する

    // currentAttack_.reset() でダウン時の攻撃を強制終了
    //  呼び出し元（BossAttack1_Rush::Update 等）は TriggerCrashStun() の直後に
    //  即座に return するため、this (攻撃オブジェクト) が破棄されても安全。
    if (currentAttack_) {
        currentAttack_->Finalize();
        currentAttack_.reset();
    }
    animTimer_ = 0.0f;
    // 予測線を完全に消す（突進終了時と同じ方法）
    if (auto* warning = GetWarningArea()) {
        warning->SetScale({ 0.0f, 0.0f, 0.0f });
        warning->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    }
    isCrashStun_ = true;
    crashStunTimer_ = 0.0f;
    flyingBlocks_.clear();

    blockStartPos_.clear();
    blockTargetPos_.clear();
    blockStartRot_.clear();
    blockTargetRot_.clear();

    Vector3 bossWorldPos = GetWorldPosition();

    for (size_t i = 0; i < armorBlocks_.size(); ++i) {
        Object3d* block = armorBlocks_[i];
        if (block) {
            // まずブロックの現在のワールド座標とワールド回転を取得
            Vector3 startWorldPos = block->GetWorldPosition();
            Vector3 startWorldRot = block->GetRotation(); // ボス本体がまだ回転していないため、GetRotation()がそのままワールド回転と一致します。

            // 親子関係を解除（ワールド空間へ移行）
            block->SetParent(nullptr);
            block->SetTranslate(startWorldPos);
            block->SetRotation(startWorldRot);
            block->UpdateWorldMatrix();

            blockStartPos_.push_back(startWorldPos);
            blockStartRot_.push_back(startWorldRot);

            // 散らばり先のターゲット座標（ワールド空間）を計算
            float angle = (static_cast<float>(rand()) / RAND_MAX) * 2.0f * std::numbers::pi_v<float>;
            float distance = 5.0f + (static_cast<float>(rand()) / RAND_MAX) * 8.0f;
            float height = 0.5f; // 地面の高さに平らに置く

            Vector3 scatterPos = {
                bossWorldPos.x + std::cos(angle) * distance,
                height,
                bossWorldPos.z + std::sin(angle) * distance
            };
            blockTargetPos_.push_back(scatterPos);

            // 散らばり先のターゲット回転（ワールド空間）
            Vector3 scatterRot = {
                0.0f,
                (static_cast<float>(rand()) / RAND_MAX) * 2.0f * std::numbers::pi_v<float>,
                0.0f
            };
            blockTargetRot_.push_back(scatterRot);
        }
    }

    SetRotation({ 0.0f, GetRotation().y, 0.0f });
    ChangeState(State::Weak);
}

void BossCore::UpgradeToFunnel(Object3d* block) {
    if (!block) return;

    // 既に Shard があるかチェック（二重生成防止）
    for (auto* child : block->GetChildren()) {
        if (child && child->GetName().find("Shard") != std::string::npos) return;
    }

    // --- 元のブロックから見た目の情報をすべて抜き出す ---
    std::string modelName = block->GetModelName();
    Vector4 color = block->GetColor();
    float metallic = block->GetMetallic();
    float roughness = block->GetRoughness();
    float emissive = block->GetEmissive();
    float envIntensity = block->GetEnvIntensity();
    bool enableNormal = block->GetEnableNormalMap();
    bool enableEnv = block->GetEnableEnvMap();
    int32_t matType = block->GetMaterialType();
    std::string texPath = block->GetTexturePath();
    std::string normalPath = block->GetNormalMapPath();
    std::string ormPath = block->GetOrmMapPath();

    // 親ブロック自体は当たり判定や座標の軸として使うため、見た目だけ消す
    block->SetIsVisible(false);

    // 1. 中心にコアを生成（攻撃時の発光体としての役割）
    auto core = std::make_unique<Object3d>();
    core->Initialize(common_);
    core->SetModel("enemy_core");
    core->SetName(block->GetName() + "_Core");
    core->SetParent(block);
    core->SetScale({ 0.5f, 0.5f, 0.5f }); // Shardより少し小さく
    core->SetColor({ 0.0f, 0.7f, 1.0f, 1.0f });
    core->SetMaterialType(2); // 発光マテリアル
    core->SetEmissive(4.0f);
    
    // 2. 8つの Shard（分割パーツ）を生成
    float offset = 0.175f; // スケール0.65fに合わせて外縁がピッタリ1.0（-0.5〜0.5）になるように配置
    Vector3 offsets[8] = {
        {-offset, -offset, -offset}, {offset, -offset, -offset},
        {-offset,  offset, -offset}, {offset,  offset, -offset},
        {-offset, -offset,  offset}, {offset, -offset,  offset},
        {-offset,  offset,  offset}, {offset,  offset,  offset}
    };

    for (int i = 0; i < 8; ++i) {
        auto shard = std::make_unique<Object3d>();
        shard->Initialize(common_);
        shard->SetModel(modelName); // 元のマップブロックと同じモデルを使用
        shard->SetName(block->GetName() + "_Shard" + std::to_string(i + 1));
        shard->SetParent(block);
        shard->SetTranslate(offsets[i]);
        shard->SetScale({ 0.65f, 0.65f, 0.65f }); // 0.5f から 0.65f に変更（主の当たり判定にピッタリ一致）
        
        // 見た目に必要な情報を元ブロックから引き継ぐ。
        shard->SetColor(color);
        shard->SetMetallic(metallic);
        shard->SetRoughness(roughness);
        shard->SetEmissive(emissive);
        shard->SetEnvIntensity(envIntensity);
        shard->SetEnableNormalMap(enableNormal);
        shard->SetEnableEnvMap(enableEnv);
        shard->SetMaterialType(matType);
        if (!texPath.empty()) shard->SetTexture(texPath);
        if (!normalPath.empty()) shard->SetNormalMap(normalPath);
        if (!ormPath.empty()) shard->SetOrmMap(ormPath);
        
        if (sceneManager_ && sceneManager_->GetCurrentScene()) {
            sceneManager_->GetCurrentScene()->AddObject(std::move(shard));
        }
    }
    
    if (sceneManager_ && sceneManager_->GetCurrentScene()) {
        sceneManager_->GetCurrentScene()->AddObject(std::move(core));
    }
}

void BossCore::FullyRecoverBarrierAndArmor() {
    barrierHp_ = attackParams_.maxBarrierHp;
    maxBarrierHp_ = attackParams_.maxBarrierHp;
    for (size_t i = 0; i < blockHps_.size(); ++i) {
        blockHps_[i] = attackParams_.maxArmorBlockHp;
        blockBroken_[i] = false;
        if (i < armorBlocks_.size() && armorBlocks_[i]) {
            armorBlocks_[i]->SetIsVisible(true);
        }
    }
}

