#include "InspectorWindow.h"
#include "DebugEditor.h"
#include "Object3d.h"
#include "imgui.h"

void InspectorWindow::DrawGimmickTypeSelector() {
#ifdef USE_IMGUI
    Object3d* selectedObject = editor_->GetSelectedObject();
    if (!selectedObject) return;

    const char* gimmickTypes[] = { "Default", "MovingFloor", "Trampoline", "LaunchStar", "ChikuwaBlock", "BlinkBlock", "BreakableBlock", "Coin", "HookAnchor", "SinkingFloor", "SeesawFloor", "DashPanel", "IceFloor", "TimedSwitch", "AppearingFloor", "Switch", "EventReceiver", "HookPullBlock", "OneWayFloor", "LiquidLevel", "MagmaHazard", "MagmaGeyser", "ChainCollapseFloor", "RotatingFloor", "RotatingPillar", "PhaseFlipFloor", "FireCannon", "StageGate", "LaserEmitter", "LaserNode" };
    const char* gimmickTypeLabels[] = {
        "通常",
        "移動床",
        "トランポリン",
        "スターランチャー",
        "ちくわブロック",
        "点滅ブロック",
        "破壊ブロック",
        "コイン",
        "フックアンカー",
        "沈む床",
        "シーソー床",
        "ダッシュパネル",
        "氷の床",
        "時限スイッチ床",
        "出現床",
        "汎用スイッチ",
        "イベント受信ギミック",
        "フックで引っ張るブロック",
        "一方通行床",
        "水位・マグマ上下",
        "マグマダメージ床",
        "周期式マグマ噴出口",
        "連鎖崩れ床",
        "回転床",
        "回転柱",
        "順番反転床",
        "火球砲台",
        "ステージゲート",
        "レーザー発生器",
        "レーザー接続ノード"
    };
    std::string currentType = selectedObject->GetGimmickType();

    int currentIndex = 0;
    for (int i = 0; i < IM_ARRAYSIZE(gimmickTypes); i++) {
        if (currentType == gimmickTypes[i]) {
            currentIndex = i;
            break;
        }
    }

    if (ImGui::Combo("ギミックの種類", &currentIndex, gimmickTypeLabels, IM_ARRAYSIZE(gimmickTypeLabels))) {
        std::string selectedGimmickType = gimmickTypes[currentIndex];
        selectedObject->SetGimmickType(selectedGimmickType);
        
        if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
        selectedObject->param_->gimmickType = selectedGimmickType;
        
        // 各ギミックに合わせた初期状態（エディタ上のデフォルト初期値）を設定
        if (selectedGimmickType == "BreakableBlock") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_BreakableBlock");
            selectedObject->SetModel("Stages/bomb_break_block");
            selectedObject->SetTexture("Resources/3DModel/Stages/bomb_break_block/bomb_break_block_albedo.png");
            selectedObject->SetEnableNormalMap(true);
            selectedObject->SetNormalMap("Resources/3DModel/Stages/bomb_break_block/bomb_break_block_normal.png");
            selectedObject->SetOrmMap("Resources/3DModel/Stages/bomb_break_block/bomb_break_block_orm.png");
            selectedObject->SetMaterialType(0);
            selectedObject->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
            selectedObject->SetMetallic(0.0f);
            selectedObject->SetRoughness(0.72f);
            selectedObject->SetEnableEnvMap(false);
            selectedObject->SetEmissive(1.0f);
            selectedObject->SetScale({ 1.0f, 1.0f, 1.0f });
            
            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);
            
            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "Coin") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_Coin");
            selectedObject->SetModel("Primitives/sphere");
            selectedObject->SetColor({ 1.0f, 0.9f, 0.0f, 1.0f }); // ゴールドイエロー
            selectedObject->SetScale({ 0.6f, 0.6f, 0.15f }); // 薄いコインの形
            
            selectedObject->SetCollisionAttribute(CollisionAttribute::kTrigger);
            selectedObject->SetCollisionMask(CollisionAttribute::kPlayer);
            
            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kSphere;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
            selectedObject->SetCollisionRadius(1.0f);
        }
        else if (selectedGimmickType == "HookAnchor") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_HookAnchor");
            selectedObject->SetModel("Primitives/sphere");
            selectedObject->SetColor({ 0.2f, 0.85f, 1.0f, 1.0f });
            selectedObject->SetScale({ 1.2f, 1.2f, 1.2f });

            selectedObject->SetCollisionAttribute(CollisionAttribute::kHookAnchor);
            selectedObject->SetCollisionMask(CollisionAttribute::kPlayer);

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kSphere;
            colConfig.size = { 2.5f, 2.5f, 2.5f };
            selectedObject->SetColliderConfig(colConfig);
            selectedObject->SetCollisionRadius(2.5f);
        }
        else if (selectedGimmickType == "LaunchStar") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_LaunchStar");
            selectedObject->SetModel("Gimmicks/star_launch");
            selectedObject->SetColor({ 1.0f, 0.92f, 0.28f, 1.0f });
            selectedObject->SetEmissive(2.2f);
            selectedObject->SetRoughness(0.34f);
            selectedObject->SetMetallic(0.12f);
            selectedObject->SetScale({ 1.0f, 1.0f, 1.0f });
            selectedObject->param_->moveAmount = 52.0f;
            selectedObject->param_->jumpPower = 14.0f;
            selectedObject->param_->speed = 38.0f;

            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.35f, 0.35f, 1.35f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "SinkingFloor") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_SinkingFloor");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.55f, 0.75f, 1.0f, 1.0f });
            selectedObject->SetScale({ 1.0f, 1.0f, 1.0f });

            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "SeesawFloor") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_SeesawFloor");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.9f, 0.75f, 0.35f, 1.0f });
            selectedObject->SetScale({ 4.0f, 0.35f, 1.4f });

            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "DashPanel") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_DashPanel");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetMaterialType(24);
            selectedObject->SetBlendMode(BlendMode::kNone);
            selectedObject->SetColor({ 0.25f, 0.95f, 1.0f, 1.0f });
            selectedObject->SetRoughness(0.62f);
            selectedObject->SetMetallic(0.56f);
            selectedObject->SetEmissive(1.0f);
            selectedObject->SetTextureTiling({ 1.0f, 1.0f });
            selectedObject->SetAutoTextureTiling(false);
            selectedObject->SetScale({ 2.0f, 0.25f, 1.2f });

            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "IceFloor") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_IceFloor");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.65f, 0.9f, 1.0f, 0.9f });
            selectedObject->SetScale({ 1.0f, 1.0f, 1.0f });

            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "TimedSwitch") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_TimedSwitch");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.25f, 0.75f, 1.0f, 1.0f });
            selectedObject->SetScale({ 1.5f, 0.25f, 1.5f });

            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "AppearingFloor") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_AppearingFloor");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.55f, 1.0f, 0.7f, 1.0f });
            selectedObject->SetScale({ 2.0f, 0.35f, 2.0f });

            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "Switch") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_Switch");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.25f, 0.75f, 1.0f, 1.0f });
            selectedObject->SetScale({ 1.5f, 0.25f, 1.5f });

            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->switchMode = 0;
            selectedObject->param_->interval = 3.0f;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "EventReceiver") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_EventReceiver");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.65f, 1.0f, 0.65f, 1.0f });
            selectedObject->SetScale({ 2.0f, 0.35f, 2.0f });

            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->actionMode = 0;
            selectedObject->param_->moveAmount = 10.0f;
            selectedObject->param_->moveSpeed = 6.0f;
            selectedObject->param_->startActive = false;
            selectedObject->param_->returnOnOff = true;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "HookPullBlock") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_HookPullBlock");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.55f, 0.85f, 1.0f, 1.0f });
            selectedObject->SetScale({ 1.0f, 1.0f, 1.0f });

            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);
            selectedObject->SetStatic(false);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->speed = 42.0f;
            selectedObject->param_->gravity = 50.0f;
            selectedObject->param_->maxFallSpeed = 60.0f;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "OneWayFloor") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_OneWayFloor");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.85f, 0.9f, 0.65f, 0.9f });
            selectedObject->SetScale({ 2.5f, 0.22f, 2.5f });

            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "LiquidLevel") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_LiquidLevel");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.45f, 0.85f, 1.0f, 0.65f });
            selectedObject->SetScale({ 4.0f, 0.08f, 4.0f });
            selectedObject->SetMaterialType(8);
            selectedObject->SetCollisionAttribute(0);
            selectedObject->SetCollisionMask(0);
            selectedObject->SetStatic(false);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->colorType = 0;
            selectedObject->param_->moveAmount = 6.0f;
            selectedObject->param_->moveSpeed = 3.0f;
            selectedObject->param_->startActive = false;
            selectedObject->param_->returnOnOff = true;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "MagmaHazard") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_MagmaHazard");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 1.0f, 0.24f, 0.015f, 1.0f });
            selectedObject->SetMaterialType(9);
            selectedObject->SetEmissive(1.8f);
            selectedObject->SetRoughness(0.38f);
            selectedObject->SetMetallic(0.0f);
            selectedObject->SetScale({ 4.0f, 0.2f, 4.0f });
            selectedObject->SetCollisionAttribute(CollisionAttribute::kTrigger);
            selectedObject->SetCollisionMask(CollisionAttribute::kPlayer);
            selectedObject->SetStatic(true);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->speed = 12.0f;
            selectedObject->param_->interval = 0.8f;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "MagmaGeyser") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_MagmaGeyser");
            selectedObject->SetModel("Stages/magma_vent");
            selectedObject->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
            selectedObject->SetMaterialType(0);
            selectedObject->SetEmissive(1.0f);
            selectedObject->SetRoughness(0.72f);
            selectedObject->SetMetallic(0.08f);
            selectedObject->SetScale({ 1.0f, 1.0f, 1.0f });
            selectedObject->SetCollisionAttribute(0);
            selectedObject->SetCollisionMask(0);
            selectedObject->SetStatic(false);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->speed = 4.0f;
            selectedObject->param_->interval = 2.6f;
            selectedObject->param_->shakeDuration = 1.35f;
            selectedObject->param_->fallDuration = 1.15f;
            selectedObject->param_->moveAmount = 9.5f;
            selectedObject->param_->detectionRange = 2.15f;
            selectedObject->param_->gravity = 5.5f;
            selectedObject->param_->jumpPower = 15.0f;
            selectedObject->param_->maxFallSpeed = 110.0f;
            selectedObject->param_->moveSpeed = 0.0f;
            selectedObject->param_->startActive = true;
            selectedObject->param_->returnOnOff = true;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kCylinder;
            colConfig.center = { 0.0f, 5.47f, 0.0f };
            colConfig.size = { 2.15f, 4.75f, 2.15f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "ChainCollapseFloor") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_ChainCollapseFloor");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.75f, 0.92f, 1.0f, 0.82f });
            selectedObject->SetScale({ 2.0f, 0.25f, 2.0f });
            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);
            selectedObject->SetStatic(false);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->shakeDuration = 0.45f;
            selectedObject->param_->fallDuration = 1.4f;
            selectedObject->param_->interval = 0.18f;
            selectedObject->param_->gravity = 48.0f;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "RotatingFloor") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_RotatingFloor");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.95f, 0.65f, 0.35f, 1.0f });
            selectedObject->SetScale({ 3.0f, 0.3f, 1.2f });
            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);
            selectedObject->SetStatic(false);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->speed = 45.0f;
            selectedObject->param_->actionMode = 1;
            selectedObject->param_->startActive = true;
            selectedObject->param_->returnOnOff = true;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "RotatingPillar") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_RotatingPillar");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.95f, 0.65f, 0.35f, 1.0f });
            selectedObject->SetScale({ 0.75f, 3.0f, 0.75f });
            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);
            selectedObject->SetStatic(false);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->speed = 60.0f;
            selectedObject->param_->actionMode = 1;
            selectedObject->param_->startActive = true;
            selectedObject->param_->returnOnOff = true;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "PhaseFlipFloor") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_PhaseFlipFloor");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.35f, 0.75f, 1.0f, 1.0f });
            selectedObject->SetScale({ 2.0f, 0.25f, 2.0f });
            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);
            selectedObject->SetStatic(false);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->colorType = 0;
            selectedObject->param_->maxCount = 3;
            selectedObject->param_->interval = 1.0f;
            selectedObject->param_->startActive = true;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "FireCannon") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_FireCannon");
            selectedObject->SetModel("Primitives/cube");
            selectedObject->SetColor({ 0.22f, 0.11f, 0.08f, 1.0f });
            selectedObject->SetBlendMode(BlendMode::kNormal);
            selectedObject->SetMaterialType(0);
            selectedObject->SetEmissive(1.1f);
            selectedObject->SetScale({ 0.55f, 0.55f, 1.1f });
            selectedObject->SetCollisionAttribute(CollisionAttribute::kGround);
            selectedObject->SetCollisionMask(0b11111111);
            selectedObject->SetStatic(false);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->speed = 13.0f;
            selectedObject->param_->interval = 1.35f;
            selectedObject->param_->moveAmount = 0.55f;
            selectedObject->param_->moveSpeed = 360.0f;
            selectedObject->param_->detectionRange = 45.0f;
            selectedObject->param_->actionMode = 1;
            selectedObject->param_->startActive = true;
            selectedObject->param_->returnOnOff = true;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "StageGate") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_StageGate");
            selectedObject->SetModel("Gimmicks/portal_surface");
            selectedObject->SetColor({ 0.35f, 0.75f, 1.0f, 1.0f });
            selectedObject->SetBlendMode(BlendMode::kNormal);
            selectedObject->SetMaterialType(22);
            selectedObject->SetEmissive(1.8f);
            selectedObject->SetEnableEnvMap(false);
            selectedObject->SetScale({ 1.4f, 1.4f, 1.4f });
            selectedObject->SetCollisionAttribute(CollisionAttribute::kTrigger);
            selectedObject->SetCollisionMask(CollisionAttribute::kPlayer);
            selectedObject->SetStatic(false);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->gimmickType = "StageGate";
            selectedObject->param_->actionMode = 0;
            selectedObject->param_->targetScene = "SELECT";
            selectedObject->param_->startActive = true;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kSphere;
            colConfig.size = { 4.0f, 4.0f, 4.0f };
            selectedObject->SetColliderConfig(colConfig);
            selectedObject->SetCollisionRadius(4.0f);
        }
        else if (selectedGimmickType == "LaserEmitter") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_LaserEmitter");
            selectedObject->SetModel("Primitives/cube");
            selectedObject->SetColor({ 1.0f, 0.08f, 0.05f, 0.9f });
            selectedObject->SetBlendMode(BlendMode::kAdd);
            selectedObject->SetMaterialType(12);
            selectedObject->SetTexture("Resources/sprite/common/white.png");
            selectedObject->SetEmissive(6.0f);
            selectedObject->SetScale({ 0.25f, 0.25f, 1.0f });
            selectedObject->SetCollisionAttribute(CollisionAttribute::kTrigger);
            selectedObject->SetCollisionMask(CollisionAttribute::kPlayer);
            selectedObject->SetStatic(false);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->speed = 10.0f;
            selectedObject->param_->interval = 0.7f;
            selectedObject->param_->moveAmount = 0.14f;
            selectedObject->param_->startActive = true;
            selectedObject->param_->returnOnOff = true;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "LaserNode") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_LaserNode");
            selectedObject->SetModel("Primitives/sphere");
            selectedObject->SetColor({ 1.0f, 0.18f, 0.08f, 1.0f });
            selectedObject->SetBlendMode(BlendMode::kAdd);
            selectedObject->SetMaterialType(3);
            selectedObject->SetTexture("Resources/sprite/common/white.png");
            selectedObject->SetEmissive(3.5f);
            selectedObject->SetScale({ 0.35f, 0.35f, 0.35f });
            selectedObject->SetCollisionAttribute(0);
            selectedObject->SetCollisionMask(0);
            selectedObject->SetStatic(false);

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->speed = 10.0f;
            selectedObject->param_->interval = 0.7f;
            selectedObject->param_->moveAmount = 0.14f;
            selectedObject->param_->startActive = true;
            selectedObject->param_->returnOnOff = true;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kSphere;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
            selectedObject->SetCollisionRadius(1.0f);
        }
        else if (selectedGimmickType == "Default") {
            selectedObject->SetClassName("Default");
            selectedObject->SetName("Cube");
        }
        else {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_" + selectedGimmickType);
        }
    }
    
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("ロード時に生成されるギミッククラスを指定します。");
#endif
}

