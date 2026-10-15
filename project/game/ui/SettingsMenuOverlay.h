#pragma once
#include "json.hpp"
#include "WinApp.h"
#include "engine/utility/math/Math.h"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class InputManager;
namespace engine { class Sprite; }
namespace engine { class SpriteCommon; }

// ゲーム中・タイトル共通の設定画面。編集中の値と確定済みの値を分離します。
class SettingsMenuOverlay {
  public:
    enum class Result { None, Closed };
    /// 設定メニューの表示状態を初期化する。
    SettingsMenuOverlay();
    ~SettingsMenuOverlay();
    /// JSON 配置を読み込み、設定項目の Sprite を準備する。
    void Initialize(engine::SpriteCommon* common, const std::string& layoutPath = "Resources/json/ui/settingsMenu.json");
    /// 保持する表示資源と入力状態を片付ける。
    void Finalize();
    /// メニューの開閉と編集開始値を切り替える。
    void SetActive(bool active);
    bool IsActive() const { return active_; }
    /// 入力と画面遷移を進め、閉じたフレームに Closed を返す。
    Result Update(float deltaTime);
    /// 背景、設定行、カーソルと確認ダイアログを描画する。
    void Draw();
    /// Replay へ表示状態を保存する対象 Sprite を集める。
    void CollectReplaySprites(std::vector<engine::Sprite*>& sprites) const;
    /// 開閉と編集途中の値を Replay JSON へ記録する。
    void CaptureReplayState(nlohmann::json& state) const;
    /// Replay JSON から表示状態を復元する。
    void RestoreReplayState(const nlohmann::json& state);
    static bool IsAnyActive() { return activeCount_ > 0; }
    // エディターから描画範囲を受け取り、Game View外のクリックを無視します。
    static void SetEditorViewport(int left, int top, int right, int bottom);

  private:
    enum class Modal { None, Discard, Reset };
    // 設定画面の編集値一式。適用・キャンセルに備え、確定済み設定と分けて保持する。
    struct Values {
        float bgm = .8f, se = .8f, sensitivity = 1.0f;
    };
    /// 確定済みの設定を編集用の値へ読み込む。
    void ReadValues();
    /// 編集値が開始時の値から変わっているか返す。
    bool IsDirty() const;
    /// 編集中の音量を確認用に一時反映する。
    void PreviewAudio();
    /// 編集値を現在のゲーム設定へ適用する。
    void Apply();
    /// 編集値を確定して保存する。
    void Commit();
    /// 選択カテゴリの値を既定値へ戻す。
    void ResetCategory();
    /// 現在カテゴリに表示する行数を返す。
    int RowCount() const;
    /// 選択項目の値を指定方向へ調整する。
    void Adjust(int direction);
    /// 選択行の決定操作を処理する。
    void ActivateRow();
    /// 下部の適用・取消操作を処理する。
    void ActivateFooter(int index);
    /// 破棄・初期化の確認ダイアログを処理する。
    void HandleModal(bool accept, bool back);
    /// 現在の値と選択状態から描画用 Sprite を構築する。
    void BuildFrame();
    void DrawCursor(float x, float y);
    void Rect(float x, float y, float w, float h, const engine::Vector4& color);
    void Label(const std::string& id, float x, float y, float height = 22.0f);
    void Number(int value, float x, float y, float height = 22.0f);
    void Picture(const std::string& texture, float x, float y, float w, float h, const engine::Vector4& color);
    engine::Vector2 Pointer() const;
    engine::SpriteCommon* common_ = nullptr;
    InputManager* input_ = nullptr;
    std::vector<std::unique_ptr<engine::Sprite>> sprites_;
    std::unordered_map<std::string, std::string> labelTextures_;
    std::string whiteTexture_ = "common/white.png";
    std::string menuIconTexture_ = "title/slime_save_icon.png";
    size_t usedSprites_ = 0;
    Values original_, draft_;
    Modal modal_ = Modal::None;
    std::string status_;
    int category_ = 0, row_ = 0, scroll_ = 0, footer_ = -1;
    bool active_ = false, suppressInput_ = false;
    bool replayOnly_ = false;
    int modalSelection_ = 1;
    bool dragging_ = false;
    engine::Vector2 previousPointer_{-1000, -1000};
    float animationTime_ = 0.f;
    int horizontalHeldDirection_ = 0;
    uint64_t horizontalHoldStartMs_ = 0;
    uint64_t horizontalNextRepeatMs_ = 0;
    float rowHeight_ = 90.0f;
    engine::Vector4 panelColor_{.94f, .97f, .95f, 1}, selectedColor_{.68f, .93f, .91f, 1};
    inline static int activeCount_ = 0;
    inline static RECT editorViewport_{};
    inline static bool hasEditorViewport_ = false;
};
