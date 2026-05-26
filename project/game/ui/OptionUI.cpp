#include "OptionUI.h"
#include "BaseScene.h"
#include "DebugConsole.h"
#include "KeyConfig.h"
#include "SaveDataManager.h"
#include "SpriteCommon.h"
#include "TextureManager.h"
#include "AudioPlayer.h"
#include "CameraManager.h"
#include <CameraEditor.h>
#include <cmath>


void OptionUI::SetSpriteTexturePreserveSize(Sprite* sprite, const std::string& textureName) {
  if (!sprite || sprite->GetTextureName() == textureName) {
    return;
  }

  Vector2 currentSize = sprite->GetSize();
  sprite->SetTextureHandle(Sprite::LoadTexture(textureName));
  sprite->SetTextureName(textureName);
  sprite->SetSize(currentSize);
}

void OptionUI::ApplyInputUiIfNeeded() {
  InputManager* input = InputManager::GetInstance();
  const bool useGamepadUi = input && input->IsGamepadMode();
  if (hasAppliedOptionInputUi_ && optionUiUsesGamepad_ == useGamepadUi) {
    return;
  }

  SetSpriteTexturePreserveSize(
      optionControlSprite_,
      useGamepadUi ? "option/operationUI_pad.png" : "option/operationUI.png");

  optionUiUsesGamepad_ = useGamepadUi;
  hasAppliedOptionInputUi_ = true;

  DebugConsole::GetInstance()->AddLog(
      useGamepadUi
          ? "[OptionUI] Operation display switched to Controller"
          : "[OptionUI] Operation display switched to Keyboard");
}

void OptionUI::Initialize(BaseScene *scene, SpriteCommon *spriteCommon) {
  if (!scene || !spriteCommon)
    return;
  spriteCommon_ = spriteCommon;
  KeyConfig::GetInstance()->Initialize();
  // エディタで配置したベースUIの取得
  optionBackSprite_ = scene->GetSpriteByName("option/poseBack.png");
  if (optionBackSprite_) {
      optionBackSprite_->SetColor({ 0.0f, 0.0f, 0.0f, 170.0f / 255.0f }); // 半透明グレー
  }
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
  optionControlSprite_ = scene->GetSpriteByName("option/operationUI.png");
  optionAIconSprite_ = scene->GetSpriteByName("option/optionUI_A.png");
  optionDIconSprite_ = scene->GetSpriteByName("option/optionUI_D.png");
  if (optionAIconSprite_) {
      optionAIconBaseSize_ = optionAIconSprite_->GetSize();
  }
  if (optionDIconSprite_) {
      optionDIconBaseSize_ = optionDIconSprite_->GetSize();
  }

  bgmSelectSprite_ = scene->GetSpriteByName("option/BGM_select.png");
  seSelectSprite_ = scene->GetSpriteByName("option/SE_select.png");
  cameraSelectSprite_ = scene->GetSpriteByName("option/camera_select.png");

  bgmBarSprite_ = bgmSelectSprite_;
  seBarSprite_ = seSelectSprite_;
  sensitivityBarSprite_ = cameraSelectSprite_;

  currentTopTab_ = (int)TopTab::AudioCamera;
  currentState_ = MenuState::TabSelect; 
  currentSoundOptionIndex_ = (int)SoundOptionIndex::BGM;
  currentOptionIndex_ = (int)OptionIndex::Sound;
  currentConfigIndex_ = 0;
  tabConfirmBlinkTime_ = 0.0f;
  confirmedTopTab_ = currentTopTab_;

  // サウンド用
  volumeBarSprite_ = scene->GetSpriteByName("UI/volume_pole.png");
  soundConfigCursorSprite_ = scene->GetSpriteByName("UI/sound_cursor.png");

  SaveDataManager::GetInstance()->Load();
  float initialBGM = SaveDataManager::GetInstance()->GetBGMVolume();
  float initialSE = SaveDataManager::GetInstance()->GetSEVolume();
  int initialSens = SaveDataManager::GetInstance()->GetCameraSensitivity();
  AudioPlayer::GetInstance()->SetBGMVolume(initialBGM);
  AudioPlayer::GetInstance()->SetSEVolume(initialSE);
  CameraManager::GetInstance()->GetMainCamera()->SetSensitivity(initialSens);

  seCursorMove_ = AudioPlayer::GetInstance()->LoadSoundFile("Resources/audio/se/Setting/SelectOpen1.mp3");
  seDecide_ = AudioPlayer::GetInstance()->LoadSoundFile("Resources/audio/se/Setting/SelectOpen2.mp3");
  seCancel_ = AudioPlayer::GetInstance()->LoadSoundFile("Resources/audio/se/Setting/SelectClose.mp3");

  bgmBarMaxPosX_ = 770.0f; // 全画面スプライトの中心を基準にする
  seBarMaxPosX_ = 770.0f;
  cameraCenterPosX_ = 770.0f;

  UpdateSensitivityBar();
  UpdateBGMBar();
  UpdateSEBar();
  ApplyInputUiIfNeeded();
  // ========================================================
  // エディタ配置スプライトは「目印」なので、透明にして隠す
  // ========================================================
  actionSprites_.clear();
  for (const auto &name : actionSpriteNames_) {
    Sprite *sp = scene->GetSpriteByName(name);
    if (sp) {
      sp->SetColor({1.0f, 1.0f, 1.0f, 0.0f});
    }
    actionSprites_.push_back(sp);
  }

  RefreshKeyIcons(); // 初回生成
}

void OptionUI::Reset() {
  currentState_ = MenuState::TabSelect;
  currentTopTab_ = (int)TopTab::AudioCamera;
  currentSoundOptionIndex_ = (int)SoundOptionIndex::BGM;
  currentOptionIndex_ = (int)OptionIndex::Sound;
  currentConfigIndex_ = 0;
  tabConfirmBlinkTime_ = 0.0f;
  confirmedTopTab_ = currentTopTab_;
  hasAppliedOptionInputUi_ = false;
  ApplyInputUiIfNeeded();
}

bool OptionUI::Update(float deltaTime) {
  ApplyInputUiIfNeeded();
  InputManager *input = InputManager::GetInstance();
  Vector4 normalColor = {0.5f, 0.5f, 0.5f, 1.0f};
  Vector4 selectColor = {1.0f, 1.0f, 1.0f, 1.0f};
  if (currentState_ != MenuState::TabSelect) {
      tabConfirmBlinkTime_ += deltaTime;
  } else {
      tabConfirmBlinkTime_ = 0.0f;
  }

  // --- タブなどの表示状態を更新 ---
  auto UpdateSelectHighlights = [&]() {
      bool isAudioTab = (currentTopTab_ == (int)TopTab::AudioCamera);
      if (selectLeftSprite_) {
          selectLeftSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
      }
      if (selectRightSprite_) {
          selectRightSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
      }
      const float activeIconScale = 1.25f;
      const float inactiveIconScale = 0.7f;
      if (optionAIconSprite_) {
          float scale = isAudioTab ? inactiveIconScale : activeIconScale;
          optionAIconSprite_->SetSize({ optionAIconBaseSize_.x * scale, optionAIconBaseSize_.y * scale });
      }
      if (optionDIconSprite_) {
          float scale = isAudioTab ? activeIconScale : inactiveIconScale;
          optionDIconSprite_->SetSize({ optionDIconBaseSize_.x * scale, optionDIconBaseSize_.y * scale });
      }
      if (currentState_ != MenuState::TabSelect) {
          Sprite* blinkSprite = nullptr;
          if (confirmedTopTab_ == (int)TopTab::AudioCamera) {
              blinkSprite = selectLeftSprite_;
          } else if (confirmedTopTab_ == (int)TopTab::Credit) {
              blinkSprite = selectRightSprite_;
          }
          if (blinkSprite) {
              const float blinkPeriod = 1.2f;
              float blinkT = std::fmod(tabConfirmBlinkTime_, blinkPeriod) / blinkPeriod;
              float pulse = (blinkT < 0.5f) ? (blinkT * 2.0f) : ((1.0f - blinkT) * 2.0f);
              float alpha = 0.35f + (0.65f * pulse);
              blinkSprite->SetColor({ 1.0f, 1.0f, 1.0f, alpha });
          }
      }
      
      // カーソルの表示制御
      if (cursorSprite_) {
          cursorSprite_->SetVisible(isAudioTab && currentState_ != MenuState::TabSelect);
      }

      // 各項目の強調処理
      auto ApplyHighlight = [&](Sprite* sp, int index, Vector2 cursorTargetPos) {
          if (!sp) return;
          
          bool isSelected = (isAudioTab && currentState_ != MenuState::TabSelect && currentSoundOptionIndex_ == index);
          
          // タブが合っていれば表示、そうでなければ非表示
          sp->SetVisible(isAudioTab);

          if (isSelected) {
              // 選択中：明るく表示
              if (currentState_ == MenuState::ValueAdjust) {
                  sp->SetColor({ 1.0f, 0.0f, 0.0f, 1.0f }); // 調整中は赤
              } else {
                  sp->SetColor({ 1.0f, 1.0f, 0.0f, 1.0f }); // 選択中は黄色
              }
              // カーソルをこの項目の横に持ってくる
              if (cursorSprite_) {
                  cursorSprite_->SetPosition(cursorTargetPos);
              }
          } else {
              // 非選択：少し暗く、透明にする
              sp->SetColor({ 0.5f, 0.5f, 0.5f, 0.7f });
          }
      };

      // カーソルは各項目の左側に置く。
      ApplyHighlight(bgmSelectSprite_, (int)SoundOptionIndex::BGM, { 250.0f, 310.0f });
      ApplyHighlight(seSelectSprite_, (int)SoundOptionIndex::SE, { 250.0f, 560.0f });
      ApplyHighlight(cameraSelectSprite_, (int)SoundOptionIndex::Camera, { 1080.0f, 310.0f });
  };

  switch (currentState_) {
  case MenuState::TabSelect: {
      if (input->IsKeyTriggered(DIK_A) ||
          input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_LEFT) ||
          input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_LEFT_SHOULDER)) {
          int prevTab = currentTopTab_;
          currentTopTab_ = (int)TopTab::AudioCamera;
          if (prevTab != currentTopTab_) {
              AudioPlayer::GetInstance()->PlaySE(seCursorMove_, false, 1.0f);
          }
      }
      if (input->IsKeyTriggered(DIK_D) ||
          input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_RIGHT) ||
          input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_RIGHT_SHOULDER)) {
          int prevTab = currentTopTab_;
          currentTopTab_ = (int)TopTab::Credit;
          if (prevTab != currentTopTab_) {
              AudioPlayer::GetInstance()->PlaySE(seCursorMove_, false, 1.0f);
          }
      }

      if (input->IsKeyTriggered(DIK_BACKSPACE) || input->IsKeyTriggered(DIK_ESCAPE) || input->IsKeyTriggered(DIK_TAB) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_B) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_START)) {
          AudioPlayer::GetInstance()->PlaySE(seCancel_, false, 1.0f);
          SaveDataManager::GetInstance()->Save();
          return true; // オプションを閉じる
      }

      if (input->IsKeyTriggered(DIK_SPACE) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_A)) {
          tabConfirmBlinkTime_ = 0.0f;
          confirmedTopTab_ = currentTopTab_;
          if (currentTopTab_ == (int)TopTab::AudioCamera) {
              AudioPlayer::GetInstance()->PlaySE(seDecide_, false, 1.0f);
              currentState_ = MenuState::ItemSelect;
          }
      }
      UpdateSelectHighlights();
      break;
  }
  case MenuState::ItemSelect: {
      bool cursorMoved = false;
      if (input->IsKeyTriggered(DIK_W) ||
          input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_UP)) {
          if (currentSoundOptionIndex_ == (int)SoundOptionIndex::SE) {
              currentSoundOptionIndex_ = (int)SoundOptionIndex::BGM;
              cursorMoved = true;
          }
      }
      if (input->IsKeyTriggered(DIK_S) ||
          input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_DOWN)) {
          if (currentSoundOptionIndex_ == (int)SoundOptionIndex::BGM) {
              currentSoundOptionIndex_ = (int)SoundOptionIndex::SE;
              cursorMoved = true;
          }
      }
      if (input->IsKeyTriggered(DIK_A) ||
          input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_LEFT)) {
          if (currentSoundOptionIndex_ == (int)SoundOptionIndex::Camera) {
              currentSoundOptionIndex_ = (int)SoundOptionIndex::BGM;
              cursorMoved = true;
          }
      }
      if (input->IsKeyTriggered(DIK_D) ||
          input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_RIGHT)) {
          if (currentSoundOptionIndex_ == (int)SoundOptionIndex::BGM || currentSoundOptionIndex_ == (int)SoundOptionIndex::SE) {
              currentSoundOptionIndex_ = (int)SoundOptionIndex::Camera;
              cursorMoved = true;
          }
      }
      if (cursorMoved) {
          AudioPlayer::GetInstance()->PlaySE(seCursorMove_, false, 1.0f);
      }

      if (input->IsKeyTriggered(DIK_BACKSPACE) || input->IsKeyTriggered(DIK_ESCAPE) || input->IsKeyTriggered(DIK_TAB) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_B) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_START)) {
          AudioPlayer::GetInstance()->PlaySE(seCancel_, false, 1.0f);
          currentState_ = MenuState::TabSelect;
      }

      if (input->IsKeyTriggered(DIK_SPACE) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_A)) {
          AudioPlayer::GetInstance()->PlaySE(seDecide_, false, 1.0f);
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
              AudioPlayer::GetInstance()->SetBGMVolume(vol); // 即座に反映
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
              AudioPlayer::GetInstance()->SetSEVolume(vol); // 今後のSEに反映
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
              CameraManager::GetInstance()->GetMainCamera()->SetSensitivity(sens); // 即座に反映
              UpdateSensitivityBar();
          }
      }

      if (input->IsKeyTriggered(DIK_BACKSPACE) || input->IsKeyTriggered(DIK_ESCAPE) || input->IsKeyTriggered(DIK_TAB) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_B) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_START) || input->IsKeyTriggered(DIK_SPACE) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_A)) {
          AudioPlayer::GetInstance()->PlaySE(seDecide_, false, 1.0f);
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

  // option_ui.json で配置されたスプライトをオプションUIとして扱う。
  std::string name = sp->GetName();
  if (name.find("option/") != std::string::npos ||
      name.find("Option/") != std::string::npos) {
    return true;
  }

  // 既存レイアウト側のスプライトも対象に含める。
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
    if (sp == optionBackSprite_ || sp == bgSprite_ || sp == titleSprite_) return true;

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

    // エディタ配置の目印スプライトから表示位置を取る。
    Vector2 markerPos = {0, 0};
    if (i < actionSprites_.size() && actionSprites_[i]) {
      markerPos = actionSprites_[i]->GetPosition();
    }

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

    // キー種別に応じたサイズで実表示用スプライトを生成する。
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

  // 実表示用に生成したキーアイコンだけを描画する。
  for (auto &sp : keyIconSprites_) {
    sp->Draw();
  }
}

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

  // ユーザー指定: -5 = 547.0, 5 = 994.0, 0 = 770.0
  // (994 - 547) / 10 = 44.7px
  float step = 44.7f;
  float offset = (float)sens * step;

  Vector2 pos = sensitivityBarSprite_->GetPosition();
  pos.x = cameraCenterPosX_ + offset;
  sensitivityBarSprite_->SetPosition(pos);
}

void OptionUI::UpdateBGMBar() {
  if (!bgmBarSprite_)
    return;

  float vol = SaveDataManager::GetInstance()->GetBGMVolume();

  // ユーザー指定: 100% = 770.0, 0% = 299.0
  // 可動域は 770 - 299 = 471px
  float range = 471.0f;
  float offset = (vol - 1.0f) * range;

  Vector2 pos = bgmBarSprite_->GetPosition();
  pos.x = bgmBarMaxPosX_ + offset;
  bgmBarSprite_->SetPosition(pos);
}

void OptionUI::UpdateSEBar() {
  if (!seBarSprite_)
    return;

  float vol = SaveDataManager::GetInstance()->GetSEVolume();

  // BGMと同様の可動域
  float range = 471.0f;
  float offset = (vol - 1.0f) * range;

  Vector2 pos = seBarSprite_->GetPosition();
  pos.x = seBarMaxPosX_ + offset;
  seBarSprite_->SetPosition(pos);
}
