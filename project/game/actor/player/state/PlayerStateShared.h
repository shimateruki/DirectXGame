#pragma once
#include "PlayerState.h"
#include "Camera.h"
#include "CameraManager.h"
#include "DebugConsole.h"
#include "InputManager.h"
#include "Player.h"
#include "SceneManager.h"
#include "engine/utility/math/Math.h"
#include <MeshEffectManager.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
// ========================================================
// ヘルパ: 小文字化
// ========================================================
inline std::string ToLower(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (unsigned char c : s)
    out.push_back(static_cast<char>(std::tolower(c)));
  return out;
}

// ========================================================
// 再帰検索: 指定ルート配下から左右の足パーツを探す
// ========================================================
inline void FindFeetRecursive(Object3d *node, Object3d *&leftOut,
                              Object3d *&rightOut) {
  if (!node)
    return;
  const auto &children = node->GetChildren();
  for (Object3d *child : children) {
    if (!child)
      continue;
    std::string name = ToLower(child->GetName());

    bool hasFoot = (name.find("foot") != std::string::npos);
    bool hasRight = (name.find("right") != std::string::npos) ||
                    (name.find("_r") != std::string::npos);
    bool hasLeft = (name.find("left") != std::string::npos) ||
                   (name.find("_l") != std::string::npos);

    if (hasFoot) {
      if (hasRight && !rightOut)
        rightOut = child;
      else if (hasLeft && !leftOut)
        leftOut = child;
      else {
        if (!leftOut)
          leftOut = child;
        else if (!rightOut)
          rightOut = child;
      }
    }
    if (leftOut && rightOut)
      return;
    FindFeetRecursive(child, leftOut, rightOut);
    if (leftOut && rightOut)
      return;
  }
}

// ========================================================
// シーン検索（名前ベース）
// ========================================================
inline void FindFeetInSceneByName(Player *player, Object3d *&leftOut,
                                  Object3d *&rightOut) {
  if (!SceneManager::GetInstance())
    return;
  auto scene = SceneManager::GetInstance()->GetCurrentScene();
  if (!scene)
    return;

  for (auto &obj : scene->GetObjects()) {
    if (!obj)
      continue;
    std::string n = ToLower(obj->GetName());
    if (!leftOut) {
      if (n.find("player_leftfoot") != std::string::npos ||
          n.find("leftfoot") != std::string::npos ||
          (n.find("left") != std::string::npos &&
           n.find("foot") != std::string::npos)) {
        leftOut = obj.get();
      }
    }
    if (!rightOut) {
      if (n.find("player_rightfoot") != std::string::npos ||
          n.find("rightfoot") != std::string::npos ||
          (n.find("right") != std::string::npos &&
           n.find("foot") != std::string::npos)) {
        if (obj.get() != leftOut)
          rightOut = obj.get();
      }
    }
    if (leftOut && rightOut)
      return;
  }

  // プレイヤー名プレフィックスを使って探す補助
  if (!player)
    return;
  std::string playerName = ToLower(player->GetName());
  if (!playerName.empty()) {
    for (auto &obj : scene->GetObjects()) {
      if (!obj)
        continue;
      std::string n = ToLower(obj->GetName());
      if (!leftOut && n.find(playerName) != std::string::npos &&
          n.find("left") != std::string::npos)
        leftOut = obj.get();
      if (!rightOut && n.find(playerName) != std::string::npos &&
          n.find("right") != std::string::npos)
        rightOut = obj.get();
      if (leftOut && rightOut)
        return;
    }
  }
}

inline void TryFindFeet(Player *player, Object3d *&leftOut,
                        Object3d *&rightOut) {
  if (!player)
    return;
  FindFeetRecursive(player, leftOut, rightOut);
  if (leftOut && rightOut)
    return;
  FindFeetInSceneByName(player, leftOut, rightOut);
}

// ========================================================
// 腕探索ヘルパー
// ========================================================
inline void FindArmsRecursive(Object3d *node, Object3d *&leftArmOut,
                              Object3d *&rightArmOut) {
  if (!node)
    return;
  const auto &children = node->GetChildren();
  for (Object3d *child : children) {
    if (!child)
      continue;
    std::string name = ToLower(child->GetName());
    bool hasArm = (name.find("arm") != std::string::npos) ||
                  (name.find("upperarm") != std::string::npos) ||
                  (name.find("shoulder") != std::string::npos);
    bool isRight = (name.find("right") != std::string::npos) ||
                   (name.find("_r") != std::string::npos);
    bool isLeft = (name.find("left") != std::string::npos) ||
                  (name.find("_l") != std::string::npos);

    if (hasArm) {
      if (isRight && !rightArmOut)
        rightArmOut = child;
      else if (isLeft && !leftArmOut)
        leftArmOut = child;
      else {
        if (!leftArmOut)
          leftArmOut = child;
        else if (!rightArmOut)
          rightArmOut = child;
      }
    }
    if (leftArmOut && rightArmOut)
      return;
    FindArmsRecursive(child, leftArmOut, rightArmOut);
    if (leftArmOut && rightArmOut)
      return;
  }
}

inline void FindArmsInSceneByName(Player *player, Object3d *&leftArmOut,
                                  Object3d *&rightArmOut) {
  if (!SceneManager::GetInstance())
    return;
  auto scene = SceneManager::GetInstance()->GetCurrentScene();
  if (!scene)
    return;
  for (auto &obj : scene->GetObjects()) {
    if (!obj)
      continue;
    std::string n = ToLower(obj->GetName());
    if (!leftArmOut) {
      if (n.find("player_leftarm") != std::string::npos ||
          (n.find("left") != std::string::npos &&
           n.find("arm") != std::string::npos))
        leftArmOut = obj.get();
    }
    if (!rightArmOut) {
      if (n.find("player_rightarm") != std::string::npos ||
          (n.find("right") != std::string::npos &&
           n.find("arm") != std::string::npos)) {
        if (obj.get() != leftArmOut)
          rightArmOut = obj.get();
      }
    }
    if (leftArmOut && rightArmOut)
      return;
  }

  if (!player)
    return;
  std::string playerName = ToLower(player->GetName());
  if (!playerName.empty()) {
    for (auto &obj : scene->GetObjects()) {
      if (!obj)
        continue;
      std::string n = ToLower(obj->GetName());
      if (!leftArmOut && n.find(playerName) != std::string::npos &&
          n.find("left") != std::string::npos &&
          n.find("arm") != std::string::npos)
        leftArmOut = obj.get();
      if (!rightArmOut && n.find(playerName) != std::string::npos &&
          n.find("right") != std::string::npos &&
          n.find("arm") != std::string::npos)
        rightArmOut = obj.get();
      if (leftArmOut && rightArmOut)
        return;
    }
  }
}

inline void TryFindArms(Player *player, Object3d *&leftArmOut,
                        Object3d *&rightArmOut) {
  if (!player)
    return;
  FindArmsRecursive(player, leftArmOut, rightArmOut);
  if (leftArmOut && rightArmOut)
    return;
  FindArmsInSceneByName(player, leftArmOut, rightArmOut);
}

// ========================================================
// 剣・頭探索ヘルパー
// ========================================================
inline void FindSwordRecursive(Object3d *node, Object3d *&swordOut) {
  if (!node)
    return;
  const auto &children = node->GetChildren();
  for (Object3d *child : children) {
    if (!child)
      continue;
    std::string name = ToLower(child->GetName());
    if (name.find("sword") != std::string::npos ||
        name.find("katana") != std::string::npos ||
        name.find("blade") != std::string::npos) {
      swordOut = child;
      return;
    }
    FindSwordRecursive(child, swordOut);
    if (swordOut)
      return;
  }
}

inline void FindSwordInSceneByName(Player *player, Object3d *&swordOut) {
  if (!SceneManager::GetInstance())
    return;
  auto scene = SceneManager::GetInstance()->GetCurrentScene();
  if (!scene)
    return;
  for (auto &obj : scene->GetObjects()) {
    if (!obj)
      continue;
    std::string n = ToLower(obj->GetName());
    if (n.find("sword") != std::string::npos ||
        n.find("katana") != std::string::npos ||
        n.find("blade") != std::string::npos) {
      swordOut = obj.get();
      return;
    }
  }
}

inline void TryFindSword(Player *player, Object3d *&swordOut) {
  if (!player)
    return;
  FindSwordRecursive(player, swordOut);
  if (swordOut)
    return;
  FindSwordInSceneByName(player, swordOut);
}

inline void SetSwordActive(Player *player, bool isActive, float damage = 10.0f) {
  Object3d *swordObj = nullptr;
  // ★最強の探索関数を使って、確実に剣を見つけ出す！
  TryFindSword(player, swordObj);

  if (swordObj) {
    // 見つけたら、ONなら「kPlayerAttack」、OFFなら「0 (無害)」にする
    swordObj->SetCollisionAttribute(isActive ? kPlayerAttack : 0);
    if (isActive) {
      swordObj->SetAttackDamage(damage);
    }
  }
}

inline void FindHeadRecursive(Object3d *node, Object3d *&headOut) {
  if (!node)
    return;
  const auto &children = node->GetChildren();
  for (Object3d *child : children) {
    if (!child)
      continue;
    std::string name = ToLower(child->GetName());
    if (name.find("head") != std::string::npos ||
        name.find("neck") != std::string::npos) {
      headOut = child;
      return;
    }
    FindHeadRecursive(child, headOut);
    if (headOut)
      return;
  }
}

inline void FindHeadInSceneByName(Player *player, Object3d *&headOut) {
  if (!SceneManager::GetInstance())
    return;
  auto scene = SceneManager::GetInstance()->GetCurrentScene();
  if (!scene)
    return;
  for (auto &obj : scene->GetObjects()) {
    if (!obj)
      continue;
    std::string n = ToLower(obj->GetName());
    if (n.find("head") != std::string::npos ||
        n.find("neck") != std::string::npos) {
      headOut = obj.get();
      return;
    }
  }
}

inline void TryFindHead(Player *player, Object3d *&headOut) {
  if (!player)
    return;
  FindHeadRecursive(player, headOut);
  if (headOut)
    return;
  FindHeadInSceneByName(player, headOut);
}

inline void SetSwordCollisionActive(Player *player, bool isActive) {
  Object3d *swordObj = nullptr;
  TryFindSword(player, swordObj);
  if (swordObj) {
    // trueなら敵(kEnemy)と当たる、falseなら誰とも当たらない(0)
    swordObj->SetCollisionMask(isActive ? kEnemy : 0);
  }
}

// ========================================================
// ヘルパ: Lerp / Easing / Angle
// ========================================================
inline Vector3 LerpVec(const Vector3 &a, const Vector3 &b, float t) {
  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

inline float EaseInOutSine(float t) {
  const float pi = 3.14159265358979323846f;
  return 0.5f * (1.0f - std::cos(pi * t));
}
// map [-1..1] -> eased [-1..1]
inline float EaseSinToSmooth(float s) {
  float u = (s + 1.0f) * 0.5f;
  float e = EaseInOutSine(u);
  return e * 2.0f - 1.0f;
}

inline float NormalizeAngle(float a) {
  const float PI = 3.14159265358979323846f;
  while (a > PI)
    a -= 2.0f * PI;
  while (a < -PI)
    a += 2.0f * PI;
  return a;
}

inline float LerpAngle(float a, float b, float t) {
  float diff = NormalizeAngle(b - a);
  return a + diff * t;
}

inline Vector3 LerpEuler(const Vector3 &a, const Vector3 &b, float t) {
  // X/Z は線形、Y は角度補間（最短回転）を使う
  return Vector3{a.x + (b.x - a.x) * t, LerpAngle(a.y, b.y, t),
                 a.z + (b.z - a.z) * t};
}

// ========================================================
// グローバル: 待機状態での体の向きブレンド管理
// ========================================================
inline bool s_bodyBlendActive = false;
inline float s_bodyStartY = 0.0f;
inline float s_bodyTargetY = 0.0f;
inline Vector3 s_bodyStartRotVec = {0, 0, 0};
inline Vector3 s_bodyTargetRotVec = {0, 0, 0};

// ========================================================
// グローバル: 待機状態での足・腕・剣・頭のブレンド管理
// ========================================================
struct PendingIdleBlend {
  bool active = false;
  float blendDuration = 0.35f;
  bool leftFoot = false, rightFoot = false, leftArm = false, rightArm = false,
       head = false, body = false;
  Vector3 leftFootStart{}, leftFootTarget{};
  Vector3 rightFootStart{}, rightFootTarget{};
  Vector3 leftArmStart{}, leftArmTarget{};
  Vector3 rightArmStart{}, rightArmTarget{};
  Vector3 headStart{}, headTarget{};
  // 変更点: body は Y のみではなくフル回転を保持
  Vector3 bodyStart{}, bodyTarget{};
};
inline PendingIdleBlend s_pendingIdleBlend;
