#include "engine/utility/SystemDebug/editor/windows/inspector/InspectorWindow.h"
#include "EditorPropertyRegistry.h"

#include "DebugEditor.h"
#include "EditorManager.h"
#include "GhostRecorder.h"
#include "IconsFontAwesome5.h"
#include "Object3d.h"
#include "Player.h"
#include "game/actor/enemy/boss/false_king/EnemyFalseKingSlime.h"
#include "imgui.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

#ifdef USE_IMGUI

void InspectorWindow::DrawPathMoverSection(engine::Object3d *selectedObject) {
  if (!selectedObject) {
    return;
  }

  // ==========================================
  // 2. パス移動 (GhostRecorder) 設定
  // ==========================================
  ImGui::Separator();
  ImGui::Text(ICON_FA_GHOST " 【パス移動 (GhostRecorder)】");
  if (editor_->GetGhostRecorder() && ImGui::Button("このObjectの動きを作る##GhostRecorder")) {
    editor_->GetGhostRecorder()->SetTarget(selectedObject);
    EditorManager::GetInstance()->SetSelectedObject(editor_->GetGhostRecorder());
  }

  // 乗車タイムラインと旧Pathの排他をComponentの状態で判定し、具体的な型名を持ちません。
  const auto* ridePath = selectedObject->GetPathMoverComponent();
  if (ridePath && !ridePath->GetRideSequence().empty()) {
    ImGui::TextDisabled("乗車タイムラインを使用中です。上のクラス設定から変更できます。");
    return;
  }

  std::string currentRecordPreview = selectedObject->GetRecordPathName().empty()
                                         ? "(なし)"
                                         : selectedObject->GetRecordPathName();

  if (ImGui::BeginCombo("パスデータ", currentRecordPreview.c_str())) {
    bool isNoneSelected = selectedObject->GetRecordPathName().empty();

    if (ImGui::Selectable("(なし)", isNoneSelected)) {
      selectedObject->SetRecordPathName("");
      if (selectedObject->GetRecorder())
        selectedObject->GetRecorder()->Stop();
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

            if (selectedObject->GetRecorder()) {
              bool isCinematic = selectedObject->IsCameraObject();
              selectedObject->GetRecorder()->Play(
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

  auto* pathSettings = selectedObject->EnsurePathMoverComponent();
  if (!selectedObject->IsCameraObject()) {
    int playbackMode = static_cast<int>(pathSettings->GetPlaybackMode());
    const char* modes[] = {"常時再生", "乗っている間再生（離れるとその場で保持）", "乗ると終点まで再生（離れても継続）"};
    if (ImGui::Combo("再生条件", &playbackMode, modes, IM_ARRAYSIZE(modes))) {
      pathSettings->SetPlaybackMode(static_cast<PathMoverComponent::PlaybackMode>(playbackMode));
      // 再設定時は古い開始ラッチを破棄する。相対基準の累積を避けてから再開する。
      if (selectedObject->GetRecorder() && !selectedObject->GetRecordPathName().empty()) {
        selectedObject->GetRecorder()->Stop();
        selectedObject->GetRecorder()->Play(selectedObject->GetRecordPathName(), selectedObject->IsRecordLoop(),
                                       selectedObject->IsRecordRelative(), false);
      }
    }
    ImGui::TextWrapped("乗車条件は地面Colliderへの接地で判定します。横からの接触や敵の乗車では開始しません。");
    if (pathSettings->GetPlaybackMode() == PathMoverComponent::PlaybackMode::RideToEnd)
      ImGui::TextWrapped("終点まで1回再生して停止します（ループ指定より優先）。再実行はテスト再生またはシーン再開始で行います。");
  }

  bool pathLoop = selectedObject->IsRecordLoop();

  if (ImGui::Checkbox("ループ再生##Record", &pathLoop)) {
    selectedObject->SetRecordLoop(pathLoop);
    if (selectedObject->GetRecorder() &&
        !selectedObject->GetRecordPathName().empty()) {
      bool isCinematic = selectedObject->IsCameraObject();
      selectedObject->GetRecorder()->Play(
          selectedObject->GetRecordPathName(), selectedObject->IsRecordLoop(),
          selectedObject->IsRecordRelative(), isCinematic);
    }
  }

  bool pathRelative = selectedObject->IsRecordRelative();

  if (ImGui::Checkbox("配置位置を基準に移動（固定座標を解除）##Record", &pathRelative)) {
    selectedObject->SetRecordRelative(pathRelative);
    if (selectedObject->GetRecorder() &&
        !selectedObject->GetRecordPathName().empty()) {
      bool isCinematic = selectedObject->IsCameraObject();
      selectedObject->GetRecorder()->Play(
          selectedObject->GetRecordPathName(), selectedObject->IsRecordLoop(),
          selectedObject->IsRecordRelative(), isCinematic);
    }
  }

  ImGui::TextDisabled("OFF: パスJSONの固定座標 / ON: Objectの配置位置 + パス内の移動量");

  float phaseOffset = pathSettings->GetPhaseOffset();
  if (ImGui::SliderFloat("開始タイミング##Record", &phaseOffset, 0.0f, 0.99f, "周期の %.2f")) {
    pathSettings->SetPhaseOffset(phaseOffset);
    if (selectedObject->GetRecorder() && !selectedObject->GetRecordPathName().empty()) {
      selectedObject->GetRecorder()->Play(selectedObject->GetRecordPathName(), selectedObject->IsRecordLoop(),
                                      selectedObject->IsRecordRelative(), selectedObject->IsCameraObject());
    }
  }

  if (ImGui::Button("テスト再生##Record")) {
    if (selectedObject->GetRecorder() &&
        !selectedObject->GetRecordPathName().empty()) {
      bool isCinematic = selectedObject->IsCameraObject();
      selectedObject->GetRecorder()->Play(
          selectedObject->GetRecordPathName(), selectedObject->IsRecordLoop(),
          selectedObject->IsRecordRelative(), isCinematic);
    }
  }
}

void InspectorWindow::DrawGameplayDataSection(engine::Object3d *selectedObject) {
  // 選択物のイベント、生成種別、型固有パラメータを同じInspectorへ表示します。
  // Scene JSONに保存される値を直接編集するため、型変更時だけ必要な初期値もここで補います。
  if (!selectedObject) {
    return;
  }

  // 偽王の終幕調整は通常の共通パラメータでは表せないため、専用UIへ委譲します。
  if (auto* king = dynamic_cast<EnemyFalseKingSlime*>(selectedObject))
      king->DrawFinaleEditor();

  if (auto* player = dynamic_cast<Player*>(selectedObject))
      player->DrawWhipEditor();

  // --- Game Data (Stats) ---
  ImGui::Separator();
  if (ImGui::CollapsingHeader(ICON_FA_GAMEPAD " ゲームデータ (Stats)",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                       "--- Object Type Settings ---");
    const char *classItems[] = {"Model",   "Spawner", "Player",       "Enemy",
                                "Gimmick", "InvisibleBox", "Block"};
    std::string currentClass = selectedObject->GetClassName();
    int currentClassIndex = 0;

    for (int i = 0; i < IM_ARRAYSIZE(classItems); i++) {
      if (currentClass == classItems[i]) {
        currentClassIndex = i;
        break;
      }
    }

    // 一時ドロップ星はゲームが生成するため、配置用の型へ切り替えません。
    if (currentClass == "Item") {
      ImGui::TextUnformatted("Class Type: Item（実行時生成）");
    } else if (ImGui::Combo(ICON_FA_CUBES " Class Type", &currentClassIndex,
                     classItems, IM_ARRAYSIZE(classItems))) {
      selectedObject->SetClassName(classItems[currentClassIndex]);

      if (std::string(classItems[currentClassIndex]) == "Spawner") {
        if (selectedObject->GetName().find("Object") != std::string::npos)
          selectedObject->SetName("Spawner_New");
        if (!selectedObject->GetParameters().has_value())
          selectedObject->ResetParameters();
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

    // Player/Enemyの共通ステータスは専用管理器が正本です。この表からは変更させず、
    // 配置ごとの例外設定だけを下で編集します。
    if (selectedObject->GetClassName() == "Enemy" ||
        selectedObject->GetClassName() == "Player") {
      ImGui::SeparatorText("キャラクター・ステータス（表示のみ）");
      ImGui::TextDisabled("共通値の編集は下の「この種類の基本ステータスを調整」から行います。現在HPだけは"
                          "実行中に変動します。");
      if (selectedObject->GetParameters().has_value()) {
        const auto p = selectedObject->GetParameters().value();
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



      if (selectedObject->GetParameters().has_value() &&
          selectedObject->GetParameters()->enemyType == "RingBurner") {
        // リング攻撃の半径・速度・予告は配置ごとの値として保存する。
        auto p = selectedObject->GetParameters().value();
        ImGui::SeparatorText("リング攻撃（配置ごとに保存）");
        ImGui::DragFloat("最大半径", &p.moveAmount, 0.25f, 6.0f, 48.0f);
        ImGui::DragFloat("リング拡大速度 (倍率)", &p.ringWaveSpeed, 0.05f, 0.25f, 4.0f, "%.2f x");
        p.ringWaveSpeed = (std::clamp)(p.ringWaveSpeed, 0.25f, 4.0f);
        ImGui::DragFloat("攻撃の待機時間", &p.interval, 0.05f, 1.2f, 8.0f);
        ImGui::DragFloat("発射前の予告時間", &p.shakeDuration, 0.05f, 0.45f, 1.4f);
        ImGui::Text("最大半径への到達: %.2f 秒", 3.6f / p.ringWaveSpeed);
        ImGui::TextWrapped("リングは等速に拡大します。頭に乗ると停止し、降りた後も4秒間停止します。感知範囲は上の「この種類の基本ステータスを調整」から変更できます。");
        selectedObject->SetParameters(p);
      }
    } else if (selectedObject->GetClassName() == "Gimmick") {
      // 種類別の入力欄はクラス側の登録から共通Component欄へ自動生成します。
      ImGui::TextDisabled("固有設定は上のクラス名付きComponent欄で編集できます。");
    }
  }
}

#else

void InspectorWindow::DrawPathMoverSection(engine::Object3d *) {}
void InspectorWindow::DrawGameplayDataSection(engine::Object3d *) {}

#endif
