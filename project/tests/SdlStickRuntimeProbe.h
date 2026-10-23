#pragma once
#ifdef USE_IMGUI
#include "InputManager.h"
#include "WinApp.h"
#include <fstream>

// 仮想SDLコントローラーから実際のUpdateを通し、押し込み変換と押下エッジを確認します。
namespace SdlStickRuntimeProbe {
inline bool Enabled() { return GetEnvironmentVariableA("CG2_SDL_STICK_TEST", nullptr, 0) > 0; }
// 専用プロセスで一度だけ仮想パッドを接続し、検証結果を path のログへ書き出す。
inline void AfterDraw(const std::string& path) {
    static bool done = false;
    if (done) return;
    done = true;
    std::ofstream log(path, std::ios::trunc);
    auto finish = [&](const char* message) { log << message << std::endl; WinApp::CloseNow(); };
    XINPUT_STATE physical{};
    const bool xinputConnected = XInputGetState(0, &physical) == ERROR_SUCCESS;
    log << "XInput index 0 connected=" << xinputConnected << " SDL devices=" << SDL_NumJoysticks() << std::endl;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        const char* name = SDL_JoystickNameForIndex(i);
        log << "SDL device " << i << ' ' << (name ? name : "unknown") << std::endl;
    }
    if (xinputConnected || SDL_NumJoysticks() != 0) {
        finish("FAIL: virtual SDL test requires a process without physical controllers");
        return;
    }
    SDL_VirtualJoystickDesc description{};
    description.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    description.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    description.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    description.button_mask = (1u << SDL_CONTROLLER_BUTTON_LEFTSTICK) |
                              (1u << SDL_CONTROLLER_BUTTON_RIGHTSTICK) |
                              (1u << SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    description.name = "CG2 SDL stick input regression";
    const int index = SDL_JoystickAttachVirtualEx(&description);
    SDL_GameController* controller = index >= 0 ? SDL_GameControllerOpen(index) : nullptr;
    if (!controller) {
        if (index >= 0) SDL_JoystickDetachVirtual(index);
        finish("FAIL: virtual game controller creation failed"); return;
    }
    SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
    const auto left = SDL_GameControllerGetBindForButton(controller, SDL_CONTROLLER_BUTTON_LEFTSTICK);
    const auto right = SDL_GameControllerGetBindForButton(controller, SDL_CONTROLLER_BUTTON_RIGHTSTICK);
    const auto shoulder = SDL_GameControllerGetBindForButton(controller, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    bool ok = left.bindType == SDL_CONTROLLER_BINDTYPE_BUTTON &&
              right.bindType == SDL_CONTROLLER_BINDTYPE_BUTTON && shoulder.bindType == SDL_CONTROLLER_BINDTYPE_BUTTON;
    auto* input = engine::InputManager::GetInstance();
    input->Update();
    auto set = [&](int button, bool pressed) {
        ok = SDL_JoystickSetVirtualButton(joystick, button, pressed ? 1 : 0) == 0 && ok;
        input->Update();
    };
    auto check = [&](bool condition, const char* name) {
        log << (condition ? "OK " : "FAIL ") << name << std::endl;
        ok = condition && ok;
    };
    if (ok) {
        set(right.value.button, true);
        check(input->IsActionTriggered("LockOn") && input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_RIGHT_THUMB) &&
              !input->IsActionTriggered("LockOnNext"), "R3 press reaches LockOn");
        input->Update();
        check(!input->IsActionTriggered("LockOn") && input->IsGamepadButtonPressed(XINPUT_GAMEPAD_RIGHT_THUMB),
              "R3 hold does not retrigger");
        set(right.value.button, false);
        check(!input->IsGamepadButtonPressed(XINPUT_GAMEPAD_RIGHT_THUMB), "R3 release clears button");
        set(left.value.button, true);
        check(input->IsActionTriggered("LockOnNext") && !input->IsActionTriggered("LockOn"), "L3 press reaches target switch");
        set(left.value.button, false);
        set(right.value.button, true);
        check(input->IsActionTriggered("LockOn"), "R3 can be pressed again after release");
        set(right.value.button, false);
        set(shoulder.value.button, true);
        check(input->IsActionTriggered("SecondaryAction"), "existing LB mapping still works");
    }
    SDL_GameControllerClose(controller);
    SDL_JoystickDetachVirtual(index);
    finish(ok ? "PASS: virtual SDL R3/L3 input conversion, action bindings, press/hold/release and LB regression" :
                "FAIL: SDL stick input regression");
}
}
#endif