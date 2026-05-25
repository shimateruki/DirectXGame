#pragma once
#include "BossCore.h"
#include "InputManager.h"
#include "imgui.h"
#include "easing.h"
#include "DebugConsole.h"
#include <cmath>
#include <fstream>
#include <filesystem>
#include <numbers>
#include <ctime>
#include <cstdlib>
#include "GPUParticleManager.h"
#include <algorithm>
#undef max

#include "CameraManager.h"
#include "CameraEditor.h"
#include "PostEffect.h"
#include "GhostRecorder.h"

// ==========================================
// 攻撃クラスを読み込む
// ==========================================
#include "BossAttack/BossAttack1_Rush.h"
#include "BossAttack/BossAttack2_Shoot.h"
#include "BossAttack/BossAttack3_Hammer.h"
#include "BossAttack/BossAttack4_Wall.h"
#include "BossAttack/BossAttack5_Humanoid.h"
#include "BossAttack/BossAttack6_Laser.h"
#include "BossAttack/BossAttack7_Absorb.h"
#include "BossAttack/BossAttack8_Final.h"
#include "BossAttack/BossAttack9_Funnels.h"
#include "BossAttack/BossAttack9_Spawn.h"
#include "MeshEffectManager.h"
#include "game/system/BulletManager.h"
#include "Player.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "MapBlock.h"
#include "CollisionConfig.h"
#include "CollisionManager.h"


// =================================================================
// 待機アニメーション用のタイマーと軌道計算関数
// =================================================================
inline float s_globalIdleTimer = 0.0f;
inline int s_debugForceAttack = 0;

inline Object3d* FindWeaponRecursive(Object3d* node) {
    if (!node) return nullptr;
    if (node->GetCollisionAttribute() & kPlayerAttack) {
        return node;
    }
    for (Object3d* child : node->GetChildren()) {
        Object3d* result = FindWeaponRecursive(child);
        if (result) return result;
    }
    return nullptr;
}

inline Object3d* FindObjectByNameRecursive(Object3d* node, const std::string& name) {
    if (!node) return nullptr;
    if (node->GetName() == name) return node;
    for (Object3d* child : node->GetChildren()) {
        if (Object3d* found = FindObjectByNameRecursive(child, name)) return found;
    }
    return nullptr;
}
