#pragma once
// 分割実装専用の定数・補助関数。外部からは公開ヘッダーを使用します。
#include "game/actor/enemy/boss/core/BossCore.h"
#include "SceneManager.h"
#include "InputManager.h"
#include "imgui.h"
#include "easing.h"
#include "DebugConsole.h"
#include <cmath>
#include <numbers>
#include <ctime>
#include <cstdlib>

// 待機アニメーション用のタイマーと軌道計算関数
// =================================================================
namespace BossCoreImplementation {
    inline float s_globalIdleTimer = 0.0f; // 待機アニメーション用のタイマー

    struct OrbitData {
        Vector3 pos;
        Vector3 rot;
        Vector3 scale;
    };

    // ブロックの待機軌道（現在の理想の位置・回転・スケール）を計算する便利関数！
    inline OrbitData GetIdleOrbit(size_t index) {
        OrbitData data;

        // スケールをランダム生成（初回のみ計算して記憶）
        // static を付けることで、インスタンス間で共有される固定値として保持される
        static std::vector<Vector3> randomScales;
        if (randomScales.empty()) {
            for (int i = 0; i < 6; ++i) {
                // 0.0 ～ 1.0 の乱数を生成し、0.5 ～ 1.0 の範囲に調整する
                float randomVal = 0.5f + (static_cast<float>(rand()) / RAND_MAX) * 0.5f;
                randomScales.push_back({ randomVal, randomVal, randomVal });
            }
        }

        // 1. 各ブロックの「まばらな初期配置」を設定！
        Vector3 basePos;
        switch (index % 6) {
        case 0: basePos = { 2.0f,  2.0f,  0.5f }; break; // 高め・右
        case 1: basePos = { -1.5f, -1.5f,  1.8f }; break; // 低め・左手前
        case 2: basePos = { 0.6f,  2.5f, -1.8f }; break; // 一番高い・奥
        case 3: basePos = { -2.0f,  0.5f, -1.2f }; break; // 中段・左奥
        case 4: basePos = { 1.5f, -2.0f,  1.2f }; break; // 一番低い・右手前
        case 5: basePos = { -0.8f, -0.8f, -2.0f }; break; // やや低め・奥
        }

        // 2. 全ブロックを「同じスピード」「同じ方向」に回転させる！
        // 速度は0.8fでゆっくり回します
        float angle = s_globalIdleTimer * 0.8f;
        float cosY = std::cos(angle);
        float sinY = std::sin(angle);

        // Y軸を中心に、陣形を崩さずに全体を回す
        Vector3 rotatedPos = {
            basePos.x * cosY - basePos.z * sinY,
            basePos.y, // 高さはそれぞれの初期配置を維持！
            basePos.x * sinY + basePos.z * cosY
        };

        // 全体が呼吸するように、少しだけゆっくりフワフワ上下させる
        float hover = std::sin(s_globalIdleTimer * 1.5f) * 0.3f;
        rotatedPos.y += hover;

        data.pos = rotatedPos;

        // 3. 常にコア(中心)を向くように角度を計算
        float rotY = std::atan2(-data.pos.x, -data.pos.z);
        float xzLen = std::sqrt(data.pos.x * data.pos.x + data.pos.z * data.pos.z);
        float rotX = std::atan2(data.pos.y, xzLen);

        data.rot = { rotX, rotY, 0.0f }; // コアを睨みつける！

        // 記憶したランダムな大きさを適用
        data.scale = randomScales[index % 6];

        return data;
    }
    inline Object3d* FindWeaponRecursive(Object3d* node) {
        if (!node) return nullptr;
        // 自分が kPlayerAttack(凶器) なら見つけた！
        if (node->GetCollisionAttribute() & kPlayerAttack) {
            return node;
        }
        // 見つからなければ子パーツの中を再帰的に探す
        for (Object3d* child : node->GetChildren()) {
            Object3d* result = FindWeaponRecursive(child);
            if (result) return result;
        }
        return nullptr;
    }
    inline float EaseOutElasticMario(float t) {
        if (t == 0.0f) return 0.0f;
        if (t == 1.0f) return 1.0f;

        // バウンドの周期と強度
        float c4 = (2.0f * std::numbers::pi_v<float>) / 3.0f;

        // 減衰するサイン波： std::pow(2.0f, -10.0f * t) で徐々に振動を小さくする
        return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
    }
}

// =================================================================
// 初期化・更新
// =================================================================

