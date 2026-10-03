#include "EnemyAttackPreviewWindow.h"
#include "EnemyAttackPreviewWindow.Constants.h"

#include "BaseEnemy.h"
#include "BaseScene.h"
#include "Camera.h"
#include "CameraManager.h"
#include "engine/utility/SystemDebug/editor/graphics/camera/CameraEditor.h"
#include "BulletManager.h"
#include "CollisionConfig.h"
#include "DebrisEffectManager.h"
#include "EffectPreviewStage.h"
#include "EnemyBomber.h"
#include "game/actor/enemy/spawn/EnemyFactory.h"
#include "game/actor/enemy/boss/false_king/EnemyFalseKingSlime.h"
#include "EnemyGiantSlime.h"
#include "game/actor/enemy/boss/magma/EnemyMagmaSlime.h"
#include "game/actor/enemy/boss/prism/EnemyPrismSlime.h"
#include "EnemySlime.h"
#include "EnemyThunderSlime.h"
#include "EnemyWindSlime.h"
#include "GPUParticleManager.h"
#include "GameRule.h"
#include "EventManager.h"
#include "InputManager.h"
#include "MeshEffectManager.h"
#include "MeshRenderer.h"
#include "Object3d.h"
#include "ParticleSystem.h"
#include "Player.h"
#include "SceneManager.h"
#include "VFXSequencer.h"

#ifdef USE_IMGUI
#include "IconsFontAwesome5.h"
#include "imgui.h"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <iterator>
#include <unordered_set>

using EnemyAttackPreviewConstants::kCopyActions;
using EnemyAttackPreviewConstants::kCopyLabels;
using EnemyAttackPreviewConstants::kEnemyTypes;
using EnemyAttackPreviewConstants::kEnemyLabels;
using EnemyAttackPreviewConstants::kFixedPreviewStep;
using EnemyAttackPreviewConstants::kMinLoopDuration;
using EnemyAttackPreviewConstants::kMaxLoopDuration;
using EnemyAttackPreviewConstants::kDefaultTargetDistanceLimit;
using EnemyAttackPreviewConstants::kPi;

namespace {
#ifdef USE_IMGUI
bool DrawProfileString(const char* label, std::string& value) {
    std::array<char, 512> buffer{};
    const size_t copyLength = (std::min)(value.size(), buffer.size() - 1);
    std::memcpy(buffer.data(), value.data(), copyLength);
    if (!ImGui::InputText(label, buffer.data(), buffer.size())) {
        return false;
    }
    value = buffer.data();
    return true;
}

#endif
}

void EnemyAttackPreviewWindow::DrawPlayerCopySettings() {
#ifdef USE_IMGUI
    ImGui::TextUnformatted("プレイヤー能力プレビュー");
    ImGui::TextWrapped("実モデルと実コピー能力を隔離ステージで再生します。下段で停止・コマ送り・先頭からの再構築ができます。");
    if (!IsCurrentSceneReady() || sceneManager_->IsPlaying()) {
        ImGui::TextUnformatted("シーンを開き、編集モードで使用してください。");
        return;
    }
    bool enabled = enabled_;
    if (ImGui::Checkbox("プレビューを有効化", &enabled)) {
        enabled_ = enabled;
        if (enabled_) CreatePreviewObjects();
        else { ClearPreviewTransientState(); RemovePreviewObjects(); }
    }
    bool changed = ImGui::Combo("コピー形態", &copyTypeIndex_, kCopyLabels, IM_ARRAYSIZE(kCopyLabels));
    changed |= ImGui::Combo("確認する動作", &copyActionIndex_, kCopyActions, IM_ARRAYSIZE(kCopyActions));
    const char* descriptions[] = {
        "左: 追加クリックで3段打撃 / 右: 身を縮めるガード / E: 跳ね上がり突進",
        "左長押し: 炎ブレス / 右: 炎ダッシュ / E: 火球",
        "左長押し: 周囲に帯電 / 離す: 電撃弾（1秒で最大） / 右: 雷移動 / E: 雷の直線攻撃",
        "左長押し: 竜巻攻撃（最大1.6秒） / 右: 風移動 / E: 上昇気流",
        "左: 設置ボム / 右: 爆風ジャンプ / E: ボム投擲",
        "左: 王冠槍 / 左長押し解除: 槍の雨 / 右: 王威の波 / E: 王翼突進"
    };
    ImGui::TextWrapped("%s", descriptions[copyTypeIndex_]);
    if (previewPlayer_) {
        ImGui::Text("実行中の能力: %s", previewPlayer_->GetCopyAbilityStateLabel());
        ImGui::Text("接地: %s / 高さ: %.2f / 上下速度: %.2f",
            previewPlayer_->IsGrounded() ? "地上" : "空中",
            previewPlayer_->GetTranslate().y - EffectPreviewStage::GetInstance()->GetGroundPosition().y,
            previewPlayer_->GetVelocity().y);
    }
    changed |= ImGui::SliderFloat("長押し時間", &copyHoldTime_, 0.1f, 3.0f, "%.2f 秒");
    changed |= ImGui::SliderFloat("再生区間", &loopDuration_, 4.0f, 12.0f, "%.1f 秒");
    changed |= ImGui::Checkbox("命中確認ダミー", &showTarget_);
    changed |= ImGui::SliderFloat("ダミーまでの距離", &targetDistance_, 2.0f, 25.0f, "%.1f m");
    if (previewEnemy_ && previewEnemy_->param_) ImGui::Text("ダミー累計ダメージ: %.1f", 100000.0f - previewEnemy_->param_->hp);
    ImGui::SliderFloat("再生速度", &playbackSpeed_, 0.05f, 2.0f, "%.2fx");
    ImGui::Checkbox("ループ再生", &loop_);
    if (changed && enabled_) RestartPreview();
    if (enabled_ && ImGui::Button("先頭から再生", ImVec2(-1,0))) RestartPreview();
    if (enabled_ && copyTypeIndex_ == 0) {
        if (ImGui::Button("ガード中を確認")) { copyActionIndex_ = 3; SeekPreview(0.95f); }
        ImGui::SameLine();
        if (ImGui::Button("Eの跳躍を確認")) { copyActionIndex_ = 4; SeekPreview(0.88f); }
        if (ImGui::Button("近接1段目: 引きを確認")) { copyActionIndex_ = 1; targetDistance_ = 2.4f; SeekPreview(0.66f); }
        if (ImGui::Button("近接1段目: 打点を確認")) { copyActionIndex_ = 1; targetDistance_ = 2.4f; SeekPreview(0.80f); }
        if (ImGui::Button("近接2段目: 打点を確認")) { copyActionIndex_ = 7; targetDistance_ = 2.4f; SeekPreview(1.48f); }
        if (ImGui::Button("近接3段目: プレスを確認")) { copyActionIndex_ = 7; targetDistance_ = 2.4f; SeekPreview(2.26f); }
        ImGui::TextDisabled("追加クリックで次の段へ。3段コンボは実入力を3回送って確認します。");
        ImGui::Text("現在の演出: メッシュ %d / 3D破片 %d",
            static_cast<int>(MeshEffectManager::GetInstance()->GetActiveEffects().size()),
            static_cast<int>(DebrisEffectManager::GetInstance()->GetActivePieceCount()));
        ImGui::TextDisabled("打点を確認で停止できます。命中専用演出はダミーへのダメージ成立時のみです。");
    }
    if (enabled_ && ImGui::Button(playing_ ? "一時停止" : "再生", ImVec2(-1,0))) playing_ = !playing_;
    float seek = elapsedTime_;
    if (enabled_ && ImGui::SliderFloat("確認時刻", &seek, 0.0f, loopDuration_, "%.3f 秒")) SeekPreview(seek);
    if (ImGui::Button("カメラを初期位置へ", ImVec2(-1,0)))
        EffectPreviewStage::GetInstance()->RequestCameraRecenter();
    ImGui::Separator();
    ImGui::TextUnformatted("入力開始: 0.5 秒 / 爆弾は点火から爆発まで実更新");
    ImGui::TextWrapped("形態・動作を切り替えると先頭へ戻ります。エフェクトの編集はGPUパーティクル／メッシュエフェクトの各エディターで行えます。");
#endif
}

void EnemyAttackPreviewWindow::DrawImGui() {
#ifdef USE_IMGUI
    if (playerCopyMode_) { DrawPlayerCopySettings(); return; }
    ImGui::Text(ICON_FA_PAW " 敵攻撃プレビュー");
    ImGui::Separator();
    ImGui::TextWrapped("ゲーム本体と同じ敵AIを、隔離ステージ上の実プレイヤーまたは無害なダミーへ向けて再生します。");
    ImGui::TextDisabled("実プレイヤー時はHP、属性別被弾、状態異常、火球の命中まで同じDamageEventで確認できます。");

    if (!IsCurrentSceneReady()) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.28f, 1.0f), "Object3dを配置できるSceneが必要です。");
        return;
    }
    if (sceneManager_->IsPlaying()) {
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.20f, 1.0f), "実行中はプレビューを停止します。編集モードへ戻してください。");
        return;
    }

    bool nextEnabled = enabled_;
    if (ImGui::Checkbox("プレビューを有効化", &nextEnabled)) {
        enabled_ = nextEnabled;
        if (enabled_) {
            CreatePreviewObjects();
        } else {
            ClearPreviewTransientState();
            RemovePreviewObjects();
        }
    }

    ImGui::SeparatorText("攻撃対象");
    const char* currentLabel = kEnemyLabels[
        std::clamp(enemyTypeIndex_, 0, static_cast<int>(std::size(kEnemyLabels)) - 1)];
    if (ImGui::BeginCombo("敵タイプ", currentLabel)) {
        for (int index = 0; index < static_cast<int>(std::size(kEnemyTypes)); ++index) {
            const bool selected = enemyTypeIndex_ == index;
            const char* itemLabel = kEnemyLabels[index];
            if (ImGui::Selectable(itemLabel, selected)) {
                enemyTypeIndex_ = index;
                LoadSelectedProfile();
                ApplyRecommendedSettings();
                if (enabled_) RestartPreview();
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (editableProfile_.attacks.size() > 1 && !IsGiantHookSplitPreview()) {
        const EnemyAttackDefinition* currentAttack = GetSelectedAttackDefinition();
        const char* attackLabel = currentAttack ? currentAttack->displayName.c_str() : "(攻撃なし)";
        if (ImGui::BeginCombo("攻撃種類", attackLabel)) {
            for (int index = 0; index < static_cast<int>(editableProfile_.attacks.size()); ++index) {
                const bool selected = profileAttackIndex_ == index;
                if (ImGui::Selectable(editableProfile_.attacks[index].displayName.c_str(), selected)) {
                    profileAttackIndex_ = index;
                    ApplyRecommendedSettings();
                    if (enabled_) RestartPreview();
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    if (enemyTypeIndex_ == 3) {
        const char* giantPreviewModes[] = {
            "ジャンププレス攻撃",
            "フック分裂（接地から抵抗）",
            "フック分裂（空中拘束から着地）"
        };
        int nextMode = giantPreviewModeIndex_;
        if (ImGui::Combo("確認モーション", &nextMode, giantPreviewModes, IM_ARRAYSIZE(giantPreviewModes))) {
            giantPreviewModeIndex_ = nextMode;
            ApplyRecommendedSettings();
            if (enabled_) RestartPreview();
        }
        if (IsGiantHookSplitPreview()) {
            ImGui::TextDisabled("青いダミーをフック所有者として、抵抗から分裂までを再生します。");
        }
    }

    if (auto* prismSlime = dynamic_cast<EnemyPrismSlime*>(previewEnemy_)) {
        if (ImGui::Checkbox("詠唱エフェクトのみ表示（遮蔽確認）", &prismChantOnly_)) {
            prismSlime->SetDebugPreviewChantOnly(prismChantOnly_);
        }
        if (ImGui::Button("術者を斜め前から見る", ImVec2(-1.0f, 0.0f))) {
            EffectPreviewStage::GetInstance()->ApplyCameraOverride();
            if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
                const Vector3 boss = prismSlime->GetWorldPosition();
                Vector3 forward = previewTarget_
                    ? previewTarget_->GetWorldPosition() - boss : Vector3{ 0.0f, 0.0f, 1.0f };
                forward.y = 0.0f;
                const float length = std::sqrt(forward.x * forward.x + forward.z * forward.z);
                forward = length > 0.001f
                    ? Vector3{ forward.x / length, 0.0f, forward.z / length }
                    : Vector3{ 0.0f, 0.0f, 1.0f };
                const Vector3 side = { forward.z, 0.0f, -forward.x };
                const Vector3 focus = boss + forward * 1.0f + Vector3{ 0.0f, 1.4f, 0.0f };
                const Vector3 offset = forward * 7.0f + side * 7.0f + Vector3{ 0.0f, 6.0f, 0.0f };
                const float azimuth = std::atan2(offset.x, -offset.z);
                camera->SetFollowTarget(nullptr);
                camera->SetEye(focus + offset);
                camera->SetTarget(focus);
                camera->SetRotation({ std::atan2(offset.y, std::sqrt(offset.x * offset.x + offset.z * offset.z)),
                    -azimuth, 0.0f });
                camera->Update();
                CameraEditor::GetInstance()->SetMode(CameraEditor::Mode::Editor);
            }
        }
        ImGui::TextDisabled("詠唱のみ表示で光が見えれば、本体との重なりが原因です。");
    }

    if (dynamic_cast<EnemyFalseKingSlime*>(previewEnemy_)) {
        // ボスの近接表示とは分け、標的周辺と上空から来る攻撃を一緒に観察する。
        const auto frameAttack = [this]() {
            EffectPreviewStage::GetInstance()->ApplyCameraOverride();
            Camera* camera = CameraManager::GetInstance()->GetMainCamera();
            if (!camera) return;
            Vector3 target = EffectPreviewStage::GetInstance()->GetGroundPosition();
            target.y += 5.0f;
            target.z += targetDistance_ * 0.5f;
            const float distance = (std::max)(42.0f, targetDistance_ + 30.0f);
            const float azimuth = 65.0f * kPi / 180.0f;
            const float height = distance * 0.45f;
            camera->SetFollowTarget(nullptr);
            camera->SetEye({target.x + std::sin(azimuth) * distance, target.y + height,
                target.z - std::cos(azimuth) * distance});
            camera->SetTarget(target);
            camera->SetRotation({std::atan2(height, distance), -azimuth, 0.0f});
            camera->Update();
            CameraEditor::GetInstance()->SetMode(CameraEditor::Mode::Editor);
        };
        if (ImGui::Button("攻撃範囲を見渡す", ImVec2(-1.0f, 0.0f))) frameAttack();
        if (ImGui::Button("攻撃本体が出た時点で停止", ImVec2(-1.0f, 0.0f))) {
            SeekPreview(0.0f);
            bool found = false;
            // 固定刻みで実AIを進める。未生成の場合も最大12秒で必ず終了する。
            const int frameLimit = static_cast<int>(std::ceil(std::clamp(loopDuration_, kMinLoopDuration, kMaxLoopDuration) / kFixedPreviewStep));
            for (int frame = 0; frame < frameLimit && previewEnemy_; ++frame) {
                StepSimulation(kFixedPreviewStep);
                const auto* boss = dynamic_cast<const EnemyFalseKingSlime*>(previewEnemy_);
                if (boss && boss->GetDebugVisibleAttackVisualCount() > 0) { found = true; break; }
            }
            playing_ = false;
            simulationAccumulator_ = 0.0f;
            ApplyEffectPlaybackState();
            frameAttack();
            profileStatus_ = found ? "攻撃モデルが生成されたフレームで停止しました。+1Fで形を確認できます。"
                : "1周内に攻撃モデルが生成されませんでした。攻撃状態・距離・モデル読込を確認してください。";
        }
        if (const auto* boss = dynamic_cast<const EnemyFalseKingSlime*>(previewEnemy_)) {
            ImGui::Text("現在: %s / 攻撃モデル: %zu 個", boss->GetDebugAttackPhaseName(), boss->GetDebugVisibleAttackVisualCount());
        }
        ImGui::TextWrapped("槍雨は標的の周辺へ落下します。攻撃フェーズの後半は槍が消えた後も続きます。");
    }

    if (const auto* enemy = dynamic_cast<const BaseEnemy*>(previewEnemy_)) {
        const float bodyDamage = enemy->GetActiveBodyAttackDamage();
        ImGui::Text("本体攻撃: %s / 基礎威力 %.2f", bodyDamage > 0.0f ? "有効" : "無効", bodyDamage);
        ImGui::TextDisabled("通常接触は無害。弾・衝撃波などの独立した攻撃判定は別です。");
    }

    // 攻撃プロファイルの射程が既定値を超える場合も、プレビュー上で標的を配置できるようにします。
    float maxTargetDistance = (std::max)(kDefaultTargetDistanceLimit, GetRecommendedTargetDistance());
    if (const EnemyAttackDefinition* attack = GetSelectedAttackDefinition()) {
        maxTargetDistance = (std::max)(maxTargetDistance, attack->maxRange);
    }
    maxTargetDistance = (std::max)(maxTargetDistance, targetDistance_);
    bool restartForDistance = ImGui::DragFloat(
        "標的までの距離", &targetDistance_, 0.1f, 1.0f, maxTargetDistance, "%.1f m");
    bool restartForTargetMode = false;
    ImGui::BeginDisabled(IsGiantHookSplitPreview());
    restartForTargetMode = ImGui::Checkbox("実プレイヤーを攻撃対象にする", &usePlayerTarget_);
    ImGui::EndDisabled();
    if (IsGiantHookSplitPreview()) {
        ImGui::TextDisabled("フック分裂確認だけは、フック所有者用のダミーを使用します。");
    }
    ImGui::Checkbox(previewPlayer_ ? "プレイヤー標的を表示" : "ダミー標的を表示", &showTarget_);

    if (previewPlayer_) {
        const float maxHp = (std::max)(1.0f, previewPlayer_->GetMaxHp());
        const float hp = std::clamp(previewPlayer_->GetHp(), 0.0f, maxHp);
        char hpLabel[64]{};
        std::snprintf(hpLabel, sizeof(hpLabel), "HP %.1f / %.1f", hp, maxHp);
        ImGui::ProgressBar(hp / maxHp, ImVec2(-1.0f, 0.0f), hpLabel);
        if (ImGui::Button("選択中の攻撃で今すぐ被弾")) {
            TriggerPreviewPlayerDamage();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("通常再生でも攻撃が届けば自動で被弾します");
    }

    bool restartForHitPreview = ImGui::Checkbox("敵側の被弾リアクションを重ねて確認", &previewHitReaction_);
    if (previewHitReaction_) {
        restartForHitPreview |= ImGui::DragFloat(
            "被弾させる時刻",
            &previewHitTime_,
            0.05f,
            0.0f,
            (std::max)(0.0f, loopDuration_ - kFixedPreviewStep),
            "%.2f sec");
        ImGui::TextDisabled("標的側から敵が攻撃された状態を重ね、敵自身の潰れ・反発を確認します。");
    }
    if (ImGui::Button("推奨距離と長さへ戻す")) {
        ApplyRecommendedSettings();
        restartForDistance = true;
    }
    if ((restartForDistance || restartForTargetMode || restartForHitPreview) && enabled_) {
        RestartPreview();
    }

    ImGui::SeparatorText("攻撃プロファイル");
    const std::string profilePath = EnemyAttackProfile::GetDefaultPath(GetSelectedEnemyType());
    ImGui::TextDisabled("%s", profilePath.c_str());

    EnemyAttackDefinition* editedAttack = GetSelectedAttackDefinition();
    if (editedAttack) {
        ImGui::Text("攻撃ID: %s", editedAttack->id.c_str());
        bool changed = false;
        changed |= DrawProfileString("表示名", editedAttack->displayName);

        if (ImGui::TreeNodeEx("距離・範囲", ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= ImGui::DragFloat("最小発動距離", &editedAttack->minRange, 0.05f, 0.0f, 50.0f, "%.2f m");
            changed |= ImGui::DragFloat("最大発動距離", &editedAttack->maxRange, 0.05f, 0.0f, 50.0f, "%.2f m");
            changed |= ImGui::DragFloat("攻撃半径・幅", &editedAttack->radius, 0.05f, 0.01f, 50.0f, "%.2f m");
            const char* hitShapes[] = { "Sphere", "Capsule", "Box", "Cone" };
            int hitShapeIndex = 0;
            if (editedAttack->hitShape == "capsule") hitShapeIndex = 1;
            else if (editedAttack->hitShape == "box") hitShapeIndex = 2;
            else if (editedAttack->hitShape == "cone") hitShapeIndex = 3;
            if (ImGui::Combo("判定形状", &hitShapeIndex, hitShapes, IM_ARRAYSIZE(hitShapes))) {
                constexpr const char* kHitShapeIds[] = { "sphere", "capsule", "box", "cone" };
                editedAttack->hitShape = kHitShapeIds[hitShapeIndex];
                changed = true;
            }
            changed |= ImGui::DragFloat3("判定ローカル位置", &editedAttack->hitOffset.x, 0.05f, -50.0f, 50.0f, "%.2f m");
            changed |= ImGui::DragFloat3("判定サイズ", &editedAttack->hitSize.x, 0.05f, 0.01f, 50.0f, "%.2f m");
            ImGui::TextDisabled("サイズは形状に応じて半径・高さ・奥行きとして利用できます。");
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("タイミング", ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= ImGui::DragFloat("予備動作", &editedAttack->windupDuration, 0.01f, 0.0f, 20.0f, "%.2f sec");
            changed |= ImGui::DragFloat("攻撃継続", &editedAttack->activeDuration, 0.01f, 0.0f, 20.0f, "%.2f sec");
            changed |= ImGui::DragFloat("硬直", &editedAttack->recoveryDuration, 0.01f, 0.0f, 20.0f, "%.2f sec");
            changed |= ImGui::DragFloat("再使用間隔", &editedAttack->cooldown, 0.01f, 0.0f, 30.0f, "%.2f sec");
            changed |= ImGui::DragFloat("警告先行時間", &editedAttack->warningLeadTime, 0.01f, 0.0f, 5.0f, "%.2f sec");
            changed |= ImGui::DragFloat("キャンセル開始", &editedAttack->cancelWindowStart, 0.01f, 0.0f, 60.0f, "%.2f sec");
            changed |= ImGui::DragFloat("キャンセル終了", &editedAttack->cancelWindowEnd, 0.01f, 0.0f, 60.0f, "%.2f sec");
            ImGui::TextDisabled("開始からの秒数。0 / 0 の場合はキャンセル不可です。");
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("威力・移動", ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= ImGui::DragFloat("ダメージ", &editedAttack->damage, 0.05f, 0.0f, 999.0f, "%.2f");
            changed |= ImGui::DragFloat("最低速度", &editedAttack->minSpeed, 0.1f, 0.0f, 200.0f, "%.1f");
            changed |= ImGui::DragFloat("最高速度", &editedAttack->maxSpeed, 0.1f, 0.0f, 200.0f, "%.1f");
            changed |= ImGui::DragFloat("寿命", &editedAttack->lifetime, 0.05f, 0.0f, 30.0f, "%.2f sec");
            changed |= ImGui::DragFloat3("吹き飛ばし (右・上・前)", &editedAttack->knockbackVelocity.x, 0.1f, -100.0f, 100.0f, "%.1f");
            changed |= ImGui::DragFloat("被弾無敵時間", &editedAttack->invincibilityDuration, 0.01f, 0.0f, 10.0f, "%.2f sec");
            ImGui::TextDisabled("吹き飛ばしが 0,0,0 の既存データは従来値を使います。");
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("状態異常", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* statusTypes[] = { "なし", "炎上" };
            int statusIndex = editedAttack->statusEffectType == "burning" ? 1 : 0;
            if (ImGui::Combo("種類", &statusIndex, statusTypes, IM_ARRAYSIZE(statusTypes))) {
                editedAttack->statusEffectType = statusIndex == 1 ? "burning" : "";
                if (statusIndex == 1 && editedAttack->statusDuration <= 0.0f) {
                    editedAttack->statusDuration = 2.2f;
                    editedAttack->statusTickInterval = 0.55f;
                    editedAttack->statusTickDamage = 0.12f;
                    editedAttack->statusVfx = "status_burning_flame";
                }
                changed = true;
            }
            ImGui::BeginDisabled(statusIndex == 0);
            changed |= ImGui::DragFloat("持続時間", &editedAttack->statusDuration, 0.05f, 0.0f, 30.0f, "%.2f sec");
            changed |= ImGui::DragFloat("継続間隔", &editedAttack->statusTickInterval, 0.01f, 0.05f, 10.0f, "%.2f sec");
            changed |= ImGui::DragFloat("継続ダメージ", &editedAttack->statusTickDamage, 0.01f, 0.0f, 999.0f, "%.2f");
            changed |= DrawProfileString("状態VFX", editedAttack->statusVfx);
            ImGui::EndDisabled();
            ImGui::TextDisabled("同種の再付与はスタックせず、残り時間と威力を更新します。");
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("プレビュー設定", ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= ImGui::DragFloat("推奨標的距離", &editedAttack->recommendedTargetDistance, 0.1f, 0.0f, 50.0f, "%.1f m");
            changed |= ImGui::DragFloat("推奨再生時間", &editedAttack->previewDuration, 0.05f, 0.1f, 30.0f, "%.2f sec");
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("演出参照")) {
            changed |= DrawProfileString("アニメーション", editedAttack->animation);
            changed |= DrawProfileString("予備動作VFX", editedAttack->windupVfx);
            changed |= DrawProfileString("攻撃中VFX", editedAttack->activeVfx);
            changed |= DrawProfileString("命中・着地VFX", editedAttack->impactVfx);
            changed |= DrawProfileString("SEキュー", editedAttack->audioCue);
            changed |= DrawProfileString("Feedback Cue", editedAttack->feedbackCue);
            ImGui::TreePop();
        }

        profileDirty_ |= changed;
        if (profileDirty_) {
            ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.22f, 1.0f), "未保存の変更があります");
        }

        if (ImGui::Button(ICON_FA_SAVE " 保存してプレビューへ反映")) {
            SaveSelectedProfile();
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_SYNC " 再読み込み")) {
            LoadSelectedProfile();
            ApplyRecommendedSettings();
            if (enabled_) RestartPreview();
        }
        ImGui::SameLine();
        if (ImGui::Button("既定値へ戻す")) {
            ResetSelectedProfile();
        }
    } else {
        ImGui::TextDisabled("この敵は、今回のスライム攻撃プロファイル対象外です。");
    }

    if (!profileStatus_.empty()) {
        ImGui::TextWrapped("%s", profileStatus_.c_str());
    }

    ImGui::SeparatorText("再生");
    ImGui::BeginDisabled(!enabled_);
    if (ImGui::Button(playing_ ? ICON_FA_PAUSE " 一時停止" : ICON_FA_PLAY " 再生")) {
        playing_ = !playing_;
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_REDO " 先頭から")) {
        RestartPreview();
    }
    ImGui::SameLine();
    if (ImGui::Button("-10F")) {
        playing_ = false;
        SeekPreview(elapsedTime_ - kFixedPreviewStep * 10.0f);
    }
    ImGui::SameLine();
    if (ImGui::Button("-1F")) {
        playing_ = false;
        SeekPreview(elapsedTime_ - kFixedPreviewStep);
    }
    ImGui::SameLine();
    if (ImGui::Button("+1F")) {
        playing_ = false;
        SeekPreview(elapsedTime_ + kFixedPreviewStep);
    }
    ImGui::SameLine();
    if (ImGui::Button("+10F")) {
        playing_ = false;
        SeekPreview(elapsedTime_ + kFixedPreviewStep * 10.0f);
    }

    ImGui::SliderFloat("再生速度", &playbackSpeed_, 0.05f, 2.0f, "%.2fx");
    ImGui::Checkbox("ループ", &loop_);
    ImGui::DragFloat("1周の長さ", &loopDuration_, 0.05f, kMinLoopDuration, kMaxLoopDuration, "%.2f sec");
    float seekTime = elapsedTime_;
    if (ImGui::SliderFloat("タイムライン", &seekTime, 0.0f, (std::max)(loopDuration_, kFixedPreviewStep), "%.2f sec")) {
        playing_ = false;
        SeekPreview(seekTime);
    }
    const float progress = loopDuration_ > 0.001f ? std::clamp(elapsedTime_ / loopDuration_, 0.0f, 1.0f) : 0.0f;
    char progressLabel[64]{};
    sprintf_s(progressLabel, "%.2f / %.2f sec", elapsedTime_, loopDuration_);
    ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f), progressLabel);
    if (previewEnemy_) {
        ImGui::Text("現在フェーズ: %s", GetCurrentPhaseName());
        if (const auto* prismSlime = dynamic_cast<const EnemyPrismSlime*>(previewEnemy_)) {
            const int chantLayers = prismSlime->GetDebugChantVisibleLayerCount();
            if (chantLayers == -2) {
                ImGui::TextDisabled("詠唱エフェクト: 詠唱待機中");
            } else if (chantLayers < 0) {
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.28f, 1.0f),
                    "詠唱エフェクト: 未生成（Debug Console の読込エラーを確認）");
            } else {
                ImGui::Text("詠唱エフェクト: 表示対象 %d / 7 レイヤー", chantLayers);
            }
        }
        if (const auto* falseKingSlime = dynamic_cast<const EnemyFalseKingSlime*>(previewEnemy_)) {
            const size_t visibleVisuals = falseKingSlime->GetDebugVisibleAttackVisualCount();
            ImGui::Text("Attack body: %s", falseKingSlime->GetDebugAttackBodyName());
            if (visibleVisuals > 0) {
                ImGui::TextColored(
                    ImVec4(0.48f, 1.0f, 0.82f, 1.0f),
                    "攻撃本体モデル: %zu 個が表示対象",
                    visibleVisuals);
            } else {
                ImGui::TextDisabled("攻撃本体モデル: 現在0個（発生前、または消滅後）");
            }
        }
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("確認ポイント");
    ImGui::BulletText("予兆が攻撃より先に読めるか");
    ImGui::BulletText("溜め・発射・着地・硬直の姿勢が連続して見えるか");
    ImGui::BulletText("0.25x～0.5xでエフェクトの発生位置と残り方を確認");
#endif
}

