#include "engine/editor/scene/inspector/InspectorWindow.h"
#include "DebugEditor.h"
#include "EditorObjectFactoryRegistry.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "Object3D.h"
#ifdef USE_IMGUI
#include "imgui.h"
#endif

void InspectorWindow::DrawEnemyTypeSelector() {
#ifdef USE_IMGUI
    auto* object = editor_->GetSelectedObject();
    auto* scene = editor_->GetSceneManager() ? editor_->GetSceneManager()->GetCurrentScene() : nullptr;
    if (!object || !scene) return;
    using Registry = EditorObjectFactoryRegistry;
    const auto& types = Registry::GetTypes(Registry::Kind::Enemy);
    const std::string current = object->GetEnemyType();
    std::string label = current.empty() ? "(未設定)" : current;
    for (const auto& type : types) if (type.id == current) label = type.displayName;
    if (ImGui::BeginCombo("敵の種類", label.c_str())) {
        for (const auto& type : types) {
            if (!ImGui::Selectable(type.displayName.c_str(), current == type.id) || current == type.id) continue;
            // 実際のFactoryから表示・Collider・基本値を取得する。配置位置と親子関係は保つ。
            auto defaults = Registry::Create(Registry::Kind::Enemy, type.id, scene->GetObject3DCommon());
            if (!defaults || defaults->GetEnemyType() != type.id) continue;
            if (auto* session = editor_->GetActorSession()) session->Stop();
            object->SetEnemyType(type.id);
            object->SetName("Enemy_" + type.id);
            object->SetModel(defaults->GetModelName());
            object->SetColor(defaults->GetColor());
            object->SetMaterialType(defaults->GetMaterialType());
            object->SetEmissive(defaults->GetEmissive());
            object->SetScale(defaults->GetScale());
            object->SetColliderConfig(defaults->GetColliderConfig());
            object->SetCollisionAttribute(defaults->GetCollisionAttribute());
            object->SetCollisionMask(defaults->GetCollisionMask());
            object->SetParameters(defaults->GetParameters());
            object->SetAnimationName(defaults->GetAnimationName()); object->SetAnimationLoop(defaults->IsAnimationLoop());
        }
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("種類と初期値はゲーム側の登録を使用します。動作は下の調整欄で確認できます。");
#endif
}
