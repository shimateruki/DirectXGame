#include "BossAttack4_Wall.h"
#include "../BossCore.h"
#include "./easing.h" // ※環境に合わせてパスを調整してください
#include <algorithm>
#include <cmath>
#include <numbers>

void BossAttack4_Wall::Initialize(BossCore* boss) {
    BaseBossAttack::Initialize(boss);

    blockStartPos_.clear();
    blockTargetPos_.clear();
    wallStep_ = 0; // カウントリセット

    animPhase_ = 39;
}

void BossAttack4_Wall::Update(BossCore* boss, float deltaTime) {
    auto& armorBlocks = boss->GetArmorBlocks();

    // --- Phase 39: コンボ数に応じた壁の配置計算 ---
    if (animPhase_ == 39) {
        blockStartPos_.clear();
        blockTargetPos_.clear();

        float blockWidth = 25.0f;

        Vector3 bossCurrentPos = boss->GetTranslate();
        animStartPos_ = bossCurrentPos; // 移動のスタート地点を記憶

        for (size_t i = 0; i < armorBlocks.size(); ++i) {
            // 最初の1回目だけ親子関係を解除してワールド座標に変換
            if (wallStep_ == 0) {
                Vector3 localPos = armorBlocks[i]->GetTranslate();
                float bossRotY = boss->GetRotation().y;
                Vector3 worldPos;
                worldPos.x = bossCurrentPos.x + (localPos.x * std::cos(bossRotY) + localPos.z * std::sin(bossRotY));
                worldPos.y = bossCurrentPos.y + localPos.y;
                worldPos.z = bossCurrentPos.z + (-localPos.x * std::sin(bossRotY) + localPos.z * std::cos(bossRotY));

                armorBlocks[i]->SetParent(nullptr);
                armorBlocks[i]->SetTranslate(worldPos);
                blockStartPos_.push_back(worldPos);
            }
            else {
                blockStartPos_.push_back(armorBlocks[i]->GetTranslate());
            }

            float offset = -(static_cast<float>(i) - 2.5f) * blockWidth;
            Vector3 targetPos;

            if (wallStep_ == 0) {
                targetPos = { offset, 2.0f, 150.0f };
                armorBlocks[i]->SetScale({ blockWidth, 4.0f, 1.0f });
            }
            else if (wallStep_ == 1) {
                targetPos = { offset, 2.0f, -150.0f };
                armorBlocks[i]->SetScale({ blockWidth, 4.0f, 1.0f });
            }
            else if (wallStep_ == 2) {
                targetPos = { 150.0f, 2.0f, offset };
                armorBlocks[i]->SetScale({ 1.0f, 4.0f, blockWidth });
            }
            else if (wallStep_ == 3) {
                targetPos = { -150.0f, 2.0f, offset };
                armorBlocks[i]->SetScale({ 1.0f, 4.0f, blockWidth });
            }

            blockTargetPos_.push_back(targetPos);
            armorBlocks[i]->SetRotation({ 0.0f, 0.0f, 0.0f });
            armorBlocks[i]->GetTransform()->isQuaternionMaster = false;
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

        Vector3 bossPos = boss->GetTranslate();

        if (wallStep_ == 0) {
            bossPos.x = Math::Lerp(animStartPos_.x, 0.0f, easeT);
            bossPos.y = Math::Lerp(animStartPos_.y, 8.0f, easeT);
            bossPos.z = Math::Lerp(animStartPos_.z, 150.0f, easeT); // 奥
        }
        else if (wallStep_ == 1) {
            bossPos.x = Math::Lerp(animStartPos_.x, 0.0f, easeT);
            bossPos.y = Math::Lerp(animStartPos_.y, 8.0f, easeT);
            bossPos.z = Math::Lerp(animStartPos_.z, -150.0f, easeT); // 手前
        }
        else if (wallStep_ == 2) {
            bossPos.x = Math::Lerp(animStartPos_.x, 150.0f, easeT); // 右
            bossPos.y = Math::Lerp(animStartPos_.y, 8.0f, easeT);
            bossPos.z = Math::Lerp(animStartPos_.z, 0.0f, easeT);
        }
        else if (wallStep_ == 3) {
            bossPos.x = Math::Lerp(animStartPos_.x, -150.0f, easeT); // 左
            bossPos.y = Math::Lerp(animStartPos_.y, 8.0f, easeT);
            bossPos.z = Math::Lerp(animStartPos_.z, 0.0f, easeT);
        }
        boss->SetTranslate(bossPos);

        for (size_t i = 0; i < armorBlocks.size(); ++i) {
            if (i < blockStartPos_.size() && i < blockTargetPos_.size()) {
                Vector3 pos = Math::Lerp(blockStartPos_[i], blockTargetPos_[i], easeT);
                armorBlocks[i]->SetTranslate(pos);
            }
        }

        boss->SetRotation({ 0.0f, 0.0f, 0.0f });
        boss->GetTransform()->isQuaternionMaster = false;

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

        for (size_t i = 0; i < armorBlocks.size(); ++i) {
            Vector3 blockPos = armorBlocks[i]->GetTranslate();

            if (wallStep_ == 0) {
                blockPos.z = Math::Lerp(150.0f, -150.0f, easeT);
            }
            else if (wallStep_ == 1) {
                blockPos.z = Math::Lerp(-150.0f, 150.0f, easeT);
            }
            else if (wallStep_ == 2) {
                blockPos.x = Math::Lerp(150.0f, -150.0f, easeT);
            }
            else if (wallStep_ == 3) {
                blockPos.x = Math::Lerp(-150.0f, 150.0f, easeT);
            }
            armorBlocks[i]->SetTranslate(blockPos);
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
            wallStep_++; // ★ ループ回数を進める

            if (wallStep_ < 4) {
                animPhase_ = 39; // まだ4回終わってなければ次へ
            }
            else {
                animPhase_ = 43; // 4回終わったら復帰フェーズへ
            }
            animTimer_ = 0.0f;
        }
    }
    // --- Phase 43: 親子関係を復活させ、コアも元の定位置に戻る ---
    else if (animPhase_ == 43) {
        if (animTimer_ == 0.0f) {
            blockStartPos_.clear();
            Vector3 bossPos = boss->GetTranslate();
            float bossRotY = boss->GetRotation().y;

            for (size_t i = 0; i < armorBlocks.size(); ++i) {
                Vector3 worldPos = armorBlocks[i]->GetTranslate();
                Vector3 offset = { worldPos.x - bossPos.x, worldPos.y - bossPos.y, worldPos.z - bossPos.z };
                Vector3 localPos;
                localPos.x = offset.x * std::cos(-bossRotY) + offset.z * std::sin(-bossRotY);
                localPos.y = offset.y;
                localPos.z = -offset.x * std::sin(-bossRotY) + offset.z * std::cos(-bossRotY);

                armorBlocks[i]->SetParent(boss); // ★ 親を boss に戻す
                armorBlocks[i]->SetTranslate(localPos);
                blockStartPos_.push_back(localPos);
            }
            animStartPos_ = bossPos;
        }

        animTimer_ += deltaTime;
        float duration = 3.0f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::OutExpo(t);

        Vector3 bossPos = boss->GetTranslate();
        bossPos.x = Math::Lerp(animStartPos_.x, 0.0f, easeT);
        bossPos.y = Math::Lerp(animStartPos_.y, 4.0f, easeT);
        bossPos.z = Math::Lerp(animStartPos_.z, 0.0f, easeT);
        boss->SetTranslate(bossPos);

        boss->SetRotation({ 0.0f, 0.0f, 0.0f });
        boss->GetTransform()->isQuaternionMaster = false;

        for (size_t i = 0; i < armorBlocks.size(); ++i) {
            if (i < blockStartPos_.size()) {
                BossCore::OrbitData orbit = boss->GetIdleOrbit(i);
                Vector3 pos = Math::Lerp(blockStartPos_[i], orbit.pos, easeT);
                armorBlocks[i]->SetTranslate(pos);
                armorBlocks[i]->SetScale(orbit.scale);
                armorBlocks[i]->SetRotation(orbit.rot);
                armorBlocks[i]->GetTransform()->isQuaternionMaster = false;
            }
        }

        if (t >= 1.0f) {
            isFinished_ = true; // 攻撃完了！
        }
    }
}