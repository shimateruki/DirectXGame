#pragma once

#include <memory>
#include <string>

#include "engine/utility/math/Math.h"
#include "game/actor/combat/PlayerAttackMovement.h"

class Player;
class Camera;
class EnemyAttackProfile;
struct ID3D12Resource;

// 1フレーム分のコピー能力入力です。
// 入力デバイスの詳細を能力実装へ持ち込まず、再生・AI操作にも流用できる形にします。
struct PlayerCopyAbilityInput {
    bool specialTriggered = false;
    bool primaryTriggered = false;
    bool primaryHeld = false;
    bool secondaryTriggered = false;
    bool secondaryHeld = false;
};

// 吸収後のコピー能力をプレイヤーの寿命で管理します。
// 元になった敵は設定値の提供だけを担当し、能力中の状態や更新処理は保持しません。
class PlayerCopyAbilityController {
  public:
    PlayerCopyAbilityController();

    ~PlayerCopyAbilityController();

    PlayerCopyAbilityController(const PlayerCopyAbilityController&) = delete;

    PlayerCopyAbilityController& operator=(const PlayerCopyAbilityController&) = delete;

    // morphType に対応する能力セッションを生成する。未対応ならセッションは空になる。
    void Activate(int morphType, const EnemyAttackProfile& attackProfile);

    // 敵種別のJSONを読み込みます。ファイルがない場合だけ既定値を使います。
    void ActivateDefault(int morphType);

    // 1 フレーム分の入力を現在の能力セッションへ渡す。能力がなければ何もしない。
    void ProcessInput(Player& player, const PlayerCopyAbilityInput& input);

    // 経過秒数が正の場合だけ能力を進める。Play 停止時の 0 秒更新では進まない。
    void Update(Player& player, float deltaTime);

    // 能力固有の3D表示を、現在のセッションがある時だけ描く。
    void Draw(Player& player, ID3D12Resource* pointLight, ID3D12Resource* spotLight, Camera* camera = nullptr,
              int previewBufferIndex = 0);

    bool HasOwnedVisuals() const;

    // 深度・画面取得テクスチャを使う能力の見た目を専用パスへ転送する。
    void DrawOwnedVisuals(Player& player, uint32_t depthSrvHandle, uint32_t grabSrvHandle, Camera* camera = nullptr,
                          int previewBufferIndex = 0);

    // 能力側の終了処理を呼んでからセッションを破棄する。
    void Cancel(Player& player);

    // ガード中に攻撃を受け止めたことを能力へ通知します。
    // ダメージ判定とコピー固有の反応を分離し、敵クラスへ能力処理を戻しません。
    bool NotifyGuardedHit(Player& player, const engine::Vector3& sourcePosition);

    // セッションの実在と変身種別の一致をともに確認する。
    bool HandlesMorphType(int morphType) const;

    bool IsActive() const;

    // セッションの現在の制限を値で取得します。終了・解除後は無制限です。
    PlayerAttackMovement GetAttackMovement() const;

    const char* GetStateLabel() const;

    // エディターが選択したデータIDだけを渡します。能力の実行経路は本編と共通です。
    void SetPreviewAttackId(const std::string& id);

    // ゲート破壊の可否は現在の能力動作に応じて変わる。
    bool CanBreakImpactGate() const;

    bool IsPinkBounceSlamImpactActive() const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
