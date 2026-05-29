#include "BossCoreShared.h"
#include "AudioPlayer.h"

namespace {
constexpr Vector4 kBossBreakCoreColor = { 0.0f, 0.59459448f, 1.0f, 1.0f };

void ApplyBossBreakCoreVisual(Object3d* object, float emissive = 2.0f) {
    if (!object) {
        return;
    }

    object->SetColor(kBossBreakCoreColor);
    object->SetMaterialType(2);
    object->SetSelectedLighting(2);
    object->SetEmissive(emissive);
    object->SetMetallic(0.0f);
    object->SetRoughness(0.5f);
    object->SetEnableNormalMap(false);
    object->SetEnableEnvMap(false);
    object->SetEnvIntensity(0.0f);
}
}

void BossCore::StartDeathSequence() {
    if (deathPhase_ != 0) return; // 既に死亡処理中なら何もしない

    isWaitingForFinisher_ = false; // トドメ待ちモーションを解除し、座標の固定を防ぐ

    deathPhase_ = 1;         // フェーズ1（無音で静止）
    sequenceTimer_ = 1.0f;   // 1秒間待機

    // 演出中はBGMを一度止める
    AudioPlayer::GetInstance()->StopBGM();

    // ボス撃破SE再生
    AudioPlayer::GetInstance()->PlaySE(seBossDieHandle_, false, 1.0f);
    ApplyBossBreakCoreVisual(this);
    defaultColor_ = kBossBreakCoreColor;

    DebugConsole::GetInstance()->AddLog("[撃破] ボス沈黙…！！");

    if (target_) {
        if (auto player = dynamic_cast<Player*>(target_)) {
            player->SetControlLock(true);
            player->SetIsControlActive(false);
            player->SetIsPhysicsActive(false);
            player->SetVelocity({ 0.0f, 0.0f, 0.0f });
            player->SetTranslate({ 0.0f, 2.0f, -68.0f });
            player->UpdateWorldMatrix();
        }
    }

    // ====================================================
    // ボスに付いているすべてのパーティクルを止める
    // これにより、新しいパーティクルが発生しなくなります。
    // ====================================================
    for (auto& emitter : particleEmitters_) {
        if (emitter) {
            emitter->Stop();
        }
    }

    if (currentAttack_) {
        currentAttack_->Finalize();
        currentAttack_.reset();
    }
    HideWarningArea();
    isWaitingForDeath_ = true;
    ChangeState(State::Idle);

    // 周りのブロックを消す
    for (Object3d* block : armorBlocks_) {
        if (block) {
            block->SetScale({ 0.0f, 0.0f, 0.0f });
            block->SetCollisionAttribute(0);
        }
    }

    for (auto& emitter : particleEmitters_) {
        if (emitter) {
            emitter->Stop();
        }
    }

    // ボス登場演出の際のコアの高さ（13.16f）に配置
    float targetY = 13.160861015319824f;
    this->SetRotation({ 0.0f, 0.0f, 0.0f });
    this->SetTranslate({ 0.07232095301151276f, targetY, -2.0776538848876953f });

    // カメラをパッと切り替え（0秒） - カメラ「a」のアングルと距離（2.5倍）を維持
    if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
        Camera::CameraOverrideParams params;
        params.duration = 0.0f;
        params.trackEyeX = false; params.trackEyeY = false; params.trackEyeZ = false;
        params.fixedEyePos = { 0.4466233355404443f, 14.659861015319824f, -27.02978838708496f };
        params.trackTargetX = false; params.trackTargetY = false; params.trackTargetZ = false;
        params.fixedTargetPos = { 0.07232095301151276f, 13.160861015319824f, -2.0776538848876953f };
        camera->StartOverride(params);
    }
}

// ==========================================
// 段階2：亀裂状態（少し隙間をあけた破片）を出現させる
// ==========================================
void BossCore::ShowCrackedCore() {
    DebugConsole::GetInstance()->AddLog("[撃破] コアに亀裂が…！生成予約");
    AudioPlayer::GetInstance()->PlaySE(seBossGlassBreaks1Handle_, false, 1.0f);

    // 生成はここではやらず、フラグだけ立てる
    isShardSpawnRequested_ = true;
}

void BossCore::ActuallySpawnShards() {
    if (!isShardSpawnRequested_) return; // 予約がなければ何もしない

    this->SetScale({ 0.0f, 0.0f, 0.0f });
    this->SetCollisionAttribute(0);

    Vector3 corePos = this->GetTranslate();
    BaseScene* currentScene = SceneManager::GetInstance()->GetCurrentScene();

    if (currentScene) {
        for (int i = 0; i < 18; ++i) {
            auto pieceObj = std::make_unique<Object3d>();
            pieceObj->Initialize(common_);
            pieceObj->SetStatic(true);
            pieceObj->SetModel("enemy_core_shards/enemy_core" + std::to_string(i + 1));
            ApplyBossBreakCoreVisual(pieceObj.get());
            pieceObj->SetScale({ 1.0f, 1.0f, 1.0f });
            pieceObj->SetCollisionAttribute(0);

            float rx = ((static_cast<float>(rand()) / RAND_MAX) - 0.5f) * 2.0f;
            float ry = ((static_cast<float>(rand()) / RAND_MAX) - 0.5f) * 2.0f;
            float rz = ((static_cast<float>(rand()) / RAND_MAX) - 0.5f) * 2.0f;
            Vector3 crackOffset = { rx * 0.2f, ry * 0.2f, rz * 0.2f };
            pieceObj->SetTranslate({ corePos.x + crackOffset.x, corePos.y + crackOffset.y, corePos.z + crackOffset.z });

            CorePiece piece;
            piece.obj = pieceObj.get();
            piece.velocity = { 0.0f, 0.0f, 0.0f };
            piece.rotSpeed = { 0.0f, 0.0f, 0.0f };
            corePieces_.push_back(piece);

            currentScene->AddObject(std::move(pieceObj));
        }
    }
    isShardSpawnRequested_ = false; // 生成完了
}

// ==========================================
// 段階3：一気に吹き飛ばす（爆散）
// ==========================================
void BossCore::BreakCore() {
    isCoreBroken_ = true; // UpdateCorePieces のスローモーションを起動
    deathTimer_ = 0.0f;

    AudioPlayer::GetInstance()->PlaySE(seBossGlassBreaks2Handle_, false, 1.0f);
    DebugConsole::GetInstance()->AddLog("[撃破] コア完全粉砕！！！");

    for (auto& piece : corePieces_) {
        if (piece.obj) {
            float rx = ((static_cast<float>(rand()) / RAND_MAX) - 0.5f) * 2.0f;
            float ry = ((static_cast<float>(rand()) / RAND_MAX) * 1.0f) + 0.5f;
            float rz = ((static_cast<float>(rand()) / RAND_MAX) - 0.5f) * 2.0f;

            float speed = 30.0f + (rand() % 30);
            piece.velocity = { rx * speed, ry * speed, rz * speed };
            piece.rotSpeed = {
                (rand() % 60) - 30.0f, (rand() % 60) - 30.0f, (rand() % 60) - 30.0f
            };

            // 発光を元に戻す
            ApplyBossBreakCoreVisual(piece.obj, 1.0f);
        }
    }
}

void BossCore::UpdateCorePieces(float deltaTime) {
    if (!isCoreBroken_) return;

    deathTimer_ += deltaTime;

    if (deathTimer_ > 8.0f) { // 5.0s -> 8.0sへ延長
        for (auto& piece : corePieces_) {
            if (piece.obj) {
                piece.obj->isDead = true;
                piece.obj = nullptr; // ダングリングポインタ防止
            }
        }
        corePieces_.clear(); // リストもクリア
        isDead = true;
        isCompletelyDead_ = true;
        // ==========================================
        // 変更：ボスが完全に消滅したら、カメラを元のプレイヤー視点に戻す
        // 一瞬で戻すなら 0.0f に変更します。
        // ==========================================
        if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
            camera->EndOverride(0.0f); // 0秒で一瞬で戻す
        }
        return;
    }

    // --- スローモーション計算 ---
    float timeScale = 1.0f;
    if (deathTimer_ < 0.2f) { // ヒットストップをわずかに延長
        timeScale = 0.01f;
    }
    else if (deathTimer_ < 2.5f) { // スロー時間を2.5sへ延長
        timeScale = 0.05f;  // 0.2 -> 0.05へ（より深いスロー）
    }
    float slowDeltaTime = deltaTime * timeScale;

    // --- 物理計算 ---
    for (auto& piece : corePieces_) {
        if (piece.obj) {
            Vector3 pos = piece.obj->GetTranslate();

            piece.velocity.y -= 98.0f * slowDeltaTime;

            pos.x += piece.velocity.x * slowDeltaTime;
            pos.y += piece.velocity.y * slowDeltaTime;
            pos.z += piece.velocity.z * slowDeltaTime;

            if (pos.y <= 0.0f) {
                pos.y = 0.0f;
                piece.velocity.y *= -0.5f;
                piece.velocity.x *= 0.8f;
                piece.velocity.z *= 0.8f;
                piece.rotSpeed.x *= 0.8f;
                piece.rotSpeed.y *= 0.8f;
                piece.rotSpeed.z *= 0.8f;
            }

            piece.obj->SetTranslate(pos);

            Vector3 rot = piece.obj->GetRotation();
            rot.x += piece.rotSpeed.x * slowDeltaTime;
            rot.y += piece.rotSpeed.y * slowDeltaTime;
            rot.z += piece.rotSpeed.z * slowDeltaTime;
            piece.obj->SetRotation(rot);

            piece.obj->UpdateWorldMatrix();
        }
    }
}

