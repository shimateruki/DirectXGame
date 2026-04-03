#include "BossAttack7_Absorb.h"
#include "../BossCore.h"
#include "./easing.h" 
#include "../MapBlock.h" // ★ 吸収処理のためにインクルード
#include <algorithm>
#include <cmath>

void BossAttack7_Absorb::Initialize(BossCore* boss) {
    BaseBossAttack::Initialize(boss);

    animPhase_ = 70;
    animStartPos_ = boss->GetTranslate();
}

void BossAttack7_Absorb::Update(BossCore* boss, float deltaTime) {
    auto& armorBlocks = boss->GetArmorBlocks();

    // ==========================================
    // ★ ボスが登録済みのマップブロックリストをもらう！
    // ==========================================
    auto& mapBlocks = boss->GetMapBlocks();

    // --- Phase 70: ボスが上空にフワッと浮かび上がり、タメを作る ---
    if (animPhase_ == 70) {
        animTimer_ += deltaTime;
        float duration = 2.0f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::InOutSine(t);

        Vector3 pos = boss->GetTranslate();
        pos.x = Math::Lerp(animStartPos_.x, 0.0f, easeT);
        pos.y = Math::Lerp(animStartPos_.y, 10.0f, easeT); // 高く浮く
        pos.z = Math::Lerp(animStartPos_.z, 0.0f, easeT);
        boss->SetTranslate(pos);

        Vector3 rot = boss->GetRotation();
        rot.y += 5.0f * deltaTime;
        boss->SetRotation(rot);
        boss->GetTransform()->isQuaternionMaster = false;

        if (t >= 1.0f) {
            animPhase_ = 71;
            animTimer_ = 0.0f;
        }
    }
    // --- Phase 71: リストにあるマップブロックを念動力で引き寄せる！ ---
    else if (animPhase_ == 71) {
        animTimer_ += deltaTime;
        Vector3 bossPos = boss->GetTranslate();

        // ボス本体は超高速回転！
        Vector3 rot = boss->GetRotation();
        rot.y += 12.0f * deltaTime;
        boss->SetRotation(rot);
        boss->GetTransform()->isQuaternionMaster = false;

        bool allAbsorbed = true; // 全部吸収し終わったかチェックするフラグ

        // ==========================================
        // MapBlockが自己登録している名簿を直接読みに行く！
        // ==========================================
        for (MapBlock* block : MapBlock::s_activeBlocks) {

            // すでに吸収済み（見えなくなっている）ならスキップ
            if (!block || !block->GetIsVisible()) continue;

            allAbsorbed = false; // まだ吸収していないブロックがあった！

            Vector3 blockPos = block->GetTranslate();
            Vector3 dir = { bossPos.x - blockPos.x, bossPos.y - blockPos.y, bossPos.z - blockPos.z };
            float dist = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);

            // ボスに十分に近づいたら吸収！
            if (dist < 2.5f) {
                block->OnAbsorbed(); // ★ ここで吸収関数を呼ぶ！
            }
            // まだ遠い場合はボスに向かって引き寄せる
            else {
                dir.x /= dist; dir.y /= dist; dir.z /= dist;
                float pullSpeed = 40.0f; // 引き寄せるスピード

                blockPos.x += dir.x * pullSpeed * deltaTime;
                blockPos.y += dir.y * pullSpeed * deltaTime;
                blockPos.z += dir.z * pullSpeed * deltaTime;
                block->SetTranslate(blockPos);

                // ブロックも空中で乱回転させる
                Vector3 bRot = block->GetRotation();
                bRot.x += 10.0f * deltaTime;
                bRot.y += 15.0f * deltaTime;
                block->SetRotation(bRot);
                block->GetTransform()->isQuaternionMaster = false;
            }
        }

        // 全部吸収し終わった、または5秒経過したら次のフェーズへ
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