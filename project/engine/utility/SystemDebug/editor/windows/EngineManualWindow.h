#pragma once

class EngineManualWindow {
public:
    EngineManualWindow() = default;
    ~EngineManualWindow() = default;

    void Draw();

    void Open() { isOpen_ = true; }
    void Close() { isOpen_ = false; }
    bool IsOpen() const { return isOpen_; }

private:
    bool isOpen_ = false;
    int selectedIndex_ = 0;
};
