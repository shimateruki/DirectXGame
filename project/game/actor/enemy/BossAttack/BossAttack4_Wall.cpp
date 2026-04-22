#include "BossAttack4_Wall.h"
#include "../BossCore.h"
#include "./easing.h" // ※環境に合わせてパスを調整してください
#include <algorithm>
#include <cmath>
#include <numbers>
#include "CameraManager.h"
#include "CameraEditor.h"
#include"Player.h"
#include"LockOnSystem.h"
#include <DebugConsole.h>
void BossAttack4_Wall::Initialize(BossCore* boss) {
    BaseBossAttack::Initialize(boss);

    blockStartPos_.clear();
    blockTargetPos_.clear();
    wallStep_ = 0; // カウントリセット
    Player* player = dynamic_cast<Player*>(boss->GetTarget());
    if (player) {
        player->SetForceLockOnTarget(boss);
        DebugConsole::GetInstance()->AddLog("強制ロックオン命令を送信！"); // ★ここを追加
    }
    else {
        DebugConsole::GetInstance()->AddLog(LogLevel::Error, "エラー！プレイヤーが見つからない！"); // ★ここを追加
    }
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

            // ==========================================
            // 現在のブロック数に合わせて、常に「真ん中」を自動計算する！
            // ==========================================
            float centerIndex = (armorBlocks.size() - 1.0f) / 2.0f;
            float offset = -(static_cast<float>(i) - centerIndex) * blockWidth;

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
    else if (animPhase_ == 40) {
        if (animTimer_ == 0.0f) {
            Object3d* warning = boss->GetWarningArea();
            if (warning) {
                warning->SetParent(nullptr);
                warning->SetCollisionAttribute(0);
                warning->SetCollisionMask(0);
                warning->SetMaterialType(8); // 隙間シェーダー
                warning->SetEmissive(3.0f);
                float baseModelSize = 2.0f; // Planeの基本サイズ
                float wallWidth = 25.0f * armorBlocks.size(); // 壁の総横幅
                float stageLength = 150.0f;

                // 実際のScale値を計算
                float scaleWidth = wallWidth / baseModelSize;
                float scaleLength = stageLength / baseModelSize;

                if (wallStep_ == 0 || wallStep_ == 1) {
                    warning->SetTexture("Resources/sprite/yazirusi1.png"); // 上向き
                    warning->SetScale({ scaleWidth, 1.0f, scaleLength });
                }
                else {
                    warning->SetTexture("Resources/sprite/yazirusi1Right.png"); // 右向き
                    warning->SetScale({ scaleLength, 1.0f, scaleWidth });
                }

                warning->SetTranslate({ 0.0f, 0.7f, 0.0f });
                warning->SetRotation({ 0.0f, 0.0f, 0.0f });
                warning->GetTransform()->isQuaternionMaster = false;
            }
        }
        // --- 毎フレーム実行する処理 ---
        animTimer_ += deltaTime;
        float duration = 1.5f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::OutExpo(t);

        Vector3 bossPos = boss->GetTranslate();

        if (wallStep_ == 0)      bossPos.x = Math::Lerp(animStartPos_.x, 0.0f, easeT), bossPos.y = Math::Lerp(animStartPos_.y, 8.0f, easeT), bossPos.z = Math::Lerp(animStartPos_.z, 150.0f, easeT);
        else if (wallStep_ == 1) bossPos.x = Math::Lerp(animStartPos_.x, 0.0f, easeT), bossPos.y = Math::Lerp(animStartPos_.y, 8.0f, easeT), bossPos.z = Math::Lerp(animStartPos_.z, -150.0f, easeT);
        else if (wallStep_ == 2) bossPos.x = Math::Lerp(animStartPos_.x, 150.0f, easeT), bossPos.y = Math::Lerp(animStartPos_.y, 8.0f, easeT), bossPos.z = Math::Lerp(animStartPos_.z, 0.0f, easeT);
        else if (wallStep_ == 3) bossPos.x = Math::Lerp(animStartPos_.x, -150.0f, easeT), bossPos.y = Math::Lerp(animStartPos_.y, 8.0f, easeT), bossPos.z = Math::Lerp(animStartPos_.z, 0.0f, easeT);

        boss->SetTranslate(bossPos);

        for (size_t i = 0; i < armorBlocks.size(); ++i) {
            if (i < blockStartPos_.size() && i < blockTargetPos_.size()) {
                Vector3 pos = Math::Lerp(blockStartPos_[i], blockTargetPos_[i], easeT);
                armorBlocks[i]->SetTranslate(pos);
            }
        }

        boss->SetRotation({ 0.0f, 0.0f, 0.0f });
        boss->GetTransform()->isQuaternionMaster = false;

        // ==================================================
        // ★ 修正2：UVのマイナス反転による、完璧な方向コントロール！
        // ==================================================
        Object3d* warning = boss->GetWarningArea();
        if (warning) {
            float alpha = std::min(t * 1.5f, 0.6f);
            warning->SetColor({ 1.0f, 0.8f, 0.0f, alpha });

            static Math math;
            Vector3 uvScale;
            Vector3 uvTranslate;
            float scrollSpeed = 5.0f;

       
            // ==================================================
                // ★ UVスクロール最終決定版！
                // ==================================================
            float tilesX = 10.0f;
            float tilesY = 25.0f;

            if (wallStep_ == 0) {
                // 奥から手前（下へ移動）
                uvScale = { tilesX, -tilesY, 1.0f }; 
                uvTranslate = { 0.0f, animTimer_ * scrollSpeed, 0.0f };
            }
            else if (wallStep_ == 1) {
                // 手前から奥（上へ移動）
                uvScale = { tilesX, tilesY, 1.0f };
                // ★ 修正：マイナスを外しました！（これで下から上へ流れます）
                uvTranslate = { 0.0f, animTimer_ * scrollSpeed, 0.0f };
            }
            else if (wallStep_ == 2) {
                // 右から左（左へ移動）
                uvScale = { -tilesY, tilesX, 1.0f };
                // マイナスをつけました！（これで右から左へ流れます）
                uvTranslate = { -(animTimer_ * scrollSpeed), 0.0f, 0.0f };
            }
            else if (wallStep_ == 3) {
                // 左から右（右へ移動）
                uvScale = { tilesY, tilesX, 1.0f };
                uvTranslate = { -(animTimer_ * scrollSpeed), 0.0f, 0.0f };
            }
            Matrix4x4 uvMat = math.MakeAffineMatrix(uvScale, { 0.0f, 0.0f, 0.0f }, uvTranslate);
            warning->SetUVTransform(uvMat);
        }

        if (t >= 1.0f) {
            animPhase_ = 41;
            animTimer_ = 0.0f;
        }
    }
    // --- Phase 41: 壁だけがステージを往復横断！ ---
    else if (animPhase_ == 41) {
        if (animTimer_ == 0.0f) {
            Object3d* warning = boss->GetWarningArea();
            if (warning) {
                warning->SetColor({ 1.0f, 0.0f, 0.0f, 0.8f }); // 真っ赤

                static Math math;
                Vector3 uvScale;
                float tilesX = 10.0f;
                float tilesY = 25.0f;

                // ★ スクロールを止める時も、反転状態は維持する！
                if (wallStep_ == 0)      uvScale = { tilesX, -tilesY, 1.0f };
                else if (wallStep_ == 1) uvScale = { tilesX, tilesY, 1.0f };
                else if (wallStep_ == 2) uvScale = { -tilesY, tilesX, 1.0f };
                else if (wallStep_ == 3) uvScale = { tilesY, tilesX, 1.0f };

                Matrix4x4 uvMat = math.MakeAffineMatrix(uvScale, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f });
                warning->SetUVTransform(uvMat);
            }
        }

        animTimer_ += deltaTime;
        float duration = 5.0f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = std::pow(t, 2.0f);

        for (size_t i = 0; i < armorBlocks.size(); ++i) {
            Vector3 blockPos = armorBlocks[i]->GetTranslate();

            if (wallStep_ == 0)      blockPos.z = Math::Lerp(150.0f, -150.0f, easeT);
            else if (wallStep_ == 1) blockPos.z = Math::Lerp(-150.0f, 150.0f, easeT);
            else if (wallStep_ == 2) blockPos.x = Math::Lerp(150.0f, -150.0f, easeT);
            else if (wallStep_ == 3) blockPos.x = Math::Lerp(-150.0f, 150.0f, easeT);

            armorBlocks[i]->SetTranslate(blockPos);
        }

        if (t >= 1.0f) {
            Object3d* warning = boss->GetWarningArea();
            if (warning) {
                warning->SetScale({ 0.0f, 0.0f, 0.0f });
            }
     
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
                Player* player = dynamic_cast<Player*>(boss->GetTarget());
                if (player) {
                    player->SetForceLockOnTarget(nullptr);
                    player->RequestClearLockOn();
                }
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