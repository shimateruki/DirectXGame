#define NOMINMAX
#include "game/scene/gameplay/GamePlayScene.h"
#include "engine/utility/SystemDebug/editor/graphics/camera/CameraEditor.h"
#include "CameraManager.h"
#include "DebugConsole.h"
#include "game/actor/enemy/spawn/EnemyFactory.h"
#include "BaseEnemy.h"
#include "game/actor/enemy/boss/prism/EnemyPrismSlime.h"
#include "game/actor/enemy/boss/false_king/EnemyFalseKingSlime.h"
#include "StageManager.h"
#include "SceneManager.h"
#include "GameDataManager.h"
#include "LockOnSystem.h"
#include "game/actor/player/state/PlayerState.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#ifdef USE_IMGUI
#include "imgui.h"
#include "engine/utility/SystemDebug/editor/core/EditorCommandRegistry.h"
#include "IconsFontAwesome5.h"
#endif

// デバッグ操作。USE_IMGUIの条件と、更新開始時に要求を反映する仕組みを維持する。
namespace {
#ifdef USE_IMGUI
constexpr const char* kDebugPrismArenaAnchorName = "Stage1_Collision_V4_PreBossCheckpoint";
constexpr const char* kDebugPrismSlimeName = "Stage1_V4_PrismArena_Boss";
constexpr const char* kDebugGoalEntryAnchorName = "Stage1_Collision_V4_GoalApproach";
constexpr const char* kDebugGoalName = "goal";
constexpr float kDebugTeleportPlayerFloorClearance = 0.78f;
constexpr float kDebugTeleportCameraDistance = 16.0f;
constexpr float kDebugTeleportCameraHeight = 3.2f;
constexpr float kDebugTeleportCameraPitch = 0.32f;

const char* GetDebugMorphDisplayName(Player::EnemyMorphType type) {
    // 内部の変身種別を、デバッグUIで識別できる日本語名へ変換する。
    switch (type) {
    case Player::EnemyMorphType::Slime:
        return "ピンクスライム";
    case Player::EnemyMorphType::Bomber:
        return "ボムスライム";
    case Player::EnemyMorphType::FireSlime:
        return "炎スライム";
    case Player::EnemyMorphType::ThunderSlime:
        return "雷スライム";
    case Player::EnemyMorphType::WindSlime:
        return "風スライム";
    case Player::EnemyMorphType::None:
        return "通常スライム";
    default:
        return "その他";
    }
}
#endif
} // namespace
void GamePlayScene::DrawImGui() {
#ifdef USE_IMGUI
    ImGui::Text(ICON_FA_INFO_CIRCLE " Scene: GamePlay");
    ImGui::Separator();

    if (ImGui::CollapsingHeader(ICON_FA_MAP " Stage Selection", ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto& stages = StageManager::GetInstance()->GetStages();
        int currentIndex = StageManager::GetInstance()->GetCurrentStageIndex();

        ImGui::Text("ステージJSON切り替え");
        constexpr int kStageButtonCount = 5;
        for (int stageIndex = 0; stageIndex < kStageButtonCount; ++stageIndex) {
            const bool registered = stageIndex < static_cast<int>(stages.size());
            const bool isCurrent = stageIndex == currentIndex;
            if (!registered || isCurrent) {
                ImGui::BeginDisabled();
            }

            std::string label = std::string(ICON_FA_PLAY) + " Stage " + std::to_string(stageIndex + 1);
            if (ImGui::Button(label.c_str(), ImVec2(112.0f, 30.0f)) && registered) {
                StageManager::GetInstance()->SetCurrentStage(stageIndex);
                SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
            }

            if (!registered || isCurrent) {
                ImGui::EndDisabled();
            }

            if (stageIndex < kStageButtonCount - 1) {
                ImGui::SameLine();
            }
        }

        if (currentIndex >= 0 && currentIndex < static_cast<int>(stages.size())) {
            const StageData& currentStage = stages[currentIndex];
            ImGui::TextDisabled("現在: %s", currentStage.name.c_str());
            ImGui::TextDisabled("3D: %s", currentStage.levelPath.c_str());
            ImGui::TextDisabled("Sprite: %s", currentStage.spritePath.c_str());
        }
        ImGui::Separator();

        std::vector<const char*> stageNames;
        for (const auto& s : stages)
            stageNames.push_back(s.name.c_str());

        if (ImGui::Combo("Select Stage", &currentIndex, stageNames.data(), (int)stageNames.size())) {
            StageManager::GetInstance()->SetCurrentStage(currentIndex);
        }

        if (ImGui::Button(ICON_FA_SYNC " Reload Scene with Selected Stage", ImVec2(-1, 30))) {
            SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
        }
    }

    if (ImGui::CollapsingHeader(ICON_FA_TROPHY " Stage Status", ImGuiTreeNodeFlags_DefaultOpen)) {
        int currentStage = StageManager::GetInstance()->GetCurrentStageIndex();
        bool isCleared = GameDataManager::GetInstance()->IsStageCleared(currentStage);

        ImGui::Text("Stage ID: %d", currentStage);
        ImGui::SameLine();
        if (isCleared)
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "[ CLEARED ]");
        else
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "[ NOT CLEARED ]");

        ImGui::Separator();
        ImGui::Text("Star Coins (Session):");
        for (int i = 0; i < 3; i++) {
            ImGui::SameLine();
            if (sessionStarCoins_[i]) {
                ImGui::TextColored(ImVec4(1, 0.9f, 0, 1), ICON_FA_STAR);
            } else {
                ImGui::TextDisabled(ICON_FA_STAR);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Star Coin %d", i);
        }
    }

    if (ImGui::CollapsingHeader(ICON_FA_USER " Player Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (player_) {
            Vector3 pos = player_->GetTranslate();
            ImGui::Text("Position: (%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z);

            float hp = player_->GetHp();
            float maxHp = player_->GetMaxHp();
            ImGui::ProgressBar(hp / maxHp, ImVec2(-1, 0), "HP");

            // --- Debug HP Control ---
            if (ImGui::Button(ICON_FA_AMBULANCE " HPを1にする (Debug)")) {
                if (player_->param_.has_value()) {
                    player_->param_->hp = 1.0f;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_SKULL " HPを0にする (Debug)")) {
                if (player_->param_.has_value()) {
                    player_->param_->hp = 0.0f;
                }
            }

            ImGui::Separator();
            int lives = GameDataManager::GetInstance()->GetLives();
            int coins = GameDataManager::GetInstance()->GetCoins();
            ImGui::Text(ICON_FA_HEART " Remaining Lives: %d", lives);
            ImGui::Text(ICON_FA_COINS " Coins: %d / 100", coins);
            if (ImGui::Button("Reset Lives to 3")) {
                GameDataManager::GetInstance()->ResetLives();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset Coins")) {
                GameDataManager::GetInstance()->ResetCoins();
            }

            ImGui::Separator();
            ImGui::Text("デバッグ移動");
            const bool debugRuntimePlaying = GetSceneManager() && GetSceneManager()->IsPlaying();
            const bool teleportRequestPending = pendingDebugTeleportDestination_ != DebugTeleportDestination::None;
            const bool teleportStateReady =
                (debugRuntimePlaying || EditorCommandRegistry::GetInstance()->CanExecute(EditorCommandId::Play)) &&
                !player_->isDead && !isGoal_ && !teleportRequestPending;
            const bool hasPrismAnchor = FindDebugObjectByName(kDebugPrismArenaAnchorName) != nullptr;
            const bool hasGoalAnchor = FindDebugObjectByName(kDebugGoalEntryAnchorName) != nullptr;
            const float teleportSpacing = ImGui::GetStyle().ItemSpacing.x;
            const float teleportButtonWidth =
                (std::max)(130.0f, (ImGui::GetContentRegionAvail().x - teleportSpacing) * 0.5f);

            if (!teleportStateReady || !hasPrismAnchor) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button(ICON_FA_GEM " 中ボス前へ##DebugTeleport", ImVec2(teleportButtonWidth, 32.0f))) {
                QueueDebugTeleport(DebugTeleportDestination::PrismArena);
            }
            if (!teleportStateReady || !hasPrismAnchor) {
                ImGui::EndDisabled();
            }

            ImGui::SameLine();
            if (!teleportStateReady || !hasGoalAnchor) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button(ICON_FA_FLAG_CHECKERED " ゴール前へ##DebugTeleport",
                              ImVec2(teleportButtonWidth, 32.0f))) {
                QueueDebugTeleport(DebugTeleportDestination::GoalApproach);
            }
            if (!teleportStateReady || !hasGoalAnchor) {
                ImGui::EndDisabled();
            }
            if (!debugRuntimePlaying) {
                ImGui::TextDisabled("編集状態を保持して再生を開始し、移動します。");
            }
            if (!hasPrismAnchor) {
                ImGui::TextDisabled("中ボス前の移動先がありません。ステージ1を開いてください。");
            }
            if (stageEntryPresentationActive_) {
                ImGui::TextDisabled("開始演出中でも、演出を安全に終了してから移動します。");
            } else {
                ImGui::TextDisabled("ステージ1専用。移動時は攻撃・運搬・変身・ロックオン状態を解除します。");
            }

            ImGui::Separator();
            ImGui::Text("デバッグ変身（時間制限なし）");
            ImGui::TextDisabled("現在: %s", GetDebugMorphDisplayName(player_->GetEnemyMorphType()));

            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float buttonWidth = (std::max)(90.0f, (ImGui::GetContentRegionAvail().x - spacing * 2.0f) / 3.0f);
            auto drawMorphButton = [this, buttonWidth](const char* label, const char* enemyType) {
                if (ImGui::Button(label, ImVec2(buttonWidth, 30.0f))) {
                    ApplyDebugPlayerMorph(enemyType);
                }
            };

            drawMorphButton("ピンク##DebugMorph", "Slime");
            ImGui::SameLine();
            drawMorphButton("ボム##DebugMorph", "Bomber");
            ImGui::SameLine();
            drawMorphButton("炎##DebugMorph", "FireSlime");

            drawMorphButton("雷##DebugMorph", "ThunderSlime");
            ImGui::SameLine();
            drawMorphButton("風##DebugMorph", "WindSlime");
            ImGui::SameLine();

            const bool hasMorph = player_->IsEnemyMorphed();
            if (!hasMorph) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("通常スライムへ戻す##DebugMorph", ImVec2(-1.0f, 30.0f))) {
                ClearDebugPlayerMorph();
            }
            if (!hasMorph) {
                ImGui::EndDisabled();
            }
            ImGui::TextDisabled("吸収演出を省略し、能力実装済みのスライムは固有技まで直接確認できます。");
        } else {
            ImGui::TextDisabled("Player not found");
        }
    }

    if (ImGui::CollapsingHeader(ICON_FA_GEM " クリスタルスライム Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
        EnemyPrismSlime* prismBoss = FindPrismBossForHUD();
        if (prismBoss) {
            const float bossHp = prismBoss->GetEncounterCurrentHp();
            const float bossMaxHp = prismBoss->GetEncounterMaximumHp();
            char bossHpLabel[64]{};
            std::snprintf(bossHpLabel, sizeof(bossHpLabel), "HP %.0f / %.0f", bossHp, bossMaxHp);
            ImGui::ProgressBar(std::clamp(bossHp / (std::max)(bossMaxHp, 1.0f), 0.0f, 1.0f), ImVec2(-1.0f, 0.0f),
                               bossHpLabel);

            if (ImGui::Button(ICON_FA_SKULL " 撃破演出を確認##DebugDefeatPrismSlime", ImVec2(-1.0f, 32.0f))) {
                prismBoss->TriggerDebugDefeat();
            }
            ImGui::TextDisabled("実際のHPを0にして、撃破演出からバリア解除までを通して再生します。");
        } else {
            ImGui::TextDisabled("中ボス戦の開始後に使用できます。");
        }
    }

    if (ImGui::CollapsingHeader(ICON_FA_CROWN " 偽王スライム Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
        EnemyFalseKingSlime* falseKing = FindFalseKingBossForHUD();
        if (falseKing) {
            const float bossHp = falseKing->GetEncounterCurrentHp();
            const float bossMaxHp = falseKing->GetEncounterMaximumHp();
            char bossHpLabel[64]{};
            std::snprintf(bossHpLabel, sizeof(bossHpLabel), "HP %.0f / %.0f", bossHp, bossMaxHp);
            ImGui::ProgressBar(std::clamp(bossHp / (std::max)(bossMaxHp, 1.0f), 0.0f, 1.0f), ImVec2(-1.0f, 0.0f),
                               bossHpLabel);

            ImGui::Text("現在: 第%d形態 / 封印 %d / 3", falseKing->GetBattlePhase(), falseKing->GetBrokenSealCount());
            ImGui::TextDisabled("見たい区間を選ぶと、実際の形態遷移処理を通して再生します。");

            const bool canEndPhaseOne = falseKing->CanTriggerDebugPhaseTwoTransition();
            if (!canEndPhaseOne)
                ImGui::BeginDisabled();
            if (ImGui::Button(ICON_FA_PLAY " 第一形態を終了（形態移行を見る）##DebugFalseKingPhase2",
                              ImVec2(-1.0f, 34.0f))) {
                falseKing->TriggerDebugPhaseTwoTransition();
            }
            if (!canEndPhaseOne)
                ImGui::EndDisabled();

            const bool canStartInterlude = falseKing->CanTriggerDebugCrownInterlude();
            if (!canStartInterlude)
                ImGui::BeginDisabled();
            if (ImGui::Button(ICON_FA_CROWN " 封印戦を終了（王冠継承・脱出を見る）##DebugFalseKingInterlude",
                              ImVec2(-1.0f, 34.0f))) {
                falseKing->TriggerDebugCrownInterlude();
            }
            if (!canStartInterlude)
                ImGui::EndDisabled();

            const bool canDefeatFinalPhase = falseKing->CanTriggerDebugFinalDefeat();
            if (!canDefeatFinalPhase)
                ImGui::BeginDisabled();
            if (ImGui::Button(ICON_FA_SKULL " 最終形態を撃破（撃破演出を見る）##DebugDefeatFalseKing",
                              ImVec2(-1.0f, 34.0f))) {
                falseKing->TriggerDebugDefeat();
            }
            if (!canDefeatFinalPhase)
                ImGui::EndDisabled();
            ImGui::TextDisabled("上から順に確認できます。王冠継承・脱出は第1形態から直接再生することもできます。");
        } else {
            ImGui::TextDisabled("ステージ3のボス登場後に使用できます。");
        }
    }

    if (ImGui::CollapsingHeader(ICON_FA_GHOST " Scene Events", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button(ICON_FA_PLAY " Start Bridge Drop Movie", ImVec2(-1, 30))) {
            StartBridgeDropMovie();
        }
    }

    if (saveIndicatorOverlay_ &&
        ImGui::CollapsingHeader(ICON_FA_SAVE " Save Indicator", ImGuiTreeNodeFlags_DefaultOpen)) {
        saveIndicatorOverlay_->DrawImGui();
    }

    DrawGoalPresentationEditor();

    ImGui::Separator();
    ImGui::TextDisabled("※この項目は GamePlayScene::DrawImGui() で編集可能です");
#endif
}

#ifdef USE_IMGUI
void GamePlayScene::ApplyDebugPlayerMorph(const char* enemyType) {
    if (!player_ || !object3dCommon_ || !enemyType || enemyType[0] == '\0') {
        return;
    }
    if (player_->isDead || player_->IsCinematicLocked()) {
        DebugConsole::GetInstance()->AddLog("Debug morph skipped: player is unavailable.");
        return;
    }

    // 先にプレイヤー側の参照を解除してから、以前のデバッグ用変身元を破棄します。
    ClearDebugPlayerMorph();

    std::unique_ptr<BaseEnemy> source = EnemyFactory::GetInstance()->CreateEnemy(enemyType, object3dCommon_.get());
    if (!source || source->GetEnemyType().empty()) {
        DebugConsole::GetInstance()->AddLog(std::string("Debug morph create failed: ") + enemyType);
        return;
    }

    source->SetTarget(player_);
    source->SetTranslate(player_->GetWorldPosition());
    source->SetIsVisible(false);
    source->SetCollisionAttribute(0);
    source->SetCollisionMask(0);

    BaseEnemy* sourcePointer = source.get();
    debugPlayerMorphSource_ = std::move(source);
    player_->DebugForceEnemyMorph(sourcePointer);

    if (!player_->IsEnemyMorphed()) {
        debugPlayerMorphSource_.reset();
        DebugConsole::GetInstance()->AddLog(std::string("Debug morph start failed: ") + enemyType);
        return;
    }

    DebugConsole::GetInstance()->AddLog(std::string("Debug morph selected: ") + enemyType);
}

void GamePlayScene::ClearDebugPlayerMorph() {
    // Player本体とデバッグ用の変身元参照を同時に初期化する。
    if (player_) {
        player_->DebugClearEnemyMorph();
    }
    debugPlayerMorphSource_.reset();
}

void GamePlayScene::QueueDebugTeleport(DebugTeleportDestination destination) {
    if (destination == DebugTeleportDestination::None) {
        return;
    }

    // 通常の再生開始を通して編集状態を保存した後、次の更新で移動します。
    if (!GetSceneManager() || !GetSceneManager()->IsPlaying()) {
        if (!EditorCommandRegistry::GetInstance()->Execute(EditorCommandId::Play) || !GetSceneManager() ||
            !GetSceneManager()->IsPlaying()) {
            return;
        }
    }
    pendingDebugTeleportDestination_ = destination;
    DebugConsole::GetInstance()->AddLog("Debug teleport queued for the next gameplay update.");
}

void GamePlayScene::ProcessPendingDebugTeleport() {
    const DebugTeleportDestination destination = pendingDebugTeleportDestination_;
    pendingDebugTeleportDestination_ = DebugTeleportDestination::None;
    if (destination == DebugTeleportDestination::None) {
        return;
    }

    if (!player_ || player_->isDead || isGoal_) {
        DebugConsole::GetInstance()->AddLog("Debug teleport skipped: player is unavailable.");
        return;
    }

    // Play開始直後に押されても、入口演出が次フレームで再開して座標を戻さないよう完了扱いにします。
    if (stageEntryPresentationActive_) {
        FinishStageEntryPresentation();
    }
    stageEntryPresentationPending_ = false;
    stageEntryPresentationCompleted_ = true;
    stageEntryRuntimeWasPlaying_ = true;
    stageEntryPresentationRetryTimer_ = 0.0f;

    switch (destination) {
    case DebugTeleportDestination::PrismArena:
        TeleportPlayerToDebugTarget(kDebugPrismArenaAnchorName, {18.0f, 0.0f, 5.0f}, kDebugPrismSlimeName, "中ボス前");
        break;
    case DebugTeleportDestination::GoalApproach:
        TeleportPlayerToDebugTarget(kDebugGoalEntryAnchorName, {0.0f, 0.0f, 0.0f}, kDebugGoalName, "ゴール前");
        break;
    case DebugTeleportDestination::None:
    default:
        break;
    }
}

Object3d* GamePlayScene::FindDebugObjectByName(const char* objectName) const {
    // デバッグ操作用に現在のシーン配置から名前を解決する。
    if (!objectManager_ || !objectName || objectName[0] == '\0') {
        return nullptr;
    }

    std::vector<Object3d*> objectsToVisit;
    objectsToVisit.reserve(objectManager_->GetObjects().size());
    for (const auto& object : objectManager_->GetObjects()) {
        if (object) {
            objectsToVisit.push_back(object.get());
        }
    }

    while (!objectsToVisit.empty()) {
        Object3d* object = objectsToVisit.back();
        objectsToVisit.pop_back();
        if (object->GetName() == objectName) {
            return object;
        }
        for (Object3d* child : object->GetChildren()) {
            if (child) {
                objectsToVisit.push_back(child);
            }
        }
    }
    return nullptr;
}

bool GamePlayScene::TeleportPlayerToDebugTarget(const char* anchorName, const Vector3& anchorOffset,
                                                const char* facingObjectName, const char* destinationLabel) {
    if (!player_ || player_->isDead || isGoal_) {
        DebugConsole::GetInstance()->AddLog("Debug teleport skipped: player is unavailable.");
        return false;
    }

    Object3d* anchor = FindDebugObjectByName(anchorName);
    if (!anchor) {
        DebugConsole::GetInstance()->AddLog(std::string("Debug teleport anchor not found: ") +
                                            (anchorName ? anchorName : "(null)"));
        return false;
    }

    // 親子階層を含む編集直後のTransformも確定してから、コライダー上面を着地点にします。
    Object3d* anchorRoot = anchor;
    while (anchorRoot->GetParent()) {
        anchorRoot = anchorRoot->GetParent();
    }
    anchorRoot->UpdateWorldMatrix(false);

    Vector3 destination = anchor->GetWorldPosition() + anchorOffset;
    const AABB anchorBounds = anchor->GetAABB();
    if (anchorBounds.max.y > anchorBounds.min.y) {
        destination.y = anchorBounds.max.y + kDebugTeleportPlayerFloorClearance;
    }

    Vector3 facingDirection = {1.0f, 0.0f, 0.0f};
    if (Object3d* facingObject = FindDebugObjectByName(facingObjectName)) {
        facingDirection = facingObject->GetWorldPosition() - destination;
        facingDirection.y = 0.0f;
        const float facingLength =
            std::sqrt(facingDirection.x * facingDirection.x + facingDirection.z * facingDirection.z);
        if (facingLength > 0.001f) {
            facingDirection.x /= facingLength;
            facingDirection.z /= facingLength;
        } else {
            facingDirection = {1.0f, 0.0f, 0.0f};
        }
    }

    // デバッグ移動では進行中の移動演出も明示的に止め、次フレームの座標上書きを防ぎます。
    if (arenaBossIntroActive_) {
        FinishArenaBossIntro();
    }
    // プレイヤー内部の各コントローラーを一括解除し、次フレームの座標上書きを防ぎます。
    player_->DebugPrepareForTeleport();
    debugPlayerMorphSource_.reset();
    player_->SetVelocity({0.0f, 0.0f, 0.0f});
    player_->SetGrounded(false);
    player_->SetTranslate(destination);
    player_->SetRespawnPosition(destination);
    player_->SetMoveYaw(std::atan2(facingDirection.x, facingDirection.z));
    player_->SetIsVisible(true);
    player_->SetIsControlActive(true);
    player_->UpdateLocalMatrix();
    player_->UpdateWorldMatrix();

    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
    if (lockOnSystem_) {
        lockOnSystem_->ResetLockOn(camera);
    }
    player_->SetLockOn(false);
    if (camera) {
        CameraEditor::GetInstance()->SetMode(CameraEditor::Mode::Game);
        camera->EndOverride(0.0f);
        camera->ClearPresentationLayers();
        CameraManager::GetInstance()->SetActiveCamera(camera);
        camera->SetInputEnabled(true);
        camera->SetFollowTarget(player_);
        camera->SetFollowMode(Camera::FollowMode::kAimable);
        camera->SetRotation({kDebugTeleportCameraPitch, std::atan2(facingDirection.x, facingDirection.z), 0.0f});
        camera->SnapToThirdPerson(kDebugTeleportCameraDistance, kDebugTeleportCameraHeight, kDebugTeleportCameraPitch);
        camera->Update(0.0f);
    }

    DebugConsole::GetInstance()->AddLog(std::string("Debug teleport completed: ") +
                                        (destinationLabel ? destinationLabel : "unknown"));
    return true;
}
#endif
