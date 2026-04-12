#include "BossAttack8_Final.h"
#include "../BossCore.h"
#include "Object3d.h" // ★ ご指定のパス形式に統一！
#include "./easing.h" 
#include <algorithm>
#include <cmath>
#include <numbers>
#include "DebugConsole.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "CollisionManager.h"

BossAttack8_Final::~BossAttack8_Final() {
    for (Object3d* meteor : meteors_) {
        if (meteor) meteor->isDead = true;
    }
}

void BossAttack8_Final::Initialize(BossCore* boss) {
    BaseBossAttack::Initialize(boss);
    meteors_.clear();
    rainTimer_ = 0.0f;
    rainCount_ = 0;
    animPhase_ = 80;
    animStartPos_ = boss->GetTranslate();

    auto& armorBlocks = boss->GetArmorBlocks();
    for (Object3d* block : armorBlocks) {
        if (block) {
            block->SetScale({ 0.0f, 0.0f, 0.0f });
            block->SetCollisionAttribute(0);
            block->isDead = true; // エンジンの自動削除に任せる
        }
    }

    DebugConsole::GetInstance()->AddLog("【最終奥義】 フェーズ0：ボス上昇開始！");
}

void BossAttack8_Final::Update(BossCore* boss, float deltaTime) {
    BaseScene* currentScene = SceneManager::GetInstance()->GetCurrentScene();

    // ボス本体の高速回転（オーラ演出）
    if (animPhase_ < 86) {
        Vector3 bossRot = boss->GetRotation();
        bossRot.y += 15.0f * deltaTime;
        boss->SetRotation(bossRot);
        boss->GetTransform()->isQuaternionMaster = false;
    }

    // 生み出したすべての隕石を落下させる！
    for (Object3d* meteor : meteors_) {
        // Y座標が 900 以下なら「落下命令が出た」とみなす
        if (meteor && !meteor->isDead && meteor->GetTranslate().y <= 900.0f) {
            Vector3 pos = meteor->GetTranslate();
            if (pos.y > 0.0f) {
                pos.y -= 80.0f * deltaTime; // 猛スピードで落下
                if (pos.y <= 0.0f) pos.y = 0.0f; // 地面で止まる
                meteor->SetTranslate(pos);
            }
        }
    }

    // --- Phase 80: ボス上昇 ＆ 必要な【11個】だけを事前生成！ ---
    if (animPhase_ == 80) {
        if (animTimer_ == 0.0f) {
            for (int i = 0; i < 11; ++i) {
                auto meteor = std::make_unique<Object3d>();
                meteor->Initialize(boss->GetCommon());

                if (!boss->GetArmorBlocks().empty() && boss->GetArmorBlocks()[0]) {
                    meteor->SetModel(boss->GetArmorBlocks()[0]->GetModelName());
                }
                else {
                    meteor->SetModel("enemy_block");
                }

                if (i < 10) { // 通常の雨用（10個）
                    meteor->SetName("Final_Meteor");
                    meteor->SetTranslate({ 0.0f, 1000.0f, 0.0f }); // 見えない上空に待機
                    meteor->SetScale({ 3.0f, 3.0f, 3.0f });
                    meteor->SetColor({ 0.8f, 0.2f, 0.2f, 1.0f });
                }
                else { // 巨大隕石用（最後の1個）
                    meteor->SetName("Giant_Meteor");
                    meteor->SetTranslate({ 0.0f, 1000.0f, 0.0f });
                    meteor->SetScale({ 15.0f, 15.0f, 15.0f });
                    meteor->SetColor({ 1.0f, 0.0f, 0.0f, 1.0f });
                }

                // ==========================================
                // ★ 修正：エンジンにお任せするため、Sizeの手動設定を削除！
                // ==========================================
                meteor->SetColliderType(ColliderType::kOBB);
                meteor->SetCollisionAttribute(kEnemyAttack);
                meteor->SetCollisionMask(kPlayer | kGround);

                meteor->UpdateLocalMatrix();
                meteor->UpdateWorldMatrix();

                CollisionManager::GetInstance()->AddObject(meteor.get());
                meteors_.push_back(meteor.get());

                if (currentScene) {
                    currentScene->GetObjects().push_back(std::move(meteor));
                }
            }
        }

        animTimer_ += deltaTime;
        float duration = 2.0f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::InOutSine(t);

        Vector3 pos = boss->GetTranslate();
        pos.x = Math::Lerp(animStartPos_.x, 0.0f, easeT);
        pos.y = Math::Lerp(animStartPos_.y, 25.0f, easeT); // はるか上空へ
        pos.z = Math::Lerp(animStartPos_.z, 0.0f, easeT);
        boss->SetTranslate(pos);

        float flash = (std::sin(animTimer_ * 20.0f) + 1.0f) * 0.5f;
        boss->SetColor({ 1.0f, flash * 0.3f, flash * 0.3f, 1.0f });

        if (t >= 1.0f) {
            animPhase_ = 81;
            animTimer_ = 0.0f;
            DebugConsole::GetInstance()->AddLog("【最終奥義】 ブロックの雨、開始！");
        }
    }
    // --- Phase 81〜84: 四分割エリアに【10個のブロックを使い回して】降らせる ---
    else if (animPhase_ >= 81 && animPhase_ <= 84) {
        if (animTimer_ == 0.0f) {
            for (int i = 0; i < 10; ++i) {
                if (meteors_.size() > i && meteors_[i]) {
                    meteors_[i]->SetTranslate({ 0.0f, 1000.0f, 0.0f });
                }
            }
        }

        animTimer_ += deltaTime;
        rainTimer_ += deltaTime;

        if (rainTimer_ >= 0.1f && rainCount_ < 10) {
            rainTimer_ = 0.0f;

            int meteorIndex = rainCount_; // 0〜9のブロックを順番に使う

            if (meteorIndex < 10 && meteors_[meteorIndex]) {
                float rx = (static_cast<float>(rand()) / RAND_MAX) * 75.0f;
                float rz = (static_cast<float>(rand()) / RAND_MAX) * 75.0f;
                float targetX = 0.0f; float targetZ = 0.0f;

                if (animPhase_ == 81) { targetX = -rx; targetZ = rz; }  // 左奥
                if (animPhase_ == 82) { targetX = rx;  targetZ = rz; }  // 右奥
                if (animPhase_ == 83) { targetX = -rx; targetZ = -rz; } // 左手前
                if (animPhase_ == 84) { targetX = rx;  targetZ = -rz; } // 右手前

                meteors_[meteorIndex]->SetTranslate({ targetX, 50.0f + (static_cast<float>(rand()) / RAND_MAX) * 10.0f, targetZ });
            }
            rainCount_++;
        }

        if (animTimer_ >= 1.5f) {
            animPhase_++;
            animTimer_ = 0.0f;
            rainCount_ = 0;
            if (animPhase_ == 85) {
                DebugConsole::GetInstance()->AddLog("【最終奥義】 中央へ超巨大隕石、投下！！");
            }
        }
    }
    // --- Phase 85: 最後にステージ中央へ「超巨大ブロック」を落とす ---
    else if (animPhase_ == 85) {
        if (rainCount_ == 0) {
            rainCount_ = 1;

            // ==========================================
            // ★ 修正：非表示ではなく、完全に削除して Hierarchy から消す！
            // ==========================================
            for (int i = 0; i < 10; ++i) {
                if (meteors_.size() > i && meteors_[i]) {
                    meteors_[i]->SetScale({ 0.0f, 0.0f, 0.0f });
                    meteors_[i]->SetCollisionAttribute(0);
                    meteors_[i]->isDead = true; // エンジンの自動削除に任せる
                    meteors_[i] = nullptr;           // ポインタを空にする
                }
            }

            // 10番目（巨大隕石）をワープさせて落下開始！
            if (meteors_.size() > 10 && meteors_[10]) {
                meteors_[10]->SetTranslate({ 0.0f, 80.0f, 0.0f });
            }
        }

        animTimer_ += deltaTime;
        if (animTimer_ >= 2.5f) {
            animPhase_ = 86;
            animTimer_ = 0.0f;
            animStartPos_ = boss->GetTranslate();
        }
    }
    // --- Phase 86: 全ての力を使い果たし、ボスが地に落ちる ---
    else if (animPhase_ == 86) {
        animTimer_ += deltaTime;
        float duration = 3.0f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::OutBounce(t);

        Vector3 pos = boss->GetTranslate();
        pos.x = Math::Lerp(animStartPos_.x, 0.0f, easeT);
        pos.y = Math::Lerp(animStartPos_.y, 2.0f, easeT); // 地面に落下
        pos.z = Math::Lerp(animStartPos_.z, 0.0f, easeT);
        boss->SetTranslate(pos);

        boss->SetColor({ 0.2f, 0.2f, 0.2f, 1.0f });
        boss->SetRotation({ 0.0f, 0.0f, 0.0f });

        if (t >= 1.0f) {
            // ==========================================
            // ★ 修正：巨大隕石もスマートに存在を消去！
            // ==========================================
            if (meteors_.size() > 10 && meteors_[10]) {
                meteors_[10]->SetScale({ 0.0f, 0.0f, 0.0f });
                meteors_[10]->SetCollisionAttribute(0);
                meteors_[10]->isDead = true; // エンジンの自動削除に任せる
                meteors_[10] = nullptr;           // ポインタを空にする
            }
            meteors_.clear();

            boss->SetWaitingForDeath(true);
            isFinished_ = true;

            DebugConsole::GetInstance()->AddLog("ボスは完全に沈黙した…！今だ、トドメを刺せ！！");
        }
    }
}