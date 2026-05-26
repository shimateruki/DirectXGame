#pragma once

class EngineManualWindow {
public:
    EngineManualWindow() = default;
    ~EngineManualWindow() = default;

    void Draw();

    // メニューバーから呼び出すための関数
    void Open() { isOpen_ = true; }
    void Close() { isOpen_ = false; }
    bool IsOpen() const { return isOpen_; }

private:
    bool isOpen_ = false;
    int selectedIndex_ = 0; // 現在左側のリストで選ばれている項目の番号
};