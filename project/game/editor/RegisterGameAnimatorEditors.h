#pragma once

// ゲーム固有の登録入口：RegisterGameAnimatorEditors。登録先の共通Editorに、個別の型名や分岐を持ち込まない。
#ifdef USE_IMGUI

#include "engine/editor/animation/AnimatorEditorRegistry.h"
#include "Player.h"
#include "DebugConsole.h"
#include "imgui.h"

// Player固有の再生規則とUIはゲーム側で定義し、汎用Editorには具体型を教えない。
inline void RegisterGameAnimatorEditors() {
    engine::AnimatorEditorRegistry::Entry entry;
    entry.assetName = "player_slime";
    entry.accepts = [](engine::Object3D& object) {
        return dynamic_cast<Player*>(&object) != nullptr;
    };
    entry.reload = [](engine::Object3D& object) {
        static_cast<Player&>(object).ReloadSlimeAnimatorController();
    };

    // acceptsを通った現在のObjectだけが渡される。選択先の生ポインタは保存しない。
    entry.drawPreview = [](engine::Object3D& object, const AnimatorStateDefinition* selectedState,
                           const engine::AnimatorEditorRegistry::SaveAction& save) {
        auto* player = &static_cast<Player&>(object);
        AnimatorControllerRuntime* runtime = player->GetSlimeAnimatorControllerRuntime();
        const AnimatorStateDefinition* state = runtime ? runtime->GetCurrentState() : nullptr;
        ImGui::Text("Playerの現在State: %s", state ? state->name.c_str() : "(未読込)");

        if (runtime) {
            ImGui::Text("LocomotionMode: %d / 切替重み: %.2f",
                runtime->GetInt("LocomotionMode"), runtime->GetTransitionWeight());
        }

        if (selectedState) {
            const AnimatorStateDefinition& selected = *selectedState;

            if (selected.name.rfind("player_", 0) == 0 && !selected.bodyClipName.empty()) {
                if (ImGui::Button("保存して選択した能力モーションを確認", ImVec2(-1.0f, 0.0f))) {
                    save();
                    if (!player->PlaySlimeAbilityMotion(selected.name, selected.loop, 1.0f,
                        selected.blendDuration, selected.blendDuration)) {
                        DebugConsole::GetInstance()->AddLog("Animator ability preview failed: " + selected.name);
                    }
                }

                ImGui::TextDisabled("見た目のみを再生します。攻撃判定とダメージは発生しません。");
            }
        }

        ImGui::TextWrapped("保存するとシーン内のPlayerへ再読込します。通常動作はゲーム側の状態から決まり、Model用のState再生ボタンは使いません。");
    };

    entry.drawStateHelp = [](const AnimatorStateDefinition& state) {
        if (state.name.rfind("player_", 0) == 0) {
            ImGui::TextWrapped("能力モーション: State名がC++の再生キーです。Body ClipとSpeedは本編へ反映します。LoopとBlendは攻撃処理側が指定し、Eventはこの別レイヤーでは未接続です。");
        }
    };
    entry.getRuntime = [](engine::Object3D& object) {
        return static_cast<Player&>(object).GetSlimeAnimatorControllerRuntime();
    };
    entry.drawRuntimeParameter = [](AnimatorControllerRuntime& runtime, const AnimatorParameterDefinition& parameter) {
        // 行動Stateが毎フレーム書く値だけ読み取り専用。他の値編集は共通Editorに任せる。
        if (parameter.name != "LocomotionMode") return false;
        ImGui::TextDisabled("現在値: %d （ゲームの行動状態から毎フレーム設定）", runtime.GetInt(parameter.name));
        return true;
    };
    engine::AnimatorEditorRegistry::Register(std::move(entry));
}
#endif
