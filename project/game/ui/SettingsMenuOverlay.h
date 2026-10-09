#pragma once
#include "KeyConfig.h"
#include "WinApp.h"
#include "engine/utility/math/Math.h"
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class InputManager;
class Sprite;
class SpriteCommon;

// ゲーム中・タイトル共通の設定画面。編集中の値と確定済みの値を分離します。
class SettingsMenuOverlay {
  public:
    enum class Result { None, Closed };
    SettingsMenuOverlay();
    ~SettingsMenuOverlay();
    void Initialize(SpriteCommon* common, const std::string& layoutPath = "Resources/json/sprite/settingsScene.json");
    void Finalize();
    void SetActive(bool active);
    bool IsActive() const { return active_; }
    Result Update(float deltaTime);
    void Draw();
    void CollectReplaySprites(std::vector<Sprite*>& sprites) const;
    void CaptureReplayState(nlohmann::json& state) const;
    void RestoreReplayState(const nlohmann::json& state);
    static bool IsAnyActive() { return activeCount_ > 0; }
    // エディターから描画範囲を受け取り、Game View外のクリックを無視します。
    static void SetEditorViewport(int left, int top, int right, int bottom);

  private:
    enum class Modal { None, Discard, Reset, Binding, Conflict, Display };
    // 設定画面の編集値一式。適用・キャンセルに備え、確定済み設定と分けて保持する。
    struct Values {
        float bgm = .8f, se = .8f, sensitivity = 1.0f;
        int width = 1280, height = 720;
        bool borderless = false;
        bool invertX = false, invertY = false;
        std::unordered_map<std::string, BindData> bindings;
    };
    void ReadValues();
    bool IsDirty() const;
    void PreviewAudio();
    void Apply();
    void Commit();
    void RollbackDisplay();
    void ResetCategory();
    void ChangeCategory(int category);
    int RowCount() const;
    void Adjust(int direction);
    void ActivateRow();
    void ActivateFooter(int index);
    void HandleModal(bool accept, bool back);
    void CaptureBinding();
    void AcceptBinding(bool swap);
    void BuildFrame();
    void Rect(float x, float y, float w, float h, const Vector4& color);
    void Label(const std::string& id, float x, float y, float height = 22.0f);
    void Number(int value, float x, float y, float height = 22.0f);
    void Picture(const std::string& texture, float x, float y, float w, float h, const Vector4& color);
    Vector2 Pointer() const;
    SpriteCommon* common_ = nullptr;
    InputManager* input_ = nullptr;
    std::vector<std::unique_ptr<Sprite>> sprites_;
    size_t usedSprites_ = 0;
    Values original_, draft_;
    WinApp::DisplayState previousDisplay_;
    std::chrono::steady_clock::time_point displayDeadline_;
    Modal modal_ = Modal::None;
    BindData candidate_;
    std::string conflictAction_;
    std::string status_;
    int category_ = 0, row_ = 0, scroll_ = 0, footer_ = -1, device_ = 0;
    bool active_ = false, suppressInput_ = false, bindingArmed_ = false, displayTrial_ = false;
    bool replayOnly_ = false;
    int modalSelection_ = 1;
    bool dragging_ = false;
    Vector2 previousPointer_{-1000, -1000};
    float rowHeight_ = 56.0f;
    Vector4 panelColor_{.94f, .97f, .95f, 1}, selectedColor_{.68f, .93f, .91f, 1};
    inline static int activeCount_ = 0;
    inline static RECT editorViewport_{};
    inline static bool hasEditorViewport_ = false;
};
