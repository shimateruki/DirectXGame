#pragma once
#include "BaseScene.h"
#include "game/system/scene/SceneGameplayAccess.h"
#include "game/system/scene/ObjectManager.h"
#include "engine/utility/math/Math.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

// --- 前方宣言 ---
namespace engine { class InputManager; }
namespace engine { class Object3DCommon; }
namespace engine { class SpriteCommon; }
namespace engine { class ParticleCommon; }
namespace engine { class ParticleSystem; }
class Player;
class LevelLoader;
class GameRule;
class Skybox;
class SettingsMenuOverlay;

/// <summary>
/// タイトルシーン
/// </summary>
// 関連：GameDataManagerのセーブ枠情報を表示し、選択を確定してSceneManagerへ遷移を要求する。
// 主な入口：Initialize / Update / Draw / Finalize
class TitleScene : public BaseScene, public ISceneGameplayAccess {
  public:
    /// <summary>タイトル画面の初期状態を作る。</summary>
    TitleScene();

    /// <summary>タイトル用の描画資源と画面部品を破棄する。</summary>
    ~TitleScene() override;

    /// <summary>タイトルの3D背景、UI、セーブ選択、入力を初期化する。</summary>
    void Initialize() override;

    /// <summary>配布版は起動時に必要な範囲だけ準備し、Editorでは全材質を使えるようにする。</summary>
    engine::RenderPreparation GetRenderPreparation() const override {
#ifdef USE_IMGUI
        return engine::RenderPreparation::Full;
#else
        return engine::RenderPreparation::Minimal;
#endif
    }

    /// <summary>非同期ロードに必要なタイトル素材を列挙する。</summary>
    SceneLoadManifest BuildAsyncLoadManifest() const override;

    /// <summary>タイトルで使用したシーン資源を解放する。</summary>
    void Finalize() override;

    /// <summary>タイトル演出とメニュー・セーブ選択の入力を更新する。</summary>
    void Update(float deltaTime) override;

    /// <summary>タイトルの3D背景とオブジェクトを描画する。</summary>
    void Draw() override;

    /// <summary>タイトルロゴ、メニュー、セーブ選択のUIを描画する。</summary>
    void DrawUi() override;

    /// <summary>タイトル内オブジェクトのシャドウマップを描画する。</summary>
    void DrawShadow() override;

    // --- BaseScene インターフェース実装 (ObjectManagerへ委譲) ---
    /// <summary>タイトルが所有する3Dオブジェクトの一覧を返す。</summary>
    const std::vector<std::unique_ptr<engine::Object3D>>& GetObjects() const override { return objectManager_->GetObjects(); }

    /// <summary>所有権を受け取って3Dオブジェクトを即時登録する。</summary>
    engine::Object3D* AddObjectImmediate(std::unique_ptr<engine::Object3D> object) override { return objectManager_->AddObjectImmediate(std::move(object)); }

    /// <summary>3Dオブジェクトの所有権をObjectManagerから取り出す。</summary>
    std::unique_ptr<engine::Object3D> TakeObjectImmediate(engine::Object3D* object) override { return objectManager_->TakeObjectImmediate(object); }

    /// <summary>3Dオブジェクトの所有権を受け取り、追加を予約する。</summary>
    void AddObject(std::unique_ptr<engine::Object3D> object) override { objectManager_->AddObject(std::move(object)); }

    /// <summary>指定オブジェクトの削除を予約する。</summary>
    void RequestRemoveObject(engine::Object3D* object) override { objectManager_->RequestRemove(object); }

    /// <summary>タイトルが所有するSpriteの一覧を返す。</summary>
    const std::vector<std::unique_ptr<engine::Sprite>>& GetSprites() const override { return sprites_; }

    /// <summary>タイトルのSkyboxを返す。所有権は移さない。</summary>
    Skybox* GetSkybox() override { return skybox_.get(); }

    /// <summary>タイトルの粒子システムを返す。所有権は移さない。</summary>
    engine::ParticleSystem* GetParticleSystem() override { return particleSystem_.get(); }

    /// <summary>タイトルで参照するプレイヤーを取得・設定する。所有権は移さない。</summary>
    Player* GetPlayer() const override { return player_; }

    void SetPlayer(Player* player) override { player_ = player; }

  private:
    /// <summary>タイトルのSprite所有リストを編集処理へ渡す。</summary>
    std::vector<std::unique_ptr<engine::Sprite>>* GetMutableSprites() override { return &sprites_; }

    enum class TitleMode { MainMenu, SaveSelect };

    enum class SaveSelectMode { Browse, DeleteConfirm, Launching };

    /// <summary>セーブスロットのカードと詳細表示を生成する。</summary>
    void InitializeSaveSlotUi();

    /// <summary>タイトルロゴの各文字Spriteを配置する。</summary>
    void InitializeTitleLogoUi();

    // 起動案内のSpriteを用意し、同じプロセスで完了済みなら非表示にする。
    void InitializeControllerNotice();
    // 秒単位でフェードとスキップ入力を更新し、このフレームのタイトル進行を待つか返す。
    bool UpdateControllerNotice(float deltaTime);

    /// <summary>メインメニューの選択肢とカーソルを配置する。</summary>
    void InitializeMainMenuUi();

    /// <summary>メインメニューの入力と選択演出を更新する。</summary>
    void UpdateMainMenu();

    /// <summary>セーブスロット選択、削除確認、起動待ちを更新する。</summary>
    void UpdateSaveSelect(float deltaTime);

    /// <summary>ロゴの導入アニメーションを進める。</summary>
    void UpdateTitleLogoIntro(float deltaTime);

    /// <summary>背景にいるスライムの演出を進める。</summary>
    void UpdateTitleHeroAnimation(float deltaTime);

    /// <summary>選択中のセーブスロットと詳細表示を更新する。</summary>
    void UpdateSaveSlotUi(float deltaTime);

    /// <summary>セーブスロットのUIを描画する。</summary>
    void DrawSaveSlotUi();

    /// <summary>選択中のセーブデータを開いてゲーム開始へ進む。</summary>
    void StartSelectedSaveSlot();

    /// <summary>選択中のセーブデータを削除する。</summary>
    void DeleteSelectedSaveSlot();

    /// <summary>指定した画像、位置、サイズ、色のUI Spriteを生成・保持する。</summary>
    /// <returns>タイトルが所有するSpriteへの非所有ポインター。</returns>
    engine::Sprite* CreateUiSprite(const std::string& texturePath, const engine::Vector2& position, const engine::Vector2& size,
                           const engine::Vector4& color);

    /// <summary>2桁の数字Spriteへ値、色、表示状態を反映する。</summary>
    void SetNumberSprites(std::array<engine::Sprite*, 2>& digits, int value, const engine::Vector4& color, bool visible);

    // 入力基盤への非所有参照。描画基盤はBaseSceneの共通窓口を利用する。
    engine::InputManager* inputManager_ = nullptr;

    // タイトル上の配置Object、読込処理、ゲームルールをシーンが所有する。
    std::unique_ptr<ObjectManager> objectManager_ = nullptr;
    std::unique_ptr<LevelLoader> levelLoader_ = nullptr;
    std::unique_ptr<GameRule> gameRule_ = nullptr;

    // 配置JSONから生成したSpriteと、タイトル用の粒子・空を所有する。
    std::vector<std::unique_ptr<engine::Sprite>> sprites_;
    std::unique_ptr<engine::ParticleSystem> particleSystem_ = nullptr;
    std::unique_ptr<Skybox> skybox_ = nullptr;

    // タイトル用プレイヤーObjectへの非所有参照。
    Player* player_ = nullptr;

    uint32_t skyboxTextureHandle_ = 0;

    // GPUパーティクルの画像をTextureManagerから引くハンドル。
    uint32_t gpuParticleTexHandle_ = 0;

    // メニューの選択肢
    enum class MenuIndex {
        GameStart,
        Setting,
        Max // 項目数を取るためのダミー
    };

    // メインメニューの選択位置。GameStartを初期項目にする。
    int currentMenuIndex_ = (int)MenuIndex::GameStart;

    // メインメニュー・セーブ選択画面の表示段階。
    TitleMode titleMode_ = TitleMode::MainMenu;
    SaveSelectMode saveSelectMode_ = SaveSelectMode::Browse;

    // 操作対象のスロット、スロット内のフォーカス、削除確認の選択肢。
    int currentSaveSlotIndex_ = 0; // 選択中のセーブ枠。0始まり。
    int saveSelectFocusIndex_ = 0;
    int deleteConfirmIndex_ = 1; // 削除確認の選択番号。初期値1で取消側から始める。

    // タイトル表示、保存画面の開閉、ゲーム開始フェードの経過秒数。
    float titleUiTime_ = 0.0f;
    float saveSelectOpenTime_ = 0.0f;
    float saveLaunchTime_ = 0.0f;
    bool saveLaunchTransitionRequested_ = false; // セーブ枠確定後の遷移要求を一度だけ送るための印。

    // セーブ選択の光の表示位置と、各スロット枠の強調倍率。
    float saveSelectionGlowY_ = 0.0f;
    std::array<float, 3> saveSlotSelectionWeights_{};

    // 以下のSprite*は上の所有配列から取得した非所有参照。
    // メインメニューの文字、枠、カーソル、操作案内を個別に動かすため保持する。
    engine::Sprite* titleTextSprite_ = nullptr;
    engine::Sprite* startTextSprite_ = nullptr;
    engine::Sprite* settingTextSprite_ = nullptr;
    engine::Sprite* mainMenuStartFrameSprite_ = nullptr;
    engine::Sprite* mainMenuSettingFrameSprite_ = nullptr;
    engine::Sprite* mainMenuArrowSprite_ = nullptr;
    engine::Sprite* mainMenuCursorSprite_ = nullptr;
    engine::Sprite* mainMenuPromptSprite_ = nullptr;

    // 演出でサイズ・位置を変えても戻せるように保存した初期配置。
    engine::Vector2 startTextBaseSize_ = engine::Vector2::ZERO;
    engine::Vector2 settingTextBaseSize_ = engine::Vector2::ZERO;
    engine::Vector2 startTextBasePosition_ = engine::Vector2::ZERO;
    engine::Vector2 settingTextBasePosition_ = engine::Vector2::ZERO;
    engine::Vector2 mainMenuArrowBaseSize_ = engine::Vector2::ZERO;
    engine::Vector2 mainMenuCursorBaseSize_ = engine::Vector2::ZERO;
    engine::Vector2 mainMenuPromptBaseSize_ = engine::Vector2::ZERO;

    // メニュー選択に応じて基準配置へ加える矢印・カーソルの差分。
    engine::Vector2 mainMenuArrowOffset_ = engine::Vector2::ZERO;
    engine::Vector2 mainMenuCursorOffset_ = engine::Vector2::ZERO;
    engine::Vector2 mainMenuPromptBasePosition_ = engine::Vector2::ZERO;

    // 配置済み主人公は借用し、カメラ追従用の補助Objectだけ所有する。
    engine::Object3D* titleHeroSlime_ = nullptr;
    std::unique_ptr<engine::Object3D> titleCameraAnchor_ = nullptr;

    // タイトル演出開始前に記録する主人公の基準姿勢。
    engine::Vector3 titleHeroBasePosition_ = engine::Vector3::ZERO;
    engine::Vector3 titleHeroBaseScale_ = {1.0f, 1.0f, 1.0f};
    engine::Vector3 titleHeroBaseRotation_ = engine::Vector3::ZERO;
    bool titleHeroBaseCaptured_ = false; // タイトル演出前の主人公の配置姿勢を退避済みか。

    // 主人公の演出シーケンスの再生時刻。
    float titleHeroSequenceTime_ = 0.0f;

    // 文字 Sprite の借用参照と基準配置。導入アニメーションはこの基準へオフセットを加える。
    struct TitleLogoGlyph {
        engine::Sprite* sprite = nullptr;
        engine::Vector2 basePosition = engine::Vector2::ZERO;
        engine::Vector2 baseSize = engine::Vector2::ZERO;
        float delay = 0.0f;
    };

    // ロゴ文字を1字ずつずらして表示するための基準位置と遅延。
    std::array<TitleLogoGlyph, 8> titleLogoGlyphs_{};

    // 暗幕・アイコン・本文・補足・操作案内。所有権はsprites_にあり、Inspectorで配置できる。
    std::array<engine::Sprite*, 5> controllerNoticeSprites_{};
    float controllerNoticeTime_ = 0.0f; // 起動案内の経過秒数。シーン切替中は進めない。
    bool controllerNoticeActive_ = false;

    // タイトル導入演出が完了するまでの経過秒数。
    float titleIntroTime_ = 0.0f;
    bool titleIntroComplete_ = false; // 導入演出が終わり、通常メニュー操作を受け付けるか。

    // タイトル固有の追加UIを所有する配列。以下のSprite*は配置済みUIか追加UIを借用する。
    std::vector<std::unique_ptr<engine::Sprite>> titleUiSprites_;
    engine::Sprite* saveSelectBackdrop_ = nullptr;
    engine::Sprite* saveDetailPanel_ = nullptr;
    engine::Sprite* saveDetailFileName_ = nullptr;
    engine::Sprite* saveDetailEmptyLabel_ = nullptr;
    engine::Sprite* saveLaunchLabel_ = nullptr;
    engine::Sprite* saveSelectTitle_ = nullptr;
    engine::Sprite* saveSelectionGlow_ = nullptr;
    engine::Sprite* saveHintBackdrop_ = nullptr;
    engine::Sprite* saveKeyboardHint_ = nullptr;
    engine::Sprite* saveGamepadHint_ = nullptr;

    // 3つのセーブ枠に対応するカード画像への非所有参照。
    std::array<engine::Sprite*, 3> saveSlotCards_ = {nullptr, nullptr, nullptr};

    // JSONで調整した枠サイズを保持し、選択時の拡大を毎フレーム累積させない。
    std::array<engine::Vector2, 3> saveSlotCardBaseSizes_{};
    std::array<engine::Vector2, 3> saveSlotCardBasePositions_{};
    std::array<engine::Vector2, 3> saveSlotNumberBasePositions_{};
    std::array<engine::Vector2, 3> saveSlotFileNameBasePositions_{};
    std::array<engine::Vector2, 3> saveSlotStatusBasePositions_{};
    engine::Vector2 saveDetailPanelBasePosition_{};
    engine::Vector2 saveSelectTitleBasePosition_{};

    // 各スロットの枠・番号・状態・残機・王冠・星・プレイ時間の表示部品。
    // 外側添字はセーブ枠、内側添字は桁または枠の部品番号。
    std::array<std::array<engine::Sprite*, 4>, 3> saveSlotFrames_{};
    std::array<engine::Sprite*, 3> saveSlotNumberSprites_ = {nullptr, nullptr, nullptr};
    std::array<engine::Sprite*, 3> saveSlotFileNameTexts_ = {nullptr, nullptr, nullptr};
    std::array<engine::Sprite*, 3> saveSlotStatusTexts_ = {nullptr, nullptr, nullptr};

    std::array<engine::Sprite*, 3> saveSlotLifeIcons_ = {nullptr, nullptr, nullptr};
    std::array<engine::Sprite*, 3> saveSlotLifeXIcons_ = {nullptr, nullptr, nullptr};
    std::array<std::array<engine::Sprite*, 2>, 3> saveSlotLifeDigits_{};

    std::array<engine::Sprite*, 3> saveSlotCrownIcons_ = {nullptr, nullptr, nullptr};
    std::array<engine::Sprite*, 3> saveSlotCrownXIcons_ = {nullptr, nullptr, nullptr};
    std::array<std::array<engine::Sprite*, 2>, 3> saveSlotCrownDigits_{};

    std::array<engine::Sprite*, 3> saveSlotStarIcons_ = {nullptr, nullptr, nullptr};
    std::array<engine::Sprite*, 3> saveSlotStarXIcons_ = {nullptr, nullptr, nullptr};
    std::array<std::array<engine::Sprite*, 2>, 3> saveSlotStarDigits_{};

    std::array<engine::Sprite*, 3> saveSlotPlayTimeLabels_ = {nullptr, nullptr, nullptr};
    std::array<std::array<engine::Sprite*, 5>, 3> saveSlotPlayTimeDigits_{};

    // セーブ画面共通の見出し、削除ボタン、確認ダイアログ用の表示部品。
    engine::Sprite* saveSelectHeader_ = nullptr;
    engine::Sprite* saveDeleteButtonBack_ = nullptr;
    engine::Sprite* saveDeleteButtonText_ = nullptr;
    engine::Sprite* savePromptBubble_ = nullptr;
    engine::Sprite* saveDeleteQuestionText_ = nullptr;
    engine::Sprite* saveConfirmYesText_ = nullptr;
    engine::Sprite* saveConfirmBackText_ = nullptr;

    // タイトルから開く設定画面の状態とSpriteを独立して所有する。
    std::unique_ptr<SettingsMenuOverlay> settingsOverlay_ = nullptr;
};
