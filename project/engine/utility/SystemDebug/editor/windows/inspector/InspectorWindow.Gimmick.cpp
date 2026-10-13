#include "engine/utility/SystemDebug/editor/windows/inspector/InspectorWindow.h"
#include "DebugEditor.h"
#include "Object3d.h"
#include "imgui.h"

void InspectorWindow::DrawGimmickTypeSelector() {
#ifdef USE_IMGUI
    // このUIは配置済みObjectの種別と初期見た目を直接変更します。
    // Factoryで再生成する経路ではないため、モデル・当たり判定・paramの初期値をここで揃えます。
    Object3d* selectedObject = editor_->GetSelectedObject();
    if (!selectedObject) return;

    // 型名はScene JSONにも保存される識別子です。表示名との件数・順序の不整合は
    // 調査MDに記録済みで、ここでは既存データの挙動を変えずに残します。
    const char* gimmickTypes[] = {
        "Default", "MovingFloor", "HazardRideFloor", "Trampoline", "ChikuwaBlock",
        "BlinkBlock", "BreakableBlock", "Coin", "SinkingFloor", "SeesawFloor",
        "DashPanel", "TimedSwitch", "AppearingFloor", "Switch", "EventReceiver",
        "ArenaEncounter", "GameplayVolume", "CopyMemoryStation", "PrismBarrier",
        "HookPullBlock", "LiquidLevel", "MagmaHazard", "MagmaGeyser",
        "ChainCollapseFloor", "RotatingFloor", "RotatingPillar", "PhaseFlipFloor",
        "FireCannon", "StageGate"};
    const char* gimmickTypeLabels[] = {
        "通常",
        "移動床",
        "妨害付き輸送床",
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
        "中ボス遭遇管理",
        "ゲームプレイボリューム",
        "コピー記憶台",
        "プリズム障壁",
        "フックで引っ張るブロック",
        "一方通行床",
        "水位・マグマ上下",
        "マグマダメージ床",
        "周期式マグマ噴出口",
        "落下する棘",
        "連鎖崩れ床",
        "回転床",
        "回転柱",
        "順番反転床",
        "火球砲台",
        "ステージゲート",
        "レーザー発生器",
        "レーザー接続ノード"
    };
    // 現在の保存値から選択位置を探します。未登録の型は先頭表示のままにします。
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

        // 実行時のギミック型判定とエディターの種別表示が食い違わないよう、paramにも反映します。
        if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
        selectedObject->param_->gimmickType = selectedGimmickType;

        // 以下はギミック別の配置直後の初期状態です。床系は接地用、取得物と領域系は
        // プレイヤーを検知するTriggerを設定し、個別の動きに使う値だけparamへ書きます。
        if (selectedGimmickType == "HazardRideFloor") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_HazardRideFloor");
            selectedObject->SetModel("Stages/star_garden_ride_platform");
            selectedObject->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
            selectedObject->SetEmissive(1.08f);
            selectedObject->SetRoughness(0.48f);
            selectedObject->SetMetallic(0.12f);
            selectedObject->SetScale({ 1.0f, 1.0f, 1.0f });
            selectedObject->SetCollisionAttribute(CollisionCategories::Get("ground"));
            selectedObject->SetCollisionMask(CollisionCategories::GetDefaultGameplayMask());
            selectedObject->SetStatic(false);

            selectedObject->param_->speed = 7.5f;
            selectedObject->param_->moveAmount = 46.0f;
            selectedObject->param_->shakeDuration = 0.55f;
            selectedObject->param_->interval = 0.7f;
            selectedObject->param_->fallDuration = 2.0f;
            selectedObject->param_->gravity = 38.0f;
            selectedObject->param_->maxCount = 4;
            selectedObject->param_->actionMode = 0;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.center = { 0.0f, -0.55f, 0.0f };
            colConfig.size = { 6.8f, 0.65f, 5.6f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "BreakableBlock") {
            // 破壊対象はテクスチャ群と通常材質を指定し、床としての衝突を持たせます。
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
            
            selectedObject->SetCollisionAttribute(CollisionCategories::Get("ground"));
            selectedObject->SetCollisionMask(CollisionCategories::GetDefaultGameplayMask());
            
            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "Coin") {
            // コインは物理的な床にせず、PlayerとのTrigger接触だけを受けます。
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_Coin");
            selectedObject->SetModel("Primitives/sphere");
            selectedObject->SetColor({ 1.0f, 0.9f, 0.0f, 1.0f }); // ゴールドイエロー
            selectedObject->SetScale({ 0.6f, 0.6f, 0.15f }); // 薄いコインの形
            
            selectedObject->SetCollisionAttribute(CollisionCategories::Get("trigger"));
            selectedObject->SetCollisionMask(CollisionCategories::Get("player"));
            
            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kSphere;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
            selectedObject->SetCollisionRadius(1.0f);
        }
        else if (selectedGimmickType == "SinkingFloor") {
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_SinkingFloor");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.55f, 0.75f, 1.0f, 1.0f });
            selectedObject->SetScale({ 1.0f, 1.0f, 1.0f });

            selectedObject->SetCollisionAttribute(CollisionCategories::Get("ground"));
            selectedObject->SetCollisionMask(CollisionCategories::GetDefaultGameplayMask());

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

            selectedObject->SetCollisionAttribute(CollisionCategories::Get("ground"));
            selectedObject->SetCollisionMask(CollisionCategories::GetDefaultGameplayMask());

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "DashPanel") {
            // ダッシュ板の見た目は専用材質で、足場としての接地判定は維持します。
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

            selectedObject->SetCollisionAttribute(CollisionCategories::Get("ground"));
            selectedObject->SetCollisionMask(CollisionCategories::GetDefaultGameplayMask());

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

            selectedObject->SetCollisionAttribute(CollisionCategories::Get("ground"));
            selectedObject->SetCollisionMask(CollisionCategories::GetDefaultGameplayMask());

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

            selectedObject->SetCollisionAttribute(CollisionCategories::Get("ground"));
            selectedObject->SetCollisionMask(CollisionCategories::GetDefaultGameplayMask());

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "Switch") {
            // スイッチのモードと間隔は配置ごとに編集できる初期値として保存します。
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_Switch");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.25f, 0.75f, 1.0f, 1.0f });
            selectedObject->SetScale({ 1.5f, 0.25f, 1.5f });

            selectedObject->SetCollisionAttribute(CollisionCategories::Get("ground"));
            selectedObject->SetCollisionMask(CollisionCategories::GetDefaultGameplayMask());

            if (!selectedObject->param_.has_value()) selectedObject->param_.emplace();
            selectedObject->param_->switchMode = 0;
            selectedObject->param_->interval = 3.0f;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "EventReceiver") {
            // 受信機は外部イベントが来るまで停止し、移動量と戻り方を配置値として持ちます。
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_EventReceiver");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.65f, 1.0f, 0.65f, 1.0f });
            selectedObject->SetScale({ 2.0f, 0.35f, 2.0f });

            selectedObject->SetCollisionAttribute(CollisionCategories::Get("ground"));
            selectedObject->SetCollisionMask(CollisionCategories::GetDefaultGameplayMask());

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
            // フックで引く対象なので静的物体にせず、移動と落下の値を初期化します。
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_HookPullBlock");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.55f, 0.85f, 1.0f, 1.0f });
            selectedObject->SetScale({ 1.0f, 1.0f, 1.0f });

            selectedObject->SetCollisionAttribute(CollisionCategories::Get("ground"));
            selectedObject->SetCollisionMask(CollisionCategories::GetDefaultGameplayMask());
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
        else if (selectedGimmickType == "LiquidLevel") {
            // 水位表示は水面材質を使い、地形との衝突には参加させません。
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
            // マグマ床はPlayerへのTriggerを持ち、ダメージ周期を配置値にします。
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_MagmaHazard");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 1.0f, 0.24f, 0.015f, 1.0f });
            selectedObject->SetMaterialType(9);
            selectedObject->SetEmissive(1.8f);
            selectedObject->SetRoughness(0.38f);
            selectedObject->SetMetallic(0.0f);
            selectedObject->SetScale({ 4.0f, 0.2f, 4.0f });
            selectedObject->SetCollisionAttribute(CollisionCategories::Get("trigger"));
            selectedObject->SetCollisionMask(CollisionCategories::Get("player"));
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
            // 噴出口本体の衝突属性は0にし、円柱Colliderの形状を別途定義します。
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
        else if (selectedGimmickType == "ArenaEncounter") {
            // 遭遇管理は見える地形ではなく、Playerが侵入する領域として置きます。
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_ArenaEncounter");
            selectedObject->SetModel("Primitives/cube");
            selectedObject->SetColor({ 0.42f, 0.24f, 1.0f, 0.22f });
            selectedObject->SetScale({ 1.0f, 1.0f, 1.0f });
            selectedObject->SetCastShadow(false);
            selectedObject->SetCollisionAttribute(CollisionCategories::Get("trigger"));
            selectedObject->SetCollisionMask(CollisionCategories::Get("player"));
            selectedObject->SetStatic(false);

            selectedObject->param_->maxCount = 4;
            selectedObject->param_->shakeDuration = 0.72f;
            selectedObject->param_->startActive = true;
            selectedObject->param_->returnOnOff = false;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.center = { 0.0f, 1.5f, 0.0f };
            colConfig.size = { 3.0f, 3.0f, 8.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "GameplayVolume") {
            // 汎用領域は入退場イベントと再発火の設定を持ち、影は落としません。
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_GameplayVolume");
            selectedObject->SetModel("Primitives/cube");
            selectedObject->SetColor({ 0.15f, 0.75f, 1.0f, 0.3f });
            selectedObject->SetScale({ 3.0f, 3.0f, 3.0f });
            selectedObject->SetBlendMode(BlendMode::kNormal);
            selectedObject->SetCastShadow(false);
            selectedObject->SetCollisionAttribute(CollisionCategories::Get("trigger"));
            selectedObject->SetCollisionMask(CollisionCategories::Get("player"));
            selectedObject->SetStatic(false);

            selectedObject->param_->volumeMode = 0;
            selectedObject->param_->volumePayload.clear();
            selectedObject->param_->volumeTriggerOnce = true;
            selectedObject->param_->volumeTriggerOnExit = false;
            selectedObject->param_->volumeRearmDelay = 0.0f;
            selectedObject->param_->startActive = true;
            selectedObject->param_->returnOnOff = false;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.center = { 0.0f, 0.0f, 0.0f };
            colConfig.size = { 1.0f, 1.0f, 1.0f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "CopyMemoryStation") {
            // 台座の見た目と4枠までのコピー能力候補は、Sceneのparamを初期値にします。
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_CopyMemoryStation");
            selectedObject->SetModel("Gimmicks/copy_memory_station");
            selectedObject->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
            selectedObject->SetScale({ 1.0f, 1.0f, 1.0f });
            selectedObject->SetMaterialType(0);
            selectedObject->SetEmissive(1.0f);
            selectedObject->SetRoughness(0.34f);
            selectedObject->SetMetallic(0.18f);
            selectedObject->SetCastShadow(true);
            selectedObject->SetCollisionAttribute(CollisionCategories::Get("trigger"));
            selectedObject->SetCollisionMask(CollisionCategories::Get("player"));
            selectedObject->SetStatic(false);

            selectedObject->param_->copyMemoryTypeA = "FireSlime";
            selectedObject->param_->copyMemoryTypeB = "ThunderSlime";
            selectedObject->param_->copyMemoryTypeC = "WindSlime";
            selectedObject->param_->copyMemoryActivationRadius = 0.9f;
            selectedObject->param_->copyMemoryUnlimitedDuration = true;
            selectedObject->param_->startActive = true;

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.center = { 0.0f, 0.95f, 0.0f };
            colConfig.size = { 3.1f, 1.6f, 1.85f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "PrismBarrier") {
            // ポータル材質の歪みはRenderer側の補助パラメータも同時に初期化します。
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_PrismBarrier");
            selectedObject->SetModel("Gimmicks/portal_surface");
            selectedObject->SetMaterialType(22);
            selectedObject->SetBlendMode(BlendMode::kNormal);
            selectedObject->SetColor({ 0.30f, 0.86f, 1.0f, 0.84f });
            selectedObject->SetEmissive(2.4f);
            selectedObject->SetScale({ 8.0f, 3.0f, 1.0f });
            selectedObject->SetCastShadow(false);
            selectedObject->SetEnableEnvMap(false);
            selectedObject->SetCollisionAttribute(CollisionCategories::Get("ground"));
            selectedObject->SetCollisionMask(0xffffffffu);
            selectedObject->SetStatic(false);

            selectedObject->param_->moveSpeed = 0.42f;
            selectedObject->param_->interval = 2.4f;
            selectedObject->param_->startActive = false;
            selectedObject->param_->returnOnOff = true;

            if (auto* renderer = selectedObject->GetMeshRenderer(); renderer && renderer->GetWaterParamData()) {
                renderer->GetWaterParamData()->effectType = 3.0f;
                renderer->GetWaterParamData()->effectIntensity = 1.78f;
            }

            Object3d::ColliderConfig colConfig;
            colConfig.type = ColliderType::kOBB;
            colConfig.center = { 0.0f, 0.0f, 0.0f };
            colConfig.size = { 1.0f, 1.0f, 0.45f };
            selectedObject->SetColliderConfig(colConfig);
        }
        else if (selectedGimmickType == "ChainCollapseFloor") {
            // 崩落の遅延と連鎖間隔は別値なので、どちらも配置時に明示します。
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_ChainCollapseFloor");
            selectedObject->SetModel("Stages/block");
            selectedObject->SetColor({ 0.75f, 0.92f, 1.0f, 0.82f });
            selectedObject->SetScale({ 2.0f, 0.25f, 2.0f });
            selectedObject->SetCollisionAttribute(CollisionCategories::Get("ground"));
            selectedObject->SetCollisionMask(CollisionCategories::GetDefaultGameplayMask());
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
            selectedObject->SetCollisionAttribute(CollisionCategories::Get("ground"));
            selectedObject->SetCollisionMask(CollisionCategories::GetDefaultGameplayMask());
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
            selectedObject->SetCollisionAttribute(CollisionCategories::Get("ground"));
            selectedObject->SetCollisionMask(CollisionCategories::GetDefaultGameplayMask());
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
            selectedObject->SetCollisionAttribute(CollisionCategories::Get("ground"));
            selectedObject->SetCollisionMask(CollisionCategories::GetDefaultGameplayMask());
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
            // 砲台は床衝突を持ち、射程・弾速・発射間隔をparamから調整します。
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_FireCannon");
            selectedObject->SetModel("Primitives/cube");
            selectedObject->SetColor({ 0.22f, 0.11f, 0.08f, 1.0f });
            selectedObject->SetBlendMode(BlendMode::kNormal);
            selectedObject->SetMaterialType(0);
            selectedObject->SetEmissive(1.1f);
            selectedObject->SetScale({ 0.55f, 0.55f, 1.1f });
            selectedObject->SetCollisionAttribute(CollisionCategories::Get("ground"));
            selectedObject->SetCollisionMask(CollisionCategories::GetDefaultGameplayMask());
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
            // ゲートは球Triggerで接触を取り、初期遷移先はセレクトへ向けます。
            selectedObject->SetClassName("Gimmick");
            selectedObject->SetName("Gimmick_StageGate");
            selectedObject->SetModel("Gimmicks/portal_surface");
            selectedObject->SetColor({ 0.35f, 0.75f, 1.0f, 1.0f });
            selectedObject->SetBlendMode(BlendMode::kNormal);
            selectedObject->SetMaterialType(22);
            selectedObject->SetEmissive(1.8f);
            selectedObject->SetEnableEnvMap(false);
            selectedObject->SetScale({ 1.4f, 1.4f, 1.4f });
            selectedObject->SetCollisionAttribute(CollisionCategories::Get("trigger"));
            selectedObject->SetCollisionMask(CollisionCategories::Get("player"));
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

