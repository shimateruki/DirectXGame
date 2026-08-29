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

    const std::string className = selectedObject->GetClassName();
    if (className == "Player" || className == "Enemy" ||
        className == "Gimmick" || className == "Item" || className == "Spawner") {
      ImGui::SeparatorText("ゲーム側の拡張パラメータ");
      ImGui::TextDisabled("型固有の編集UIはゲーム側の実装と一緒に追加してください。");
      ImGui::TextDisabled("登録済みの型名は上のFactory連動セレクターから選択できます。");
    }

  }
}

#else

void InspectorWindow::DrawPathMoverSection(Object3d *) {}
void InspectorWindow::DrawGameplayDataSection(Object3d *) {}

#endif
