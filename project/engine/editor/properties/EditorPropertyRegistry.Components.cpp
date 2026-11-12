#include "EditorPropertyRegistry.h"
#include "Object3D.h"

#include <cmath>
#include <utility>

void EditorPropertyRegistry::RegisterBuiltInComponents() {
    // コンポーネントの表示・編集手段を登録し、Inspector 側の分岐を増やさず拡張します。
    const auto always = [](const engine::Object3D&) { return true; };
    const auto never = [](const engine::Object3D&) { return false; };
    const auto registerInspectorHiddenComponent = [this](EditorComponentDescriptor descriptor) {
        descriptor.showInInspector = false;
        return RegisterComponent(std::move(descriptor));
    };

    // 常設の基本情報を先に置き、Camera では使えない描画・衝突機能を条件付きで登録します。
    registerInspectorHiddenComponent({ "SceneObject", "Scene Object", "名前、GUID、Tag、LayerなどObject自体の情報です。", 0, true, false,
        always, always, EditorComponentInspectorMode::Custom });
    RegisterComponent({ "Transform", "Transform", "位置・回転・スケールと親子Transformを管理します。", 10, true, false, always,
        [](const engine::Object3D& object) { return object.HasBuiltInComponent(engine::Object3D::kTransformComponentType); },
        EditorComponentInspectorMode::Custom });
    RegisterComponent({ "MeshRenderer", "Mesh Renderer", "モデル、マテリアル、影を管理します。", 20, false, false,
        [](const engine::Object3D& object) { return !object.IsCameraObject(); },
        [](const engine::Object3D& object) { return object.HasBuiltInComponent(engine::Object3D::kMeshRendererComponentType); },
        EditorComponentInspectorMode::Custom });

    // Collider は形状の有無だけでなく、旧データの属性・Presence Marker でも存在を判定します。
    // 追加時は通常の床向け設定を与え、削除時は判定に関わる状態をまとめて解除します。
    EditorComponentDescriptor colliderComponent{ "Collider", "Collider", "衝突形状、属性、衝突Maskを管理します。", 30, false, true,
        [](const engine::Object3D& object) { return !object.IsCameraObject(); },
        [](const engine::Object3D& object) {
            return object.HasComponentPresenceMarker("Collider") ||
                object.GetColliderType() != ColliderType::None ||
                object.GetCollisionAttribute() != 0 || object.GetCollisionMask() != 0;
        },
        EditorComponentInspectorMode::Custom,
        [](engine::Object3D& object) {
            engine::Object3D::ColliderConfig config = object.GetColliderConfig();
            config.type = ColliderType::Aabb;
            if (std::fabs(config.size.x) < 0.001f || std::fabs(config.size.y) < 0.001f ||
                std::fabs(config.size.z) < 0.001f) {
                config.size = { 1.0f, 1.0f, 1.0f };
            }

            object.SetColliderConfig(config);
            object.SetCollisionAttribute(CollisionCategories::Get("ground"));
            object.SetCollisionMask(0xFFFFFFFFu);
            object.SetStatic(true);
            object.SetComponentPresenceMarker("Collider", true);
            return true;
        },
        [](engine::Object3D& object) {
            engine::Object3D::ColliderConfig config = object.GetColliderConfig();
            config.type = ColliderType::None;
            object.SetColliderConfig(config);
            object.SetCollisionAttribute(0);
            object.SetCollisionMask(0);
            object.SetStatic(false);
            object.SetComponentPresenceMarker("Collider", false);
            return true;
        } };

    // Trigger プリセットは同じ Collider を再設定し、Player との接触だけを検知します。
    colliderComponent.presets.push_back({
        "Trigger",
        "Trigger Collider Preset",
        "Playerを検知するTrigger Colliderとして追加または再設定します。",
        [](engine::Object3D& object) {
            engine::Object3D::ColliderConfig config = object.GetColliderConfig();
            config.type = ColliderType::Aabb;
            if (std::fabs(config.size.x) < 0.001f || std::fabs(config.size.y) < 0.001f ||
                std::fabs(config.size.z) < 0.001f) {
                config.size = { 1.0f, 1.0f, 1.0f };
            }
            object.SetColliderConfig(config);
            object.SetCollisionAttribute(CollisionCategories::Get("trigger"));
            object.SetCollisionMask(CollisionCategories::Get("player"));
            object.SetStatic(false);
            object.SetComponentPresenceMarker("Collider", true);
            return true;
        },
    });
    RegisterComponent(std::move(colliderComponent));

    // 以下は追加・削除可能な描画／演出コンポーネントです。
    RegisterComponent({ "ParticleEmitter", "Particle Emitter", "CPU/GPU Particle AssetをObjectへ接続します。", 40, false, true, always,
        [](const engine::Object3D& object) {
            return object.HasParticleEmitterComponent();
        },
        EditorComponentInspectorMode::Custom,
        [](engine::Object3D& object) {
            object.EnsureParticleEmitterComponent();
            return true;
        },
        [](engine::Object3D& object) {
            return object.RemoveParticleEmitterComponent();
        } });
    RegisterComponent({ "MeshEffect", "Mesh Effect", "Objectに追従するMesh Effectを管理します。", 60, false, true, always,
        [](const engine::Object3D& object) {
            return object.HasMeshEffectComponent();
        },
        EditorComponentInspectorMode::Custom,
        [](engine::Object3D& object) {
            object.EnsureMeshEffectComponent();
            return true;
        },
        [](engine::Object3D& object) {
            return object.RemoveMeshEffectComponent();
        } });

    // Animator は既存の Animation 名と Controller Path も存在判定に含め、旧形式の設定を拾います。
    // 削除時には再生名・Loop・Controller を一緒に消して状態の食い違いを防ぎます。
    RegisterComponent({ "Animator", "Animator", "AnimationとAnimator Controllerを管理します。", 70, false, true, always,
        [](const engine::Object3D& object) {
            return object.HasComponentPresenceMarker("Animator") ||
                !object.GetAnimationName().empty() || !object.GetAnimatorControllerPath().empty();
        },
        EditorComponentInspectorMode::Custom,
        [](engine::Object3D& object) {
            object.SetComponentPresenceMarker("Animator", true);
            return true;
        },
        [](engine::Object3D& object) {
            object.SetAnimationName("");
            object.SetAnimationLoop(false);
            object.ClearAnimatorController();
            object.SetComponentPresenceMarker("Animator", false);
            return true;
        } });

    // 移動と参照を担うコンポーネントを続けて登録します。Automatic は共通プロパティ描画を使用します。
    RegisterComponent({ "PathMover", "Path Mover", "Ghost Recorderで記録したPath移動を管理します。", 80, false, true, always,
        [](const engine::Object3D& object) {
            return object.HasPathMoverComponent();
        },
        EditorComponentInspectorMode::Custom,
        [](engine::Object3D& object) {
            object.EnsurePathMoverComponent();
            return true;
        },
        [](engine::Object3D& object) {
            return object.RemovePathMoverComponent();
        } });
    RegisterComponent({ "GameplayLink", "Gameplay Link", "Event IDとTarget IDの接続を管理します。", 90, false, true, always,
        [](const engine::Object3D& object) {
            return object.HasGameplayLinkComponent();
        },
        EditorComponentInspectorMode::Automatic,
        [](engine::Object3D& object) {
            object.EnsureGameplayLinkComponent();
            return true;
        },
        [](engine::Object3D& object) {
            return object.RemoveGameplayLinkComponent();
        } });
    RegisterComponent({ "NavAgent", "Nav Agent", "Physics QueryとA*で障害物を避ける移動方向を計算します。", 95, false, true,
        [](const engine::Object3D& object) { return !object.IsCameraObject(); },
        [](const engine::Object3D& object) { return object.HasNavAgentComponent(); },
        EditorComponentInspectorMode::Automatic,
        [](engine::Object3D& object) {
            object.EnsureNavAgentComponent();
            return true;
        },
        [](engine::Object3D& object) {
            return object.RemoveNavAgentComponent();
        } });

    // Camera や Gameplay は専用パネルで編集するため、共通 Inspector の追加候補には表示しません。
    registerInspectorHiddenComponent({ "Camera", "Camera", "Scene CameraのLens、追従、Blendを管理します。", 100, false, false,
        [](const engine::Object3D& object) { return object.IsCameraObject(); },
        [](const engine::Object3D& object) { return object.IsCameraObject(); },
        EditorComponentInspectorMode::Custom });
    registerInspectorHiddenComponent({ "Gameplay", "Gameplay", "敵、ギミック、Item固有の配置Parameterです。", 110, false, false, always,
        [](const engine::Object3D& object) { return object.GetParameters().has_value(); },
        EditorComponentInspectorMode::Custom });
    registerInspectorHiddenComponent({ "EditorState", "Editor State", "Editorだけが使用する表示・Lock状態です。", 1000, true, false,
        never, never, EditorComponentInspectorMode::Custom });
}
