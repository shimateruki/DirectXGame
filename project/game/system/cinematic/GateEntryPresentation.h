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

    bool Start(Player* player, engine::Object3d* gate, bool manageCinemaBars = true);
    void Update(float deltaTime);
    void Reset();
    bool IsActive() const;
    bool IsFinished() const;
    float GetProgress() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};
