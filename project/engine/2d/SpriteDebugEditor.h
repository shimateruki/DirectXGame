#pragma once
#include <vector>
#include <memory> // unique_ptr
#include <string>
#include "InputManager.h"


// 前方宣言
class Sprite;
class SceneManager;
class  SpriteCommon;
class BaseScene;
class SpriteDebugEditor {
public:
    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize(SceneManager* sceneManager, InputManager* inputManager);

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize();

    /// <summary>
    /// ImGui を使った毎フレーム更新
    /// </summary>
    void Update();

    /// <summary>
    /// (任意) デバッグ描画 (選択中のスプライト枠など)
    /// </summary>
    void Draw(); 

    void DrawImGui();

    bool IsMouseOver(Sprite* sprite) const;
    /// <summary>
    /// スプライトエディタがマウスを（ギズモ操作で）使用中か
    /// </summary>
    bool IsMouseBusy() const;
private:
       // プライトレイアウト保存用
       void SaveSpriteLayout(const std::string& filename);
       SceneManager* sceneManager_ = nullptr;
       /// <summary>
    /// 最後に Update を実行したシーン
    /// </summary>
       BaseScene* lastUpdatedScene_ = nullptr;
    Sprite* selectedSprite_ = nullptr; // 現在選択中のスプライトへのポインタ
    InputManager* inputManager_ = nullptr; // マウス座標とクリック用
    /// <summary>
    /// ギズモの初期化に使った SpriteCommon のポインタ
    /// </summary>
    SpriteCommon* initializedSpriteCommon_ = nullptr;


    std::unique_ptr<Sprite> gizmoArrowX_; // X軸（赤）
    std::unique_ptr<Sprite> gizmoArrowY_; // Y軸（緑）
    uint32_t gizmoTextureHandle_ = 0;     // ギズモ用テクスチャハンドル

    bool isMovingX_ = false; // X軸をドラッグ中か
    bool isMovingY_ = false; // Y軸をドラッグ中か
    Vector2 dragStartMousePos_; // ドラッグ開始時のマウス座標
    Vector2 dragStartSpritePos_; // ドラッグ開始時のスプライト座標
};