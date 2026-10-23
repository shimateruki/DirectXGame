#pragma once

#include "InputManager.h"
#include <array>
#include <cmath>

namespace engine {

// 画面ごとの割り当てを受け取る。入力の消費やリピート時計は保持しない。
struct MenuButtonBinding {
    // キー・ボタンの0は未割当。stickDirectionにはXInputの十字方向を一つ指定する。
    std::array<BYTE, 3> keys{};
    std::array<WORD, 2> buttons{};
    WORD stickDirection = 0;

    // キーとボタンは押下直後、スティックはInputManager側のリピートを含む入力として読む。
    bool Triggered(const InputManager& input) const {
        for (BYTE key : keys)
            if (key && input.IsKeyTriggered(key)) return true;
        for (WORD button : buttons)
            if (button && input.IsGamepadButtonTriggered(button)) return true;
        return stickDirection && input.IsMenuStickTriggered(stickDirection);
    }

    // 押し続けを毎フレーム判定する。Triggeredと異なり、反復待ち時間は挟まない。
    bool Held(const InputManager& input) const {
        for (BYTE key : keys)
            if (key && input.IsKeyPressed(key)) return true;
        for (WORD button : buttons)
            if (button && input.IsGamepadButtonPressed(button)) return true;
        const auto stick = input.GetLeftStick();
        // メニューの方向判定と同じ、主軸優先の閾値を使う。
        switch (stickDirection) {
        case XINPUT_GAMEPAD_DPAD_LEFT: return stick.x <= -.45f && std::abs(stick.x) >= std::abs(stick.y);
        case XINPUT_GAMEPAD_DPAD_RIGHT: return stick.x >= .45f && std::abs(stick.x) >= std::abs(stick.y);
        case XINPUT_GAMEPAD_DPAD_UP: return stick.y >= .45f && std::abs(stick.y) > std::abs(stick.x);
        case XINPUT_GAMEPAD_DPAD_DOWN: return stick.y <= -.45f && std::abs(stick.y) > std::abs(stick.x);
        default: return false;
        }
    }
};

struct MenuInputBindings {
    MenuButtonBinding up, down, left, right, confirm, cancel, remove;
};

// このフレームの操作結果。複数画面から読んでも入力を消費しない。
struct MenuInputFrame {
    bool up = false, down = false, left = false, right = false;
    bool confirm = false, cancel = false, remove = false, gamepad = false;
    // 軸の符号は右・下が正。左右または上下の同時入力は0に相殺する。
    int Horizontal() const { return static_cast<int>(right) - static_cast<int>(left); }
    int Vertical() const { return static_cast<int>(down) - static_cast<int>(up); }
};

inline MenuInputFrame ReadMenuInput(const InputManager& input, const MenuInputBindings& bindings) {
    return {bindings.up.Triggered(input), bindings.down.Triggered(input),
            bindings.left.Triggered(input), bindings.right.Triggered(input),
            bindings.confirm.Triggered(input), bindings.cancel.Triggered(input),
            bindings.remove.Triggered(input), input.IsGamepadMode()};
}

// 連続調整用の左右軸。音量などの反復速度は呼び出し側で管理する。
inline int ReadMenuHorizontalHeld(const InputManager& input, const MenuInputBindings& bindings) {
    return static_cast<int>(bindings.right.Held(input)) - static_cast<int>(bindings.left.Held(input));
}

} // namespace engine
