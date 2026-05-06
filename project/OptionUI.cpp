#include "OptionUI.h"
#include "BaseScene.h"
#include "DebugConsole.h"
#include "KeyConfig.h"
#include "SaveDataManager.h"
#include "SpriteCommon.h"
#include "TextureManager.h"
#include <CameraEditor.h>


void OptionUI::Initialize(BaseScene *scene, SpriteCommon *spriteCommon) {
  if (!scene || !spriteCommon)
    return;
  spriteCommon_ = spriteCommon;
  KeyConfig::GetInstance()->Initialize();
  // エディタで配置したベースUIの取得
  bgSprite_ = scene->GetSpriteByName("UI/back_ground.png");
  titleSprite_ = scene->GetSpriteByName("UI/option_UI.png");
  soundSprite_ = scene->GetSpriteByName("UI/sound.png");
  keyboardSprite_ = scene->GetSpriteByName("UI/keyboard.png");
  spaceIconSprite_ = scene->GetSpriteByName("UI/Space.png");
  cursorSprite_ = scene->GetSpriteByName("UI/cursor.png");

  if (cursorSprite_) {
    cursorBasePos_ = cursorSprite_->GetPosition();
  }

  cameraSoundUISprite_ = scene->GetSpriteByName("option/cameraSaundUI.png");
  selectLeftSprite_ = scene->GetSpriteByName("option/UI_selectLeft.png");
  selectRightSprite_ = scene->GetSpriteByName("option/UI_selectRight.png");

  bgmSelectSprite_ = scene->GetSpriteByName("option/BGM_select.png");
  seSelectSprite_ = scene->GetSpriteByName("option/SE_select.png");
  cameraSelectSprite_ = scene->GetSpriteByName("option/camera_select.png");
  bgmBarSprite_ = scene->GetSpriteByName("option/BGM_bar.png"); // 仮の名前
  seBarSprite_ = scene->GetSpriteByName("option/SE_bar.png"); // 仮の名前

  currentTopTab_ = (int)TopTab::AudioCamera;
  currentState_ = MenuState::TabSelect; 
  currentSoundOptionIndex_ = (int)SoundOptionIndex::BGM;
  currentOptionIndex_ = (int)OptionIndex::Sound;
  currentConfigIndex_ = 0;
  sensitivityBarSprite_ = scene->GetSpriteByName("option/camera_pole.png"); // 仮
  if (!sensitivityBarSprite_) sensitivityBarSprite_ = scene->GetSpriteByName("UI/pole.png");

  // サウンド用
  volumeBarSprite_ = scene->GetSpriteByName("UI/volume_pole.png");
  soundConfigCursorSprite_ = scene->GetSpriteByName("UI/sound_cursor.png");

  SaveDataManager::GetInstance()->Load();

  bgmBarMaxPosX_ = bgmBarSprite_ ? bgmBarSprite_->GetPosition().x : 750.0f;
  seBarMaxPosX_ = seBarSprite_ ? seBarSprite_->GetPosition().x : 750.0f;
  cameraCenterPosX_ = sensitivityBarSprite_ ? sensitivityBarSprite_->GetPosition().x : 1168.0f;

  UpdateSensitivityBar();
  UpdateBGMBar();
  UpdateSEBar();
  // ========================================================
  // ★ エディタ配置スプライトは「目印」なので、透明にして隠す！
  // ========================================================
  actionSprites_.clear();
  for (const auto &name : actionSpriteNames_) {
    Sprite *sp = scene->GetSpriteByName(name);
    if (sp) {
      sp->SetColor({1.0f, 1.0f, 1.0f, 0.0f}); // 透明マジック！
    }
    actionSprites_.push_back(sp);
  }

  RefreshKeyIcons(); // 初回生成
}

bool OptionUI::Update(float deltaTime) {
  InputManager *input = InputManager::GetInstance();
  Vector4 normalColor = {0.5f, 0.5f, 0.5f, 1.0f};
  Vector4 selectColor = {1.0f, 1.0f, 1.0f, 1.0f};

  // --- タブなどの表示状態を更新 ---
  auto UpdateSelectHighlights = [&]() {
      if (bgmSelectSprite_) bgmSelectSprite_->SetVisible(currentState_ != MenuState::TabSelect && currentSoundOptionIndex_ == (int)SoundOptionIndex::BGM);
      if (seSelectSprite_) seSelectSprite_->SetVisible(currentState_ != MenuState::TabSelect && currentSoundOptionIndex_ == (int)SoundOptionIndex::SE);
      if (cameraSelectSprite_) cameraSelectSprite_->SetVisible(currentState_ != MenuState::TabSelect && currentSoundOptionIndex_ == (int)SoundOptionIndex::Camera);

      // アクティブな状態のときに色を濃くする（赤色など）
      if (currentState_ == MenuState::ValueAdjust) {
          if (currentSoundOptionIndex_ == (int)SoundOptionIndex::BGM && bgmSelectSprite_) bgmSelectSprite_->SetColor({1.0f, 0.0f, 0.0f, 1.0f}); // 決定時は赤
          if (currentSoundOptionIndex_ == (int)SoundOptionIndex::SE && seSelectSprite_) seSelectSprite_->SetColor({1.0f, 0.0f, 0.0f, 1.0f});
          if (currentSoundOptionIndex_ == (int)SoundOptionIndex::Camera && cameraSelectSprite_) cameraSelectSprite_->SetColor({1.0f, 0.0f, 0.0f, 1.0f});
      } else {
          if (bgmSelectSprite_) bgmSelectSprite_->SetColor({1.0f, 1.0f, 0.0f, 1.0f}); // 選択中は黄色
          if (seSelectSprite_) seSelectSprite_->SetColor({1.0f, 1.0f, 0.0f, 1.0f});
          if (cameraSelectSprite_) cameraSelectSprite_->SetColor({1.0f, 1.0f, 0.0f, 1.0f});
      }
  };

  switch (currentState_) {
  case MenuState::TabSelect: {
      if (input->IsKeyTriggered(DIK_A)) {
          currentTopTab_ = (int)TopTab::AudioCamera;
      }
      if (input->IsKeyTriggered(DIK_D)) {
          currentTopTab_ = (int)TopTab::Credit;
      }

      if (input->IsKeyTriggered(DIK_BACKSPACE) || input->IsKeyTriggered(DIK_ESCAPE) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_B)) {
          SaveDataManager::GetInstance()->Save();
          return true; // オプションを閉じる
      }

      if (input->IsKeyTriggered(DIK_SPACE) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_A)) {
          if (currentTopTab_ == (int)TopTab::AudioCamera) {
              currentState_ = MenuState::ItemSelect;
          }
      }
      UpdateSelectHighlights();
      break;
  }
  case MenuState::ItemSelect: {
      if (input->IsKeyTriggered(DIK_W)) {
          if (currentSoundOptionIndex_ == (int)SoundOptionIndex::SE) currentSoundOptionIndex_ = (int)SoundOptionIndex::BGM;
      }
      if (input->IsKeyTriggered(DIK_S)) {
          if (currentSoundOptionIndex_ == (int)SoundOptionIndex::BGM) currentSoundOptionIndex_ = (int)SoundOptionIndex::SE;
      }
      if (input->IsKeyTriggered(DIK_A)) {
          if (currentSoundOptionIndex_ == (int)SoundOptionIndex::Camera) currentSoundOptionIndex_ = (int)SoundOptionIndex::BGM;
      }
      if (input->IsKeyTriggered(DIK_D)) {
          if (currentSoundOptionIndex_ == (int)SoundOptionIndex::BGM || currentSoundOptionIndex_ == (int)SoundOptionIndex::SE) {
              currentSoundOptionIndex_ = (int)SoundOptionIndex::Camera;
          }
      }

      if (input->IsKeyTriggered(DIK_BACKSPACE) || input->IsKeyTriggered(DIK_ESCAPE) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_B)) {
          currentState_ = MenuState::TabSelect;
      }

      if (input->IsKeyTriggered(DIK_SPACE) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_A)) {
          currentState_ = MenuState::ValueAdjust;
      }
      UpdateSelectHighlights();
      break;
  }
  case MenuState::ValueAdjust: {
      bool isChanged = false;
      if (currentSoundOptionIndex_ == (int)SoundOptionIndex::BGM) {
          float vol = SaveDataManager::GetInstance()->GetBGMVolume();
          if (input->IsKeyPressed(DIK_D) || input->IsGamepadButtonPressed(XINPUT_GAMEPAD_DPAD_RIGHT)) {
              vol += 0.01f;
              if (vol > 1.0f) vol = 1.0f;
              isChanged = true;
          }
          if (input->IsKeyPressed(DIK_A) || input->IsGamepadButtonPressed(XINPUT_GAMEPAD_DPAD_LEFT)) {
              vol -= 0.01f;
              if (vol < 0.0f) vol = 0.0f;
              isChanged = true;
          }
          if (isChanged) {
              SaveDataManager::GetInstance()->SetBGMVolume(vol);
              UpdateBGMBar();
          }
      } else if (currentSoundOptionIndex_ == (int)SoundOptionIndex::SE) {
          float vol = SaveDataManager::GetInstance()->GetSEVolume();
          if (input->IsKeyPressed(DIK_D) || input->IsGamepadButtonPressed(XINPUT_GAMEPAD_DPAD_RIGHT)) {
              vol += 0.01f;
              if (vol > 1.0f) vol = 1.0f;
              isChanged = true;
          }
          if (input->IsKeyPressed(DIK_A) || input->IsGamepadButtonPressed(XINPUT_GAMEPAD_DPAD_LEFT)) {
              vol -= 0.01f;
              if (vol < 0.0f) vol = 0.0f;
              isChanged = true;
          }
          if (isChanged) {
              SaveDataManager::GetInstance()->SetSEVolume(vol);
              UpdateSEBar();
          }
      } else if (currentSoundOptionIndex_ == (int)SoundOptionIndex::Camera) {
          int sens = SaveDataManager::GetInstance()->GetCameraSensitivity();
          if (input->IsKeyTriggered(DIK_D) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_RIGHT)) {
              if (sens < 5) { sens++; isChanged = true; }
          }
          if (input->IsKeyTriggered(DIK_A) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_LEFT)) {
              if (sens > -5) { sens--; isChanged = true; }
          }
          if (isChanged) {
              SaveDataManager::GetInstance()->SetCameraSensitivity(sens);
              CameraEditor::GetInstance()->SetCameraSensitivity(sens);
              UpdateSensitivityBar();
          }
      }

      if (input->IsKeyTriggered(DIK_BACKSPACE) || input->IsKeyTriggered(DIK_ESCAPE) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_B) || input->IsKeyTriggered(DIK_SPACE) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_A)) {
          currentState_ = MenuState::ItemSelect;
      }
      UpdateSelectHighlights();
      break;
  }
  }
  return false;
}

bool OptionUI::IsOptionSprite(Sprite *sp) const {
  if (!sp)
    return false;

  // 新しいJSON (option_ui.json) で配置されたスプライトへの対応
  std::string name = sp->GetName();
  if (name.find("option/") != std::string::npos ||
      name.find("Option/") != std::string::npos) {
    return true;
  }

  // 以前のハードコーディング分への対応
  if (sp == bgSprite_ || sp == titleSprite_ || sp == soundSprite_ ||
      sp == keyboardSprite_ || sp == spaceIconSprite_ || sp == cursorSprite_ ||
      sp == sensitivityBarSprite_ || sp == volumeBarSprite_ ||
      sp == soundConfigCursorSprite_) {
    return true;
  }
  for (Sprite *actionSp : actionSprites_) {
    if (sp == actionSp)
      return true;
  }
  return false;
}

bool OptionUI::IsSpriteVisibleInCurrentTab(Sprite* sp) const {
    if (!sp) return false;

    // 常に表示する背景など
    if (sp == bgSprite_ || sp == titleSprite_) return true;

    // タブに依存するスプライト
    bool isAudio = (currentTopTab_ == (int)TopTab::AudioCamera);

    if (sp == selectLeftSprite_) return isAudio;
    if (sp == selectRightSprite_) return !isAudio;
    if (sp == cameraSoundUISprite_) return isAudio;

    // オーディオ/カメラタブの場合のみ表示
    if (sp == volumeBarSprite_ || sp == sensitivityBarSprite_ || sp == soundConfigCursorSprite_ ||
        sp == bgmSelectSprite_ || sp == seSelectSprite_ || sp == cameraSelectSprite_ ||
        sp == bgmBarSprite_ || sp == seBarSprite_) {
        // OptionUI内部の選択状態によって非表示にするものはここで判定してもよいが、
        // 基本的には親タブが開いていれば true を返し、個別の表示非表示は Update() で制御する。
        return isAudio;
    }

    // 基本的に上記以外はタブに関わらず表示を許可（上位で制御）
    return true;
}

void OptionUI::RefreshKeyIcons() {
  keyIconSprites_.clear(); // 古い見切れた画像をクリア

  for (size_t i = 0; i < configActions_.size(); ++i) {
    int keyCode = KeyConfig::GetInstance()->GetKeyCode(configActions_[i]);
    std::string spriteName = GetKeySpriteName(keyCode);
    uint32_t texHandle =
        TextureManager::GetInstance()->Load("Resources/sprite/" + spriteName);

    // ========================================================
    // ★ エディタで配置されたダミー（目印）の座標を取得
    // ========================================================
    Vector2 markerPos = {0, 0};
    if (i < actionSprites_.size() && actionSprites_[i]) {
      markerPos = actionSprites_[i]->GetPosition();
    }

    // ==========================================
    // ★ ここから可変サイズ対応！
    // ==========================================

    // 通常キーの基準サイズ（正方形の1辺）
    const float normalKeySize = 42.0f;

    // これから決める動的なサイズと調整量
    float dynamicWidth = normalKeySize;
    float dynamicHeight = normalKeySize;
    float horizontalShift = 0.0f; // 中央揃えにするための横移動量

    // --- キーコードによって幅（dynamicWidth）を変える ---
    if (keyCode == DIK_SPACE) {
      // Spaceキーは思い切り広く (文字が見えるように)
      dynamicWidth = 80.0f;
    } else if (keyCode == DIK_LSHIFT || keyCode == DIK_RSHIFT ||
               keyCode == DIK_LCONTROL || keyCode == DIK_RCONTROL ||
               keyCode == DIK_RETURN) {
      // Shift, Ctrl, Enterは中くらいに広く
      dynamicWidth = 80.0f;
    }

    // --- 中央揃えの計算 ---
    // 通常の正方形から幅が広がった分の「半分」だけ左にずらす
    horizontalShift = (dynamicWidth - normalKeySize) / 2.0f;

    // ★ 新しくスプライトを生成し、完璧なサイズと座標に設定する！
    auto sp = std::make_unique<Sprite>();
    sp->Initialize(spriteCommon_, texHandle);

    // 可変サイズを設定
    sp->SetSize({dynamicWidth, dynamicHeight});

    // 目印の座標から、広がった分だけ左にずらして中央に配置
    sp->SetPosition({markerPos.x - horizontalShift, markerPos.y});

    sp->SetColor({1.0f, 1.0f, 1.0f, 1.0f});
    sp->Update();

    keyIconSprites_.push_back(std::move(sp));
  }
}

void OptionUI::DrawKeyIcons() {

  // ★ プログラムで新しく生成した完璧なスプライトだけを描画！
  for (auto &sp : keyIconSprites_) {
    sp->Draw();
  }
}

// --- IsValidKey と GetKeySpriteName はそのまま ---
bool OptionUI::IsValidKey(int keyCode) const {
  if (keyCode >= DIK_1 && keyCode <= DIK_0)
    return true;
  if (keyCode >= DIK_Q && keyCode <= DIK_P)
    return true;
  if (keyCode >= DIK_A && keyCode <= DIK_L)
    return true;
  if (keyCode >= DIK_Z && keyCode <= DIK_M)
    return true;
  if (keyCode == DIK_SPACE || keyCode == DIK_LSHIFT || keyCode == DIK_RSHIFT ||
      keyCode == DIK_LCONTROL || keyCode == DIK_RCONTROL ||
      keyCode == DIK_RETURN) {
    return true;
  }
  return false;
}

std::string OptionUI::GetKeySpriteName(int keyCode) const {
  if (keyCode == DIK_SPACE)
    return "UI/Space.png";
  if (keyCode == DIK_LSHIFT || keyCode == DIK_RSHIFT)
    return "UI/Shift.png";
  if (keyCode == DIK_LCONTROL || keyCode == DIK_RCONTROL)
    return "UI/Ctrl.png";
  if (keyCode == DIK_RETURN)
    return "UI/Eenter.png";

  if (keyCode == DIK_A)
    return "UI/A.png";
  if (keyCode == DIK_B)
    return "UI/B.png";
  if (keyCode == DIK_C)
    return "UI/C.png";
  if (keyCode == DIK_D)
    return "UI/D.png";
  if (keyCode == DIK_E)
    return "UI/E.png";
  if (keyCode == DIK_F)
    return "UI/F.png";
  if (keyCode == DIK_G)
    return "UI/G.png";
  if (keyCode == DIK_H)
    return "UI/H.png";
  if (keyCode == DIK_I)
    return "UI/I.png";
  if (keyCode == DIK_J)
    return "UI/J.png";
  if (keyCode == DIK_K)
    return "UI/K.png";
  if (keyCode == DIK_L)
    return "UI/L.png";
  if (keyCode == DIK_M)
    return "UI/M.png";
  if (keyCode == DIK_N)
    return "UI/N.png";
  if (keyCode == DIK_O)
    return "UI/O.png";
  if (keyCode == DIK_P)
    return "UI/P.png";
  if (keyCode == DIK_Q)
    return "UI/Q.png";
  if (keyCode == DIK_R)
    return "UI/R.png";
  if (keyCode == DIK_S)
    return "UI/S.png";
  if (keyCode == DIK_T)
    return "UI/T.png";
  if (keyCode == DIK_U)
    return "UI/U.png";
  if (keyCode == DIK_V)
    return "UI/V.png";
  if (keyCode == DIK_W)
    return "UI/W.png";
  if (keyCode == DIK_X)
    return "UI/X.png";
  if (keyCode == DIK_Y)
    return "UI/Y.png";
  if (keyCode == DIK_Z)
    return "UI/Z.png";

  if (keyCode == DIK_0)
    return "UI/0.png";
  if (keyCode == DIK_1)
    return "UI/1.png";
  if (keyCode == DIK_2)
    return "UI/2.png";
  if (keyCode == DIK_3)
    return "UI/3.png";
  if (keyCode == DIK_4)
    return "UI/4.png";
  if (keyCode == DIK_5)
    return "UI/5.png";
  if (keyCode == DIK_6)
    return "UI/6.png";
  if (keyCode == DIK_7)
    return "UI/7.png";
  if (keyCode == DIK_8)
    return "UI/8.png";
  if (keyCode == DIK_9)
    return "UI/9.png";

  return "white.png";
}

void OptionUI::UpdateSensitivityBar() {
  if (!sensitivityBarSprite_)
    return;

  int sens = CameraEditor::GetInstance()->GetCameraSensitivity();

  float offset = 0.0f;
  if (sens > 0) {
      offset = 45.0f + 50.0f * (sens - 1);
  } else if (sens < 0) {
      offset = -45.0f - 50.0f * (-sens - 1);
  }

  Vector2 pos = sensitivityBarSprite_->GetPosition();
  pos.x = cameraCenterPosX_ + offset;
  sensitivityBarSprite_->SetPosition(pos);
}

void OptionUI::UpdateBGMBar() {
  if (!bgmBarSprite_)
    return;

  float vol = SaveDataManager::GetInstance()->GetBGMVolume();

  // BGMバーの座標
  float minPosX = 300.0f; 
  float maxPosX = bgmBarMaxPosX_; 

  Vector2 pos = bgmBarSprite_->GetPosition();
  pos.x = minPosX + (maxPosX - minPosX) * vol;
  bgmBarSprite_->SetPosition(pos);
}

void OptionUI::UpdateSEBar() {
  if (!seBarSprite_)
    return;

  float vol = SaveDataManager::GetInstance()->GetSEVolume();

  // SEバーの座標
  float minPosX = 300.0f; 
  float maxPosX = seBarMaxPosX_; 

  Vector2 pos = seBarSprite_->GetPosition();
  pos.x = minPosX + (maxPosX - minPosX) * vol;
  seBarSprite_->SetPosition(pos);
}