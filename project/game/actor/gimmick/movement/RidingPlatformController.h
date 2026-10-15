#pragma once
#include "CinematicPlayer.h"
#include <string>
#include <vector>

class GimmickMovingFloor;
class BaseScene;
class BaseEnemy;

// 乗車開始と乗員追従だけを担当し、軌道・停車・敵の登場時刻はTimelineへ委譲する。
class RidingPlatformController {
  public:
    // deltaTime は秒。前フレームの接地から乗員を確定し、タイムラインで床と乗員を進めます。
    void Update(GimmickMovingFloor& floor, float deltaTime);

  private:
    // この信号を持つ乗車床だけ、破壊待ちと乗り場への呼び戻しを使う。
    bool ferry_ = false;
    float absentTime_ = 0.0f;
    // 敵の初期座標から床への移乗を補間する一時状態。attribute/mask は解放時の復元用です。
    struct Boarder {
        std::string name;
        engine::Vector3 origin{};
        engine::Vector3 offset{};
        float duration = 1.1f;
        float height = 6.0f;
        float elapsed = 0.0f;
        uint32_t attribute = 0, mask = 0;
        bool active = false, released = false;
    };
    BaseEnemy* FindEnemy(BaseScene& scene, const std::string& name) const;
    void Reset(GimmickMovingFloor& floor, BaseScene& scene);
    CinematicSequence sequence_;
    CinematicPlayer player_;
    std::vector<Boarder> boarders_;
    std::string loadedPath_;
    engine::Vector3 start_{};
    bool ready_ = false, running_ = false, wasPlaying_ = false;
};
