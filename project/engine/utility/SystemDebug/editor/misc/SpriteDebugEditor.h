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

// SpriteDebugEditorは、2D Spriteの配置、選択、保存、Inspector編集を行うための専用エディタです。
class SpriteDebugEditor : public IEditable {
public:
    // 依存マネージャーの注入と初期化
    void Initialize(SceneManager* sceneManager, InputManager* inputManager);
    void Finalize();

    // 毎フレームのロジック更新（選択判定・ギズモ操作）
        // マウス入力からSpriteの選択やドラッグ操作を更新します。
void Update(const Vector2& localMousePos, bool isHovered);

    // Scene遷移前に、破棄予定のSpriteと描画基盤への参照を解放します。
    void ClearSceneSelection();

    // GameView内でのデバッグ描画（選択枠やギズモ）
    void Draw();

    // Inspectorに表示するUI描画処理
        // Sprite Hierarchy、Inspector、Projectパネルを描画します。
void DrawImGui() override;

    // Inspector上部のタイトルバーに表示される名前
    std::string GetName() override { return "Sprite Editor"; }

    // マウスがスプライト上にあるか判定
    bool IsMouseOver(Sprite* sprite, const Vector2& localMousePos) const;

    // ギズモ操作中など、エディタがマウス入力を占有しているか
    bool IsMouseBusy() const;
    void DrawHierarchyWindow();
    void DrawSpriteNode(Sprite* sprite);
    void DrawInspectorWindow();
    void DrawProjectWindow();
    void SetSelectedSprite(Sprite* sprite) { selectedSprite_ = sprite; }
    void SetSpriteFilename(const std::string& filepath) {
        std::string name = filepath;
        // パスからファイル名だけを抜き出す
        size_t pos = name.find_last_of("/\\");
        if (pos != std::string::npos) name = name.substr(pos + 1);
        strcpy_s(currentSpriteFilename_, sizeof(currentSpriteFilename_), name.c_str());
    }
private:
    // レイアウト保存
    void SaveSpriteLayout(const std::string& filename);
    bool TryGetTexturePixelSize(Sprite* sprite, Vector2& outSize) const;
    bool IsGeneratedTextSprite(Sprite* sprite) const;
    float CalcAspectError(Sprite* sprite, const Vector2& textureSize) const;
    Vector2 MakeAspectFixedSize(Sprite* sprite, const Vector2& textureSize, bool keepWidth) const;
    Vector2 MakeNearestIntegerScaleSize(Sprite* sprite, const Vector2& textureSize) const;
    int FixSpriteAspectInCurrentScene(bool textOnly, bool keepWidth);

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
    std::string currentSpriteDirectory_ = "Resources/sprite/";
    std::string spriteQualityStatus_;
};
