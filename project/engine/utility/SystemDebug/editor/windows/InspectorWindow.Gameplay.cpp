#include "InspectorWindow.h"

#include "DebugEditor.h"
#include "EditorManager.h"
#include "GhostRecorder.h"
#include "IconsFontAwesome5.h"
#include "Object3d.h"
#include "imgui.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <cstring>

namespace fs = std::filesystem;

#ifdef USE_IMGUI

void InspectorWindow::DrawPathMoverSection(Object3d *selectedObject) {
  if (!selectedObject) {
    return;
  }

  // ==========================================
  // 2. パス移動 (GhostRecorder) 設定
  // ==========================================
  ImGui::Separator();
  ImGui::Text(ICON_FA_GHOST " 【パス移動 (GhostRecorder)】");
  std::string currentRecordPreview = selectedObject->GetRecordPathName().empty()
                                         ? "(なし)"
                                         : selectedObject->GetRecordPathName();

  if (ImGui::BeginCombo("パスデータ", currentRecordPreview.c_str())) {
    bool isNoneSelected = selectedObject->GetRecordPathName().empty();
    if (ImGui::Selectable("(なし)", isNoneSelected)) {
      selectedObject->SetRecordPathName("");
      if (selectedObject->recorder_)
        selectedObject->recorder_->Stop();
    }
    if (isNoneSelected)
      ImGui::SetItemDefaultFocus();

    std::string dirPath = "Resources/json/animation/";
    if (fs::exists(dirPath) && fs::is_directory(dirPath)) {
      for (const auto &entry : fs::directory_iterator(dirPath)) {
        if (entry.path().extension() == ".json") {
          std::string fileName = entry.path().stem().string();
          bool isSelected = (selectedObject->GetRecordPathName() == fileName);

          if (ImGui::Selectable(fileName.c_str(), isSelected)) {
            selectedObject->SetRecordPathName(fileName);
            if (selectedObject->recorder_) {
              bool isCinematic = selectedObject->IsCameraObject();
              selectedObject->recorder_->Play(
                  selectedObject->GetRecordPathName(),
                  selectedObject->IsRecordLoop(),
                  selectedObject->IsRecordRelative(), isCinematic);
            }
          }
          if (isSelected)
            ImGui::SetItemDefaultFocus();
        }
      }
    }
    ImGui::EndCombo();
  }

  bool pathLoop = selectedObject->IsRecordLoop();
  if (ImGui::Checkbox("ループ再生##Record", &pathLoop)) {
    selectedObject->SetRecordLoop(pathLoop);
    if (selectedObject->recorder_ &&
        !selectedObject->GetRecordPathName().empty()) {
      bool isCinematic = selectedObject->IsCameraObject();
      selectedObject->recorder_->Play(
          selectedObject->GetRecordPathName(), selectedObject->IsRecordLoop(),
          selectedObject->IsRecordRelative(), isCinematic);
    }
  }
  bool pathRelative = selectedObject->IsRecordRelative();
  if (ImGui::Checkbox("相対座標モード##Record", &pathRelative)) {
    selectedObject->SetRecordRelative(pathRelative);
    if (selectedObject->recorder_ &&
        !selectedObject->GetRecordPathName().empty()) {
      bool isCinematic = selectedObject->IsCameraObject();
      selectedObject->recorder_->Play(
          selectedObject->GetRecordPathName(), selectedObject->IsRecordLoop(),
          selectedObject->IsRecordRelative(), isCinematic);
    }
  }
  if (ImGui::Button("テスト再生##Record")) {
    if (selectedObject->recorder_ &&
        !selectedObject->GetRecordPathName().empty()) {
      bool isCinematic = selectedObject->IsCameraObject();
      selectedObject->recorder_->Play(
          selectedObject->GetRecordPathName(), selectedObject->IsRecordLoop(),
          selectedObject->IsRecordRelative(), isCinematic);
    }
  }
}

void InspectorWindow::DrawGameplayDataSection(Object3d *selectedObject) {
  if (!selectedObject) {
    return;
  }

  // --- Game Data (Stats) ---
  ImGui::Separator();
  if (ImGui::CollapsingHeader(ICON_FA_GAMEPAD " ゲームデータ (Stats)",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    EventType currentType = selectedObject->GetEventType();
    int currentItemIndex = static_cast<int>(currentType);
    const char *eventNames[] = {"なし",
                                "ダメージ",
                                "ワープ",
                                "映像演出 (橋落ち)",
                                "中間地点 (Checkpoint)",
                                "ゴール",
                                "ステージセレクト",
                                "スターコイン (StarCoin)"};
    if (ImGui::Combo(ICON_FA_FLAG " イベント種類", &currentItemIndex,
                     eventNames, IM_ARRAYSIZE(eventNames))) {
      const EventType selectedEventType =
          static_cast<EventType>(currentItemIndex);
      selectedObject->SetEventType(selectedEventType);
      if (selectedEventType == EventType::Goal) {
        selectedObject->SetCollisionAttribute(CollisionAttribute::kTrigger);
        selectedObject->SetCollisionMask(CollisionAttribute::kPlayer);

        if (selectedObject->GetColliderType() == ColliderType::kNone) {
          Object3d::ColliderConfig colConfig;
          colConfig.type = ColliderType::kSphere;
          colConfig.size = {1.2f, 1.2f, 1.2f};
          selectedObject->SetColliderConfig(colConfig);
          selectedObject->SetCollisionRadius(1.2f);
        }
      }
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                       "--- Object Type Settings ---");
    const char *classItems[] = {"Model",   "Spawner", "Player",       "Enemy",
                                "Gimmick", "Item",    "InvisibleBox", "Block"};
    std::string currentClass = selectedObject->GetClassName();
    int currentClassIndex = 0;
    for (int i = 0; i < IM_ARRAYSIZE(classItems); i++) {
      if (currentClass == classItems[i]) {
        currentClassIndex = i;
        break;
      }
    }

    if (ImGui::Combo(ICON_FA_CUBES " Class Type", &currentClassIndex,
                     classItems, IM_ARRAYSIZE(classItems))) {
      selectedObject->SetClassName(classItems[currentClassIndex]);
      if (std::string(classItems[currentClassIndex]) == "Spawner") {
        if (selectedObject->GetName().find("Object") != std::string::npos)
          selectedObject->SetName("Spawner_New");
        if (!selectedObject->param_.has_value())
          selectedObject->param_.emplace();
      } else if (std::string(classItems[currentClassIndex]) == "Item") {
        if (selectedObject->GetName().find("Object") != std::string::npos)
          selectedObject->SetName("Item_Heal");
        if (!selectedObject->param_.has_value())
          selectedObject->param_.emplace();
        selectedObject->SetItemType("Heal");
        selectedObject->param_->itemType = "Heal";
        selectedObject->param_->healAmount = 1.0f;
        selectedObject->SetModel("Item/heart.gltf");
        selectedObject->SetColor({1.0f, 0.15f, 0.35f, 1.0f});
        selectedObject->SetEmissive(1.8f);
        selectedObject->SetScale({0.8f, 0.8f, 0.8f});
        selectedObject->SetCollisionAttribute(CollisionAttribute::kTrigger);
        selectedObject->SetCollisionMask(CollisionAttribute::kPlayer);
        selectedObject->SetStatic(false);

        Object3d::ColliderConfig colConfig;
        colConfig.type = ColliderType::kSphere;
        colConfig.size = {1.2f, 1.2f, 1.2f};
        selectedObject->SetColliderConfig(colConfig);
        selectedObject->SetCollisionRadius(1.2f);
      }
    }

    if (selectedObject->GetClassName() == "Spawner")
      DrawSpawnerSettings();

    ImGui::Spacing();
    if (selectedObject->GetClassName() == "Enemy") {
      ImGui::Indent();
      DrawEnemyTypeSelector();
      ImGui::Unindent();
    }
    if (selectedObject->GetClassName() == "Gimmick") {
      ImGui::Indent();
      DrawGimmickTypeSelector();
      ImGui::Unindent();
    }
    if (selectedObject->GetClassName() == "Item") {
      ImGui::Indent();
      DrawItemTypeSelector();
      ImGui::Unindent();
    }

    if (selectedObject->GetClassName() == "Enemy" ||
        selectedObject->GetClassName() == "Player") {
      ImGui::SeparatorText("キャラクター・ステータス（表示のみ）");
      ImGui::TextDisabled("共通値の編集はステータス管理で行います。現在HPだけは"
                          "実行中に変動します。");
      if (selectedObject->param_.has_value()) {
        const auto &p = selectedObject->param_.value();
        if (ImGui::BeginTable("ManagedCharacterStatus", 2,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_SizingStretchProp)) {
          auto drawValue = [](const char *name, const char *format,
                              float value) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(name);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text(format, value);
          };
          drawValue("現在HP", "%.1f", p.hp);
          drawValue("最大HP", "%.1f", p.maxHp);
          drawValue("攻撃力倍率", "%.2f", p.attackPower);
          drawValue("移動速度", "%.2f", p.speed);
          drawValue("重力", "%.2f", p.gravity);
          drawValue("最大落下速度", "%.2f", p.maxFallSpeed);
          drawValue("ジャンプ力", "%.2f", p.jumpPower);
          drawValue("感知範囲", "%.2f", p.detectionRange);
          ImGui::EndTable();
        }
      } else {
        ImGui::TextDisabled(
            "ステータスは次回の生成・ロード時に自動設定されます。");
      }
      if (ImGui::Button(ICON_FA_SLIDERS_H
                        " ステータス管理を開く##CharacterStatus",
                        ImVec2(-1, 0)) &&
          editor_->GetStatusTuningWindow()) {
        EditorManager::GetInstance()->SetSelectedObject(
            editor_->GetStatusTuningWindow());
      }
    } else if (selectedObject->GetClassName() == "Gimmick") {
      if (!selectedObject->param_.has_value())
        selectedObject->param_.emplace();
      auto &p = selectedObject->param_.value();

      ImGui::Text(ICON_FA_TOOLS " ギミック設定:");
      ImGui::Indent();

      std::string gType = selectedObject->GetGimmickType();
      if (gType == "Trampoline") {
        ImGui::DragFloat(ICON_FA_ARROW_UP " ジャンプ力 (Jump Power)",
                         &p.jumpPower, 1.0f, 0.0f, 100.0f);
      } else if (gType == "LaunchStar") {
        ImGui::DragFloat(ICON_FA_RULER_HORIZONTAL " 発射距離", &p.moveAmount,
                         1.0f, 8.0f, 300.0f, "%.1f m");
        ImGui::DragFloat(ICON_FA_ARROW_UP " 軌道の高さ", &p.jumpPower, 0.5f,
                         2.0f, 100.0f, "%.1f m");
        ImGui::DragFloat(ICON_FA_TACHOMETER_ALT " 飛行速度", &p.speed, 0.5f,
                         8.0f, 160.0f, "%.1f m/s");
        ImGui::TextDisabled("回転X/Yが発射方向になります");
      } else if (gType == "MovingFloor") {
        ImGui::DragFloat(ICON_FA_TACHOMETER_ALT " 移動速度 (Speed)", &p.speed,
                         0.1f, 0.0f, 100.0f);
        const char *moveModes[] = {"従来の上下移動", "浮遊", "X方向に往復",
                                   "Z方向に往復", "Y方向に往復"};
        p.actionMode = (std::clamp)(p.actionMode, 0, 4);
        ImGui::Combo("移動方式", &p.actionMode, moveModes,
                     IM_ARRAYSIZE(moveModes));
        if (p.actionMode == 1) {
          ImGui::DragFloat("浮き沈み幅", &p.moveAmount, 0.01f, 0.0f, 2.0f,
                           "%.2f m");
        } else if (p.actionMode >= 2) {
          ImGui::DragFloat("往復幅", &p.moveAmount, 0.1f, 0.0f, 100.0f,
                           "%.1f m");
        }
      } else if (gType == "HazardRideFloor") {
        ImGui::DragFloat(ICON_FA_TACHOMETER_ALT " 移動速度", &p.speed, 0.1f,
                         0.1f, 40.0f, "%.1f m/s");
        ImGui::DragFloat(ICON_FA_RULER_HORIZONTAL " 移動距離", &p.moveAmount,
                         0.5f, 1.0f, 300.0f, "%.1f m");
        ImGui::DragFloat(ICON_FA_HOURGLASS_HALF " 発進前の予告",
                         &p.shakeDuration, 0.05f, 0.0f, 5.0f, "%.2f s");
        ImGui::DragFloat(ICON_FA_EXCLAMATION_TRIANGLE " 終点の落下予告",
                         &p.interval, 0.05f, 0.1f, 5.0f, "%.2f s");
        ImGui::DragFloat(ICON_FA_ARROW_DOWN " 落下時間", &p.fallDuration, 0.05f,
                         0.1f, 10.0f, "%.2f s");
        ImGui::DragFloat(ICON_FA_WEIGHT_HANGING " 落下重力", &p.gravity, 1.0f,
                         1.0f, 200.0f);
        ImGui::DragInt("妨害数", &p.maxCount, 1, 0, 16);
        const char *rideDirections[] = {"ローカルX+", "ローカルX-"};
        p.actionMode = (std::clamp)(p.actionMode, 0, 1);
        ImGui::Combo("進行方向", &p.actionMode, rideDirections,
                     IM_ARRAYSIZE(rideDirections));
        ImGui::TextDisabled(
            "Target ID を先頭に、Target ID + 妨害番号を順番に起動します");
      } else if (gType == "ChikuwaBlock") {
        ImGui::DragFloat(ICON_FA_HOURGLASS_HALF " 震え時間 (Shake)",
                         &p.shakeDuration, 0.1f, 0.0f, 10.0f, "%.1f s");
        ImGui::DragFloat(ICON_FA_ARROW_DOWN " 落下時間 (Fall)", &p.fallDuration,
                         0.1f, 0.0f, 10.0f, "%.1f s");
        ImGui::DragFloat(ICON_FA_RECYCLE " リスポーン間隔", &p.interval, 0.1f,
                         0.0f, 10.0f, "%.1f s");
        ImGui::DragFloat(ICON_FA_WEIGHT_HANGING " 落下速度 (Gravity)",
                         &p.gravity, 1.0f, 0.0f, 200.0f);
      } else if (gType == "BlinkBlock") {
        ImGui::Text(ICON_FA_PALETTE " ブロックの色設定:");
        ImGui::RadioButton("青 (Blue: Jump Even)", &p.colorType, 0);
        ImGui::SameLine();
        ImGui::RadioButton("赤 (Red: Jump Odd)", &p.colorType, 1);
      } else if (gType == "Switch") {
        const char *switchModes[] = {"押している間だけ", "押すたび切替",
                                     "一定時間だけ"};
        ImGui::Combo("スイッチ方式", &p.switchMode, switchModes,
                     IM_ARRAYSIZE(switchModes));
        if (p.switchMode == 2) {
          ImGui::DragFloat(ICON_FA_CLOCK " 有効時間", &p.interval, 0.1f, 0.1f,
                           60.0f, "%.1f s");
        }
        ImGui::TextDisabled(
            "Target ID と受信側の My Event ID を合わせてください");
      } else if (gType == "EventReceiver") {
        const char *actionModes[] = {
            "出現",   "Y方向に移動", "X方向に移動", "Z方向に移動",
            "有効化", "無効化",      "ボス王冠落下"};
        ImGui::Combo("動作モード", &p.actionMode, actionModes,
                     IM_ARRAYSIZE(actionModes));

        if (p.actionMode >= 1 && p.actionMode <= 3) {
          ImGui::DragFloat(ICON_FA_ARROWS_ALT " 移動量", &p.moveAmount, 0.1f,
                           -500.0f, 500.0f);
          ImGui::DragFloat(ICON_FA_TACHOMETER_ALT " 移動速度", &p.moveSpeed,
                           0.1f, 0.1f, 60.0f);
        } else if (p.actionMode == 6) {
          ImGui::DragFloat("ボスから着地点までの距離", &p.moveAmount, 0.1f,
                           0.0f, 80.0f, "%.2f");
          ImGui::DragFloat("落下演出時間", &p.fallDuration, 0.02f, 0.35f, 6.0f,
                           "%.2f s");
          ImGui::DragFloat("弧の高さ", &p.jumpPower, 0.05f, 0.0f, 12.0f,
                           "%.2f");
          ImGui::DragFloat("回転速度", &p.moveSpeed, 0.1f, -20.0f, 20.0f,
                           "%.2f");
          ImGui::TextDisabled(
              "編集時は非表示。撃破通知後にボス頭上から着地点へ落下します");
        }

        ImGui::Checkbox("開始時に有効", &p.startActive);
        ImGui::Checkbox("OFFで元に戻す", &p.returnOnOff);
        ImGui::TextDisabled(
            "My Event ID とスイッチの Target ID を合わせてください");
      } else if (gType == "ArenaEncounter") {
        ImGui::DragInt(ICON_FA_BORDER_ALL " 障壁数", &p.maxCount, 1.0f, 1, 16);
        ImGui::DragFloat(ICON_FA_HOURGLASS_HALF " ボス出現まで",
                         &p.shakeDuration, 0.02f, 0.05f, 3.0f, "%.2f s");
        ImGui::Checkbox("遭遇を有効にする", &p.startActive);
        ImGui::TextDisabled("Target ID: 中ボスの My Event ID");
        ImGui::TextDisabled("My Event ID: 中ボス撃破時の Target ID");
        ImGui::TextDisabled("障壁は Target ID + 1 から障壁数ぶんを使用します");
      } else if (gType == "GameplayVolume") {
        const char *volumeModes[] = {
            "イベント連携", "チェックポイント", "落下死",
            "Feedback Cue", "BGM", "Environment Profile"};
        p.volumeMode = (std::clamp)(p.volumeMode, 0, 5);
        ImGui::Combo("処理", &p.volumeMode, volumeModes, IM_ARRAYSIZE(volumeModes));

        if (p.volumeMode == 3 || p.volumeMode == 4 || p.volumeMode == 5) {
          char payloadBuffer[256]{};
          strncpy_s(payloadBuffer, p.volumePayload.c_str(), _TRUNCATE);
          if (ImGui::InputText("Cue / Audio ID", payloadBuffer, sizeof(payloadBuffer))) {
            p.volumePayload = payloadBuffer;
          }
        }
        if (p.volumeMode == 5) {
          ImGui::DragFloat("切替時間", &p.volumeBlendDuration, 0.05f, 0.0f, 10.0f, "%.2f s");
        }

        ImGui::Checkbox("一度だけ実行", &p.volumeTriggerOnce);
        ImGui::Checkbox("退出時に実行", &p.volumeTriggerOnExit);
        if (!p.volumeTriggerOnce) {
          ImGui::DragFloat("再実行まで", &p.volumeRearmDelay, 0.05f, 0.0f, 60.0f, "%.2f s");
        }
        ImGui::Checkbox("開始時に有効", &p.startActive);

        const bool supportsReturn = (p.volumeMode == 0 || p.volumeMode == 4 || p.volumeMode == 5) && !p.volumeTriggerOnExit;
        ImGui::BeginDisabled(!supportsReturn);
        ImGui::Checkbox("退出時にOFFへ戻す", &p.returnOnOff);
        ImGui::EndDisabled();

        if (p.volumeMode == 0) {
          ImGui::TextDisabled("Target ID と受信側の My Event ID を合わせてください");
        } else if (p.volumeMode == 1) {
          ImGui::TextDisabled("ボリューム中心をリスポーン地点として記録します");
        } else if (p.volumeMode == 2) {
          ImGui::TextDisabled("既存の落下・残機処理へ接続します");
        }
      } else if (gType == "PrismBarrier") {
        ImGui::DragFloat(ICON_FA_HOURGLASS_HALF " 展開・解除時間", &p.moveSpeed,
                         0.02f, 0.05f, 3.0f, "%.2f s");
        ImGui::DragFloat(ICON_FA_BOLT " 発光パルス速度", &p.interval, 0.05f,
                         0.1f, 8.0f, "%.2f Hz");
        ImGui::Checkbox("開始時に展開", &p.startActive);
        ImGui::Checkbox("OFFで解除", &p.returnOnOff);
        ImGui::TextDisabled(
            "My Event ID は遭遇管理の Target ID + 連番にします");
      } else if (gType == "HookPullBlock") {
        ImGui::DragFloat(ICON_FA_TACHOMETER_ALT " 引っ張り速度", &p.speed, 0.5f,
                         1.0f, 120.0f);
        ImGui::DragFloat(ICON_FA_WEIGHT_HANGING " 重力", &p.gravity, 1.0f, 0.0f,
                         200.0f);
        ImGui::TextDisabled("フックを当てるとプレイヤー側へ引き寄せます");
      } else if (gType == "OneWayFloor") {
        ImGui::TextDisabled("上から着地した時だけ足場になります");
      } else if (gType == "LiquidLevel") {
        const char *liquidTypes[] = {"水", "マグマ"};
        ImGui::Combo("液体の種類", &p.colorType, liquidTypes,
                     IM_ARRAYSIZE(liquidTypes));
        ImGui::DragFloat(ICON_FA_ARROWS_ALT_V " 上下量", &p.moveAmount, 0.1f,
                         -500.0f, 500.0f);
        ImGui::DragFloat(ICON_FA_TACHOMETER_ALT " 上下速度", &p.moveSpeed, 0.1f,
                         0.1f, 60.0f);
        ImGui::Checkbox("開始時に上昇", &p.startActive);
        ImGui::Checkbox("OFFで元に戻す", &p.returnOnOff);
        ImGui::TextDisabled(
            "スイッチの Target ID とこの My Event ID を合わせてください");
      } else if (gType == "MagmaHazard") {
        ImGui::DragFloat(ICON_FA_FIRE " 接触ダメージ", &p.speed, 0.5f, 0.0f,
                         100.0f);
        ImGui::DragFloat(ICON_FA_CLOCK " ダメージ間隔", &p.interval, 0.05f,
                         0.1f, 10.0f, "%.2f s");
        ImGui::TextDisabled("触れたプレイヤーを炎上させ、上方向へ弾き返します");
      } else if (gType == "MagmaGeyser") {
        ImGui::DragFloat(ICON_FA_HOURGLASS_HALF " 警告時間", &p.shakeDuration,
                         0.05f, 0.2f, 10.0f, "%.2f s");
        ImGui::DragFloat(ICON_FA_FIRE " 噴出時間", &p.fallDuration, 0.05f, 0.2f,
                         10.0f, "%.2f s");
        ImGui::DragFloat(ICON_FA_CLOCK " 休止時間", &p.interval, 0.05f, 0.2f,
                         30.0f, "%.2f s");
        ImGui::DragFloat(ICON_FA_STOPWATCH " 初回ディレイ", &p.moveSpeed, 0.05f,
                         0.0f, 30.0f, "%.2f s");
        ImGui::DragFloat(ICON_FA_ARROWS_ALT_V " 噴出高", &p.moveAmount, 0.25f,
                         0.5f, 60.0f, "%.1f m");
        ImGui::DragFloat(ICON_FA_EYE " シミュレーション距離", &p.maxFallSpeed,
                         1.0f, 20.0f, 500.0f, "%.0f m");
        ImGui::DragFloat(ICON_FA_CIRCLE " 危険半径", &p.detectionRange, 0.05f,
                         0.25f, 20.0f, "%.2f m");
        ImGui::DragFloat(ICON_FA_HEART_BROKEN " ダメージ", &p.speed, 0.5f, 0.0f,
                         100.0f);
        ImGui::DragFloat(ICON_FA_ARROWS_ALT_H " 横吹き飛ばし", &p.gravity,
                         0.25f, 0.0f, 80.0f);
        ImGui::DragFloat(ICON_FA_ARROW_UP " 上吹き飛ばし", &p.jumpPower, 0.5f,
                         0.0f, 100.0f);
        ImGui::Checkbox("開始時に有効", &p.startActive);
        ImGui::Checkbox("OFFで停止", &p.returnOnOff);
        ImGui::TextDisabled(
            "警告リングが消えている噴出中だけダメージ判定が有効です");
      } else if (gType == "FallingSpike") {
        ImGui::DragFloat(ICON_FA_HOURGLASS_HALF " 落下前の予告",
                         &p.shakeDuration, 0.05f, 0.05f, 5.0f, "%.2f s");
        ImGui::DragFloat(ICON_FA_RULER_VERTICAL " 落下距離", &p.moveAmount,
                         0.5f, 1.0f, 100.0f, "%.1f m");
        ImGui::DragFloat(ICON_FA_WEIGHT_HANGING " 落下重力", &p.gravity, 1.0f,
                         1.0f, 200.0f);
        ImGui::DragFloat(ICON_FA_HEART_BROKEN " ダメージ", &p.speed, 0.5f, 0.0f,
                         100.0f);
        ImGui::DragFloat(ICON_FA_ARROWS_ALT_H " 横吹き飛ばし", &p.moveSpeed,
                         0.25f, 0.0f, 80.0f);
        ImGui::DragFloat(ICON_FA_ARROW_UP " 上吹き飛ばし", &p.jumpPower, 0.5f,
                         0.0f, 100.0f);
        ImGui::Checkbox("開始時に落下", &p.startActive);
        ImGui::TextDisabled("My Event ID を輸送床から順番に起動します");
      } else if (gType == "ChainCollapseFloor") {
        ImGui::DragFloat(ICON_FA_HOURGLASS_HALF " 揺れ時間", &p.shakeDuration,
                         0.05f, 0.0f, 10.0f, "%.2f s");
        ImGui::DragFloat(ICON_FA_LINK " 連鎖までの時間", &p.interval, 0.01f,
                         0.0f, 10.0f, "%.2f s");
        ImGui::DragFloat(ICON_FA_ARROW_DOWN " 落下時間", &p.fallDuration, 0.05f,
                         0.1f, 10.0f, "%.2f s");
        ImGui::DragFloat(ICON_FA_WEIGHT_HANGING " 重力", &p.gravity, 1.0f, 0.0f,
                         200.0f);
        ImGui::TextDisabled(
            "Target ID に次の床の My Event ID を入れると連鎖します");
      } else if (gType == "RotatingFloor" || gType == "RotatingPillar") {
        const char *axes[] = {"X", "Y", "Z"};
        ImGui::Combo("回転軸", &p.actionMode, axes, IM_ARRAYSIZE(axes));
        ImGui::DragFloat(ICON_FA_SYNC_ALT " 回転速度 (度/秒)", &p.speed, 1.0f,
                         -720.0f, 720.0f);
        ImGui::Checkbox("開始時に回転", &p.startActive);
        ImGui::Checkbox("OFFで停止", &p.returnOnOff);
        ImGui::TextDisabled("スイッチ連動で回転の開始/停止ができます");
      } else if (gType == "PhaseFlipFloor") {
        int floorNumber = p.colorType + 1;
        int phaseCount = (std::max)(1, p.maxCount);
        ImGui::DragInt("床番号", &floorNumber, 1, 1, phaseCount);
        p.colorType = (std::clamp)(floorNumber, 1, phaseCount) - 1;

        ImGui::DragInt("全体の床数", &p.maxCount, 1, 1, 16);
        if (p.colorType >= p.maxCount)
          p.colorType = p.maxCount - 1;

        ImGui::DragFloat(ICON_FA_CLOCK " 1フェーズの時間", &p.interval, 0.05f,
                         0.1f, 30.0f, "%.2f s");
        ImGui::Checkbox("正方向に回転", &p.startActive);
        ImGui::TextDisabled("床番号 1 -> 2 -> 3 ... "
                            "の順に、当たり判定を残したまま180度回転します");
      } else if (gType == "FireCannon") {
        const char *aimModes[] = {"前方固定", "プレイヤーを狙う"};
        ImGui::Combo("発射方向", &p.actionMode, aimModes,
                     IM_ARRAYSIZE(aimModes));
        ImGui::DragFloat(ICON_FA_FIRE " 火球速度", &p.speed, 0.5f, 1.0f,
                         120.0f);
        ImGui::DragFloat(ICON_FA_CLOCK " 発射間隔", &p.interval, 0.05f, 0.08f,
                         20.0f, "%.2f s");
        ImGui::DragFloat(ICON_FA_CIRCLE " 火球サイズ", &p.moveAmount, 0.01f,
                         0.1f, 5.0f);
        ImGui::DragFloat(ICON_FA_SEARCH " 索敵範囲", &p.detectionRange, 0.5f,
                         0.0f, 300.0f);
        ImGui::DragFloat(ICON_FA_SYNC_ALT " 旋回速度 (度/秒)", &p.moveSpeed,
                         1.0f, 1.0f, 720.0f);
        ImGui::Checkbox("開始時に有効", &p.startActive);
        ImGui::Checkbox("OFFで停止", &p.returnOnOff);
        ImGui::TextDisabled("前方固定はローカルZ+方向に火球を撃ちます");
      } else if (gType == "LaserEmitter") {
        ImGui::DragFloat(ICON_FA_BOLT " ダメージ量", &p.speed, 0.5f, 0.0f,
                         100.0f);
        ImGui::DragFloat(ICON_FA_CLOCK " ダメージ間隔", &p.interval, 0.05f,
                         0.05f, 10.0f, "%.2f s");
        ImGui::DragFloat(ICON_FA_ARROWS_ALT_H " レーザーの太さ", &p.moveAmount,
                         0.01f, 0.03f, 5.0f);
        ImGui::Checkbox("開始時に有効", &p.startActive);
        ImGui::Checkbox("OFFで停止", &p.returnOnOff);
        ImGui::TextDisabled(
            "Target ID に終点ノードの My Event ID を入れると接続します");
      } else if (gType == "LaserNode") {
        ImGui::DragFloat(ICON_FA_BOLT " ダメージ量", &p.speed, 0.5f, 0.0f,
                         100.0f);
        ImGui::DragFloat(ICON_FA_CLOCK " ダメージ間隔", &p.interval, 0.05f,
                         0.05f, 10.0f, "%.2f s");
        ImGui::DragFloat(ICON_FA_ARROWS_ALT_H " レーザーの太さ", &p.moveAmount,
                         0.01f, 0.03f, 5.0f);
        ImGui::Checkbox("開始時に有効", &p.startActive);
        ImGui::Checkbox("OFFで停止", &p.returnOnOff);
        ImGui::TextDisabled("Target ID に次の LaserNode の My Event ID "
                            "を入れると、その間にレーザーが出ます");
      } else if (gType == "StageGate") {
        const char *gateModes[] = {"ステージセレクト用ノード",
                                   "シーン転移ゲート", "ステージ開始ゲート"};
        p.actionMode = (std::clamp)(p.actionMode, 0, 2);
        ImGui::Combo("ゲートモード", &p.actionMode, gateModes,
                     IM_ARRAYSIZE(gateModes));
        ImGui::Checkbox("開始時に有効", &p.startActive);

        if (p.actionMode == 0) {
          int stageIndex = selectedObject->GetTargetID();
          if (ImGui::InputInt("ステージ番号 (Target ID)", &stageIndex)) {
            selectedObject->SetTargetID(stageIndex);
          }
          ImGui::TextDisabled(
              "ステージセレクトで近づいて決定した時だけ使われます");
        } else if (p.actionMode == 1) {
          const char *sceneValues[] = {"TITLE",    "TUTORIAL",  "SELECT",
                                       "GAMEPLAY", "GAMECLEAR", "GAMEOVER",
                                       "SETTING",  "PREVIEW"};
          const char *sceneLabels[] = {
              "タイトル",   "チュートリアル", "ステージセレクト",
              "ゲーム本編", "ゲームクリア",   "ゲームオーバー",
              "設定",       "プレビュー"};
          int sceneIndex = 2;
          for (int i = 0; i < IM_ARRAYSIZE(sceneValues); ++i) {
            if (p.targetScene == sceneValues[i]) {
              sceneIndex = i;
              break;
            }
          }
          if (ImGui::Combo("転移先シーン", &sceneIndex, sceneLabels,
                           IM_ARRAYSIZE(sceneLabels))) {
            p.targetScene = sceneValues[sceneIndex];
          }
          if (p.targetScene.empty()) {
            p.targetScene = "SELECT";
          }
          ImGui::TextDisabled("プレイヤーが触れると指定シーンへ遷移します");
        } else if (p.actionMode == 2) {
          int stageIndex = selectedObject->GetTargetID();
          if (ImGui::InputInt("開始ステージ番号 (Target ID)", &stageIndex)) {
            selectedObject->SetTargetID(stageIndex);
          }
          ImGui::TextDisabled("プレイヤーが触れると指定ステージをセットしてゲー"
                              "ム本編へ遷移します");
        }
      } else {
        ImGui::TextDisabled("(この種類には個別設定がありません)");
      }
      ImGui::Unindent();
    } else if (selectedObject->GetClassName() == "Item") {
      if (!selectedObject->param_.has_value())
        selectedObject->param_.emplace();
      auto &p = selectedObject->param_.value();

      ImGui::Text(ICON_FA_HEART " アイテム設定:");
      ImGui::Indent();

      std::string itemType = selectedObject->GetItemType();
      if (itemType == "Heal") {
        ImGui::DragFloat(ICON_FA_HEARTBEAT " 回復量", &p.healAmount, 0.1f, 0.0f,
                         999.0f);
        ImGui::TextDisabled("プレイヤーが触れるとHPを回復して消えます");
      } else {
        ImGui::TextDisabled("(この種類には個別設定がありません)");
      }

      ImGui::Unindent();
    }
  }
}

#else

void InspectorWindow::DrawPathMoverSection(Object3d *) {}
void InspectorWindow::DrawGameplayDataSection(Object3d *) {}

#endif
