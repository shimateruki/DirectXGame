#pragma once
#include <memory>

class Player;
namespace engine { class Object3d; }

// ゲートへの移動・子モデルを含むディゾルブ・カメラを、各シーンで同じ手順で再生します。
// 参照先は所有しません。シーンのオブジェクトを解放する前に Reset を呼びます。
class GateEntryPresentation {
public:
    GateEntryPresentation();
    ~GateEntryPresentation();
    GateEntryPresentation(const GateEntryPresentation&) = delete;
    GateEntryPresentation& operator=(const GateEntryPresentation&) = delete;

    // 既に再生中・対象不足・死亡中などでは false。manageCinemaBars が false なら帯を外部管理へ任せる。
    bool Start(Player* player, engine::Object3d* gate, bool manageCinemaBars = true);
    // deltaTime は秒。ゲートへの移動・ディゾルブ・視点を同じ進行率で更新する。
    void Update(float deltaTime);
    // 借用対象が生存している間に、姿勢・材質・カメラを開始前へ戻して演出状態を破棄する。
    void Reset();
    bool IsActive() const;
    bool IsFinished() const;
    float GetProgress() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};
