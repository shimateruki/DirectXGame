#pragma once

#include "engine/system/input/MenuInput.h"

namespace MenuControls {

enum class Screen { Title, SaveSelect, Settings, Pause, Guide };

// 操作の割り当てと、その画面が使う既存の案内画像を一緒に定義する。
// JSON側の枠・座標・selectedTextureはそのまま使う。
struct Hint {
    const char* keyboard = "";
    const char* gamepad = "";
    const char* Resolve(bool useGamepad) const { return useGamepad ? gamepad : keyboard; }
};

struct Scheme {
    engine::MenuInputBindings bindings;
    Hint confirmHint, closeHint, footerHint;
};

// タイトルとセーブ選択は矢印・Spaceを使い、他画面のWASD・Enter割当とは分ける。
inline Scheme Make(Screen screen) {
    const bool title = screen == Screen::Title || screen == Screen::SaveSelect;
    Scheme scheme;
    scheme.bindings.up = {{{DIK_UP, static_cast<BYTE>(title ? 0 : DIK_W)}}, {{XINPUT_GAMEPAD_DPAD_UP}}, XINPUT_GAMEPAD_DPAD_UP};
    scheme.bindings.down = {{{DIK_DOWN, static_cast<BYTE>(title ? 0 : DIK_S)}}, {{XINPUT_GAMEPAD_DPAD_DOWN}}, XINPUT_GAMEPAD_DPAD_DOWN};
    scheme.bindings.left = {{{DIK_LEFT, static_cast<BYTE>(title ? 0 : DIK_A)}}, {{XINPUT_GAMEPAD_DPAD_LEFT}}, XINPUT_GAMEPAD_DPAD_LEFT};
    scheme.bindings.right = {{{DIK_RIGHT, static_cast<BYTE>(title ? 0 : DIK_D)}}, {{XINPUT_GAMEPAD_DPAD_RIGHT}}, XINPUT_GAMEPAD_DPAD_RIGHT};
    scheme.bindings.confirm = {{{DIK_SPACE, static_cast<BYTE>(title ? 0 : DIK_RETURN)}}, {{XINPUT_GAMEPAD_A}}};
    switch (screen) {
    case Screen::Title:
        scheme.confirmHint = {"mainMenuPrompt", "mainMenuPrompt"};
        break;
    case Screen::SaveSelect:
        scheme.bindings.cancel = {{{DIK_ESCAPE}}, {{XINPUT_GAMEPAD_B}}};
        scheme.bindings.remove = {{{DIK_D}}, {{XINPUT_GAMEPAD_X}}};
        scheme.footerHint = {"ui/menu_hints/save_keyboard.png", "ui/menu_hints/save_gamepad.png"};
        break;
    case Screen::Settings:
        scheme.bindings.cancel = {{{DIK_ESCAPE, DIK_TAB}}, {{XINPUT_GAMEPAD_B}}};
        scheme.confirmHint = {"settings_footer_confirm_text", "settings_footer_confirm_text"};
        scheme.closeHint = {"settings_footer_close_text", "settings_footer_close_text"};
        break;
    case Screen::Pause:
    case Screen::Guide:
        scheme.bindings.cancel = {{{DIK_TAB}}, {{XINPUT_GAMEPAD_B,
            static_cast<WORD>(screen == Screen::Pause ? XINPUT_GAMEPAD_START : XINPUT_GAMEPAD_BACK)}}};
        scheme.confirmHint = {"generated/text/pause_guide/footer_confirm_keyboard.png", "generated/text/pause_guide/footer_confirm_gamepad.png"};
        scheme.closeHint = {"generated/text/pause_guide/footer_close_keyboard.png", "generated/text/pause_guide/footer_close_gamepad.png"};
        break;
    }
    return scheme;
}

// 列挙した5画面に対応する固定表を返す。有効なScreen列挙値を渡す。
inline const Scheme& Get(Screen screen) {
    static const std::array<Scheme, 5> schemes = {Make(Screen::Title), Make(Screen::SaveSelect),
        Make(Screen::Settings), Make(Screen::Pause), Make(Screen::Guide)};
    return schemes[static_cast<size_t>(screen)];
}

inline engine::MenuInputFrame Read(Screen screen, const engine::InputManager& input) {
    return engine::ReadMenuInput(input, Get(screen).bindings);
}

// タイトルの既存画像から必要な文字だけを使う。見た目・中心位置・倍率は変えない。
// 値は既存タイトル画像内のピクセル位置。表示先のウィンドウ座標ではない。
struct TitlePromptCrop { float firstWidth, suffixOrigin; };
inline TitlePromptCrop TitlePrompt(bool gamepad) {
    return gamepad ? TitlePromptCrop{65.0f, 365.0f} : TitlePromptCrop{289.0f, 517.0f};
}

} // namespace MenuControls
