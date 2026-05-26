#include "BossAttack9_Spawn.h"
#include "../BossCore.h"
#include "EnemyBomb.h" // 召喚したい雑魚敵のヘッダー
#include "SceneManager.h"
#include "CollisionManager.h"
#include "DebugConsole.h"
#include <cstdlib>

void BossAttack9_Spawn::Initialize(BossCore* boss) {
    BaseBossAttack::Initialize(boss);
    spawnCount_ = 0;
    spawnTimer_ = 0.0f;
    animPhase_ = 0; // 0: 中央への移動, 1: 回転召喚
    startPos_ = boss->GetTranslate();
}

void BossAttack9_Spawn::Update(BossCore* boss, float deltaTime) {
    animTimer_ += deltaTime;

    // 装甲ブロックの軌道更新（常に待機時の挙動を継続）
    auto& armorBlocks = boss->GetArmorBlocks();
    for (size_t i = 0; i < armorBlocks.size(); ++i) {
        if (armorBlocks[i]) {
            BossCore::OrbitData orbit = boss->GetIdleOrbit(i);
            armorBlocks[i]->SetTranslate(orbit.pos);
            armorBlocks[i]->SetRotation(orbit.rot);
            armorBlocks[i]->SetScale(orbit.scale);
        }
    }

    // Phase 0: 中央 Y=7 へのイージング移動（1秒間）
    if (animPhase_ == 0) {
        float t = std::clamp(animTimer_ / 1.0f, 0.0f, 1.0f);
        float easeT = 1.0f - std::pow(1.0f - t, 3.0f); // OutCubic
        Vector3 targetPos = { 0.0f, 7.0f, 0.0f };
        Vector3 currentPos = Math::Lerp(startPos_, targetPos, easeT);
        boss->SetTranslate(currentPos);

        if (animTimer_ >= 1.0f) {
            animPhase_ = 1;
            animTimer_ = 0.0f;
            boss->SetTranslate(targetPos);
        }
    }
    // Phase 1: 儀式的回転召喚（6秒間）
    else if (animPhase_ == 1) {
        // 位置は中央 Y=7 に固定
        boss->SetTranslate({ 0.0f, 7.0f, 0.0f });

        float phaseDuration = 6.0f;
        float nt = std::clamp(animTimer_ / phaseDuration, 0.0f, 1.0f);

        // ハニング窓のようなカーブで非常に滑らかに加減速 (0 -> max -> 0)
        float maxRotationSpeed = 35.0f; 
        float currentSpeed = maxRotationSpeed * 0.5f * (1.0f - std::cos(nt * 2.0f * 3.14159265f));

        Vector3 rot = boss->GetRotation();
        rot.y -= currentSpeed * deltaTime; // 逆回転
        boss->SetRotation(rot);

        // 召喚スケジュール: 1.2秒以降、12方向に順次出す
        if (animTimer_ >= 1.2f && spawnCount_ < 12) {
            // 1体目は即座に、2体目以降は0.35秒おきに出現させる
            if (spawnCount_ == 0 || spawnTimer_ >= 0.35f) {
                SpawnEnemy(boss);
                spawnCount_++;
                spawnTimer_ = 0.0f;
            }
            spawnTimer_ += deltaTime;
        }

        if (nt >= 1.0f) {
            isFinished_ = true;
        }
    }
}

// BossAttack9_Spawn.cpp

void BossAttack9_Spawn::SpawnEnemy(BossCore* boss) {
    BaseScene* currentScene = SceneManager::GetInstance()->GetCurrentScene();
    if (!currentScene) return;

    // 1. インスタンス生成と初期化
    auto bomb = std::make_unique<EnemyBomb>();
    bomb->Initialize(boss->GetCommon(), "sphere");

    // 召喚直後から通常の敵と同じ物理更新に乗せる。
    Object3d::EntityParameter param; // Object3d.h で定義されている構造体
    param.gravity = 50.0f;           // 重力の強さ
    param.maxFallSpeed = 60.0f;      // 落下速度の限界

    // Character::Update 内の param_.has_value() 判定を通すために必須
    bomb->param_ = param;
    // ========================================================

    // 2. ターゲット（プレイヤー）の設定
    bomb->SetTarget(boss->GetTarget());

    // 3. 出現位置の計算と設定（ボスの中心高さ Y=7.0f から外向きに投げ出す）
    Vector3 bossPos = boss->GetTranslate();
    float baseAngle = 0.0f;
    float distToPlayer = 15.0f; // デフォルト想定距離

    if (boss->GetTarget() && boss->GetTarget()->GetTransform()) {
        Vector3 playerPos = boss->GetTarget()->GetTransform()->translate;
        Vector3 toPlayer = { playerPos.x - bossPos.x, 0.0f, playerPos.z - bossPos.z };
        distToPlayer = Math::Length(toPlayer);
        if (distToPlayer > 0.1f) {
            baseAngle = std::atan2(toPlayer.z, toPlayer.x);
        }
    }

    // 1体目はプレイヤー方向へ、残りは角度をずらしてばらつきを加える。
    float randomAngleOffset = 0.0f;
    float speedMultiplier = 1.0f;
    float heightMultiplier = 1.0f;

    if (spawnCount_ > 0) {
        // 1体目（プレイヤー直撃弾）以外に、リアルなばらつきを追加
        randomAngleOffset = ((float)std::rand() / RAND_MAX - 0.5f) * (15.0f * 3.14159265f / 180.0f); // ±15度のぶれ
        speedMultiplier = 0.85f + 0.3f * ((float)std::rand() / RAND_MAX); // 85%〜115% の飛距離のばらつき
        heightMultiplier = 0.9f + 0.2f * ((float)std::rand() / RAND_MAX); // 90%〜110% の高さのばらつき
    }

    float angle = baseAngle + spawnCount_ * (3.14159265f * 2.0f / 12.0f) + randomAngleOffset;
    Vector3 launchDir = { std::cos(angle), 0.0f, std::sin(angle) };

    // ボスのすぐ横から出現
    Vector3 spawnPos = bossPos;
    spawnPos.x += launchDir.x * 1.5f;
    spawnPos.z += launchDir.z * 1.5f;
    spawnPos.y = bossPos.y; // Y=7.0f からスタート
    bomb->SetTranslate(spawnPos);

    // プレイヤーの距離に応じて、ちょうど足元に届く基本の初速にランダム倍率を乗算
    float launchHorizontalSpeed = std::clamp(distToPlayer / 1.8f, 10.0f, 22.0f) * speedMultiplier;
    float launchVerticalSpeed = 15.0f * heightMultiplier; // 上方向の初速（高さのばらつき）

    Vector3 initialVel = {
        launchDir.x * launchHorizontalSpeed,
        launchVerticalSpeed,
        launchDir.z * launchHorizontalSpeed
    };
    bomb->SetVelocity(initialVel);

    // ObjectManagerの保留追加に乗せるため、シーン経由で登録する。
    currentScene->AddObject(std::move(bomb));

    DebugConsole::GetInstance()->AddLog("【召喚】 ボムが現れた！");
}
