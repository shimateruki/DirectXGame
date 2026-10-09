#include "game/actor/enemy/boss/core/BossArmorFlightController.h"
#include "Object3d.h"
#include <algorithm>
#include <cmath>
#include <numbers>

void BossArmorFlightController::Queue(Object3d* block, const Vector3& rotation, int originalIndex) {
    flyingBlocks_.push_back({block, {0.0f, 0.0f, 0.0f}, rotation, 4, originalIndex});
}

bool BossArmorFlightController::ContainsIndex(std::size_t index) const {
    return std::any_of(flyingBlocks_.begin(), flyingBlocks_.end(),
                       [index](const FlyingBlock& block) { return block.originalIndex == index; });
}

void BossArmorFlightController::Update(Object3d& owner, Object3d* target, std::size_t armorCount, float deltaTime,
                                       RestoreOrbit restoreOrbit) {
    int landedCount = 0; // 地面に刺さっているブロックの数
    static Math math;

    // ==========================================
    // 1. 各ブロックの移動・回転・状態更新
    // ==========================================
    for (auto& fb : flyingBlocks_) {
        if (!fb.block)
            continue;

        // ==========================================
        // モード4（頭上へ装填中）
        // ==========================================
        if (fb.mode == 4) {
            Vector3 bossPos = owner.GetTranslate();
            // コアの頭上（Y + 4.0f 付近）を目標地点にする
            Vector3 headPos = {bossPos.x, bossPos.y + 4.0f, bossPos.z};
            Vector3 currentPos = fb.block->GetTranslate();

            Vector3 dir = {headPos.x - currentPos.x, headPos.y - currentPos.y, headPos.z - currentPos.z};
            float distance = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);

            if (distance < 0.5f) {
                // --- 頭上に到着！装填完了！ ---
                fb.block->SetTranslate(headPos);

                // ここで初めてプレイヤーへの方向を計算して「ドカン！」と撃ち出す！
                if (target) {
                    Vector3 targetPos = target->GetWorldPosition();

                    // プレイヤーの足元（地面）を狙う
                    targetPos.y = 0.0f;

                    Vector3 toPlayer = math.Normalize(targetPos - headPos);

                    // 初速を少し速め(60.0f)にして鋭く飛ばす！
                    float bulletSpeed = 60.0f;
                    fb.velocity = {toPlayer.x * bulletSpeed, toPlayer.y * bulletSpeed, toPlayer.z * bulletSpeed};

                    float angleY = std::atan2(toPlayer.x, toPlayer.z) + (std::numbers::pi_v<float> / 2.0f);
                    fb.currentRot = {0.0f, angleY, 0.0f};
                }
                fb.mode = 0; // 「飛翔モード」へ移行！
            } else {
                // --- 頭上に向かって移動中（シュッ！） ---
                dir.x /= distance;
                dir.y /= distance;
                dir.z /= distance;
                float gatherSpeed = 30.0f; // 頭上に移動するスピード
                currentPos.x += dir.x * gatherSpeed * deltaTime;
                currentPos.y += dir.y * gatherSpeed * deltaTime;
                currentPos.z += dir.z * gatherSpeed * deltaTime;
                fb.block->SetTranslate(currentPos);

                // 移動中も少し回転させておく
                fb.currentRot.x += 15.0f * deltaTime;
                fb.currentRot.y += 30.0f * deltaTime;
                fb.block->SetRotation(fb.currentRot);
            }
            fb.block->GetTransform()->isQuaternionMaster = false;
        }

        // ==========================================
        // モード0（飛翔中・攻撃）
        // ==========================================
        else if (fb.mode == 0) {
            // --- 攻撃中（直線的に足元へ突撃！） ---

            // 直線的な軌道で移動（重力計算なし）

            Vector3 pos = fb.block->GetTranslate();
            pos.x += fb.velocity.x * deltaTime;
            pos.y += fb.velocity.y * deltaTime;
            pos.z += fb.velocity.z * deltaTime;

            // 地面（Y=0.0f）にぶつかったら刺さって止まる
            if (pos.y <= 0.0f) {
                pos.y = 0.0f;
                fb.velocity = {0.0f, 0.0f, 0.0f}; // 速度リセット
                fb.mode = 1;                      // 地面待機モードへ！
            }
            fb.block->SetTranslate(pos);

            // 乱回転
            Vector3 spinSpeed = {30.0f, 45.0f, 60.0f};
            fb.currentRot.x += spinSpeed.x * deltaTime;
            fb.currentRot.y += spinSpeed.y * deltaTime;
            fb.currentRot.z += spinSpeed.z * deltaTime;
            fb.block->SetRotation(fb.currentRot);
            fb.block->GetTransform()->isQuaternionMaster = false;

        } else if (fb.mode == 1) {
            // --- 地面待機中 ---
            landedCount++; // 地面にある数をカウントする

        } else if (fb.mode == 2) {
            // --- ボスへ帰還中 ---
            Vector3 bossPos = owner.GetTranslate();
            Vector3 blockPos = fb.block->GetTranslate();

            // ボスとの距離と方向を計算
            Vector3 dir = {bossPos.x - blockPos.x, bossPos.y - blockPos.y, bossPos.z - blockPos.z};
            float distance = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);

            if (distance < 2.0f) {
                fb.mode = 3; // ボスに十分近づいたら回収完了！
            } else {
                // 正規化してボスの方向へ進む
                dir.x /= distance;
                dir.y /= distance;
                dir.z /= distance;
                float returnSpeed = 60.0f; // 帰りは超高速で引き戻す！
                blockPos.x += dir.x * returnSpeed * deltaTime;
                blockPos.y += dir.y * returnSpeed * deltaTime;
                blockPos.z += dir.z * returnSpeed * deltaTime;
                fb.block->SetTranslate(blockPos);

                // 帰りも回転させる
                Vector3 spinSpeed = {60.0f, 60.0f, 60.0f};
                fb.currentRot.x += spinSpeed.x * deltaTime;
                fb.currentRot.y += spinSpeed.y * deltaTime;
                fb.currentRot.z += spinSpeed.z * deltaTime;
                fb.block->SetRotation(fb.currentRot);
                fb.block->GetTransform()->isQuaternionMaster = false;
            }
        }
    }

    // ==========================================
    // 2. 「すべての弾が地面に落ちた」＆「全部撃ち終わった」なら3秒待って一斉帰還！
    // ==========================================
    if (!flyingBlocks_.empty() && landedCount == flyingBlocks_.size() && flyingBlocks_.size() == armorCount) {
        returnDelayTimer_ += deltaTime;
        if (returnDelayTimer_ >= 5.0f) {
            for (auto& fb : flyingBlocks_) {
                fb.mode = 2; // 全員一斉に帰還モードへ
            }
            returnDelayTimer_ = 0.0f; // 次の攻撃のためにタイマーをリセットしておく
        }
    } else {
        // まだ条件を満たしていない時（攻撃中など）は、タイマーを確実に0にしておく
        returnDelayTimer_ = 0.0f;
    }

    // ==========================================
    // 3. 回収完了
    // ==========================================
    for (auto it = flyingBlocks_.begin(); it != flyingBlocks_.end();) {
        if (it->mode == 3) {
            it->block->SetParent(&owner);

            int idx = it->originalIndex;

            // 帰還したブロックを待機軌道に復帰
            // ==========================================
            restoreOrbit(it->block, idx);

            it = flyingBlocks_.erase(it);
        } else {
            ++it;
        }
    }
}
