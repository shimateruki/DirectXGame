#include "BossAttack1_Rush.h"
#include "../BossCore.h"
#include "./easing.h" // ※環境に合わせてパス(../など)を調整してください
#include <algorithm>
#include <cmath>
#include <numbers>

void BossAttack1_Rush::Initialize(BossCore* boss) {
    // 親クラスの初期化（タイマーとフラグのリセット）
    BaseBossAttack::Initialize(boss);

    blockStartPos_.clear();
    blockTargetPos_.clear();

    struct BlockSetting {
        Vector3 translate;
        Vector3 scale;
        Vector3 rotation;
    };

    std::vector<BlockSetting> settings = {
        { { -3.3f,  0.0f,  0.0f }, { 0.300f, 0.500f, 0.500f }, { 0.0f, 0.0f, 0.0f } },
        { { -2.0f,  0.0f,  0.0f }, { 1.035f, 1.000f, 1.500f }, { 0.0f, 0.0f, 0.0f } },
        { {  0.0f,  1.5f,  0.0f }, { 2.000f, 0.506f, 1.500f }, { 0.0f, 0.0f, 0.0f } },
        { {  0.0f, -1.5f,  0.0f }, { 2.000f, 0.511f, 1.500f }, { 0.0f, 0.0f, 0.0f } },
        { {  2.5f,  0.0f,  0.0f }, { 0.500f, 3.000f, 1.500f }, { 0.0f, 0.0f, 0.0f } },
        { {  3.5f,  0.0f,  0.0f }, { 0.500f, 1.000f, 0.500f }, { 0.0f, 0.0f, 0.0f } }
    };

    // ボスからブロックのリストをもらう
    auto& armorBlocks = boss->GetArmorBlocks();

    for (size_t i = 0; i < armorBlocks.size(); ++i) {
        blockStartPos_.push_back(armorBlocks[i]->GetTranslate());

        if (i < settings.size()) {
            blockTargetPos_.push_back(settings[i].translate);
            armorBlocks[i]->SetScale(settings[i].scale);
            armorBlocks[i]->SetRotation(settings[i].rotation);
            armorBlocks[i]->GetTransform()->isQuaternionMaster = false;
        }
        else {
            blockTargetPos_.push_back({ 0.0f, 0.0f, 0.0f });
        }
    }

    animPhase_ = 1; // 準備完了、Phase 1へ！
}

void BossAttack1_Rush::Update(BossCore* boss, float deltaTime) {

    auto& armorBlocks = boss->GetArmorBlocks();
    Object3d* target = boss->GetTarget();

    // --- フェーズ1: 形態変化（ブロックがカシャッと合体する） ---
    if (animPhase_ == 1) {
        animTimer_ += deltaTime;
        float duration = 1.5f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::OutExpo(t);

        for (size_t i = 0; i < armorBlocks.size(); ++i) {
            if (i < blockStartPos_.size() && i < blockTargetPos_.size()) {
                Vector3 pos = Math::Lerp(blockStartPos_[i], blockTargetPos_[i], easeT);
                armorBlocks[i]->SetTranslate(pos);
            }
        }

        if (t >= 1.0f) {
            animPhase_ = 2;
            animTimer_ = 0.0f;
            animStartPos_ = boss->GetTranslate(); // ★ ボス本体の座標を取得
        }
    }
    // --- フェーズ2: 移動 (x = -50) ---
    else if (animPhase_ == 2) {
        animTimer_ += deltaTime;
        float duration = 2.5f;
        float t = std::min(animTimer_ / duration, 1.0f);

        Vector3 pos = boss->GetTranslate();
        pos.x = Math::Lerp(animStartPos_.x, -50.0f, Easing::OutExpo(t));
        boss->SetTranslate(pos); // ★ ボス本体を動かす

        if (t >= 1.0f) {
            animPhase_ = 3;
            animTimer_ = 0.0f;
            animStartPos_ = boss->GetTranslate();
        }
    }
    // --- フェーズ3: シェイク & プレイヤー注視 ---
    else if (animPhase_ == 3) {
        animTimer_ += deltaTime;
        float duration = 3.0f;
        float t = std::min(animTimer_ / duration, 1.0f);

        if (target) {
            Vector3 toPlayer = target->GetWorldPosition() - boss->GetWorldPosition();
            float angleY = std::atan2(toPlayer.x, toPlayer.z) + (std::numbers::pi_v<float> / 2.0f);
            boss->SetRotation({ boss->GetRotation().x, angleY, boss->GetRotation().z });
            boss->GetTransform()->isQuaternionMaster = false;
        }

        Vector3 pos = animStartPos_;
        float shake = 0.3f;
        pos.x += ((float)rand() / RAND_MAX * 2.0f - 1.0f) * shake;
        pos.y += ((float)rand() / RAND_MAX * 2.0f - 1.0f) * shake;
        boss->SetTranslate(pos);

        if (t >= 1.0f) {
            animPhase_ = 4;
            animTimer_ = 0.0f;
            animStartPos_ = boss->GetTranslate();
            if (target) animTargetPos_ = target->GetWorldPosition();
        }
    }
    // --- フェーズ4: 加速突進 ---
    else if (animPhase_ == 4) {
        animTimer_ += deltaTime;
        float duration = 1.5f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easedT = std::pow(t, 4.0f);

        boss->SetTranslate(Math::Lerp(animStartPos_, animTargetPos_, easedT));

        float totalRotation = std::numbers::pi_v<float> *2.0f * 5.0f;
        boss->SetRotation({ easedT * totalRotation, boss->GetRotation().y, boss->GetRotation().z });
        boss->GetTransform()->isQuaternionMaster = false;

        if (t >= 1.0f) {
            animPhase_ = 5;
            animTimer_ = 0.0f;
        }
    }
    // --- フェーズ5: 待機軌道に向かってゆっくり復帰する ---
    else if (animPhase_ == 5) {
        if (animTimer_ == 0.0f) {
            blockStartPos_.clear();
            for (size_t i = 0; i < armorBlocks.size(); ++i) {
                blockStartPos_.push_back(armorBlocks[i]->GetTranslate());
            }
        }

        animTimer_ += deltaTime;
        float duration = 3.0f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::OutExpo(t);

        boss->SetRotation({ 0.0f, 0.0f, 0.0f });
        boss->GetTransform()->isQuaternionMaster = false;

        for (size_t i = 0; i < armorBlocks.size(); ++i) {
            if (i < blockStartPos_.size()) {
                // ★ 昇格させた GetIdleOrbit を使う！
                BossCore::OrbitData orbit = boss->GetIdleOrbit(i);

                Vector3 pos = Math::Lerp(blockStartPos_[i], orbit.pos, easeT);
                armorBlocks[i]->SetTranslate(pos);
                armorBlocks[i]->SetScale(orbit.scale);
                armorBlocks[i]->SetRotation(orbit.rot);
                armorBlocks[i]->GetTransform()->isQuaternionMaster = false;
            }
        }

        // 完全に復帰したら攻撃終了！
        if (t >= 1.0f) {
            isFinished_ = true; // ★ これをtrueにするだけで自動で待機に戻ります！
        }
    }
}