#pragma once

#include "IAnimationState.h"
#include <memory>
#include <utility>
class PlayerWhipVisual;
#include "engine/utility/math/Math.h"

namespace engine { class Object3D; }

/// <summary>フックで対象物を引き寄せる状態とロープ表示を管理します。</summary>
// 関連：フック対象への接続を維持し、Playerと対象の位置関係を更新する。
// 主な入口：Enter / Update / Exit
class PlayerStatePullObject : public IAnimationState {
  public:
    PlayerStatePullObject(engine::Object3D* targetObject, const engine::Vector3& targetPos, bool alreadyHit = false,
                         std::shared_ptr<PlayerWhipVisual> whipVisual = {})
        : targetObject_(targetObject), targetPos_(targetPos), alreadyHit_(alreadyHit), whipVisual_(std::move(whipVisual)) {}

    // フックの発射中は自身の移動を止め、マーカーをプレイヤー位置から伸ばす。
    void Enter(Player* player) override;

    void Update(Player* player, float deltaTime) override;

    // 成否にかかわらず、入力とフックマーカーの可視・回転モードを戻す。
    void Exit(Player* player) override;

  private:
    bool alreadyHit_ = false;
    std::shared_ptr<PlayerWhipVisual> whipVisual_;
    // 両端の中点・方向・距離から細長い表示モデルの Transform を作る。
    void UpdateRopeMarker(Player* player, const engine::Vector3& endPos, float thickness);

    // 対象の生存期間は呼び出し元が管理します。
    engine::Object3D* targetObject_ = nullptr;
    engine::Vector3 targetPos_;
    engine::Vector3 hookTipPos_;
    float timer_ = 0.0f;
    bool pullStarted_ = false;
};
