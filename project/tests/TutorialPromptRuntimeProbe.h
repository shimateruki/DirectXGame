#pragma once
#ifdef USE_IMGUI
#include "game/scene/tutorial/TutorialScene.h"
#include "game/system/tutorial/TutorialDirector.h"
#include "InputManager.h"
#include "Sprite.h"
#include "TextureManager.h"
#include <fstream>
#include <cmath>

// 専用プロセスだけで実シーン・実テクスチャを使い、入力方式の往復切替を検証します。
namespace TutorialPromptRuntimeProbe {
using json = nlohmann::json;
inline bool Enabled() { return GetEnvironmentVariableA("CG2_TUTORIAL_PROMPT_TEST", nullptr, 0) > 0; }
inline void AfterDraw(SceneManager* manager, const std::string& path) {
    static std::ofstream log(path, std::ios::trunc);
    static bool requested = false, done = false;
    static ULONGLONG started = GetTickCount64();
    if (done) return;
    auto finish = [&](const char* message) { log << message << std::endl; done = true; WinApp::CloseNow(); };
    if (GetTickCount64() - started > 180000) { finish("FAIL: tutorial loading timeout"); return; }
    if (manager->IsTransitioning()) return;
    if (!requested) { manager->ChangeScene("TUTORIAL"); requested = true; return; }
    auto* scene = dynamic_cast<TutorialScene*>(manager->GetCurrentScene());
    if (!scene || !scene->GetPlayer()) return;
    auto* input = engine::InputManager::GetInstance();
    const auto originalInput = input->CaptureReplayState();
    TutorialDirector director;
    if (!director.Initialize(scene, scene->GetPlayer(), input)) { finish("FAIL: tutorial initialization"); return; }
    json flow;
    std::ifstream("Resources/json/tutorial/tutorial_flow.json") >> flow;
    auto* label = scene->GetSpriteByName("tutorial_prompt_label");
    auto* icon = scene->GetSpriteByName("tutorial_prompt_icon_0");
    bool ok = label && icon;
    int checked = 0;
    for (const auto& step : flow["steps"]) {
        if (!ok || !step.value("showPrompt", true) || !step.contains("promptLabel")) continue;
        engine::InputManager::ReplayState state{};
        input->ApplyReplayState(state);
        ok = director.JumpToStep(step["id"].get<std::string>());
        director.Update(0.1f);
        const std::string keyboard = step["promptLabel"].get<std::string>();
        ok = ok && label->GetTextureName() == keyboard;
        // 物理デバイスのポーリング後と同じ入力モードを再生し、手順は移動せず更新します。
        state.gamepadMode = true;
        input->ApplyReplayState(state);
        director.Update(0.0f);
        const std::string pad = step.value("promptLabelGamepad", keyboard);
        ok = ok && label->GetTextureName() == pad;
        if (step.contains("promptIconsGamepad"))
            ok = ok && icon->GetTextureName() == step["promptIconsGamepad"][0].get<std::string>();
        const auto size = label->GetSize();
        const auto& metadata = TextureManager::GetInstance()->GetMetadata(label->GetTextureHandle());
        ok = ok && label->IsVisible() && size.x > 0 && size.x <= 470.01f && size.y > 0 && size.y <= 76.01f;
        ok = ok && std::abs(size.x / size.y - float(metadata.width) / float(metadata.height)) < 0.01f;
        state.gamepadMode = false;
        input->ApplyReplayState(state);
        director.Update(0.0f);
        ok = ok && label->GetTextureName() == keyboard;
        log << (ok ? "OK " : "FAIL ") << step["id"] << " keyboard -> gamepad -> keyboard" << std::endl;
        if (!ok) break;
        ++checked;
    }
    // 達成後のフェード状態では、切替をしても表示要求を復活させません。
    if (ok) {
        director.JumpToStep("move");
        json state;
        director.CaptureReplayState(state);
        state["stepConditionSatisfied"] = true;
        state["promptRequestedVisible"] = false;
        state["promptAlpha"] = 0.5f;
        director.RestoreReplayState(state);
        auto pad = input->CaptureReplayState(); pad.gamepadMode = true;
        input->ApplyReplayState(pad);
        director.Update(0.0f);
        director.CaptureReplayState(state);
        ok = !state.value("promptRequestedVisible", true) && label->GetTextureName() == "ui/tutorial/tutorial_move_gamepad.png";
        log << (ok ? "OK" : "FAIL") << " fading prompt stays dismissed on input change" << std::endl;
    }
    director.Finalize();
    input->ApplyReplayState(originalInput);
    log << "Checked prompts=" << checked << std::endl;
    finish(ok && checked > 0 ? "PASS: tutorial labels, glyphs, aspect ratio, fallback and bidirectional mode switching" : "FAIL: tutorial prompt regression");
}
}
#endif