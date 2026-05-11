#include "BossAttack9_Spawn.h"
#include "../BossCore.h"
#include "../EnemySlime.h" // 召喚したい雑魚敵のヘッダー
#include "SceneManager.h"
#include "CollisionManager.h"
#include "DebugConsole.h"

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

        // 召喚スケジュール: 2秒以降、4体出すまで
        if (animTimer_ >= 2.0f && spawnCount_ < 4) {
            // 1体目は即座に、2体目以降は0.5秒おきに出現させる
            if (spawnCount_ == 0 || spawnTimer_ >= 0.5f) {
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
    auto slime = std::make_unique<EnemySlime>();
    slime->Initialize(boss->GetCommon(), "bunny");

    // ========================================================
    // ★ ここにステータス設定を実装します！
    // ========================================================
    Object3d::EntityParameter param; // Object3d.h で定義されている構造体
    param.gravity = 50.0f;           // 重力の強さ
    param.maxFallSpeed = 60.0f;      // 落下速度の限界[cite: 16]

    // Character::Update 内の param_.has_value() 判定を通すために必須
    slime->param_ = param;
    // ========================================================

    // 2. ターゲット（プレイヤー）の設定[cite: 11]
    slime->SetTarget(boss->GetTarget());

    // 3. 出現位置の計算と設定
    float angle = spawnCount_ * (3.1415f * 2.0f / 4.0f);
    Vector3 spawnPos = boss->GetTranslate();
    spawnPos.x += std::cos(angle) * 5.0f;
    spawnPos.z += std::sin(angle) * 5.0f;
    spawnPos.y = 5.0f;
    slime->SetTranslate(spawnPos);

    // 4. マネージャーとシーンへの登録
    // ⭕ 正解: BaseScene の AddObject を使う
    // これにより内部で ObjectManager::AddObject が呼ばれ、pendingObjects_ に入り、次のフレームの冒頭で安全に追加されます
    currentScene->AddObject(std::move(slime));

    DebugConsole::GetInstance()->AddLog("【召喚】 スライムが現れた！");
}