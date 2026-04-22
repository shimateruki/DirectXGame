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
        // --- 基本の10パーツ ---
        { { -3.3f,  0.0f,  0.0f }, { 0.300f, 0.500f, 0.500f }, { 0.0f, 0.0f, 0.0f } }, // 柄の先
        { { -2.0f,  0.0f,  0.0f }, { 1.035f, 1.000f, 1.500f }, { 0.0f, 0.0f, 0.0f } }, // 柄
        { {  0.0f,  1.5f,  0.0f }, { 2.000f, 0.506f, 1.500f }, { 0.0f, 0.0f, 0.0f } }, // 鍔(上)
        { {  0.0f, -1.5f,  0.0f }, { 2.000f, 0.511f, 1.500f }, { 0.0f, 0.0f, 0.0f } }, // 鍔(下)
        { {  2.5f,  0.0f,  0.0f }, { 0.500f, 3.000f, 1.500f }, { 0.0f, 0.0f, 0.0f } }, // 刃の根本
        { {  3.5f,  0.0f,  0.0f }, { 0.500f, 1.000f, 0.500f }, { 0.0f, 0.0f, 0.0f } }, // 刃の先
        { {  4.5f,  0.0f,  0.0f }, { 0.500f, 2.500f, 1.200f }, { 0.0f, 0.0f, 0.0f } }, // さらなる刃の延長
        { {  5.5f,  0.0f,  0.0f }, { 0.500f, 1.500f, 0.800f }, { 0.0f, 0.0f, 0.0f } }, // 鋭い切っ先
        { {  0.0f,  2.5f,  0.0f }, { 1.000f, 0.500f, 1.000f }, { 0.0f, 0.0f, 0.0f } }, // 鍔の装飾(上)
        { {  0.0f, -2.5f,  0.0f }, { 1.000f, 0.500f, 1.000f }, { 0.0f, 0.0f, 0.0f } }  // 鍔の装飾(下)
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
        pos.y = Math::Lerp(animStartPos_.y, 2.0f, Easing::OutExpo(t));
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

        // ★ 修正: プレイヤーへの「正確な方向ベクトル(dir)」を計算する
        Vector3 dir = { 0.0f, 0.0f, 1.0f }; // デフォルト
        if (target) {
            Vector3 targetPos = target->GetWorldPosition();
            // Y軸(高さ)は無視して水平方向の向きだけを取る
            dir = { targetPos.x - animStartPos_.x, 0.0f, targetPos.z - animStartPos_.z };
            float dist = std::sqrt(dir.x * dir.x + dir.z * dir.z);
            if (dist > 0.001f) {
                dir.x /= dist;
                dir.z /= dist;
            }
        }

        // ボス本体の回転（元コード通り +PI/2 の視覚的なオフセットをかける）
        float bossAngleY = std::atan2(dir.x, dir.z) + (std::numbers::pi_v<float> / 2.0f);
        boss->SetRotation({ boss->GetRotation().x, bossAngleY, boss->GetRotation().z });
        boss->GetTransform()->isQuaternionMaster = false;

        Vector3 pos = animStartPos_;
        float shake = 0.3f;
        pos.x += ((float)rand() / RAND_MAX * 2.0f - 1.0f) * shake;
        pos.y += ((float)rand() / RAND_MAX * 2.0f - 1.0f) * shake;
        boss->SetTranslate(pos);

    // ==================================================
        // ★ 予測線（予兆エリア）の表示と更新
        // ==================================================
        Object3d* warning = boss->GetWarningArea();
        if (warning) {
            warning->SetParent(nullptr);
            warning->SetCollisionAttribute(0);
            warning->SetCollisionMask(0);
            warning->SetMaterialType(0);
            warning->SetEmissive(3.0f);
            warning->SetTexture("Resources/sprite/yazirusi1.png"); // 上向き
            // ==================================================
            // ★ 画像のタイリングと縦スクロール
            // ==================================================
            static Math math;
            Vector3 uvScale = { 3.0f, 20.0f, 1.0f }; 
            float scrollSpeed = 6.0f;
            Vector3 uvTranslate = { 0.0f, t * scrollSpeed, 0.0f };
            Matrix4x4 uvMat = math.MakeAffineMatrix(uvScale, { 0.0f, 0.0f, 0.0f }, uvTranslate);
            warning->SetUVTransform(uvMat);

            float dashLength = 80.0f;
            float dashWidth = 8.0f;

            float warningAngleY = std::atan2(dir.x, dir.z);
            Vector3 warningPos = {
                animStartPos_.x + dir.x * (dashLength * 0.5f),
                0.7f,
                animStartPos_.z + dir.z * (dashLength * 0.5f)
            };

            warning->SetTranslate(warningPos);
            warning->SetRotation({ 0.0f, warningAngleY, 0.0f });
            warning->SetScale({ dashWidth, 0.1f, dashLength });
            warning->GetTransform()->isQuaternionMaster = false;

            float alpha = std::min(t * 1.5f, 0.8f);
            warning->SetColor({ 1.0f, 0.5f, 0.0f, alpha }); // 少し黄色を混ぜてオレンジに
        }
        if (t >= 1.0f) {
            animPhase_ = 4;
            animTimer_ = 0.0f;
            animStartPos_ = boss->GetTranslate();

            // ★ バスターズ風に突き抜ける設定！プレイヤーの位置ではなく、方向ベクトルの先へ！
            float dashDistance = 110.0f;
            animTargetPos_ = {
                animStartPos_.x + dir.x * dashDistance,
                animStartPos_.y, // 高さは維持
                animStartPos_.z + dir.z * dashDistance
            };
        }
    }
    // --- フェーズ4: 加速突進 ---
    else if (animPhase_ == 4) {
        animTimer_ += deltaTime;
        float duration = 1.0f; // 1秒で駆け抜ける
        float t = std::min(animTimer_ / duration, 1.0f);
        float easedT = std::pow(t, 4.0f);

        boss->SetTranslate(Math::Lerp(animStartPos_, animTargetPos_, easedT));

        // ドリル回転
        float totalRotation = std::numbers::pi_v<float> *2.0f * 5.0f;
        boss->SetRotation({ easedT * totalRotation, boss->GetRotation().y, boss->GetRotation().z });
        boss->GetTransform()->isQuaternionMaster = false;

        // ★ 突進中は予測線を最高に濃く保つ
        Object3d* warning = boss->GetWarningArea();
        if (warning) {
            warning->SetColor({ 1.0f, 0.0f, 0.0f, 0.8f });
        }

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

            // ★ 突進終了時に予測線を完全に消す
            Object3d* warning = boss->GetWarningArea();
            if (warning) {
                warning->SetScale({ 0.0f, 0.0f, 0.0f });
                warning->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
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
                BossCore::OrbitData orbit = boss->GetIdleOrbit(i);
                Vector3 pos = Math::Lerp(blockStartPos_[i], orbit.pos, easeT);
                armorBlocks[i]->SetTranslate(pos);
                armorBlocks[i]->SetScale(orbit.scale);
                armorBlocks[i]->SetRotation(orbit.rot);
                armorBlocks[i]->GetTransform()->isQuaternionMaster = false;
            }
        }

        if (t >= 1.0f) {
            isFinished_ = true;
        }
    }
}