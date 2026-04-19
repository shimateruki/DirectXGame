#include "BossAttack6_Laser.h"
#include "../BossCore.h"
#include "./easing.h" // ※環境に合わせてパスを調整してください
#include <algorithm>
#include <cmath>
#include <numbers>
#include "DebugConsole.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include <CollisionManager.h>

BossAttack6_Laser::~BossAttack6_Laser() {
    for (Object3d* beam : activeBeams_) {
        if (beam) beam->isDead = true;
    }
}

void BossAttack6_Laser::Initialize(BossCore* boss) {
    BaseBossAttack::Initialize(boss);

    blockStartPos_.clear();
    blockTargetPos_.clear();
    blockStartScale_.clear();
    blockTargetScale_.clear();
    attentionStartRot_.clear();

    // ==========================================
    // 前回の攻撃で作った余分なレーザーをリセット
    // ==========================================
    activeBeams_.clear();

    // ★ 修正：ここに残っていた「古いレーザー生成コード」は
    // クラッシュの原因になるため綺麗に削除しました！

    animPhase_ = 60;
}

void BossAttack6_Laser::Update(BossCore* boss, float deltaTime) {
    auto& armorBlocks = boss->GetArmorBlocks();

    float maxSpinSpeed = 8.0f;
    float fireSpinSpeed = 0.3f;

    // --- Phase 60: まずはコアが中央(0,0)へスゥーッと移動する ---
    if (animPhase_ == 60) {
        if (animTimer_ == 0.0f) {
            animStartPos_ = boss->GetTranslate();
        }

        animTimer_ += deltaTime;
        float duration = 2.5f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::InOutSine(t);

        Vector3 corePos = boss->GetTranslate();
        corePos.x = Math::Lerp(animStartPos_.x, 0.0f, easeT);
        corePos.y = Math::Lerp(animStartPos_.y, 2.0f, easeT);
        corePos.z = Math::Lerp(animStartPos_.z, 0.0f, easeT);
        boss->SetTranslate(corePos);

        for (size_t i = 0; i < armorBlocks.size(); ++i) {
            BossCore::OrbitData orbit = boss->GetIdleOrbit(i);
            armorBlocks[i]->SetTranslate(orbit.pos);
            armorBlocks[i]->SetScale(orbit.scale);
            armorBlocks[i]->SetRotation(orbit.rot);
            armorBlocks[i]->GetTransform()->isQuaternionMaster = false;
        }

        if (t >= 1.0f) {
            animPhase_ = 61;
            animTimer_ = 0.0f;
        }
    }
    // --- Phase 61: 待機軌道のまま、コアがギュイィィンと回転し始める ---
    else if (animPhase_ == 61) {
        if (animTimer_ == 0.0f) {
            for (size_t i = 0; i < armorBlocks.size(); ++i) {
                BossCore::OrbitData orbit = boss->GetIdleOrbit(i);
                armorBlocks[i]->SetParent(boss);
                armorBlocks[i]->SetTranslate(orbit.pos);
                armorBlocks[i]->SetScale(orbit.scale);
                armorBlocks[i]->SetRotation(orbit.rot);
                armorBlocks[i]->GetTransform()->isQuaternionMaster = false;
            }
        }

        animTimer_ += deltaTime;
        float duration = 2.25f;
        float t = std::min(animTimer_ / duration, 1.0f);

        float currentSpinSpeed = Math::Lerp(0.0f, maxSpinSpeed, Easing::InSine(t));

        Vector3 rot = boss->GetRotation();
        rot.y += currentSpinSpeed * deltaTime;
        boss->SetRotation(rot);
        boss->GetTransform()->isQuaternionMaster = false;

        if (t >= 1.0f) {
            animPhase_ = 62;
            animTimer_ = 0.0f;
        }
    }
    // --- Phase 62: 大回転を維持したまま、砲台陣形へ変形！ ---
    else if (animPhase_ == 62) {
        if (animTimer_ == 0.0f) {
            blockStartPos_.clear();
            blockTargetPos_.clear();
            blockStartScale_.clear();
            blockTargetScale_.clear();
            attentionStartRot_.clear();

            float radius = 12.0f;

            for (size_t i = 0; i < armorBlocks.size(); ++i) {
                blockStartPos_.push_back(armorBlocks[i]->GetTranslate());
                blockStartScale_.push_back(armorBlocks[i]->GetScale());
                attentionStartRot_.push_back(armorBlocks[i]->GetRotation());

                float angle = (i * 2.0f * std::numbers::pi_v<float>) / armorBlocks.size();
                Vector3 targetPos = {
                    std::cos(angle) * radius,
                    0.0f,
                    std::sin(angle) * radius
                };
                blockTargetPos_.push_back(targetPos);
                blockTargetScale_.push_back({ 1.5f, 1.5f, 1.5f });
            }
        }

        animTimer_ += deltaTime;
        float duration = 3.0f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::InOutSine(t);

        Vector3 rot = boss->GetRotation();
        rot.y += maxSpinSpeed * deltaTime;
        boss->SetRotation(rot);
        boss->GetTransform()->isQuaternionMaster = false;

        for (size_t i = 0; i < armorBlocks.size(); ++i) {
            if (i < blockStartPos_.size() && i < blockTargetPos_.size()) {
                Vector3 pos = Math::Lerp(blockStartPos_[i], blockTargetPos_[i], easeT);
                armorBlocks[i]->SetTranslate(pos);

                Vector3 scale = Math::Lerp(blockStartScale_[i], blockTargetScale_[i], easeT);
                armorBlocks[i]->SetScale(scale);

                float angle = (i * 2.0f * std::numbers::pi_v<float>) / armorBlocks.size();
                Vector3 targetRot = { 0.0f, -angle, 0.0f };

                Vector3 currentRot;
                currentRot.x = Math::Lerp(attentionStartRot_[i].x, targetRot.x, easeT);
                currentRot.y = Math::Lerp(attentionStartRot_[i].y, targetRot.y, easeT);
                currentRot.z = Math::Lerp(attentionStartRot_[i].z, targetRot.z, easeT);

                armorBlocks[i]->SetRotation(currentRot);
                armorBlocks[i]->GetTransform()->isQuaternionMaster = false;
            }
        }

        if (t >= 1.0f) {
            animPhase_ = 63;
            animTimer_ = 0.0f;
        }
    }
    else if (animPhase_ == 63) {
        if (animTimer_ == 0.0f) {
            BaseScene* currentScene = SceneManager::GetInstance()->GetCurrentScene();

            for (size_t i = 0; i < armorBlocks.size(); ++i) {
                if (!armorBlocks[i]) continue;

                auto laser = std::make_unique<Object3d>();
                laser->Initialize(boss->GetCommon());
                laser->SetModel("Cylinder");
                laser->SetName("Beam_Cylinder");

                // =========================================================
                // ★ ビームの視覚エフェクト設定（初期化）
                // =========================================================

                // 1. ブレンドモードを加算（光の重なり）にして透明感を出す
                laser->SetBlendMode(BlendMode::kAdd);

                // 2. 影を無視して強烈に発光させる
                laser->SetEmissive(5.0f);
                laser->SetColor({ 1.0f, 0.0f, 0.0f, 0.8f });
                laser->SetTexture("Resources/sprite/beamNoice.png");
                laser->SetMaterialType(9); // さっき作ったレーザー専用シェーダー
                // 4. UVスケール設定（テクスチャが縦にビローンと伸びるのを防ぎ、15回繰り返す）
                static Math math;
                Vector3 uvScale = { 1.0f, 15.0f, 1.0f };
                Matrix4x4 uvMat = math.MakeAffineMatrix(uvScale, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f });
                laser->SetUVTransform(uvMat);


                // =========================================================
                // ★ 当たり判定の設定とマネージャーへの登録
                // =========================================================
                laser->SetColliderType(ColliderType::kOBB);

                // サイズを明示的に指定してあげる
                Object3d::ColliderConfig cConfig = laser->GetColliderConfig();
                cConfig.type = ColliderType::kOBB;
                cConfig.size = { 1.0f, 1.0f, 1.0f }; // 基本サイズ（スケールで伸びるので1.0でOK）
                laser->SetColliderConfig(cConfig);

                laser->SetScale({ 0.1f, 80.0f, 0.1f }); // まずは予兆の細いレーザーとして生成

                // 予兆中は当たらないように属性を0にする
                laser->SetCollisionAttribute(0);

                // マネージャーに登録して、実際に当たり判定の世界に存在させる
                CollisionManager::GetInstance()->AddObject(laser.get());


                // 座標をブロックに追従させる（※子供リストには入れないから安全！）
                laser->SetParent(armorBlocks[i]);

                float rotX90 = std::numbers::pi_v<float> / 2.0f;
                laser->SetRotation({ rotX90, 0.0f, 0.0f });
                laser->GetTransform()->isQuaternionMaster = false;

                activeBeams_.push_back(laser.get()); // リストに記憶

                // シーンの世界に正式登録（所有権を渡す）
                if (currentScene) {
                    currentScene->GetObjects().push_back(std::move(laser));
                }
            }
        }

        animTimer_ += deltaTime;
        float stopDuration = 0.75f;

        if (animTimer_ >= stopDuration) {
            animPhase_ = 64;
            animTimer_ = 0.0f;
        }
        }
    // --- Phase 64: 陣形を維持したまま回転し、ビームを撃つ！ ---
    else if (animPhase_ == 64) {
        animTimer_ += deltaTime;
        float spinDuration = 7.5f;

        Vector3 rot = boss->GetRotation();
        rot.y += fireSpinSpeed * deltaTime;
        boss->SetRotation(rot);
        boss->GetTransform()->isQuaternionMaster = false;

        // ==========================================
        // ★ レーザーの太さと振動（脈動）の計算
        // ==========================================
        float expandTime = 0.01f;
        float t = std::min(animTimer_ / expandTime, 1.0f);

        // 基本の太さ（0.1から1.0へ一瞬で太くなる）
        float baseThickness = Math::Lerp(0.1f, 1.0f, Easing::OutExpo(t));

        // 【振動】animTimer_ を使って、1.0を中心にして ±15% ほど激しく震わせる！
        // ※ 60.0f を大きくすると震えるスピードが上がり、0.15f を大きくすると震幅（太さの差）が大きくなります。
        float pulse = 1.0f + (std::sin(animTimer_ * 60.0f) * 0.05f);

        // 最終的な太さ
        float currentThickness = baseThickness * pulse;

        // 名簿(children)を探さず、自分専用のリストを使う！
        for (Object3d* beam : activeBeams_) {
            if (beam) {
                // 震える太さを適用
                beam->SetScale({ currentThickness, 80.0f, currentThickness });

                // 発射中は完全に不透明な赤（Emissiveと加算ブレンドで白飛びするほど輝きます！）
                beam->SetColor({ 1.0f, 0.0f, 0.0f, 1.0f });
                beam->SetCollisionAttribute(kEnemyAttack);
                beam->SetCollisionMask(kPlayer);
                // ==========================================
                // ★ エネルギーのUVスクロール
                // ==========================================
                static Math math;
                Vector3 uvScale = { 1.0f, 15.0f, 1.0f }; // 縦に15回リピート
                float scrollSpeed = -30.0f; // -30.0f の猛スピードで奥へかっ飛ばす
                Vector3 uvTranslate = { 0.0f, animTimer_ * scrollSpeed, 0.0f };
                beam->SetUVTransform(math.MakeAffineMatrix(uvScale, { 0.0f, 0.0f, 0.0f }, uvTranslate));
            }
        }

        if (animTimer_ >= spinDuration) {
            animPhase_ = 65;
            animTimer_ = 0.0f;
            animStartRot_ = boss->GetRotation();

            // 撃ち終わったビームを即座に消滅(isDead)させる！
            for (Object3d* beam : activeBeams_) {
                if (beam) {
                    beam->SetScale({ 0.0f, 0.0f, 0.0f });
                    beam->SetCollisionAttribute(0);
                    beam->isDead = true; // エンジンの自動削除に任せる
                }
            }
            activeBeams_.clear(); // リストも空にする

            blockStartPos_.clear();
            blockStartScale_.clear();
            for (size_t i = 0; i < armorBlocks.size(); ++i) {
                blockStartPos_.push_back(armorBlocks[i]->GetTranslate());
                blockStartScale_.push_back(armorBlocks[i]->GetScale());
            }
        }
        }
    // --- Phase 65: 回転を止め、待機状態のバラバラ軌道へ復帰する ---
    else if (animPhase_ == 65) {
        animTimer_ += deltaTime;
        float duration = 2.25f;
        float t = std::min(animTimer_ / duration, 1.0f);
        float easeT = Easing::InOutSine(t);

        Vector3 rot = animStartRot_;
        rot.y = Math::Lerp(animStartRot_.y, 0.0f, easeT);
        boss->SetRotation(rot);
        boss->GetTransform()->isQuaternionMaster = false;

        for (size_t i = 0; i < armorBlocks.size(); ++i) {
            if (i < blockStartPos_.size()) {
                BossCore::OrbitData orbit = boss->GetIdleOrbit(i);
                Vector3 pos = Math::Lerp(blockStartPos_[i], orbit.pos, easeT);
                armorBlocks[i]->SetTranslate(pos);

                Vector3 scale = Math::Lerp(blockStartScale_[i], orbit.scale, easeT);
                armorBlocks[i]->SetScale(scale);

                armorBlocks[i]->SetRotation(orbit.rot);
                armorBlocks[i]->GetTransform()->isQuaternionMaster = false;
            }
        }

        if (t >= 1.0f) {
            isFinished_ = true;
        }
    }
}