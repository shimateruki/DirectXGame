#pragma once
#include <vector>
#include <memory>
#include <string>
#include "InputManager.h"
#include "IEditable.h"

// 前方宣言
namespace engine { class Sprite; }
class SceneManager;
namespace engine { class SpriteCommon; }
class BaseScene;

// 担当：2D Spriteの選択・配置・親子関係・保存と詳細編集を管理する。
// 関連：Sprite（対象）、SceneManager（シーン）、DebugEditor（接続）
// 主な入口：Initialize / Update / DrawImGui / SaveSpriteLayout / Finalize
class SpriteDebugEditor : public IEditable {
public:
    // 依存マネージャーの注入と初期化
    void Initialize(SceneManager* sceneManager, engine::InputManager* inputManager);

    // シーン選択とEditorが保持する状態を終了させる。
    void Finalize();

    // 毎フレームのロジック更新（選択判定・ギズモ操作）
        // マウス入力からSpriteの選択やドラッグ操作を更新します。
void Update(const engine::Vector2& localMousePos, bool isHovered);

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
    bool IsMouseOver(engine::Sprite* sprite, const engine::Vector2& localMousePos) const;

    // ギズモ操作中など、エディタがマウス入力を占有しているか
    bool IsMouseBusy() const;

    // Spriteの親子階層と選択UIを描画する。
    void DrawHierarchyWindow();

    // 一つのSpriteと子階層を再帰表示する。
    void DrawSpriteNode(engine::Sprite* sprite);

    // 選択Spriteの設定項目を描画する。
    void DrawInspectorWindow();

    // Sprite配置に使う画像一覧を描画する。
    void DrawProjectWindow();

    void SetSelectedSprite(engine::Sprite* sprite) { selectedSprite_ = sprite; }

    void SetSpriteFilename(const std::string& filepath) {
        std::string name = filepath;

        // パスからファイル名だけを抜き出す
        size_t pos = name.find_last_of("/\\");
        if (pos != std::string::npos)
            name = name.substr(pos + 1);
        strcpy_s(currentSpriteFilename_, sizeof(currentSpriteFilename_), name.c_str());
    }

private:
    // レイアウト保存
    void SaveSpriteLayout(const std::string& filename);

    // 画像の実ピクセル寸法を取得できないSpriteは縦横比調整から除きます。
    bool TryGetTexturePixelSize(engine::Sprite* sprite, engine::Vector2& outSize) const;

    bool IsGeneratedTextSprite(engine::Sprite* sprite) const;

    // 画像とSpriteの比率差を測り、補正対象を判定します。
    float CalcAspectError(engine::Sprite* sprite, const engine::Vector2& textureSize) const;

    // 幅か高さの選択軸を維持して、もう一方だけ画像比率へ合わせます。
    engine::Vector2 MakeAspectFixedSize(engine::Sprite* sprite, const engine::Vector2& textureSize, bool keepWidth) const;

    // 元画像との倍率を四分の一刻みへ丸め、細かすぎる縮小を避けます。
    engine::Vector2 MakeNearestIntegerScaleSize(engine::Sprite* sprite, const engine::Vector2& textureSize) const;

    // 現行SceneのSpriteだけを走査し、文字PNG限定にも切り替えられるようにします。
    int FixSpriteAspectInCurrentScene(bool textOnly, bool keepWidth);

    SceneManager* sceneManager_ = nullptr; // 現在シーンの取得元。借用参照。
    engine::InputManager* inputManager_ = nullptr;
    BaseScene* lastUpdatedScene_ = nullptr;
    engine::SpriteCommon* initializedSpriteCommon_ = nullptr;

    // 現在選択中のスプライト
    engine::Sprite* selectedSprite_ = nullptr;

    // ギズモ（移動用矢印）関連
    std::unique_ptr<engine::Sprite> gizmoArrowX_; // X軸（赤）
    std::unique_ptr<engine::Sprite> gizmoArrowY_; // Y軸（緑）
    uint32_t gizmoTextureHandle_ = 0;

    // ドラッグ操作の状態管理
    bool isMovingX_ = false;
    bool isMovingY_ = false;
    engine::Vector2 dragStartMousePos_;
    engine::Vector2 dragStartSpritePos_;

    // 保存用ファイル名
    char currentSpriteFilename_[128] = "sprite_layout.json";
    std::string currentSpriteDirectory_ = "Resources/sprite/";
    std::string spriteQualityStatus_;
};
