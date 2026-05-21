#include "BossAttack8_Final.h"
#include "../BossCore.h"
#include "Object3d.h" 
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
            //block->isDead = true; // エンジンの自動削除に任せる
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
        if (meteor && !meteor->isDead && meteor->GetTranslate().y <= 900.0f) {
            Vector3 pos = meteor->GetTranslate();
            if (pos.y > 0.0f) {
                pos.y -= 100.0f * deltaTime; // 落下速度を少しアップして激しさを出す
                meteor->SetTranslate(pos);
            }
            else {
                // ==========================================
                // 地面に到達したら即座に「消去」して再利用待ちにする
                // ==========================================
                meteor->SetScale({ 0.0f, 0.0f, 0.0f });
                meteor->SetCollisionAttribute(0);
                meteor->UpdateWorldMatrix();
                // 座標を上空に逃がして、再投下命令を待つ
                meteor->SetTranslate({ pos.x, 1000.0f, pos.z });
            }
        }
    }

    // --- Phase 80: ボス上昇 ＆ 必要な【11個】だけを事前生成！ ---
    if (animPhase_ == 80) {
        if (animTimer_ == 0.0f) {
            areaWarnings_.clear();
            // ★ 4つの警告エリア板を生成
            for (int i = 0; i < 4; ++i) {
                auto warn = std::make_unique<Object3d>();
                warn->Initialize(boss->GetCommon());

                // ==================================================
                // ★ 修正：データに基づいたモデル名とブレンドモードを設定
                // ==================================================
                warn->SetModel("yazirusi.gltf");
                warn->SetBlendMode(BlendMode::kNormal);

                warn->SetScale({ 0, 0, 0 });
                warn->SetIsVisible(false);
                warn->SetCollisionAttribute(0);
                warn->SetEmissive(1.0f); // データ上のデフォルト値

                areaWarnings_.push_back(warn.get());
                if (currentScene) currentScene->AddObject(std::move(warn));
            }
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
                meteor->SetAttackDamage(boss->GetAttackParams().damageFinal);

                meteor->UpdateLocalMatrix();
                meteor->UpdateWorldMatrix();

                CollisionManager::GetInstance()->AddObject(meteor.get());
                meteors_.push_back(meteor.get());

                if (currentScene) {
                    currentScene->AddObject(std::move(meteor));
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
    // --- Phase 81: 4つのエリアを順番に予告（2秒間かけて溜める！） ---
    else if (animPhase_ == 81) {
        animTimer_ += deltaTime;
        float interval = 0.5f;

        for (int i = 0; i < 4; ++i) {
            // 0.5秒ごとに1つずつ予測線を出す
            if (animTimer_ >= i * interval && areaWarnings_[i]->GetScale().x == 0.0f) {
                float centerX = (i == 0 || i == 2) ? -37.5f : 37.5f;
                float centerZ = (i == 0 || i == 1) ? 37.5f : -37.5f;

                areaWarnings_[i]->SetIsVisible(true);
                areaWarnings_[i]->SetTranslate({ centerX, 0.1f, centerZ });
                areaWarnings_[i]->SetScale({ 37.5f, 0.1f, 37.5f }); // タイクラーさん指定のサイズ
                areaWarnings_[i]->SetTexture("Resources/sprite/yazirusi1.png");
                areaWarnings_[i]->SetMaterialType(0);
                areaWarnings_[i]->SetEmissive(3.0f);
                areaWarnings_[i]->UpdateWorldMatrix(); // カメラバグ防止
            }

            // 予測線のUVスクロールと点滅
            if (areaWarnings_[i]->GetIsVisible()) {
                static Math math;
                Vector3 uvScale = { 10.0f, 10.0f, 1.0f };
                Vector3 uvTranslate = { 0.0f, animTimer_ * 4.0f, 0.0f };
                areaWarnings_[i]->SetUVTransform(math.MakeAffineMatrix(uvScale, { 0,0,0 }, uvTranslate));

                float flash = (std::sin(animTimer_ * 20.0f) + 1.0f) * 0.5f;
                areaWarnings_[i]->SetColor({ 1.0f, 0.2f, 0.0f, 0.3f + flash * 0.4f });
            }
        }

        // 4つすべて出し切って2秒経ったら、爆撃開始フェーズへ
        if (animTimer_ >= 2.0f) {
            animPhase_ = 82;
            animTimer_ = 0.0f;
            rainCount_ = 0;
            rainTimer_ = 0.0f;
        }
    }
    // --- Phase 82: 1〜4のエリアを順番に爆撃していく（予測線を追う演出） ---
    else if (animPhase_ == 82) {
        animTimer_ += deltaTime;
        rainTimer_ += deltaTime;

        // 1秒ごとに降らせるエリア(0〜3)を切り替える
        int currentTargetArea = std::min(static_cast<int>(animTimer_), 3);

        // ★ 修正：攻撃が終わったエリア（現在のターゲットより前のエリア）の予測線を消す
        for (int i = 0; i < currentTargetArea; ++i) {
            if (areaWarnings_[i] && areaWarnings_[i]->GetScale().x > 0.0f) {
                areaWarnings_[i]->SetScale({ 0.0f, 0.0f, 0.0f });
                areaWarnings_[i]->UpdateWorldMatrix(); // カメラバグ（見えない壁）を確実に防ぐ！
            }
        }

        static int lastArea = -1;
        if (currentTargetArea != lastArea) {
            rainCount_ = 0;
            lastArea = currentTargetArea;
        }

        // 予測線の演出（現在攻撃中のエリアだけを強調）
        for (int i = 0; i < 4; ++i) {
            // Scaleが0より大きい（まだ消えていない）エリアのみ処理
            if (areaWarnings_[i] && areaWarnings_[i]->GetScale().x > 0.0f) {
                float speed = (i == currentTargetArea) ? 30.0f : 20.0f;
                float flash = (std::sin(animTimer_ * speed) + 1.0f) * 0.5f;

                if (i == currentTargetArea) {
                    areaWarnings_[i]->SetColor({ 1.0f, 0.0f, 0.0f, 0.4f + flash * 0.5f }); // 落下中
                }
                else {
                    areaWarnings_[i]->SetColor({ 1.0f, 0.2f, 0.0f, 0.3f + flash * 0.2f }); // 待機中
                }
            }
        }

        // 攻撃ロジック（0.1秒間隔で投下）
        if (rainTimer_ >= 0.05f && rainCount_ < 15) {
            rainTimer_ = 0.0f;
            int meteorIdx = (rainCount_) % 10;

            if (meteors_[meteorIdx]) {
                float cX = (currentTargetArea == 0 || currentTargetArea == 2) ? -37.5f : 37.5f;
                float cZ = (currentTargetArea == 0 || currentTargetArea == 1) ? 37.5f : -37.5f;

                // ==========================================
                // ★ 修正：範囲を 37.5 から 75.0 に拡大！
                // これで Scale 37.5 の見た目（全幅 75）にピッタリ重なります
                // ==========================================
                float spread = 75.0f;
                float rx = ((static_cast<float>(rand()) / RAND_MAX) - 0.5f) * spread;
                float rz = ((static_cast<float>(rand()) / RAND_MAX) - 0.5f) * spread;

                meteors_[meteorIdx]->SetScale({ 3.0f, 3.0f, 3.0f });
                meteors_[meteorIdx]->SetCollisionAttribute(kEnemyAttack);
                meteors_[meteorIdx]->SetAttackDamage(boss->GetAttackParams().damageFinal);
                meteors_[meteorIdx]->SetTranslate({ cX + rx, 50.0f + (rand() % 10), cZ + rz });
                meteors_[meteorIdx]->UpdateWorldMatrix();
                rainCount_++;
            }
        }

        if (animTimer_ >= 4.0f) {
            animPhase_ = 85;
            animTimer_ = 0.0f;
            rainCount_ = 0;
            lastArea = -1;
            // 最後のエリアと、念のため残っている予測線をすべて完全消去
            for (auto* warn : areaWarnings_) {
                if (warn) {
                    warn->SetScale({ 0,0,0 });
                    warn->UpdateWorldMatrix();
                    warn->isDead = true;
                }
            }
        }
        }
    // --- Phase 85: 最後にステージ中央へ「超巨大ブロック」を落とす ---
    else if (animPhase_ == 85) {
        // ==========================================
        // ★ 巨大隕石用に予測線をステージ中央へ移動し、真っ赤にする！
        // ==========================================
        Object3d* warning = boss->GetWarningArea();
        if (warning) {
            warning->SetTranslate({ 0.0f, 0.05f, 0.0f });
            warning->SetScale({ 16.0f, 0.7f, 16.0f }); // 隕石サイズに合わせる
            warning->SetColor({ 1.0f, 0.0f, 0.0f, 0.9f });
            warning->UpdateWorldMatrix();
        }
        if (rainCount_ == 0) {
            rainCount_ = 1;

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
        float duration = 3.0f; // 全体の演出時間は3秒
        float t = std::min(animTimer_ / duration, 1.0f);

        Vector3 pos = boss->GetTranslate();

        // ====================================================
        // ★ カスタム落下＆バウンド計算
        // ====================================================
        float fallDuration = 1.2f;
        float fallT = std::min(animTimer_ / fallDuration, 1.0f);

        float easeXZ = fallT * (2.0f - fallT); // OutQuad
        pos.x = Math::Lerp(animStartPos_.x, 0.0f, easeXZ);
        pos.z = Math::Lerp(animStartPos_.z, 0.0f, easeXZ);

        float groundY = 0.5f;

        if (animTimer_ <= fallDuration) {
            // 【落下中】
            float easeY = fallT * fallT;
            pos.y = Math::Lerp(animStartPos_.y, groundY, easeY);

            // キリモミ回転
            float spinSpeed = 20.0f;
            boss->SetRotation({ animTimer_ * spinSpeed, 0.0f, 0.0f });
        }
        else {
            // 【バウンド中】
            float bounceTime = animTimer_ - fallDuration;

            float bouncePower = 6.0f;
            float bounceDamp = 3.0f;
            float bounceFreq = 15.0f;

            float bounceOffset = std::abs(std::sin(bounceTime * bounceFreq)) * std::exp(-bounceTime * bounceDamp) * bouncePower;
            pos.y = groundY + bounceOffset;

            // 90度倒れる
            float angle90 = std::numbers::pi_v<float> / 2.0f;
            boss->SetRotation({ angle90, 0.0f, 0.0f });
        }

        boss->SetTranslate(pos);
        boss->SetColor({ 1.0f, 0.0f, 1.0f, 1.0f });

        if (t >= 1.0f) {
            // ==========================================
            // ★ カメラめり込み対策：すべてのブロックを完全消去！
            // ==========================================
            // ① 生成した隕石（巨大隕石も含む）をすべてサイズ0にする
            for (Object3d* meteor : meteors_) {
                if (meteor) {
                    meteor->SetScale({ 0.0f, 0.0f, 0.0f });
                    meteor->SetCollisionAttribute(0);
                    meteor->UpdateWorldMatrix();
                    meteor->isDead = true;
                }

            }
            meteors_.clear();

            // ② ボスに付いている装甲ブロックもすべてサイズ0にする
            for (Object3d* block : boss->GetArmorBlocks()) {
                if (block) {
                    block->SetScale({ 0.0f, 0.0f, 0.0f });
                    block->SetCollisionAttribute(0);
                    block->UpdateWorldMatrix(); // カメラの判定更新のために必須
                }
            }

            // ==========================================
            // ★ 予測線を完全に消去
            // ==========================================
            Object3d* warning = boss->GetWarningArea();
            if (warning) {
                warning->SetScale({ 0.0f, 0.0f, 0.0f });
                warning->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
            }

            isFinished_ = true;

            DebugConsole::GetInstance()->AddLog("ボスは完全に沈黙した…！今だ、トドメを刺せ！！");
        }
        }
}