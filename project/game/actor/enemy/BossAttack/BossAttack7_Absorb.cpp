#include "BossAttack7_Absorb.h"
#include "../BossCore.h"
#include "./easing.h" 
#include "../MapBlock.h" 
#include <algorithm>
#include <cmath>

void BossAttack7_Absorb::Initialize(BossCore* boss) {
    BaseBossAttack::Initialize(boss);

    animPhase_ = 70;
    animStartPos_ = boss->GetTranslate();
    targetMapBlocks_.clear(); // 最初は空っぽにしておく
}

void BossAttack7_Absorb::Update(BossCore* boss, float deltaTime) {
    auto& armorBlocks = boss->GetArmorBlocks();

    // --- Phase 70: ボスが上空にフワッと浮かび上がり、タメを作る ---
    if (animPhase_ == 70) {
        // ==========================================
        // タメ始めた最初の瞬間に、必要な数だけロックオンする！
        // ==========================================
        if (animTimer_ == 0.0f) {
            targetMapBlocks_.clear();
            int neededCount = boss->GetNeededBlockCount();

            // ==========================================
            // 必要な数が 1 以上の時だけ探すようにする！
            // 満タン（0個）の時は、このループ自体をスキップします。
            // ==========================================
            if (neededCount > 0) {
                for (MapBlock* block : MapBlock::s_activeBlocks) {
                    if (block && block->GetIsVisible()) {
                        targetMapBlocks_.push_back(block);
                        
                        // 吸い込まれるブロックは当たり判定を消す！（ボスを押し出さないようにするため）
                        block->SetCollisionAttribute(0);
                        block->SetCollisionMask(0);

                        // 必要な数に達したら、もう探さない
                        if (targetMapBlocks_.size() >= neededCount) {
                            break;
                        }
                    }
                }
            }
        }

        animTimer_ += deltaTime;
        float duration = 2.0f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::InOutSine(t);

        Vector3 pos = boss->GetTranslate();
        // 横への移動（スライド）を無くし、元のX, Z座標を維持したまま上へ上がるようにします
        pos.x = animStartPos_.x;
        pos.y = Math::Lerp(animStartPos_.y, 10.0f, easeT);
        pos.z = animStartPos_.z;
        boss->SetTranslate(pos);

        Vector3 rot = boss->GetRotation();
        rot.y += 5.0f * deltaTime;
        boss->SetRotation(rot);
        boss->GetTransform()->isQuaternionMaster = false;

        if (t >= 1.0f) {
            // 吸い込むものが無い（最初から満タン）なら、そのまま降りる！
            if (targetMapBlocks_.empty()) {
                animPhase_ = 72;
            }
            else {
                animPhase_ = 71;
            }
            animTimer_ = 0.0f;
        }
    }
    // --- Phase 71: ロックオンしたブロックだけを念動力で引き寄せる！ ---
    else if (animPhase_ == 71) {
        animTimer_ += deltaTime;
        Vector3 bossPos = boss->GetTranslate();

        Vector3 rot = boss->GetRotation();
        rot.y += 12.0f * deltaTime;
        boss->SetRotation(rot);
        boss->GetTransform()->isQuaternionMaster = false;

        bool allAbsorbed = true;

        // ==========================================
        // 普通のfor文にして、食べ終わったものをリストから除外(nullptr)する！
        // ==========================================
        for (size_t i = 0; i < targetMapBlocks_.size(); ++i) {
            MapBlock* block = targetMapBlocks_[i];

            // すでに吸収されたか、非表示（nullptr）のものはスキップ
            if (!block || !block->GetIsVisible()) continue;

            allAbsorbed = false;

            Vector3 blockPos = block->GetTranslate();
            Vector3 dir = { bossPos.x - blockPos.x, bossPos.y - blockPos.y, bossPos.z - blockPos.z };
            float dist = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);

            if (dist < 2.5f) {
                bool assimilated = boss->AssimilateBlock(block);

                if (assimilated) {
                    // 世界の大元リストから消す
                    auto it = std::find(MapBlock::s_activeBlocks.begin(), MapBlock::s_activeBlocks.end(), block);
                    if (it != MapBlock::s_activeBlocks.end()) {
                        MapBlock::s_activeBlocks.erase(it);
                    }
                    // ==========================================
                    // 魔法の1行：自分のロックオンリストからも消す！
                    // これがないと、同じブロックを何回も食べてしまいます！
                    // ==========================================
                    targetMapBlocks_[i] = nullptr;
                }
            }
            else {
                dir.x /= dist; dir.y /= dist; dir.z /= dist;
                float pullSpeed = 40.0f;

                blockPos.x += dir.x * pullSpeed * deltaTime;
                blockPos.y += dir.y * pullSpeed * deltaTime;
                blockPos.z += dir.z * pullSpeed * deltaTime;
                block->SetTranslate(blockPos);

                Vector3 bRot = block->GetRotation();
                bRot.x += 10.0f * deltaTime;
                bRot.y += 15.0f * deltaTime;
                block->SetRotation(bRot);
                block->GetTransform()->isQuaternionMaster = false;
            }
        }

        // 全部吸収し終わった（すべてnullptrになった）ら、待たずにすぐ終了！
        if (allAbsorbed || animTimer_ > 5.0f) {
            animPhase_ = 72;
            animTimer_ = 0.0f;
            animStartPos_ = boss->GetTranslate();
        }
    }
    // --- Phase 72: 地面にドスンと降りて復帰する ---
    else if (animPhase_ == 72) {
        animTimer_ += deltaTime;
        float duration = 1.0f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::OutExpo(t);

        Vector3 pos = boss->GetTranslate();
        pos.y = Math::Lerp(animStartPos_.y, 4.0f, easeT);
        boss->SetTranslate(pos);

        for (size_t i = 0; i < armorBlocks.size(); ++i) {
            BossCore::OrbitData orbit = boss->GetIdleOrbit(i);
            armorBlocks[i]->SetTranslate(orbit.pos);
            armorBlocks[i]->SetScale(orbit.scale);
            armorBlocks[i]->SetRotation(orbit.rot);
            armorBlocks[i]->GetTransform()->isQuaternionMaster = false;
        }

        if (t >= 1.0f) {
            isFinished_ = true;
        }
    }
}