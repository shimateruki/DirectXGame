#define NOMINMAX
#include "KeyConfig.h"
#include "InputGlyphResolver.h"
#include <cassert>
#include <iostream>
#include <type_traits>

// UIを隠すだけでなく、再割当・旧JSON読込のAPI自体が消えていることを確認する。
template<class T> concept CanRebind = requires(T& value) { value.SetBinding("Jump", BindData{}); };
template<class T> concept CanLoad = requires(T& value) { value.Load(); };
template<class T> concept CanSave = requires(T& value) { value.Save(); };
static_assert(!CanRebind<KeyConfig> && !CanLoad<KeyConfig> && !CanSave<KeyConfig>);
static_assert(std::is_same_v<decltype(KeyConfig::GetInstance()->GetBindData("Jump")), const BindData*>);

int main() {
    auto* inputs = KeyConfig::GetInstance();
    inputs->Initialize();
    const auto check = [inputs](const char* action, int key, int mouse, WORD pad) {
        const auto* binding = inputs->GetBindData(action);
        assert(binding && binding->keyCode == key && binding->mouseButton == mouse && binding->padCode == pad);
    };
    check("Forward", 0x11, -1, XINPUT_GAMEPAD_DPAD_UP);
    check("Backward", 0x1F, -1, XINPUT_GAMEPAD_DPAD_DOWN);
    check("Left", 0x1E, -1, XINPUT_GAMEPAD_DPAD_LEFT);
    check("Right", 0x20, -1, XINPUT_GAMEPAD_DPAD_RIGHT);
    check("Jump", 0x39, -1, XINPUT_GAMEPAD_A);
    check("PrimaryAttack", 0, 0, XINPUT_GAMEPAD_X);
    check("SecondaryAction", 0, 1, XINPUT_GAMEPAD_LEFT_SHOULDER);
    check("Ability", 0x12, -1, XINPUT_GAMEPAD_Y);
    check("MorphRelease", 0x13, -1, XINPUT_GAMEPAD_B);
    check("LockOn", 0x10, -1, XINPUT_GAMEPAD_RIGHT_THUMB);
    check("LockOnNext", 0x22, -1, XINPUT_GAMEPAD_LEFT_THUMB);
    assert(inputs->GetBindData("RemovedLegacyAction") == nullptr);
    // 移動はWASD全体を表示し、パッドでは左スティックへ切り替わる。
    const auto keyboard = ResolveInputGlyph("ui/input/combo/combo_wasd.png", false);
    const auto pad = ResolveInputGlyph("ui/input/combo/combo_wasd.png", true);
    assert(keyboard.path == "ui/input/combo/combo_wasd.png" && !keyboard.useCrop);
    assert(pad.path == "ui/input/controller/pad_l_stick.png" && !pad.useCrop);
    assert(ResolveInputGlyph("ui/input/mouse/mouse_right.png", true).path == "ui/input/controller/pad_lb.png");
    inputs->Initialize();
    check("Jump", 0x39, -1, XINPUT_GAMEPAD_A);
    std::cout << "Fixed input and guide glyph checks passed.\n";
}
