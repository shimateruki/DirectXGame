#pragma once
#include <vector>
#include <memory>
#include <string>
#include "InputManager.h"
#include "IEditable.h"

// 前方宣言
class Sprite;
class SceneManager;
class SpriteCommon;
class BaseScene;

class SpriteDebugEditor : public IEditable {
public:
    // 依存マネージャーの注入と初期化
    void Initialize(SceneManager* sceneManager, InputManager* inputManager);
    void Finalize();

    // 毎フレームのロジック更新（選択判定・ギズモ操作）
    void Update(const Vector2& localMousePos, bool isHovered);

    // GameView内でのデバッグ描画（選択枠やギズモ）
    void Draw();

    // Inspectorに表示するUI描画処理
    void DrawImGui() override;

    // Inspector上部のタイトルバーに表示される名前
    std::string GetName() override { return "Sprite Editor"; }

    // マウスがスプライト上にあるか判定
    bool IsMouseOver(Sprite* sprite, const Vector2& localMousePos) const;

    // ギズモ操作中など、エディタがマウス入力を占有しているか
    bool IsMouseBusy() const;
    void DrawHierarchyWindow();
    void DrawInspectorWindow();
    void DrawProjectWindow();
private:
    // レイアウト保存
    void SaveSpriteLayout(const std::string& filename);

    SceneManager* sceneManager_ = nullptr;
    InputManager* inputManager_ = nullptr;
    BaseScene* lastUpdatedScene_ = nullptr;
    SpriteCommon* initializedSpriteCommon_ = nullptr;

    // 現在選択中のスプライト
    Sprite* selectedSprite_ = nullptr;

    // ギズモ（移動用矢印）関連
    std::unique_ptr<Sprite> gizmoArrowX_; // X軸（赤）
    std::unique_ptr<Sprite> gizmoArrowY_; // Y軸（緑）
    uint32_t gizmoTextureHandle_ = 0;

    // ドラッグ操作の状態管理
    bool isMovingX_ = false;
    bool isMovingY_ = false;
    Vector2 dragStartMousePos_;
    Vector2 dragStartSpritePos_;

    // 保存用ファイル名
    char currentSpriteFilename_[128] = "sprite_layout.json";
};