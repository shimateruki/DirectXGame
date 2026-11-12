#pragma once

#ifdef USE_IMGUI

#include "Object3D.h"
#include "Sprite.h"

#include <cstdint>
#include <vector>

class BaseScene;
class SceneManager;

// 担当：Play開始前の編集状態を控え、同じシーンの停止時に復元する。
// 関連：SceneManager（現シーン）、Object3D / Sprite / Camera（復元対象）
// 主な入口：Initialize / Capture / Restore / Reset
class PlayModeSnapshot {
public:
    // 現在シーンの取得元を借用する。記録の取得はCaptureで行う。
    void Initialize(SceneManager* sceneManager) { sceneManager_ = sceneManager; }
    // 編集時のObject・Sprite・シーン・視点をメモリへ控える。Scene JSONへは保存しない。
    bool Capture();
    // 同じシーンと世代の場合だけ復元する。Play中の追加物を撤去し、復元後は記録を消費する。
    bool Restore();
    // 保持指定を解除して記録を破棄する。Play中の状態を編集時へ戻す操作はRestoreで行う。
    void Reset();

private:
    // ObjectのリプレイIDと、Play開始前の復元用状態。
    // 関連：PlayModeSnapshot（この型を使う機能の本体）
    struct ObjectState {
        uint64_t id = 0;
        engine::Object3D::ReplayState state;
    };
    // SpriteのリプレイIDと、Play開始前の復元用状態。
    // 関連：PlayModeSnapshot（この型を使う機能の本体）
    struct SpriteState {
        uint64_t id = 0;
        engine::Sprite::ReplayState state;
    };
    // Play開始前の視点・投影設定。validがfalseなら未記録。
    // 関連：PlayModeSnapshot（この型を使う機能の本体）
    struct CameraState {
        bool valid = false;
        engine::Vector3 eye{};
        engine::Vector3 target{};
        engine::Vector3 rotation{};
        float fovY = 0.45f; // 縦画角（ラジアン）。
        float nearClip = 0.1f; // 近クリップ面までの距離。
        float farClip = 1000.0f; // 遠クリップ面までの距離。
    };

    // SceneManagerから現在のシーンを借用参照で返す。
    BaseScene* CurrentScene() const;

    SceneManager* sceneManager_ = nullptr; // シーンの取得元。借用参照。
    BaseScene* scene_ = nullptr; // 記録したシーン。世代確認後に参照する。
    // シーンのアドレスが再利用されても、別世代のObjectへ古い記録を適用しないために保持する。
    uint64_t sceneGeneration_ = 0;
    std::vector<ObjectState> objects_; // Play開始前のObject状態。
    std::vector<SpriteState> sprites_; // Play開始前のSprite状態。
    json sceneState_ = json::object();
    CameraState camera_; // Play開始前の視点・投影設定。
};

#endif
