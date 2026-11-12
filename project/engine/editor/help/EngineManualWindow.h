#pragma once

// 担当：Editor機能の目的・操作手順・保存先を検索して表示する。
// 関連：GameEditorController / DebugEditor（呼出側）
// 主な入口：Open / Draw / Close
class EngineManualWindow {
public:
    EngineManualWindow() = default;

    ~EngineManualWindow() = default;

    /// 選択中カテゴリの説明と操作手順をImGuiで描画する。
    void Draw();

    /// マニュアルウィンドウを表示状態にする。
    void Open() { isOpen_ = true; }

    /// マニュアルウィンドウを閉じる。
    void Close() { isOpen_ = false; }

    bool IsOpen() const { return isOpen_; }

private:
    bool isOpen_ = false;
    int selectedIndex_ = 0;
    char searchBuffer_[128] = {};
};
