#include "BossCoreShared.h"

namespace {
constexpr float kArmorShardOffset = 0.35f;
constexpr float kArmorBreakMinGroundY = 0.5f;
constexpr float kArmorBreakBossFloorOffsetY = 3.5f;
constexpr float kArmorBreakShardScale = 0.9f;

Vector3 GetArmorShardLocalOffset(size_t index) {
    const Vector3 offsets[8] = {
        {-kArmorShardOffset, -kArmorShardOffset, -kArmorShardOffset},
        { kArmorShardOffset, -kArmorShardOffset, -kArmorShardOffset},
        {-kArmorShardOffset,  kArmorShardOffset, -kArmorShardOffset},
        { kArmorShardOffset,  kArmorShardOffset, -kArmorShardOffset},
        {-kArmorShardOffset, -kArmorShardOffset,  kArmorShardOffset},
        { kArmorShardOffset, -kArmorShardOffset,  kArmorShardOffset},
        {-kArmorShardOffset,  kArmorShardOffset,  kArmorShardOffset},
        { kArmorShardOffset,  kArmorShardOffset,  kArmorShardOffset}
    };
    return offsets[index % 8];
}

size_t GetArmorShardIndex(const std::string& name, size_t fallbackIndex) {
    size_t shardPos = name.find("Shard");
    if (shardPos == std::string::npos) {
        return fallbackIndex % 8;
    }

    size_t numberPos = shardPos + 5;
    int number = 0;
    while (numberPos < name.size() && name[numberPos] >= '0' && name[numberPos] <= '9') {
        number = number * 10 + (name[numberPos] - '0');
        ++numberPos;
    }

    if (number >= 1 && number <= 8) {
        return static_cast<size_t>(number - 1);
    }
    return fallbackIndex % 8;
}

void ResetArmorShardForFunnel(Object3d* shard, size_t fallbackIndex, int32_t materialType, const Vector4& color) {
    if (!shard) {
        return;
    }

    shard->SetTranslate(GetArmorShardLocalOffset(GetArmorShardIndex(shard->GetName(), fallbackIndex)));
    shard->SetScale({ 0.65f, 0.65f, 0.65f });
    shard->SetRotation({ 0.0f, 0.0f, 0.0f });
    shard->SetIsVisible(true);
    shard->SetMaterialType(materialType);
    shard->SetColor(color);
    shard->SetCollisionAttribute(0);
    shard->SetCollisionMask(0);
    shard->GetTransform()->isQuaternionMaster = false;
}

float ResolveArmorBreakGroundY(const BossCore* boss) {
    if (!boss) {
        return kArmorBreakMinGroundY;
    }

    return (std::max)(kArmorBreakMinGroundY, boss->GetWorldPosition().y - kArmorBreakBossFloorOffsetY);
}
}

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
            if (i < armorBreakMotions_.size() && armorBlocks_[i]) {
                size_t resetIndex = 0;
                for (ArmorBreakMotion::ChildPiece& piece : armorBreakMotions_[i].childPieces) {
                    if (!piece.object) {
                        continue;
                    }
                    piece.object->SetParent(armorBlocks_[i]);
                    ResetArmorShardForFunnel(piece.object, resetIndex++, armorBlocks_[i]->GetMaterialType(), armorBlocks_[i]->GetColor());
                    piece.object->SetIsVisible(false);
                }
            }
            armorBlocks_[i] = newBlock;
            blockHps_[i] = attackParams_.maxArmorBlockHp;
            blockBroken_[i] = false;
            if (i < armorBreakMotions_.size()) {
                armorBreakMotions_[i] = {};
            }

            newBlock->SetCollisionAttribute(kGround);
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

void BossCore::StartArmorBlockBreak(size_t index) {
    if (index >= armorBlocks_.size() || !armorBlocks_[index]) {
        return;
    }

    if (armorBreakMotions_.size() < armorBlocks_.size()) {
        armorBreakMotions_.resize(armorBlocks_.size());
    }

    Object3d* block = armorBlocks_[index];
    ArmorBreakMotion& motion = armorBreakMotions_[index];
    if (motion.active) {
        return;
    }

    UpgradeToFunnel(block);
    Vector4 breakBaseColor = block->GetColor();
    if (index < savedBlockColors_.size()) {
        breakBaseColor = savedBlockColors_[index];
    }
    size_t resetShardIndex = 0;
    for (Object3d* child : block->GetChildren()) {
        if (child && child->GetName().find("Shard") != std::string::npos) {
            ResetArmorShardForFunnel(child, resetShardIndex++, block->GetMaterialType(), breakBaseColor);
        }
    }
    block->UpdateWorldMatrix();

    Vector3 worldPos = block->GetWorldPosition();
    float blockHalfHeight = (std::max)(0.65f, std::abs(block->GetScale().y) * 0.75f);
    float breakLiftY = (std::max)(0.0f, 0.5f + blockHalfHeight - worldPos.y);
    Vector3 breakOriginPos = worldPos;
    breakOriginPos.y += breakLiftY;
    Vector3 worldRot = block->GetRotation();
    Vector3 bossPos = GetWorldPosition();
    Vector3 dir = { worldPos.x - bossPos.x, 0.0f, worldPos.z - bossPos.z };
    float len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
    if (len > 0.001f) {
        dir.x /= len;
        dir.z /= len;
    } else {
        float angle = static_cast<float>(index) * 1.7f;
        dir.x = std::cos(angle);
        dir.z = std::sin(angle);
    }

    block->SetParent(nullptr);
    block->SetTranslate(breakOriginPos);
    block->SetRotation(worldRot);
    block->SetCollisionAttribute(0);
    block->SetCollisionMask(0);
    block->GetTransform()->isQuaternionMaster = false;
    block->UpdateWorldMatrix();

    motion.active = true;
    motion.landed = false;
    motion.rolling = false;
    motion.timer = 0.0f;
    motion.rollTimer = 0.0f;
    motion.landedTimer = 0.0f;
    motion.sparkTimer = 0.0f;
    motion.groundY = ResolveArmorBreakGroundY(this);
    motion.position = breakOriginPos;
    motion.rotation = worldRot;
    motion.baseColor = breakBaseColor;
    motion.velocity = {
        dir.x * 1.2f,
        1.0f,
        dir.z * 1.2f
    };
    motion.angularVelocity = {
        4.6f + static_cast<float>(index % 2) * 0.5f,
        5.4f + static_cast<float>(index % 4) * 0.35f,
        4.2f + static_cast<float>(index % 3) * 0.4f
    };
    motion.baseScale = block->GetScale();
    motion.landedScale = motion.baseScale;
    motion.childPieces.clear();

    std::vector<Object3d*> children = block->GetChildren();
    motion.childPieces.reserve(children.size());
    for (size_t childIndex = 0; childIndex < children.size(); ++childIndex) {
        Object3d* child = children[childIndex];
        if (!child) {
            continue;
        }

        child->UpdateWorldMatrix();
        if (child->GetName().find("Shard") == std::string::npos) {
            child->SetIsVisible(false);
            child->SetCollisionAttribute(0);
            child->SetCollisionMask(0);
            continue;
        }

        Vector3 childLocalOffset = child->GetTranslate();
        Vector3 childWorldPos = child->GetWorldPosition();
        Vector3 childWorldRot = {
            block->GetRotation().x + child->GetRotation().x,
            block->GetRotation().y + child->GetRotation().y,
            block->GetRotation().z + child->GetRotation().z
        };
        Vector3 childWorldScale = {
            block->GetScale().x * child->GetScale().x * kArmorBreakShardScale,
            block->GetScale().y * child->GetScale().y * kArmorBreakShardScale,
            block->GetScale().z * child->GetScale().z * kArmorBreakShardScale
        };
        Vector3 scatterDir = {
            childLocalOffset.x,
            0.0f,
            childLocalOffset.z
        };
        float scatterLen = std::sqrt(scatterDir.x * scatterDir.x + scatterDir.z * scatterDir.z);
        if (scatterLen > 0.001f) {
            scatterDir.x /= scatterLen;
            scatterDir.z /= scatterLen;
        } else {
            float angle = static_cast<float>(childIndex) * 0.785398f;
            scatterDir.x = std::cos(angle);
            scatterDir.z = std::sin(angle);
        }
        Vector3 tangentDir = { -scatterDir.z, 0.0f, scatterDir.x };
        float tangentSign = (childIndex % 2 == 0) ? 1.0f : -1.0f;

        child->SetParent(nullptr);
        child->SetTranslate(childWorldPos);
        child->SetRotation(childWorldRot);
        child->SetScale(childWorldScale);
        child->SetCollisionAttribute(0);
        child->SetCollisionMask(0);
        child->SetIsVisible(true);
        child->GetTransform()->isQuaternionMaster = false;

        ArmorBreakMotion::ChildPiece piece;
        piece.object = child;
        piece.rolling = false;
        piece.rollTimer = 0.0f;
        piece.position = childWorldPos;
        piece.rotation = childWorldRot;
        piece.baseScale = childWorldScale;
        piece.landedScale = piece.baseScale;
        piece.baseColor = breakBaseColor;
        piece.groundY = motion.groundY + static_cast<float>(childIndex % 3) * 0.03f;
        float scatterSpeed = 1.35f + static_cast<float>(childIndex % 3) * 0.18f;
        piece.velocity = {
            scatterDir.x * scatterSpeed + tangentDir.x * tangentSign * 0.32f,
            1.1f + (std::max)(0.0f, childLocalOffset.y) * 0.9f + static_cast<float>(childIndex % 4) * 0.08f,
            scatterDir.z * scatterSpeed + tangentDir.z * tangentSign * 0.32f
        };
        piece.angularVelocity = {
            5.0f + static_cast<float>(childIndex % 3) * 0.8f,
            6.4f + static_cast<float>(childIndex % 5) * 0.65f,
            4.8f + static_cast<float>(childIndex % 4) * 0.75f
        };
        motion.childPieces.push_back(piece);
    }

    if (!motion.childPieces.empty()) {
        block->SetIsVisible(false);
    } else {
        block->SetIsVisible(true);
        block->SetScale(motion.baseScale);
    }
}

void BossCore::UpdateBrokenArmorBlocks(float deltaTime) {
    if (armorBreakMotions_.size() < armorBlocks_.size()) {
        armorBreakMotions_.resize(armorBlocks_.size());
    }

    for (size_t i = 0; i < armorBlocks_.size(); ++i) {
        if (i >= blockBroken_.size() || !blockBroken_[i] || !armorBlocks_[i]) {
            continue;
        }

        ArmorBreakMotion& motion = armorBreakMotions_[i];
        if (!motion.active) {
            StartArmorBlockBreak(i);
        }

        Object3d* block = armorBlocks_[i];
        block->SetCollisionAttribute(0);
        block->SetCollisionMask(0);

        motion.timer += deltaTime;
        motion.sparkTimer += deltaTime;

        if (!motion.childPieces.empty()) {
            bool allFinished = true;
            Vector3 pieceCenter = { 0.0f, 0.0f, 0.0f };
            int activePieceCount = 0;
            for (size_t pieceIndex = 0; pieceIndex < motion.childPieces.size(); ++pieceIndex) {
                ArmorBreakMotion::ChildPiece& piece = motion.childPieces[pieceIndex];
                Object3d* child = piece.object;
                if (!child) {
                    continue;
                }

                if (!piece.landed) {
                    allFinished = false;
                    if (!piece.rolling) {
                        piece.velocity.y -= 24.0f * deltaTime;
                    }

                    piece.position.x += piece.velocity.x * deltaTime;
                    piece.position.y += piece.velocity.y * deltaTime;
                    piece.position.z += piece.velocity.z * deltaTime;

                    piece.rotation.x += piece.angularVelocity.x * deltaTime;
                    piece.rotation.y += piece.angularVelocity.y * deltaTime;
                    piece.rotation.z += piece.angularVelocity.z * deltaTime;

                    float halfHeight = (std::max)(0.05f, piece.baseScale.y * 0.5f);
                    if (piece.rolling) {
                        piece.position.y = piece.groundY + halfHeight;
                        float friction = std::pow(0.38f, deltaTime);
                        piece.velocity.x *= friction;
                        piece.velocity.z *= friction;
                        piece.angularVelocity.x *= friction;
                        piece.angularVelocity.y *= friction;
                        piece.angularVelocity.z *= friction;
                        piece.rollTimer += deltaTime;

                        float rollDuration = 0.85f + static_cast<float>(pieceIndex % 4) * 0.07f;
                        if (piece.rollTimer >= rollDuration) {
                            piece.velocity = { 0.0f, 0.0f, 0.0f };
                            piece.angularVelocity = { 0.0f, 0.0f, 0.0f };
                            piece.landed = true;
                            piece.landedTimer = 0.0f;
                            piece.landedScale = piece.baseScale;
                            child->SetMaterialType(4);
                            child->SetColor({ piece.baseColor.x, piece.baseColor.y, piece.baseColor.z, 1.0f });
                        }
                    }
                    else if (piece.position.y - halfHeight <= piece.groundY) {
                        piece.position.y = piece.groundY + halfHeight;
                        if (std::abs(piece.velocity.y) > 2.5f && piece.bounceCount < 1) {
                            piece.velocity.y = -piece.velocity.y * 0.18f;
                            piece.velocity.x *= 0.7f;
                            piece.velocity.z *= 0.7f;
                            piece.angularVelocity.x *= 0.75f;
                            piece.angularVelocity.y *= 0.75f;
                            piece.angularVelocity.z *= 0.75f;
                            ++piece.bounceCount;
                        } else {
                            float horizontalSpeed = std::sqrt(piece.velocity.x * piece.velocity.x + piece.velocity.z * piece.velocity.z);
                            if (horizontalSpeed < 0.65f) {
                                float angle = static_cast<float>(pieceIndex) * 1.37f;
                                piece.velocity.x = std::cos(angle) * 1.15f;
                                piece.velocity.z = std::sin(angle) * 1.15f;
                            } else {
                                piece.velocity.x *= 0.95f;
                                piece.velocity.z *= 0.95f;
                            }
                            piece.velocity.y = 0.0f;
                            piece.angularVelocity.x *= 1.15f;
                            piece.angularVelocity.y *= 1.15f;
                            piece.angularVelocity.z *= 1.15f;
                            piece.rolling = true;
                            piece.rollTimer = 0.0f;
                        }
                    }

                    child->SetTranslate(piece.position);
                    child->SetRotation(piece.rotation);
                    child->GetTransform()->isQuaternionMaster = false;
                } else {
                    piece.landedTimer += deltaTime;
                    float t = std::clamp((piece.landedTimer - static_cast<float>(pieceIndex) * 0.03f) / 0.95f, 0.0f, 1.0f);
                    float shrink = 1.0f - (t * 0.9f);
                    child->SetTranslate(piece.position);
                    child->SetRotation(piece.rotation);
                    child->SetScale({
                        piece.landedScale.x * shrink,
                        piece.landedScale.y * shrink,
                        piece.landedScale.z * shrink
                    });
                    child->SetColor({ piece.baseColor.x, piece.baseColor.y, piece.baseColor.z, 1.0f - t });

                    if (t >= 1.0f) {
                        child->SetScale({ 0.0f, 0.0f, 0.0f });
                        child->SetIsVisible(false);
                    } else {
                        allFinished = false;
                    }
                }

                child->UpdateWorldMatrix();
                if (child->GetIsVisible()) {
                    pieceCenter.x += piece.position.x;
                    pieceCenter.y += piece.position.y;
                    pieceCenter.z += piece.position.z;
                    ++activePieceCount;
                }
            }

            if (activePieceCount > 0) {
                motion.position = {
                    pieceCenter.x / static_cast<float>(activePieceCount),
                    pieceCenter.y / static_cast<float>(activePieceCount),
                    pieceCenter.z / static_cast<float>(activePieceCount)
                };
            }

            if (allFinished) {
                size_t resetIndex = 0;
                for (ArmorBreakMotion::ChildPiece& piece : motion.childPieces) {
                    if (!piece.object) {
                        continue;
                    }
                    piece.object->SetParent(block);
                    ResetArmorShardForFunnel(piece.object, resetIndex++, block->GetMaterialType(), block->GetColor());
                    piece.object->SetIsVisible(false);
                }
                motion.childPieces.clear();
                motion.landed = true;
                block->SetScale({ 0.0f, 0.0f, 0.0f });
                block->SetIsVisible(false);
            }
        }
        else if (!motion.landed) {
            if (!motion.rolling) {
                motion.velocity.y -= 24.0f * deltaTime;
            }

            Vector3 pos = motion.position;
            pos.x += motion.velocity.x * deltaTime;
            pos.y += motion.velocity.y * deltaTime;
            pos.z += motion.velocity.z * deltaTime;

            Vector3 rot = motion.rotation;
            rot.x += motion.angularVelocity.x * deltaTime;
            rot.y += motion.angularVelocity.y * deltaTime;
            rot.z += motion.angularVelocity.z * deltaTime;

            if (motion.rolling) {
                pos.y = motion.groundY;
                float friction = std::pow(0.38f, deltaTime);
                motion.velocity.x *= friction;
                motion.velocity.z *= friction;
                motion.angularVelocity.x *= friction;
                motion.angularVelocity.y *= friction;
                motion.angularVelocity.z *= friction;
                motion.rollTimer += deltaTime;

                if (motion.rollTimer >= 0.9f) {
                    motion.velocity = { 0.0f, 0.0f, 0.0f };
                    motion.angularVelocity = { 0.0f, 0.0f, 0.0f };
                    motion.landed = true;
                    motion.landedTimer = 0.0f;

                    motion.landedScale = motion.baseScale;
                    block->SetScale(motion.landedScale);
                    block->SetMaterialType(4);
                    block->SetColor({ motion.baseColor.x, motion.baseColor.y, motion.baseColor.z, 1.0f });
                    for (Object3d* child : block->GetChildren()) {
                        if (child) {
                            child->SetMaterialType(4);
                            child->SetColor({ motion.baseColor.x, motion.baseColor.y, motion.baseColor.z, 1.0f });
                        }
                    }
                }
            } else if (pos.y <= motion.groundY) {
                pos.y = motion.groundY;
                motion.velocity.y = 0.0f;
                float horizontalSpeed = std::sqrt(motion.velocity.x * motion.velocity.x + motion.velocity.z * motion.velocity.z);
                if (horizontalSpeed < 0.65f) {
                    float angle = static_cast<float>(i) * 1.37f;
                    motion.velocity.x = std::cos(angle) * 1.15f;
                    motion.velocity.z = std::sin(angle) * 1.15f;
                } else {
                    motion.velocity.x *= 0.9f;
                    motion.velocity.z *= 0.9f;
                }
                motion.angularVelocity.x *= 1.1f;
                motion.angularVelocity.y *= 1.1f;
                motion.angularVelocity.z *= 1.1f;
                motion.rolling = true;
                motion.rollTimer = 0.0f;
            }

            motion.position = pos;
            motion.rotation = rot;
            block->SetTranslate(pos);
            block->SetRotation(rot);
            block->GetTransform()->isQuaternionMaster = false;
        } else {
            block->SetTranslate(motion.position);
            block->SetRotation(motion.rotation);

            motion.landedTimer += deltaTime;
            float t = std::clamp(motion.landedTimer / 1.0f, 0.0f, 1.0f);
            float shrink = 1.0f - (t * 0.9f);
            block->SetScale({
                motion.landedScale.x * shrink,
                motion.landedScale.y * shrink,
                motion.landedScale.z * shrink
            });
            block->SetColor({ motion.baseColor.x, motion.baseColor.y, motion.baseColor.z, 1.0f - t });

            if (t >= 1.0f) {
                block->SetScale({ 0.0f, 0.0f, 0.0f });
                block->SetIsVisible(false);
            }
        }

        block->UpdateWorldMatrix();
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
    bool hasShard = false;
    size_t shardIndex = 0;
    for (auto* child : block->GetChildren()) {
        if (!child) {
            continue;
        }

        if (child->GetName().find("Shard") != std::string::npos) {
            ResetArmorShardForFunnel(child, shardIndex++, block->GetMaterialType(), block->GetColor());
            hasShard = true;
        } else if (child->GetName().find("Core") != std::string::npos) {
            child->SetTranslate({ 0.0f, 0.0f, 0.0f });
            child->SetScale({ 0.5f, 0.5f, 0.5f });
            child->SetIsVisible(true);
            child->SetMaterialType(2);
            child->SetColor({ 0.0f, 0.7f, 1.0f, 1.0f });
            child->SetCollisionAttribute(0);
            child->SetCollisionMask(0);
        }
    }
    if (hasShard) {
        block->SetIsVisible(false);
        return;
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
/*
    
    // 2. 8つの Shard（分割パーツ）を生成
    float offset = kArmorShardOffset; // 保存済みの8分割配置と同じ間隔に戻す
    Vector3 offsets[8] = {
    Vector3 offsets[8] = {
        {-offset, -offset, -offset}, {offset, -offset, -offset},
        {-offset,  offset, -offset}, {offset,  offset, -offset},
        {-offset, -offset,  offset}, {offset, -offset,  offset},
        {-offset,  offset,  offset}, {offset,  offset,  offset}
    };

*/
    for (int i = 0; i < 8; ++i) {
        auto shard = std::make_unique<Object3d>();
        shard->Initialize(common_);
        shard->SetModel(modelName); // 元のマップブロックと同じモデルを使用
        shard->SetName(block->GetName() + "_Shard" + std::to_string(i + 1));
        shard->SetParent(block);
        shard->SetTranslate(GetArmorShardLocalOffset(i));
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
        if (i < armorBreakMotions_.size()) {
            if (i < armorBlocks_.size() && armorBlocks_[i]) {
                size_t resetIndex = 0;
                for (ArmorBreakMotion::ChildPiece& piece : armorBreakMotions_[i].childPieces) {
                    if (!piece.object) {
                        continue;
                    }
                    piece.object->SetParent(armorBlocks_[i]);
                    ResetArmorShardForFunnel(piece.object, resetIndex++, armorBlocks_[i]->GetMaterialType(), armorBlocks_[i]->GetColor());
                    piece.object->SetIsVisible(false);
                }
            }
            armorBreakMotions_[i] = {};
        }
        if (i < armorBlocks_.size() && armorBlocks_[i]) {
            armorBlocks_[i]->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
            UpgradeToFunnel(armorBlocks_[i]);
            armorBlocks_[i]->SetCollisionAttribute(kGround);
            armorBlocks_[i]->SetCollisionMask(kPlayer);
        }
    }
}

