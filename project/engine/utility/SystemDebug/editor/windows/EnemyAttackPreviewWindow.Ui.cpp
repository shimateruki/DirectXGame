#include "EnemyAttackPreviewWindow.h"
#include "EnemyAttackPreviewWindow.Constants.h"
#include "engine/animation/AnimatorController.h"
#include "game/scene/gameplay/GamePlayScene.h"

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
#include "game/system/CrownFinaleTuning.h"
#include "game/system/CrownDefeatTuning.h"
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
    // ImGui の固定長バッファに文字列を写し、編集されたフレームだけ設定値へ戻す。
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
    // ゲーム本体と同じコピー設定を表示し、編集した値を攻撃プロファイルへ戻す。
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
    if (changed) LoadSelectedProfile();
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
    DrawAttackProfileEditor();
#endif
}

void EnemyAttackPreviewWindow::DrawImGui() {
#ifdef USE_IMGUI
    // ムービー、術者、弾、軌道部品、コピー能力の順で編集対象を優先し、同時表示を避ける。
    if (crownMovieMode_) { DrawCrownMovieSettings(); return; }
    if (authoringActorSelected_) {
        ImGui::SeparatorText("術者（プレビューActorへの参照）");
        ImGui::Text("種類: %s",GetSelectedEnemyType());
        ImGui::TextWrapped("このActorが攻撃を実行します。弾Objectのモデルを変更しても術者のモデルは変わりません。術者の動きは術者アニメーション部品で指定します。");
        if (ImGui::Button("攻撃全体・術者の種類選択へ戻る")) authoringActorSelected_ = false;
        return;
    }
    // 部品選択中は多数の既存設定の奥へ埋めず、選んだ部品をInspectorの主対象にします。
    // 編集中に特定の部品を選んだ場合は、その部品のInspectorを先に表示する。
    if (sceneManager_ && !sceneManager_->IsPlaying()) {
        const auto* attack = GetSelectedAttackDefinition();
        if (attack && attack->sequence.enabled && selectedTrack_ <= -2 &&
            -2 - selectedTrack_ < static_cast<int>(attack->sequence.events.size())) {
            ImGui::Text("生成Object / GPU放出 %d", -1 - selectedTrack_);
            if (ImGui::Button("攻撃全体の設定へ戻る")) { selectedTrack_ = -1; StopPlacementPreview(); }
            else { DrawProjectileInspector(); return; }
        }
        if (attack && attack->sequence.enabled && selectedTrack_ >= 0 &&
            selectedTrack_ < static_cast<int>(attack->sequence.tracks.size())) {
            ImGui::Text("攻撃部品: %s", attack->sequence.tracks[selectedTrack_].name.c_str());
            if (ImGui::Button("攻撃全体・プレビューの設定へ戻る")) { selectedTrack_ = -1; StopPlacementPreview(); }
            else {
                DrawTrackInspector();
                ImGui::TextWrapped("保存は左上の「保存」、追尾・接触の確認は「保存して攻撃を再生」を使います。");
                std::string error;
                if (!attack->sequence.Validate(&error)) ImGui::TextWrapped("%s", error.c_str());
                return;
            }
        }
    }
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

    // プレビューの有効化に合わせて一時Objectを生成・除去し、Sceneへ残さない。
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
    if (std::string(GetSelectedEnemyType()) == "FireSlime" && previewEnemy_) {
        const std::string state = previewEnemy_->GetAnimatorCurrentStateName();
        ImGui::Text("炎スライム本体: %s", state.empty() ? "Controller未適用" : state.c_str());
        if (const auto* runtime = previewEnemy_->GetAnimatorControllerRuntime())
            ImGui::TextDisabled("MotionMode: %d", runtime->GetInt("MotionMode"));
        ImGui::TextDisabled("攻撃ごとの状態は下の演出参照で選択できます。");
    }

    // 敵種の変更はProfileと推奨距離を更新し、表示中ならプレビューを先頭から再開する。
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

    // 選んだ攻撃により同じ敵でも演出内容が変わる。専用の槍雨は別項目として扱う。
    if (editableProfile_.attacks.size() > 1 && !IsGiantHookSplitPreview()) {
        const EnemyAttackDefinition* currentAttack = GetSelectedAttackDefinition();
        const char* attackLabel = falseKingSealBarragePreview_
                                      ? "第2形態：封印失敗の黒晶墜槍"
                                      : (currentAttack ? currentAttack->displayName.c_str() : "(攻撃なし)");
        if (ImGui::BeginCombo("攻撃種類", attackLabel)) {
            for (int index = 0; index < static_cast<int>(editableProfile_.attacks.size()); ++index) {
                if (editableProfile_.attacks[index].sequence.enabled && editableProfile_.attacks[index].sequence.binding == "player_special") continue;
                const bool selected = !falseKingSealBarragePreview_ && profileAttackIndex_ == index;
                if (ImGui::Selectable(editableProfile_.attacks[index].displayName.c_str(), selected)) {
                    profileAttackIndex_ = index;
                    falseKingSealBarragePreview_ = false;
                    ApplyRecommendedSettings();
                    if (enabled_) RestartPreview();
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            if (enemyTypeIndex_ == 8) {
                ImGui::Separator();
                if (ImGui::Selectable("第2形態：封印失敗の黒晶墜槍", falseKingSealBarragePreview_)) {
                    falseKingSealBarragePreview_ = true;
                    ApplyRecommendedSettings();
                    if (enabled_) RestartPreview();
                }
            }
            ImGui::EndCombo();
        }
    }

    // 偽王の槍雨は共有Tuningを直接編集し、保存ボタンでAssetへ反映する。
    if (enemyTypeIndex_ == 8 && falseKingSealBarragePreview_) {
        ImGui::SeparatorText("第2形態・空中槍雨の演出");
        auto& tuning = CrownFinaleTuning::Get();
        ImGui::SliderFloat("空中魔法陣の高さ", &tuning.sealBarrageSkyHeight, 8.0f, 18.0f, "%.1f m");
        ImGui::SliderFloat("空中魔法陣の広さ", &tuning.sealBarrageSkyRadiusRate, 0.45f, 0.95f, "%.2f 倍");
        ImGui::SliderFloat("墜槍の大きさ", &tuning.sealBarrageSpearScale, 0.65f, 1.45f, "%.2f 倍");
        ImGui::SliderFloat("ムービーの長さ", &tuning.sealBarrageMovieDuration, 3.1f, 5.0f, "%.2f 秒");
        ImGui::SliderFloat("カメラの距離", &tuning.sealBarrageCameraDistance, 0.7f, 1.5f, "%.2f 倍");
        ImGui::SliderFloat("カメラの高さ", &tuning.sealBarrageCameraHeight, 0.7f, 1.5f, "%.2f 倍");
        ImGui::Checkbox("ムービーカメラで再生", &sealBarrageMovieCameraPreview_);
        tuning.Sanitize();
        loopDuration_ = GetRecommendedLoopDuration();
        const float chantStart = 1.25f;
        const float attackStart = chantStart + tuning.sealCastTime;
        if (elapsedTime_ < chantStart)
            ImGui::Text("結晶の出現 %.1f / %.1f 秒", elapsedTime_, chantStart);
        else if (elapsedTime_ < attackStart)
            ImGui::Text("封印の詠唱 %.1f / %.1f 秒", elapsedTime_ - chantStart, tuning.sealCastTime);
        else
            ImGui::Text("封印失敗から %.1f 秒", elapsedTime_ - attackStart);
        if (ImGui::Button("第2形態の演出設定を保存"))
            profileStatus_ = tuning.Save() ? "第2形態の演出設定を保存しました。"
                                           : "第2形態の演出設定を保存できませんでした。";
        ImGui::TextDisabled("全域放電は即死。seal専用のメッシュエフェクト・GPU粒子・3D破片プリセットで外観を調整できます。");
    }

    // 巨大スライムの分裂確認は通常攻撃とは別のモードを選ぶ。
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

    // 詠唱の遮蔽を調べるため、エフェクト単体表示と斜め視点を用意する。
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

    // 偽王は攻撃範囲が広いため、全景・側面・実体生成フレームの三つの確認導線を置く。
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
        if (ImGui::Button("空中魔法陣を横から見る", ImVec2(-1.0f, 0.0f))) {
            EffectPreviewStage::GetInstance()->ApplyCameraOverride();
            if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
                Vector3 focus = EffectPreviewStage::GetInstance()->GetGroundPosition();
                focus.z += falseKingSealBarragePreview_ ? 0.0f : targetDistance_;
                focus.y += 6.0f;
                const Vector3 offset{21.0f, 5.0f, -26.0f};
                camera->SetFollowTarget(nullptr);
                camera->SetEye(focus + offset);
                camera->SetTarget(focus);
                camera->SetRotation({std::atan2(offset.y, std::sqrt(offset.x * offset.x + offset.z * offset.z)),
                                     -std::atan2(offset.x, -offset.z), 0.0f});
                camera->Update();
                CameraEditor::GetInstance()->SetMode(CameraEditor::Mode::Editor);
            }
        }
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
    // 対象との距離上限は攻撃の最大射程まで広げ、選択中の長距離攻撃も配置できるようにする。
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

    // 実プレイヤーを選んだ場合は実際のDamageEventを手動発火して被弾を比較できる。
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

    // 下段は攻撃定義の編集と、同じ定義を使う固定刻みの再生操作を並べる。
    DrawAttackProfileEditor();

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


void EnemyAttackPreviewWindow::DrawCrownMovieSettings() {
#ifdef USE_IMGUI
    ImGui::TextUnformatted("王冠・最終撃破ムービープレビュー");
    int mode=crownDefeatMode_?1:0;
    if(ImGui::Combo("演出の種類",&mode,"王冠継承・ステージ崩壊\0最終形態の撃破\0")) {
        ClearPreviewTransientState();RemovePreviewObjects();enabled_=false;playing_=false;
        crownDefeatMode_=mode==1;profileStatus_.clear();
        loopDuration_=crownDefeatMode_?CrownDefeatTuning::Get().duration:CrownFinaleTuning::Get().movieDuration;
    }
    ImGui::TextWrapped("ステージ3を編集モードで開いてください。専用の複製を使って本番の演出を再生します。");
    auto* game = sceneManager_ ? dynamic_cast<GamePlayScene*>(sceneManager_->GetCurrentScene()) : nullptr;
    if (!game || sceneManager_->IsPlaying()) {
        ImGui::TextDisabled("ステージ3の編集モードで使用できます。"); return;
    }
    if (ImGui::Button(enabled_ ? "プレビューを終了して配置へ戻る" : "ムービープレビューを開始", ImVec2(-1,32))) {
        if (enabled_) { ClearPreviewTransientState(); RemovePreviewObjects(); enabled_ = false; }
        else { enabled_ = true; playing_ = false; ClearPreviewTransientState(); CreatePreviewObjects(); }
    }
    if (!enabled_) ImGui::TextDisabled("開始後、下段のタイムラインで各場面へ移動できます。");
    if(crownDefeatMode_) {DrawCrownDefeatSettings();return;}
    ImGui::SeparatorText("本番に反映する演出設定");
    auto& t = CrownFinaleTuning::Get();
    bool changed = ImGui::SliderFloat("ムービーの長さ", &t.movieDuration, 12.0f,24.0f,"%.2f 秒");
    auto timeControl = [&](const char* label,float& ratio,float lo,float hi) {
        float seconds=ratio*t.movieDuration;
        if (ImGui::SliderFloat(label,&seconds,lo*t.movieDuration,hi*t.movieDuration,"%.2f 秒")) {
            ratio=seconds/t.movieDuration; changed=true;
        }
    };
    timeControl("王冠の破断",t.movieFragmentBreakTime,.03f,.10f);
    timeControl("欠片の着地",t.movieFragmentLandTime,.14f,.28f);
    timeControl("吸い込み開始",t.movieFragmentPullTime,.28f,.40f);
    timeControl("欠片を体内へ取り込む",t.movieFragmentAbsorbTime,.40f,.49f);
    timeControl("ボスの暴走・衝撃波",t.movieRageTime,0.235f,0.305f);
    timeControl("金色の体の形成",t.movieBodyTime,0.475f,0.545f);
    timeControl("王冠の形成",t.movieCrownTime,0.565f,0.635f);
    timeControl("翼の形成",t.movieWingsTime,0.645f,0.715f);
    timeControl("踏み切り・脱出",t.movieEscapeTime,0.725f,0.795f);
    timeControl("最終ステージへの着地",t.movieArriveTime,0.895f,0.965f);
    changed |= ImGui::SliderFloat("欠片までの距離",&t.movieFragmentDistance,1.5f,5.5f,"%.2f m");
    changed |= ImGui::SliderFloat("欠片の大きさ",&t.movieFragmentScale,.65f,1.5f,"%.2f 倍");
    changed |= ImGui::SliderFloat("落下の弧の高さ",&t.movieFragmentFallArc,0.0f,6.0f,"%.2f m");
    changed |= ImGui::SliderFloat("寄りのカメラ距離",&t.movieFragmentCameraDistance,.7f,1.5f,"%.2f 倍");
    changed |= ImGui::SliderFloat("脱出の高さ",&t.escapeHeight,18.0f,60.0f,"%.1f m");
    changed |= ImGui::SliderFloat("カメラの揺れ",&t.movieShake,0.0f,2.0f,"%.2f");
    changed |= ImGui::SliderFloat("翼の大きさ",&t.movieWingScale,.5f,2.0f,"%.2f 倍");
    t.Sanitize(); loopDuration_=t.movieDuration;
    if (changed && enabled_) {
        // 距離変更も開始位置から取り直し、今の確認時刻へ復元します。
        const float time=elapsedTime_;
        ClearPreviewTransientState(); RemovePreviewObjects(); CreatePreviewObjects(false); SeekPreview(time);
    }
    if (ImGui::Button("ムービー設定を保存")) profileStatus_=t.Save()?"保存しました。本番にも反映されます。":"保存できませんでした。";
    ImGui::SameLine();
    if (ImGui::Button("設定を再読込")) {
        t.Load(); loopDuration_=t.movieDuration;
        if (enabled_) { ClearPreviewTransientState(); RemovePreviewObjects(); CreatePreviewObjects(false); SeekPreview(0); }
    }
    ImGui::TextWrapped("%s",profileStatus_.c_str());
#endif
}

void EnemyAttackPreviewWindow::DrawCrownDefeatSettings() {
    // 最終形態の撃破演出を攻撃設定から調整し、プレビューと実戦を一致させる。
#ifdef USE_IMGUI
    ImGui::SeparatorText("最終撃破の演出設定");
    auto& t=CrownDefeatTuning::Get();
    bool changed=ImGui::SliderFloat("ムービーの長さ",&t.duration,9,18,"%.2f 秒");
    auto time=[&](const char* label,float& ratio,float lo,float hi) {
        float seconds=ratio*t.duration;
        if(ImGui::SliderFloat(label,&seconds,lo*t.duration,hi*t.duration,"%.2f 秒")) {ratio=seconds/t.duration;changed=true;}
    };
    time("王冠の破砕",t.rupture,.22f,.34f);time("力が抜け始める",t.shrink,t.rupture+.10f,.52f);
    time("小スライムになる",t.settle,t.shrink+.12f,.72f);time("王の勝利カット",t.victory,t.settle+.08f,.88f);
    changed|=ImGui::SliderFloat("破片の広がり",&t.spread,.5f,1.8f,"%.2f 倍");
    changed|=ImGui::SliderFloat("カメラの距離",&t.cameraDistance,.8f,1.4f,"%.2f 倍");
    changed|=ImGui::SliderFloat("衝撃の揺れ",&t.shake,0,2,"%.2f 倍");
    changed|=ImGui::SliderFloat("発光・粒子の強さ",&t.energy,.4f,1.8f,"%.2f 倍");
    t.Sanitize();loopDuration_=t.duration;
    if(changed && enabled_)SeekPreview(elapsedTime_);
    if(ImGui::Button("撃破設定を保存"))profileStatus_=t.Save()?"保存しました。本番にも反映されます。":"保存できませんでした。";
    ImGui::SameLine();if(ImGui::Button("撃破設定を再読込")) {t.Load();loopDuration_=t.duration;if(enabled_)SeekPreview(0);}
    ImGui::TextWrapped("%s",profileStatus_.c_str());
    ImGui::TextWrapped("火花はGPUパーティクルの crown_defeat_sparks、破砕時の光はVFXシーケンサーの crown_defeat_rupture_cue で調整できます。");
    ImGui::TextWrapped("本番では撃破完了後にクリアを保存します。プレビューは専用の複製を使い、セーブや撃破通知を行いません。");
#endif
}

void EnemyAttackPreviewWindow::DrawAttackProfileEditor() {
#ifdef USE_IMGUI
    ImGui::SeparatorText("攻撃プロファイル");
    const std::string profilePath = EnemyAttackProfile::GetDefaultPath(GetSelectedEnemyType());
    ImGui::TextDisabled("%s", profilePath.c_str());

    if (playerCopyMode_ && !editableProfile_.attacks.empty()) {
        if (ImGui::BeginCombo("編集・観測する攻撃", editableProfile_.attacks[profileAttackIndex_].displayName.c_str())) {
            for (int i = 0; i < static_cast<int>(editableProfile_.attacks.size()); ++i) {
                const auto& attack = editableProfile_.attacks[i];
                if (!attack.sequence.enabled || attack.sequence.binding != "player_special") continue;
                if (ImGui::Selectable(attack.displayName.c_str(), profileAttackIndex_ == i)) {
                    profileAttackIndex_ = i;
                    copyActionIndex_ = 4;
                    if (enabled_) RestartPreview();
                }
            }
            ImGui::EndCombo();
        }
    }
    const auto* selectedSequenceAttack = GetSelectedAttackDefinition();
    const bool prismSequence = !playerCopyMode_ && std::string(GetSelectedEnemyType()) == "PrismSlime";
    const bool canEditSequence = std::string(GetSelectedEnemyType()) == "FireSlime" ||
        (prismSequence && selectedSequenceAttack && (selectedSequenceAttack->id == "fire_fan" ||
            !selectedSequenceAttack->sequence.events.empty() || !selectedSequenceAttack->sequence.tracks.empty()));
    if (canEditSequence) {
        if (ImGui::Button("共通攻撃を追加・選択攻撃から複製")) {
            EnemyAttackDefinition copy;
            if (const auto* selected = GetSelectedAttackDefinition()) copy = *selected;
            copy.sequence.assetPath.clear(); // 複製後の変更で、元の共有アセットを書き換えない。
            int suffix = 1;
            std::string id;
            do { id = "custom_attack_" + std::to_string(suffix++); } while (editableProfile_.FindAttack(id));
            copy.id = id;
            copy.displayName += " 複製";
            copy.sequence.enabled = true;
            copy.sequence.binding = playerCopyMode_ ? "player_special" : "enemy";
            copy.sequence.priority = (std::min)(copy.sequence.priority, 999) + 1;
            if (copy.sequence.events.empty() && copy.sequence.tracks.empty()) {
                copy.sequence.duration = (std::max)(0.5f, copy.windupDuration);
                AttackSequenceEvent event;
                event.time = copy.windupDuration;
                copy.sequence.events.push_back(event);
                copy.maxSpeed = 28; copy.lifetime = 2.65f;
            }
            editableProfile_.attacks.push_back(copy);
            profileAttackIndex_ = static_cast<int>(editableProfile_.attacks.size()) - 1;
            profileDirty_ = true;
            if (playerCopyMode_) copyActionIndex_ = 4;
        }
        ImGui::TextWrapped("共通攻撃は射程内の候補から優先度順に選択されます。保存して再生成すると本編と同じデータで実行されます。");
        if (prismSequence) ImGui::TextWrapped("この敵では炎属性の攻撃枠で実行します。命令の時刻0は詠唱完了時です。複製した攻撃も同じ詠唱とAIを使用します。");
        else ImGui::TextWrapped("炎スライムの敵AIと炎コピーE入力に接続しています。命令の時刻0は攻撃開始時です。");
    }
    EnemyAttackDefinition* editedAttack = GetSelectedAttackDefinition();
    if (editedAttack) {
        ImGui::Text("攻撃ID: %s", editedAttack->id.c_str());
        bool changed = false;
        changed |= DrawProfileString("表示名", editedAttack->displayName);
        if (selectedTrack_ <= -2) ImGui::SetNextItemOpen(true);
        if (canEditSequence && ImGui::TreeNodeEx("共通実行シーケンス", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& sequence = editedAttack->sequence;
            changed |= ImGui::Checkbox("共通ランタイムで実行", &sequence.enabled);
            if (sequence.enabled) {
                if (selectedTrack_ >= -1) DrawTrackInspector();
                const char* bindings[] = {"敵AI", "プレイヤーE入力"};
                int binding = sequence.binding == "player_special" ? 1 : 0;
                if (ImGui::Combo("実行先", &binding, bindings, prismSequence ? 1 : 2)) { sequence.binding = binding ? "player_special" : "enemy"; changed = true; }
                changed |= ImGui::SliderInt("選択優先度", &sequence.priority, -1000, 1000);
                changed |= ImGui::DragFloat("シーケンス長", &sequence.duration, 0.01f, 0, 30, "%.3f 秒");
                if (!prismSequence && sequence.binding == "enemy" && ImGui::TreeNodeEx("術者制御（予備動作から終了まで）", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto& c = sequence.actorControl;
                    changed |= ImGui::Checkbox("術者も共通アクションで動かす", &c.enabled);
                    if (c.enabled) {
                        changed |= ImGui::Checkbox("攻撃中の水平移動を止める", &c.stopMovement);
                        changed |= ImGui::Checkbox("照準方向に本体を向ける", &c.faceTarget);
                        changed |= ImGui::DragFloat("照準を固定する時刻", &c.aimLockTime, 0.01f, 0, sequence.duration, "%.3f 秒");
                        changed |= ImGui::DragFloat("モデルの正面補正", &c.modelYawOffset, 1, -360, 360, "%.1f 度");
                        changed |= ImGui::Checkbox("直線の攻撃予告を表示", &c.telegraph);
                        changed |= ImGui::DragFloat("予備動作・予告の終了", &c.telegraphEnd, 0.01f, 0, sequence.duration, "%.3f 秒");
                        changed |= ImGui::DragFloat("予告の幅", &c.telegraphWidth, 0.01f, 0.01f, 100, "%.2f m");
                        changed |= ImGui::DragFloat("警告を出す時刻", &c.cueTime, 0.01f, 0, sequence.duration, "%.3f 秒");
                        changed |= ImGui::ColorEdit4("予告の色", c.telegraphColor.data());
                        changed |= ImGui::Checkbox("予備動作中に本体を着色", &c.tint);
                        changed |= ImGui::ColorEdit4("予備動作の色", c.color.data());
                        ImGui::TextWrapped("発射時刻は左の弾、姿勢は術者アニメーション部品で編集します。シーケンス長には後隙を含めます。終了後から再使用待ちを数えます。下の従来用の予備動作・硬直時間はこの攻撃の実行には使いません。");
                    }
                    ImGui::TreePop();
                }
                ImGui::TextWrapped("弾ごとの見た目・半径・速度・寿命は左の弾を選択して編集します。以下の外観設定は部品設定のない旧データと新規追加時の初期値です。");
                int style = sequence.projectileStyle == "plain" ? 1 : 0;
                const char* styles[] = {"炎", "通常"};
                if (ImGui::Combo("弾の外観", &style, styles, 2)) { sequence.projectileStyle = style ? "plain" : "fire"; changed = true; }
                changed |= ImGui::DragFloat("弾の大きさ (判定も拡縮)", &sequence.visualScale, 0.01f, 0.01f, 20);
                if (style == 0 && ImGui::TreeNode("炎弾の見た目")) {
                    auto& a = sequence.fireAppearance;
                    float color[] = {a.red, a.green, a.blue, a.alpha};
                    if (ImGui::ColorEdit4("色・透明度", color)) { a.red = color[0]; a.green = color[1]; a.blue = color[2]; a.alpha = color[3]; changed = true; }
                    changed |= ImGui::DragFloat("炎の描画倍率 (判定は固定)", &a.effectScale, 0.01f, 0.05f, 100);
                    changed |= ImGui::DragFloat("炎の境界の柔らかさ", &a.softness, 0.01f, 0, 1);
                    changed |= ImGui::DragFloat("炎の明るさ", &a.intensity, 0.01f, 0.05f, 100);
                    changed |= ImGui::DragFloat("炎の板の倍率", &a.billboardScale, 0.01f, 0.05f, 100);
                    changed |= ImGui::DragFloat("軌跡の発生間隔", &a.trailInterval, 0.001f, 0.01f, 100, "%.3f 秒");
                    changed |= ImGui::DragFloat("軌跡の速度倍率", &a.trailSpeedScale, 0.01f, 0.05f, 100);
                    ImGui::TreePop();
                }
                ImGui::TextWrapped("部品設定がある弾は個別の値を優先します。イベントは時刻順に各1回実行し、条件はその時点で評価します。");
                for (int i = 0; i < static_cast<int>(sequence.events.size()); ++i) {
                    if (selectedTrack_ <= -2 && i != -2 - selectedTrack_) continue;
                    ImGui::PushID(i);
                    auto& event = sequence.events[i];
                    ImGui::Separator();
                    ImGui::Text("イベント %d", i + 1);
                    changed |= ImGui::DragFloat("実行時刻", &event.time, 0.01f, 0, sequence.duration, "%.3f 秒");
                    const char* operations[] = {"弾を発射", "GPUパーティクル"};
                    int operation = event.operation == "gpu_vfx" ? 1 : 0;
                    if (ImGui::Combo("命令", &operation, operations, 2)) { event.operation = operation ? "gpu_vfx" : "projectile"; changed = true; }
                    const char* conditions[] = {"常時", "接地中", "空中", "標的が射程内"};
                    const char* conditionIds[] = {"always", "grounded", "airborne", "target_in_range"};
                    int condition = 0;
                    for (int c = 0; c < 4; ++c) if (event.condition == conditionIds[c]) condition = c;
                    if (ImGui::Combo("実行条件", &condition, conditions, 4)) { event.condition = conditionIds[condition]; changed = true; }
                    changed |= ImGui::Checkbox("条件不成立なら後続命令も中止", &event.stopOnFalse);
                    float offset[] = {event.right, event.up, event.forward};
                    if (ImGui::DragFloat3("位置 (右・上・前)", offset, 0.05f, -50, 50)) { event.right = offset[0]; event.up = offset[1]; event.forward = offset[2]; changed = true; }
                    float sizeOffset[] = {event.sizeRight, event.sizeUp, event.sizeForward};
                    if (ImGui::DragFloat3("基準サイズに比例する位置", sizeOffset, 0.01f, -50, 50)) { event.sizeRight = sizeOffset[0]; event.sizeUp = sizeOffset[1]; event.sizeForward = sizeOffset[2]; changed = true; }
                    changed |= ImGui::DragFloat("水平角度", &event.yawDegrees, 0.1f, -360, 360, "%.2f 度");
                    changed |= ImGui::Checkbox("標的へ向ける", &event.aimAtTarget);
                    changed |= ImGui::DragFloat("標的の高さ補正", &event.targetHeight, 0.05f, -10, 10);
                    if (operation == 0) {
                        changed |= ImGui::DragFloat("距離に掛ける速度倍率 (0=最高速度)", &event.distanceSpeedScale, 0.05f, 0, 10);
                        changed |= ImGui::DragFloat("距離速度への加算", &event.distanceSpeedBias, 0.05f, -100, 100);
                        changed |= ImGui::DragFloat("上向き追加速度", &event.verticalSpeed, 0.05f, -100, 100);
                        changed |= ImGui::SliderInt("同時発射数", &event.count, 1, 32);
                        changed |= ImGui::DragFloat("拡散角度", &event.spreadDegrees, 1, 0, 360);
                    } else {
                        changed |= DrawProfileString("GPUプリセット名", event.preset);
                        changed |= ImGui::Checkbox("方向付きで放出", &event.directed);
                        changed |= ImGui::DragFloat("粒子の速度倍率", &event.particleSpeedScale, 0.05f, 0, 100);
                    }
                    if (ImGui::Button("イベントを削除")) {
                        sequence.events.erase(sequence.events.begin() + i);
                        selectedTrack_ = -1;
                        changed = true;
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                }
                if (sequence.events.size() < 128 && ImGui::Button("イベントを追加")) { sequence.events.emplace_back(); changed = true; }
                std::string error;
                if (!sequence.Validate(&error)) ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "%s", error.c_str());
                if (playerCopyMode_) ImGui::TextDisabled("プレイヤーは前方へ発射します。標的が射程内条件は敵用です。");
            }
            ImGui::TreePop();
        }


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
            const bool controlled = editedAttack->sequence.enabled && editedAttack->sequence.actorControl.enabled;
            if (controlled) ImGui::TextWrapped("予備動作と後隙は上の術者制御・シーケンス長を編集してください。");
            ImGui::BeginDisabled(controlled);
            changed |= ImGui::DragFloat("予備動作", &editedAttack->windupDuration, 0.01f, 0.0f, 20.0f, "%.2f sec");
            changed |= ImGui::DragFloat("攻撃継続", &editedAttack->activeDuration, 0.01f, 0.0f, 20.0f, "%.2f sec");
            changed |= ImGui::DragFloat("硬直", &editedAttack->recoveryDuration, 0.01f, 0.0f, 20.0f, "%.2f sec");
            ImGui::EndDisabled();
            changed |= ImGui::DragFloat("再使用間隔", &editedAttack->cooldown, 0.01f, 0.0f, 30.0f, "%.2f sec");
            ImGui::BeginDisabled(controlled);
            changed |= ImGui::DragFloat("警告先行時間", &editedAttack->warningLeadTime, 0.01f, 0.0f, 5.0f, "%.2f sec");
            changed |= ImGui::DragFloat("キャンセル開始", &editedAttack->cancelWindowStart, 0.01f, 0.0f, 60.0f, "%.2f sec");
            changed |= ImGui::DragFloat("キャンセル終了", &editedAttack->cancelWindowEnd, 0.01f, 0.0f, 60.0f, "%.2f sec");
            ImGui::TextDisabled("開始からの秒数。0 / 0 の場合はキャンセル不可です。");
            ImGui::EndDisabled();
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
            if (std::string(GetSelectedEnemyType()) == "FireSlime") {
                static AnimatorControllerAsset fireSlimeController;
                static bool loaded = fireSlimeController.Load("Resources/json/animator/enemy_fire_slime.json");
                if (ImGui::SmallButton("炎スライムControllerを再読込"))
                    loaded = fireSlimeController.Load("Resources/json/animator/enemy_fire_slime.json");
                if (loaded) {
                    const char* label = editedAttack->animation.empty() ? "(未指定)" : editedAttack->animation.c_str();
                    if (ImGui::BeginCombo("本体アニメーション状態", label)) {
                        for (const auto& state : fireSlimeController.states) {
                            if (state.bodyClipName.empty()) continue;
                            if (ImGui::Selectable(state.name.c_str(), state.name == editedAttack->animation)) {
                                editedAttack->animation = state.name;
                                changed = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::TextDisabled("状態と補間はツール → 演出 → Animator Controllerで編集します。");
                } else ImGui::TextDisabled("炎スライムControllerを読み込めません。");
            } else changed |= DrawProfileString("アニメーション", editedAttack->animation);
            changed |= DrawProfileString("予備動作VFX", editedAttack->windupVfx);
            changed |= DrawProfileString("攻撃中VFX", editedAttack->activeVfx);
            changed |= DrawProfileString("命中・着地VFX", editedAttack->impactVfx);
            changed |= DrawProfileString("SEキュー", editedAttack->audioCue);
            changed |= DrawProfileString("Feedback Cue", editedAttack->feedbackCue);
            ImGui::TreePop();
        }

        profileDirty_ |= changed;
        if (changed) RequestDraftRefresh();
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

#endif
}
