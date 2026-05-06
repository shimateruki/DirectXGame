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
    animPhase_ = 0; // 0: 溜め演出, 1: 召喚中
}

void BossAttack9_Spawn::Update(BossCore* boss, float deltaTime) {
    animTimer_ += deltaTime;

    // Phase 0: 召喚前の予兆（ボスが少し浮き上がるなど）
    if (animPhase_ == 0) {
        Vector3 pos = boss->GetTranslate();
        pos.y += 2.0f * deltaTime; // 少し浮く
        boss->SetTranslate(pos);

        if (animTimer_ >= 1.0f) {
            animPhase_ = 1;
            animTimer_ = 0.0f;
        }
    }
    // Phase 1: 実際に敵を出す
    else if (animPhase_ == 1) {
        spawnTimer_ += deltaTime;

        // 0.5秒ごとに1体、合計4体出す例
        if (spawnTimer_ >= 0.5f && spawnCount_ < 4) {
            SpawnEnemy(boss);
            spawnCount_++;
            spawnTimer_ = 0.0f;
        }

        if (spawnCount_ >= 4 && animTimer_ >= 3.0f) {
            isFinished_ = true; // 攻撃終了
        }
    }
}

// BossAttack9_Spawn.cpp

void BossAttack9_Spawn::SpawnEnemy(BossCore* boss) {
    BaseScene* currentScene = SceneManager::GetInstance()->GetCurrentScene();
    if (!currentScene) return;

    // 1. インスタンス生成と初期化
    auto slime = std::make_unique<EnemySlime>();
    slime->Initialize(boss->GetCommon(), "block");

    // ========================================================
    // ★ ここにステータス設定を実装します！
    // ========================================================
    Object3d::EntityParameter param; // Object3d.h で定義されている構造体
    param.hp = 10.0f;                // 雑魚敵のHP[cite: 16]
    param.gravity = 50.0f;           // 重力の強さ[cite: 16]
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
    spawnPos.y = 1.0f;
    slime->SetTranslate(spawnPos);

    // 4. マネージャーとシーンへの登録
    // ⭕ 正解: BaseScene の AddObject を使う
    // これにより内部で ObjectManager::AddObject が呼ばれ、pendingObjects_ に入り、次のフレームの冒頭で安全に追加されます
    currentScene->AddObject(std::move(slime));

    DebugConsole::GetInstance()->AddLog("【召喚】 スライムが現れた！");
}