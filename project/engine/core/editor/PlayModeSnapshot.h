#pragma once

#ifdef USE_IMGUI

#include "Object3d.h"
#include "Sprite.h"

#include <cstdint>
#include <vector>

class BaseScene;
class SceneManager;

// Play開始時の編集状態を一度だけ保存し、同じSceneでのStop時に戻す。
class PlayModeSnapshot {
public:
    void Initialize(SceneManager* sceneManager) { sceneManager_ = sceneManager; }
    // 編集時のObject・Sprite・シーン・視点をメモリへ控える。Scene JSONへは保存しない。
    bool Capture();
    // 同じシーンと世代の場合だけ復元する。Play中の追加物を撤去し、復元後は記録を消費する。
    bool Restore();
    // 保持指定を解除して記録を破棄する。Play中の状態を編集時へ戻す操作はRestoreで行う。
    void Reset();

private:
    struct ObjectState {
        uint64_t id = 0;
        engine::Object3d::ReplayState state;
    };
    struct SpriteState {
        uint64_t id = 0;
        engine::Sprite::ReplayState state;
    };
    struct CameraState {
        bool valid = false;
        engine::Vector3 eye{};
        engine::Vector3 target{};
        engine::Vector3 rotation{};
        float fovY = 0.45f;
        float nearClip = 0.1f;
        float farClip = 1000.0f;
    };

    BaseScene* CurrentScene() const;

    SceneManager* sceneManager_ = nullptr;
    BaseScene* scene_ = nullptr;
    // シーンのアドレスが再利用されても、別世代のObjectへ古い記録を適用しないために保持する。
    uint64_t sceneGeneration_ = 0;
    std::vector<ObjectState> objects_;
    std::vector<SpriteState> sprites_;
    json sceneState_ = json::object();
    CameraState camera_;
};

#endif
