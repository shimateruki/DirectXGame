#pragma once
#include <vector>
#include <memory>

// 前方宣言
namespace engine { class Object3d; }
class Camera;
class Player;
class InputManager;

/// <summary>視界内の敵を選び、カメラ追従とプレイヤーの照準点を同期する。</summary>
class LockOnSystem {
  public:
    LockOnSystem();
    ~LockOnSystem();

    /// <summary>
    /// 入力元への参照を登録する。所有権は受け取らない。
    /// </summary>
    void Initialize(InputManager* inputManager);

    /// <summary>
    /// 敵の生存・遮蔽・距離を検査し、ロック状態を更新する。
    /// </summary>
    /// <param name="objects">シーン内の全オブジェクトリスト</param>
    /// <param name="camera">メインカメラ</param>
    /// <param name="player">操作中のプレイヤー</param>
    /// <param name="deltaTime">前フレームからの経過秒数</param>
    void Update(const std::vector<std::unique_ptr<engine::Object3d>>& objects, Camera* camera, Player* player, float deltaTime);

    /// <summary>
    /// 現在ロックオン中かどうか
    /// </summary>
    bool IsLockingOn() const { return isLockingOn_; }

    /// <summary>
    /// 現在の対象を返す。対象はシーン所有で、ロック解除後は保持しない。
    /// </summary>
    engine::Object3d* GetTarget() const { return lockOnTarget_; }

    /// <summary>
    /// 保持中の対象を外し、ロック用カメラを通常の照準モードへ戻す。
    /// </summary>
    void ResetLockOn(Camera* camera);

  private:
    /// <summary>
    /// 最も適したターゲットを検索する内部関数
    /// </summary>
    engine::Object3d* FindBestTarget(const std::vector<std::unique_ptr<engine::Object3d>>& objects, Camera* camera, Player* player,
                             bool next = false);

  private:
    InputManager* inputManager_ = nullptr;
    engine::Object3d* lockOnTarget_ = nullptr; // ロックオン対象
    bool isLockingOn_ = false;         // ロックオン中フラグ

    // 定数パラメータ
    const float kMaxLockOnDistance_ = 45.0f; // 取得可能距離。攻撃の射程は延長しません。
    const float kMinLockOnDot_ = 0.0f;
    float lostSightTimer_ = 0.0f;
};
