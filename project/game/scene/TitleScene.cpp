#define NOMINMAX
#include "TitleScene.h"
#include "AudioPlayer.h"
#include "BulletManager.h"
#include "CameraEditor.h"
#include "CameraManager.h"
#include "CollisionManager.h"
#include "DebugConsole.h"
#include "DirectXCommon.h"
#include "GPUParticleManager.h"
#include "GameProgress.h"
#include "GameRule.h"
#include "InputManager.h"
#include "LevelLoader.h"
#include "LightEditor.h"
#include "LightManager.h"
#include "ModelManager.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "ParticleManager.h"
#include "ParticleSystem.h"
#include "SceneManager.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "TextureManager.h"
#include "imgui.h"
#include <algorithm> // std::transform
#include <cctype>    // ::tolower
#include <cmath>     // std::sin


#include "Easing.h" // イージング関数利用
#include <CinematicFade.h>
#include <PostEffect.h>
#include <SaveDataManager.h>

void TitleScene::SetSpriteTexturePreserveSize(Sprite* sprite, const std::string& textureName) {
  if (!sprite || sprite->GetTextureName() == textureName) {
    return;
  }

  Vector2 currentSize = sprite->GetSize();
  sprite->SetTextureHandle(Sprite::LoadTexture(textureName));
  sprite->SetTextureName(textureName);
  sprite->SetSize(currentSize);
}

void TitleScene::ApplyInputUiIfNeeded() {
  const bool useGamepadUi = inputManager_ && inputManager_->IsGamepadMode();
  if (hasAppliedTitleInputUi_ && titleUiUsesGamepad_ == useGamepadUi) {
    return;
  }

  SetSpriteTexturePreserveSize(
      enterTextSprite_,
      useGamepadUi ? "enter_text_pad.png" : "enter_text.png");

  titleUiUsesGamepad_ = useGamepadUi;
  hasAppliedTitleInputUi_ = true;

  DebugConsole::GetInstance()->AddLog(
      useGamepadUi
          ? "[TitleUI] Input display switched to Controller"
          : "[TitleUI] Input display switched to Keyboard");
}

void TitleScene::Initialize() {
  // --- 1. システム基盤の取得 ---
  dxCommon_ = DirectXCommon::GetInstance();
  inputManager_ = InputManager::GetInstance();
  audioPlayer_ = AudioPlayer::GetInstance();

  // --- 2. モデルのプリロード ---
  ModelManager::GetInstance()->LoadModel("player");
  ModelManager::GetInstance()->LoadModel("teapot");
  LOG("TitleScene Initialized!");

  SaveDataManager::GetInstance()->Load();
  bgmHandle_ = audioPlayer_->LoadSoundFile("Resources/audio/bgm/title/title.mp3");
  audioPlayer_->PlayBGM(bgmHandle_, true, SaveDataManager::GetInstance()->GetBGMVolume());
  seCursorMove_ = audioPlayer_->LoadSoundFile("Resources/audio/se/Setting/SelectOpen1.mp3");
  seDecide_ = audioPlayer_->LoadSoundFile("Resources/audio/se/Setting/SelectOpen2.mp3");
  seCancel_ = audioPlayer_->LoadSoundFile("Resources/audio/se/Setting/SelectClose.mp3");
  seStartGame_ = audioPlayer_->LoadSoundFile("Resources/audio/se/Setting/SelectGameStart.mp3");

  // --- 3. マネージャ・共通クラスの初期化 ---
  CameraManager::GetInstance()->Initialize();
  CameraManager::GetInstance()->SetInputManager(inputManager_);

  object3dCommon_ = std::make_unique<Object3dCommon>();
  object3dCommon_->Initialize(dxCommon_);

  spriteCommon_ = std::make_unique<SpriteCommon>();
  spriteCommon_->Initialize(dxCommon_);

  particleCommon_ = std::make_unique<ParticleCommon>();
  particleCommon_->Initialize(dxCommon_);

  particleSystem_ = std::make_unique<ParticleSystem>();
  particleSystem_->Initialize(particleCommon_.get(),
                              "Resources/sprite/white.png");

  // シングルトンのParticleManagerに今のシーンのシステムを紐づける
  ParticleManager::GetInstance()->Initialize(particleSystem_.get());

  LightEditor::GetInstance()->SetObject3dCommon(object3dCommon_.get());

  GPUParticleManager::GetInstance()->Initialize(dxCommon_);
  GPUParticleManager::GetInstance()->LoadAllPresets(
      "Resources/json/gpu_particles/");
  // --- 4. サブシステムの生成 ---
  objectManager_ = std::make_unique<ObjectManager>();
  levelLoader_ = std::make_unique<LevelLoader>();
  gameRule_ = std::make_unique<GameRule>();
  gameRule_->Initialize(this);

  BulletManager::GetInstance()->Initialize(object3dCommon_.get(),
                                           CollisionManager::GetInstance());

  // GPUパーティクルの初期化
  gpuParticleTexHandle_ =
      TextureManager::GetInstance()->Load("Resources/sprite/white.png");

  // --- 6. レイアウトの読み込み (LevelLoaderへ委譲) ---
  levelLoader_->LoadObjectLayout(this,
                                 "Resources/json/3Dobject/titleScene.json");
  levelLoader_->LoadSpriteLayout(this, "Resources/json/sprite/titleScene.json");
  levelLoader_->LoadSpriteLayout(
      this, "Resources/json/sprite/option_ui.json"); // オプションUI用

  // option/poseBack.png スプライトを最背面（sprites_ の先頭）に移動する
  {
      auto it = std::find_if(sprites_.begin(), sprites_.end(), [](const auto& sprite) {
          return sprite && sprite->GetName() == "option/poseBack.png";
      });
      if (it != sprites_.end()) {
          auto poseBack = std::move(*it);
          sprites_.erase(it);
          sprites_.insert(sprites_.begin(), std::move(poseBack));
      }
  }

  LightManager::GetInstance()->LoadState(
      "Resources/json/light/titleScene.json");
  CameraEditor::GetInstance()->Initialize();
  CameraEditor::GetInstance()->LoadFile("title_camera.json");

  // --- メニュー項目スプライトのインデックスと初期Y座標を特定 ---
  menuSpriteIndices_.clear();
  spriteBaseYs_.clear();
  spriteBaseSizes_.clear();
  for (int i = 0; i < (int)sprites_.size(); ++i) {
    const std::string &name = sprites_[i]->GetName();
    if (name == "enter_text.png") {
      enterTextSprite_ = sprites_[i].get();
      enterTextBaseSize_ = sprites_[i]->GetSize();
    }
    if (name == "gameStartText.png" || name == "title_optionText.png" ||
        name == "exit.png") {
      menuSpriteIndices_.push_back(i);
    }
    spriteBaseYs_.push_back(sprites_[i]->GetPosition().y);
    spriteBaseSizes_.push_back(sprites_[i]->GetSize());
  }
  ApplyInputUiIfNeeded();
  spritesAppear_ = false;
  spritesAppearTimer_ = 0.0f;

  // --- オブジェクト一覧をログ出力して調査（デバッグ用） ---
  LOG("TitleScene: listing loaded objects:");
  if (objectManager_) {
    for (auto &obj : objectManager_->GetObjects()) {
      if (!obj)
        continue;
      LOG(" - name:\"%s\" class:\"%s\" enemyType:\"%s\"",
          obj->GetName().c_str(), obj->GetClassName().c_str(),
          obj->GetEnemyType().c_str());
    }
  }

  // --- enemy_core をシーン内から探して初期化（複数対応・名前バリエーション）
  // ---
  enemyCores_.clear();
  enemyCoreBaseYs_.clear();
  if (objectManager_) {
    for (auto &objPtr : objectManager_->GetObjects()) {
      if (!objPtr)
        continue;
      const std::string &nm = objPtr->GetName();
      std::string lower = nm;
      std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

      bool matched = false;
      // 直接一致候補
      if (lower.find("enemy__core") != std::string::npos ||
          lower.find("enemy_core") != std::string::npos) {
        matched = true;
      }
      // 汎用: 名前に enemy と core が含まれる場合
      if (!matched && lower.find("enemy") != std::string::npos &&
          lower.find("core") != std::string::npos) {
        matched = true;
      }
      // 敵タイプ（Factoryで生成された場合など）
      if (!matched) {
        std::string et = objPtr->GetEnemyType();
        std::transform(et.begin(), et.end(), et.begin(), ::tolower);
        if (et.find("bosscore") != std::string::npos ||
            et.find("boss") != std::string::npos) {
          matched = true;
        }
      }

      if (matched) {
        enemyCores_.push_back(objPtr.get());
        enemyCoreBaseYs_.push_back(objPtr->GetWorldPosition().y);
      }
    }
  }
  LOG("Found %d enemy core(s) to animate.", (int)enemyCores_.size());

  // ボスコンテナのパーティクルを各コアに設定
  bossContainerEmitters_.clear();
  for (Object3d *core : enemyCores_) {
    auto top = std::make_unique<GPUParticleEmitter>();
    top->Initialize("boss_container_top", core);
    top->Play();
    bossContainerEmitters_.push_back(std::move(top));

    auto bottom = std::make_unique<GPUParticleEmitter>();
    bottom->Initialize("boss_container_bottom", core);
    bottom->Play();
    bossContainerEmitters_.push_back(std::move(bottom));
  }

  // OptionUI の初期化
  optionUI_.Initialize(this, spriteCommon_.get());

  // =======================================================
  // リスタート演出（電脳リブート）と完全初期化
  // =======================================================
  SceneManager *scm = SceneManager::GetInstance();

  PostEffect::GetInstance()->ResetToBaseParams();

  if (scm->ShouldSkipFade()) {
    CinematicFade::GetInstance()->StartOpen(0.3f);
    scm->ResetSkipFade();
  } else {
    CinematicFade::GetInstance()->StartOpen(0.5f);
  }
  dxCommon_->FlushCommandQueue(false);
}

void TitleScene::Finalize() {
  CollisionManager::GetInstance()->ClearObjects();
  BulletManager::GetInstance()->Finalize();

  objectManager_.reset();
  sprites_.clear();
  bossContainerEmitters_.clear();
  particleSystem_.reset();
  particleCommon_.reset();
  spriteCommon_.reset();
  object3dCommon_.reset();
}

void TitleScene::Update(float deltaTime) {
  InputManager *input = InputManager::GetInstance();
  ApplyInputUiIfNeeded();
  titleMenuBlinkTimer_ += deltaTime;

  // ---------------------------------------------------------
  // 0. ESCキーでの強制終了（オプション画面以外）
  // ---------------------------------------------------------
#ifdef USE_IMGUI
  if (currentState_ != TitleState::OptionMenu &&
      input->IsKeyTriggered(DIK_ESCAPE)) {
    PostQuitMessage(0);
    return;
  }
#endif

  // --- フェード終了後にスプライト演出を開始 ---
  if (!spritesAppear_) {
    spritesAppear_ = true;
    spritesAppearTimer_ = 0.0f;
  }

  // スプライトの表示切り替え（オプション中は他のUIを隠す）
  for (auto& sprite : sprites_) {
      if (!sprite) continue;
      bool isOpt = optionUI_.IsOptionSprite(sprite.get());
      bool isPoseBack = (sprite->GetName() == "option/poseBack.png");

      // タイトルシーン自体の描画にのみ使用するスプライトか判定
      std::string name = sprite->GetName();
      bool isTitleSprite = (name == "title.png" ||
                            name == "gameStartText.png" ||
                            name == "title_optionText.png" ||
                            name == "exit.png" ||
                            name == "enter_text.png");

      if (currentState_ == TitleState::OptionMenu) {
          if (isOpt) {
              sprite->SetVisible(optionUI_.IsSpriteVisibleInCurrentTab(sprite.get()));
          } else {
              sprite->SetVisible(false);
          }
      } else {
          if (isOpt) {
              // 特例：ポーズ背景はメインメニューでも表示する
              if (isPoseBack) {
                  sprite->SetVisible(spritesAppear_);
              } else {
                  sprite->SetVisible(false);
              }
          } else {
              // オプション以外かつタイトル用スプライトのみメインメニューで表示
              if (isTitleSprite) {
                  sprite->SetVisible(spritesAppear_);
              } else {
                  // ゲームプレイ用のUIスプライトなど、タイトルシーンで不要なものは常に非表示
                  sprite->SetVisible(false);
              }
          }
      }
  }

  // --- スプライト浮上演出 ---
  float appearT = std::min(spritesAppearTimer_ / spritesAppearDuration_, 1.0f);
  float easeT = Easing::OutCubic(appearT);
  if (spritesAppear_) {
    spritesAppearTimer_ += deltaTime;
    for (size_t i = 0; i < sprites_.size(); ++i) {
      auto &sprite = sprites_[i];
      if (!sprite) continue;

      // オプション関係のスプライトは浮上演出の対象外とする
      if (optionUI_.IsOptionSprite(sprite.get())) {
        continue;
      }

      // タイトル用以外のスプライト（ゲームプレイ用のUIスプライトなど）はアルファを0にして浮上処理をスキップ
      std::string name = sprite->GetName();
      bool isTitleSprite = (name == "title.png" ||
                            name == "gameStartText.png" ||
                            name == "title_optionText.png" ||
                            name == "exit.png" ||
                            name == "enter_text.png");
      if (!isTitleSprite) {
          Vector4 color = sprite->GetColor();
          color.w = 0.0f;
          sprite->SetColor(color);
          sprite->SetVisible(false);
          continue;
      }

      float baseY = (i < spriteBaseYs_.size()) ? spriteBaseYs_[i]
                                               : sprite->GetPosition().y;
      float offsetY = (1.0f - easeT) * 60.0f; // 60px下から浮かぶ
      Vector2 newPos = sprite->GetPosition();
      newPos.y = baseY + offsetY;
      sprite->SetPosition(newPos);
      Vector4 color = sprite->GetColor();
      color.w = easeT; // アルファ
      sprite->SetColor(color);
    }
  }

  // --- 状態ごとの更新 ---
  if (currentState_ == TitleState::MainMenu) {
    // --- スティックの入力判定（トリガー処理） ---
    Vector2 lStick = input->GetLeftStick();
    bool stickUp = lStick.y > 0.5f;
    bool stickDown = lStick.y < -0.5f;
    bool stickUpTrig = stickUp && !prevStickUp_;
    bool stickDownTrig = stickDown && !prevStickDown_;
    prevStickUp_ = stickUp;
    prevStickDown_ = stickDown;

    // --- メニュー選択（W/Sキー、D-Pad、左スティック）---
    if (spritesAppear_ && appearT >= 1.0f) {
      if (input->IsKeyTriggered(DIK_W) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_UP) || stickUpTrig) {
        currentMenuIndex_--;
        if (currentMenuIndex_ < 0)
          currentMenuIndex_ = (int)menuSpriteIndices_.size() - 1;
        audioPlayer_->PlaySE(seCursorMove_, false, 1.0f);
      }
      if (input->IsKeyTriggered(DIK_S) || input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_DPAD_DOWN) || stickDownTrig) {
        currentMenuIndex_++;
        if (currentMenuIndex_ >= (int)menuSpriteIndices_.size())
          currentMenuIndex_ = 0;
        audioPlayer_->PlaySE(seCursorMove_, false, 1.0f);
      }
      // --- メニュー決定（Enter/Spaceキー、Aボタン(下側)）---
      if (input->IsKeyTriggered(DIK_RETURN) ||
          input->IsKeyTriggered(DIK_SPACE) ||
          input->IsGamepadButtonTriggered(XINPUT_GAMEPAD_A)) {
        // メニューインデックス順: 0=ゲームスタート, 1=設定, 2=終了
        switch (currentMenuIndex_) {
        case 0: // ゲームスタート
          audioPlayer_->PlaySE(seStartGame_, false, 1.0f);
          GameProgress::GetInstance()->Reset();
          SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
          break;
        case 1: // 設定
          audioPlayer_->PlaySE(seDecide_, false, 1.0f);
          optionUI_.Reset();
          currentState_ = TitleState::OptionMenu;
          break;
        case 2: // 終了
          audioPlayer_->PlaySE(seCancel_, false, 1.0f);
          PostQuitMessage(0);
          break;
        }
      }
    }
  } else if (currentState_ == TitleState::OptionMenu) {
    if (optionUI_.Update(deltaTime)) {
      currentState_ = TitleState::MainMenu;
    }
  }

  const float blink = 0.5f + 0.5f * std::sin(titleMenuBlinkTimer_ * 6.0f);
  const float appearAlpha = spritesAppear_ ? easeT : 0.0f;

  // --- メニュー色分け（毎フレーム更新）---
  for (size_t i = 0; i < sprites_.size(); ++i) {
    Sprite *sp = sprites_[i].get();
    auto it =
        std::find(menuSpriteIndices_.begin(), menuSpriteIndices_.end(), (int)i);
    if (it != menuSpriteIndices_.end()) {
      int menuIdx = (int)std::distance(menuSpriteIndices_.begin(), it);
      const Vector2 baseSize = (i < spriteBaseSizes_.size()) ? spriteBaseSizes_[i] : sp->GetSize();
      if (menuIdx == currentMenuIndex_) {
        const float alpha = (0.62f + 0.38f * blink) * appearAlpha;
        const float scale = 1.07f + 0.05f * blink;
        sp->SetColor({1.0f, 1.0f, 1.0f, alpha});
        sp->SetSize({baseSize.x * scale, baseSize.y * scale});
      } else {
        sp->SetColor({0.5f, 0.5f, 0.5f, 0.55f * appearAlpha});
        sp->SetSize(baseSize);
      }
    }
  }

  if (enterTextSprite_ && currentState_ == TitleState::MainMenu) {
    const float alpha = (0.45f + 0.55f * blink) * appearAlpha;
    const float scale = 1.0f + 0.035f * blink;
    enterTextSprite_->SetColor({1.0f, 1.0f, 1.0f, alpha});
    enterTextSprite_->SetSize({enterTextBaseSize_.x * scale, enterTextBaseSize_.y * scale});
  }

  // --- 既存の更新処理 ---
  LightEditor::GetInstance()->Update();
  Object3d *cameraTarget = player_;
  if (!cameraTarget && objectManager_ &&
      !objectManager_->GetObjects().empty()) {
    cameraTarget = objectManager_->GetObjects().front().get();
  }
  CameraEditor::GetInstance()->Update(cameraTarget, false);
  CameraManager::GetInstance()->Update();

  // --- enemy_core を上下移動（複数対応） ---
  if (!enemyCores_.empty()) {
    enemyCoreTimer_ += deltaTime * enemyCoreSpeed_;
    for (size_t i = 0; i < enemyCores_.size(); ++i) {
      Object3d *core = enemyCores_[i];
      if (!core)
        continue;
      float baseY = (i < enemyCoreBaseYs_.size()) ? enemyCoreBaseYs_[i]
                                                  : core->GetWorldPosition().y;
      float phase = enemyCoreTimer_ + static_cast<float>(i) * 0.7f;
      float newY = baseY + std::sin(phase) * enemyCoreAmplitude_;
      Vector3 pos = core->GetTranslate();
      pos.y = newY;
      core->SetTranslate(pos);
      core->UpdateWorldMatrix();
    }
  }

  if (objectManager_)
    objectManager_->Update(deltaTime);
  if (particleSystem_)
    particleSystem_->Update(deltaTime);

  // ボスコンテナのパーティクルを更新
  for (auto &emitter : bossContainerEmitters_) {
    emitter->Update(deltaTime);
  }

  for (auto &sprite : sprites_) {
    sprite->Update();
  }
  BulletManager::GetInstance()->Update(deltaTime);
  CollisionManager::GetInstance()->Update();
}

void TitleScene::DrawUI() {
  spriteCommon_->SetPipeline(dxCommon_->GetCommandList());
  for (size_t i = 0; i < sprites_.size(); ++i) {
    // 表示設定がONのものだけ描画（BaseSceneやSpriteクラスの仕様に合わせる）
    sprites_[i]->Draw();
  }

  // オプションメニュー時は動的生成分のアイコンを描画
  if (currentState_ == TitleState::OptionMenu) {
    optionUI_.DrawKeyIcons();
  }
}

void TitleScene::Draw() {
  // --- 一人称視点判定 ---
  bool isFirstPerson = false;
  Camera *camera = CameraManager::GetInstance()->GetMainCamera();
#ifndef _DEBUG
  if (camera->GetFollowTarget() &&
      camera->GetFollowMode() == Camera::FollowMode::kFirstPerson) {
    isFirstPerson = true;
  }
#endif

  ID3D12Resource *pointLightRes =
      LightManager::GetInstance()->GetPointLightResource();
  ID3D12Resource *spotLightRes =
      LightManager::GetInstance()->GetSpotLightResource();
  object3dCommon_->SetGraphicsCommand();

  auto &objects = objectManager_->GetObjects();

  // --- 1. 不透明描画 ---
  for (auto &obj : objects) {
    if (isFirstPerson && obj.get() == player_)
      continue;
    if (obj->GetMaterialType() == 1 || obj->GetMaterialType() == 7)
      continue; // フォグ(7)も不透明パスから除外
    obj->Draw(pointLightRes, spotLightRes);
  }

  // --- 2. 中間描画 (弾・デバッグ) ---
  BulletManager::GetInstance()->Draw(pointLightRes, spotLightRes);
  LightEditor::GetInstance()->Draw3D();

  // --- 3. 透明描画 ---
  for (auto &obj : objects) {
    if (isFirstPerson && obj.get() == player_)
      continue;
    if (obj->GetMaterialType() == 1) { // 透明のみ描画
      obj->Draw(pointLightRes, spotLightRes);
    }
  }
  particleSystem_->Draw();

  // =======================================================
  // 4. ローカルフォグ (霧の箱) の描画
  // =======================================================
  bool hasFog = false;
  for (auto &obj : objects) {
    if (obj->GetMaterialType() == 7)
      hasFog = true;
  }

  if (hasFog) {
    dxCommon_->PreDrawLocalFog();
    for (auto &obj : objects) {
      if (obj->GetMaterialType() == 7) {
        obj->DrawLocalFog(dxCommon_->GetDepthSrvHandle());
      }
    }
    dxCommon_->PostDrawLocalFog();
  }

  // =======================================================
  // 5. GPUパーティクルの描画
  // =======================================================
  dxCommon_->UpdateGrabTexture();
  dxCommon_->PreDrawLocalFog();
  if (camera) {
    GPUParticleManager::GetInstance()->Draw(
        dxCommon_->GetCommandList(), camera->GetViewMatrix(),
        camera->GetProjectionMatrix(), gpuParticleTexHandle_,
        dxCommon_->GetDepthSrvHandle());
  }
  dxCommon_->PostDrawLocalFog();
}

// ====================================================================
// UI描画専用の関数
// ====================================================================

// シャドウマップ描画の実装
void TitleScene::DrawShadow() {
  if (objectManager_) {
    objectManager_->DrawShadow();
  }
}
