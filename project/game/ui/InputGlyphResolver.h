#pragma once

#include "KeyConfig.h"

#include <Xinput.h>
#include <cstdio>
#include <string>

// 固定の操作画像を、現在のキー設定と1枚の透過アトラスの切り抜きへ変換する。
struct InputGlyphSpec {
    std::string path;
    bool useCrop = false;
    float cropX = 0.0f;
    float cropY = 0.0f;
    float width = 96.0f;
    float height = 64.0f;
};

inline std::string GetInputGlyphBindingSignature() {
    // ガイド表示中のキー再割当に気づけるよう、見せている操作の設定だけ比較する。
    std::string signature;
    for (const char* action : {"Forward", "Backward", "Left", "Right", "Jump", "Ability",
                               "PrimaryAttack", "SecondaryAction", "LockOn", "MorphRelease"}) {
        const BindData* binding = KeyConfig::GetInstance()->GetBindData(action);
        if (binding) signature += std::to_string(binding->keyCode) + ":" +
                                  std::to_string(binding->mouseButton) + ":" +
                                  std::to_string(binding->padCode) + ";";
    }
    return signature;
}

inline InputGlyphSpec MakeKeyboardGlyph(int keyCode) {
    const int code = keyCode >= 0 && keyCode <= 255 ? keyCode : 0;
    return {"ui/input/keyboard/key_scan_atlas.png", true,
            static_cast<float>((code % 16) * 96), static_cast<float>((code / 16) * 64), 96.0f, 64.0f};
}

inline InputGlyphSpec ResolveActionGlyph(const std::string& action, bool gamepadMode) {
    const BindData* binding = KeyConfig::GetInstance()->GetBindData(action);
    if (!binding) {
        return MakeKeyboardGlyph(0);
    }
    if (gamepadMode) {
        const char* path = nullptr;
        switch (binding->padCode) {
        case XINPUT_GAMEPAD_A: path = "ui/input/controller/pad_a.png"; break;
        case XINPUT_GAMEPAD_B: path = "ui/input/controller/pad_b.png"; break;
        case XINPUT_GAMEPAD_X: path = "ui/input/controller/pad_x.png"; break;
        case XINPUT_GAMEPAD_Y: path = "ui/input/controller/pad_y.png"; break;
        case XINPUT_GAMEPAD_LEFT_SHOULDER: path = "ui/input/controller/pad_lb.png"; break;
        case XINPUT_GAMEPAD_RIGHT_SHOULDER: path = "ui/input/controller/pad_rb.png"; break;
        case XINPUT_GAMEPAD_DPAD_UP: path = "ui/input/controller/pad_dpad_up.png"; break;
        case XINPUT_GAMEPAD_DPAD_DOWN: path = "ui/input/controller/pad_dpad_down.png"; break;
        case XINPUT_GAMEPAD_DPAD_LEFT: path = "ui/input/controller/pad_dpad_left.png"; break;
        case XINPUT_GAMEPAD_DPAD_RIGHT: path = "ui/input/controller/pad_dpad_right.png"; break;
        case XINPUT_GAMEPAD_LEFT_THUMB: path = "ui/input/controller/pad_l3.png"; break;
        case XINPUT_GAMEPAD_RIGHT_THUMB: path = "ui/input/controller/pad_r3.png"; break;
        default: break;
        }
        return path ? InputGlyphSpec{path} : MakeKeyboardGlyph(0);
    }
    if (binding->mouseButton >= 0) {
        switch (binding->mouseButton) {
        case 0: return {"ui/input/mouse/mouse_left.png"};
        case 1: return {"ui/input/mouse/mouse_right.png"};
        case 2: return {"ui/input/mouse/mouse_middle.png"};
        case 3: return {"ui/input/mouse/mouse_x1.png"};
        default: break;
        }
        // 拡張マウスボタンの画像がない場合は、識別できない画像を表示しない。
        return MakeKeyboardGlyph(0);
    }
    return MakeKeyboardGlyph(binding->keyCode);
}

inline InputGlyphSpec ResolveInputGlyph(const std::string& authoredPath, bool gamepadMode) {
    const char* action = nullptr;
    if (authoredPath == "ui/input/keyboard/key_space.png") action = "Jump";
    else if (authoredPath == "ui/input/keyboard/key_e.png") action = "Ability";
    else if (authoredPath == "ui/input/keyboard/key_q.png") action = "LockOn";
    else if (authoredPath == "ui/input/keyboard/key_r.png") action = "MorphRelease";
    else if (authoredPath == "ui/input/mouse/mouse_left.png") action = "PrimaryAttack";
    else if (authoredPath == "ui/input/mouse/mouse_right.png") action = "SecondaryAction";
    else if (authoredPath == "ui/input/combo/combo_wasd.png") {
        if (gamepadMode) return {"ui/input/controller/pad_l_stick.png"};
        return ResolveActionGlyph("Forward", false);
    }
    if (action) return ResolveActionGlyph(action, gamepadMode);
    if (gamepadMode && authoredPath == "ui/input/mouse/mouse_move.png")
        return {"ui/input/controller/pad_r_stick.png"};
    return {authoredPath};
}
