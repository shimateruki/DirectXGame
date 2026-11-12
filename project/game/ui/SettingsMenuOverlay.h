#pragma once
#include "json.hpp"
#include "WinApp.h"
#include "engine/utility/math/Math.h"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine { class InputManager; }
namespace engine { class Sprite; }
namespace engine { class SpriteCommon; }

// ゲーム中・タイトル共通の設定画面。編集中の値と確定済みの値を分離します。
// 関連：GameSettingsManagerと編集用Valuesを分け、確定・破棄・カーソル制御をまとめる。
// 主な入口：Initialize / SetActive / Update / Draw / Finalize
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
    // 開く時に確定値を読み直し、選択・入力状態を初期化する。開閉に合わせてカーソル制御も変更する。
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

    // 複数インスタンスを含む表示数で判定し、他画面でも設定UIが入力を使用中か共有する。
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
    // 編集値を適用して設定JSONへ保存する。編集中の音量プレビューだけでは確定しない。
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

    void DrawCursor(int row);

    // JSONの名前で静的な画像・配置を取得し、入力判定と描画で共有する。
    struct LayoutSprite {
        std::string texture;
        std::string selectedTexture;
        engine::Vector2 position{};
        engine::Vector2 size{};
        engine::Vector4 color{1.0f, 1.0f, 1.0f, 1.0f};
        engine::Vector2 cropOrigin{};
        engine::Vector2 cropSize{};
        bool hasCrop = false;
    };

    // レイアウト未登録ならnullptr。返す参照はこのメニューのレイアウト保持中だけ有効。
    const LayoutSprite* FindLayoutSprite(const std::string& name) const;
    // layoutの画面上の矩形にポインターが入るかを返す。未登録名ならfalse。
    bool InsideLayout(const engine::Vector2& point, const std::string& name) const;
    void PictureLayout(const std::string& name, bool selected = false, float offsetX = 0.0f,
                       float offsetY = 0.0f, float widthScale = 1.0f);
    void CenteredLabel(const std::string& id, const std::string& layoutName);


    void Label(const std::string& id, float x, float y, float height = 22.0f);

    void Number(int value, float x, float y, float height = 22.0f);

    void Picture(const std::string& texture, float x, float y, float w, float h, const engine::Vector4& color);

    // Game Viewまたはクライアント内のポインターを1280×720の設計座標へ戻す。
    engine::Vector2 Pointer() const;

    engine::SpriteCommon* common_ = nullptr;
    engine::InputManager* input_ = nullptr;

    std::vector<std::unique_ptr<engine::Sprite>> sprites_; // この画面で生成したSpriteの所有先。検索用の参照とは分ける。
    std::unordered_map<std::string, std::string> labelTextures_;
    std::unordered_map<std::string, LayoutSprite> layoutSprites_;

    size_t usedSprites_ = 0;

    Values original_, draft_; // 開いた時の確定値と、編集中の値。破棄時はoriginal_へ戻す。
    Modal modal_ = Modal::None;
    std::string status_;

    int category_ = 0, row_ = 0, scroll_ = 0, footer_ = -1; // カテゴリ・行・スクロール位置。footer_=-1はフッター未選択。
    bool active_ = false, suppressInput_ = false; // 画面の開閉状態と、開いた直後の持ち越し入力の抑止状態。
    bool replayOnly_ = false; // 復元表示用の状態。通常の操作・適用と区別する。

    int modalSelection_ = 1;

    bool dragging_ = false;
    engine::Vector2 previousPointer_{-1000, -1000};

    float animationTime_ = 0.f;

    int horizontalHeldDirection_ = 0; // 左右の長押し方向。-1が左、0がなし、1が右。
    // 左右の連続調整に使う実時刻はミリ秒。animationTime_の経過秒数とは別に管理する。
    uint64_t horizontalHoldStartMs_ = 0; // 左右キーの長押し開始時刻（ミリ秒）。
    uint64_t horizontalNextRepeatMs_ = 0; // 次の左右リピートを受け付ける時刻（ミリ秒）。

    inline static int activeCount_ = 0;
    inline static RECT editorViewport_{};
    inline static bool hasEditorViewport_ = false;
};
