#define NOMINMAX
#include "GamePlayScene.h"
#include "AudioPlayer.h"
#include "BossCore.h"
#include "BulletManager.h"
#include "CameraManager.h"
#include "CollisionManager.h"
#include "DebugConsole.h"
#include "DirectXCommon.h"
#include "GameProgress.h"
#include "GameRule.h"
#include "InputManager.h"
#include "LevelLoader.h"
#include "LightManager.h"
#include "LockOnSystem.h"
#include "ModelManager.h"
#include "MoveStrategy2D.h"
#include "MoveStrategy3D.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "ObjectManager.h"
#include "ParticleSystem.h"
#include "SaveDataManager.h"
#include "SceneManager.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "TextureManager.h"
#include "TutorialDoll.h"
#include "WinApp.h"
#include "imgui.h"
#include <EventManager.h>
#include <cassert>

#ifdef _DEBUG
#include "ParticleEditor.h"
#endif

// --- JSON (保存機能) ---
#include "TimeAttackUI.h"
#include "json.hpp"
#include <BaseEnemy.h>
#include <CameraEditor.h>
#include <CinematicFade.h>
#include <EnemyFactory.h>
#include <EnemySpawner.h>
#include <GPUParticleManager.h>
#include <LightEditor.h>
#include <MeshEffectManager.h>
#include <ParticleManager.h>
#include <PostEffect.h>
#include <SrvManager.h>
#include <fstream>
#include <numbers>
#include <string>


bool GamePlayScene::s_isRebooting_ = false;

GamePlayScene::GamePlayScene() {}
GamePlayScene::~GamePlayScene() {}

void GamePlayScene::Initialize() {
    using json = nlohmann::json;

    // --- 1. エンジン基盤・リソース初期化 ---
    dxCommon_ = DirectXCommon::GetInstance();
    inputManager_ = InputManager::GetInstance();
    audioPlayer_ = AudioPlayer::GetInstance();

    LOG("Game Initialized!");

    bgmHandle_ = audioPlayer_->LoadSoundFile("Resources/bgm/Alarm02.mp3");

    // --- 2. 各種マネージャ初期化 ---
    EventManager::GetInstance()->ClearAllListeners();
    CameraManager::GetInstance()->Initialize();
    CameraManager::GetInstance()->SetInputManager(inputManager_);

    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dxCommon_);

    object3dCommon_ = std::make_unique<Object3dCommon>();
    object3dCommon_->Initialize(dxCommon_);

    particleCommon_ = std::make_unique<ParticleCommon>();
    particleCommon_->Initialize(dxCommon_);

    particleSystem_ = std::make_unique<ParticleSystem>();
    particleSystem_->Initialize(particleCommon_.get(),
        "Resources/sprite/white.png");

    ParticleManager::GetInstance()->Initialize(particleSystem_.get());

    gameRule_ = std::make_unique<GameRule>();
    gameRule_->Initialize(this);
    GPUParticleManager::GetInstance()->Initialize(dxCommon_);
    GPUParticleManager::GetInstance()->LoadAllPresets(
        "Resources/json/gpu_particles/");
    //
    LightEditor::GetInstance()->SetObject3dCommon(object3dCommon_.get());

    // --- 3. サブシステム初期化 ---
    objectManager_ = std::make_unique<ObjectManager>();

    lockOnSystem_ = std::make_unique<LockOnSystem>();
    lockOnSystem_->Initialize(inputManager_);
    uint32_t lockOnTex =
        TextureManager::GetInstance()->Load("Resources/sprite/lockOn.png");
    lockOnSprite_ = std::make_unique<Sprite>();
    lockOnSprite_->Initialize(spriteCommon_.get(), lockOnTex);
    lockOnSprite_->SetAnchorPoint({ 0.5f, 0.5f }); // 画像の中心を基準にする
    lockOnSprite_->SetSize({ 64.0f, 64.0f });      // アイコンのサイズ（適宜調整！）
    BulletManager::GetInstance()->Initialize(object3dCommon_.get(),
        CollisionManager::GetInstance());

    MeshEffectManager::GetInstance()->Initialize(object3dCommon_.get());
    // パーティクルで使う画像を読み込み、ハンドル(番号)を保存しておく
    gpuParticleTexHandle_ =
        TextureManager::GetInstance()->Load("Resources/sprite/white.png");

    // --- 5. レベルデータ読み込み (JSON) ---
    levelLoader_ = std::make_unique<LevelLoader>();
    levelLoader_->LoadObjectLayout(this,
        "Resources/json/3Dobject/bossStage.json");
    levelLoader_->LoadSpriteLayout(this,
        "Resources/json/sprite/sprite_layout.json");
    levelLoader_->LoadSpriteLayout(
        this, "Resources/json/sprite/option_ui.json"); // オプションUI用に追加

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
        "Resources/json/light/light_layout.json");
    CameraEditor::GetInstance()->Initialize();
    CameraEditor::GetInstance()->LoadFile("game_camera.json");

    timeAttackUI_ = std::make_unique<TimeAttackUI>();
    timeAttackUI_->Initialize(spriteCommon_.get());

    // --- スプライトの中から探索
    for (auto& sprite : sprites_) {
        if (sprite->GetName() == "playerHpBar") {
            playerHpBarSprite_ = sprite.get();
            playerHpBarMaxWidth_ = sprite->GetSize().x;
        }
        else if (sprite->GetName() == "playerDamageBar") {
            playerDamageBarSprite_ = sprite.get();
        }
    }

    // =======================================================
    // ゲームオーバー用UIの取得と初期化 (最初は透明にして隠す)
    // =======================================================
    gameOverTextSprite_ = GetSpriteByName("GameOverText.png");
    restartTextSprite_ = GetSpriteByName("restartText.png");
    titleTextSprite_ = GetSpriteByName("titleText.png");

    auto SetAlphaZero = [](Sprite* sprite) {
        if (sprite) {
            Vector4 color = sprite->GetColor();
            color.w = 0.0f; // 透明度(Alpha)を0に
            sprite->SetColor(color);
        }
        };
    SetAlphaZero(gameOverTextSprite_);
    SetAlphaZero(restartTextSprite_);
    SetAlphaZero(titleTextSprite_);
    isGameOverUiReady_ = false; // フラグのリセット
    for (auto& sprite : sprites_) {
        if (sprite->GetName() == "bossrHpBar") {
            bossHpBarSprite_ = sprite.get();
            bossHpBarMaxWidth_ = sprite->GetSize().x;
            SetAlphaZero(bossHpBarSprite_);
        }
        else if (sprite->GetName() == "bossDamageBar") {
            bossDamageBarSprite_ = sprite.get();
            SetAlphaZero(bossDamageBarSprite_);
        }
        else if (sprite->GetName() == "bariaHp.png") {
            barrierHpBarSprite_ = sprite.get();
            barrierHpBarMaxWidth_ = sprite->GetSize().x;
            SetAlphaZero(barrierHpBarSprite_);
        }
        else if (sprite->GetName() == "barrierDamageBar") {
            barrierDamageBarSprite_ = sprite.get();
            SetAlphaZero(barrierDamageBarSprite_);
        }
        else if (sprite->GetName() == "bossHpBarback") {
            bossHpBackSprite_ = sprite.get();
            SetAlphaZero(bossHpBackSprite_);
        }
        else if (sprite->GetName() == "bossText") {
            bossNameSprite_ = sprite.get();
            SetAlphaZero(bossNameSprite_);
        }
        else if (sprite->GetName() == "bossIcon.png") {
            bossIconSprite_ = sprite.get();
            bossIconBasePos_ = bossIconSprite_->GetPosition();
            SetAlphaZero(bossIconSprite_);
        }
        else if (sprite->GetName() == "shieldIcon.png") {
            shieldIconSprite_ = sprite.get();
            shieldIconBasePos_ = shieldIconSprite_->GetPosition();
            SetAlphaZero(shieldIconSprite_);
        }
        else if (sprite->GetName() == "bossHpFrame.png") {
            bossHpFrameSprite_ = sprite.get();
            SetAlphaZero(bossHpFrameSprite_);
        }
        else if (sprite->GetName() == "bariaFrame.png") {
            bariaFrameSprite_ = sprite.get();
            SetAlphaZero(bariaFrameSprite_);
        }
        else if (sprite->GetName() == "hpFrame.png") {
            hpFrameSprite_ = sprite.get();
            SetAlphaZero(hpFrameSprite_);
        }
    }

    // =======================================================
    // ポーズ用UIの取得と初期化 (最初は透明にして隠す)
    // =======================================================
    poseBackSprite_ = GetSpriteByName("poseBack.png");
    poseTextSprite_ = GetSpriteByName("poseText.png");
    restartPoseTextSprite_ = GetSpriteByName("restartPoseText.png");
    titleTextPoseSprite_ = GetSpriteByName("titleTextPose.png");
    optionPoseTextSprite_ = GetSpriteByName("optionText.png");

    auto SetAlpha = [](Sprite* sprite, float alpha) {
        if (sprite) {
            Vector4 color = sprite->GetColor();
            color.w = alpha;
            sprite->SetColor(color);
        }
        };

    SetAlpha(poseBackSprite_, 0.0f);
    SetAlpha(poseTextSprite_, 0.0f);
    SetAlpha(restartPoseTextSprite_, 0.0f);
    SetAlpha(titleTextPoseSprite_, 0.0f);
    SetAlpha(optionPoseTextSprite_, 0.0f);
    isPaused_ = false;

    // ★ 1. まず objectManager からオブジェクトのリストを取得する！
    auto& objects = objectManager_->GetObjects();

    for (auto it = objects.begin(); it != objects.end(); ++it) {
        if ((*it)->GetName() == "Enemy_BossCore") {
            // 1. 古いボスの「今の住所」をメモ（まだ消さない）
            Object3d* oldAddress = it->get();

            // 2. 新しい BossCore を準備（まだリストには入れない）
            auto newBoss = std::make_unique<BossCore>();
            newBoss->SetSceneManager(
                SceneManager::GetInstance()); // シーンマネージャの設定
            newBoss->Initialize(object3dCommon_.get(),
                oldAddress->GetModelName()); // 新しいモデルで初期化
            newBoss->CopyFrom(oldAddress);                   // 座標などをコピー
            newBoss->SetClassName("BossCore");               // ★ jsonからのコピー後に確実にクラス名を設定
            newBoss->SetTarget(player_); // プレイヤーをターゲットに設定
            this->boss_ = newBoss.get(); // コントロール用ポインタを保存
            BossCore* newAddress = newBoss.get();

            // ★★★ ここが重要：古いボスが消える「前」に全てを繋ぎ直す ★★★

            // (A) 当たり判定マネージャから古いボスを抹消し、新しいボスを登録する
            // ※ もし Remove/Add 関数がない場合は、後述の「強硬手段」を使ってください
            CollisionManager::GetInstance()->RemoveObject(oldAddress);
            CollisionManager::GetInstance()->AddObject(newAddress);

            // (B) 子供たちの親を、古い住所から新しい住所へ書き換える
            for (auto& obj : objects) {
                if (obj->GetParent() == oldAddress) {
                    obj->SetParent(newAddress);

                    // ★ ここを追加！新しいボスにパーツを登録する
                    newAddress->AddArmorBlock(obj.get());
                }
            }

            // 3. 最後に実体を差し替える。ここで oldAddress は安全に消滅する
            *it = std::move(newBoss);
            break;
        }
    }

    // ボスコンテナのパーティクル発生
    if (boss_) {
        Vector3 bossPos = boss_->GetWorldPosition();
		Vector3 offsetTop = { 0.0f, 10.0f, 0.0f }; // ボスの頭上に少しオフセット
		Vector3 offsetBottom = { 0.0f, -5.0f, 0.0f }; // ボスの足元に少しオフセット
        bossContainerTopParticleId_ =
            GPUParticleManager::GetInstance()->PlayAutoEmitter(
                "boss_container_top", bossPos + offsetTop);
        bossContainerBottomParticleId_ =
            GPUParticleManager::GetInstance()->PlayAutoEmitter(
                "boss_container_bottom", bossPos + offsetBottom);
    }

    auto SetAlphaIfExists = [](Sprite* sprite, float a) {
        if (sprite) {
            Vector4 c = sprite->GetColor();
            c.w = a;
            sprite->SetColor(c);
        }
        };

    // --- チュートリアル用スプライトの取得（最初は非表示） ---
    tutorialMoveSprite_ = GetSpriteByName("tutrialText_move.png");
    tutorialCameraSprite_ = GetSpriteByName("tutrialText_cameraControl.png");
    tutorialLockOnSprite_ = GetSpriteByName("tutrialText_lockOn.png");
    tutorialAttackSprite_ = GetSpriteByName("tutrialText_attak.png");
    tutorialDodgeSprite_ = GetSpriteByName("tutrialText_donge.png");

    SetAlphaIfExists(tutorialMoveSprite_, 0.0f);
    SetAlphaIfExists(tutorialCameraSprite_, 0.0f);
    SetAlphaIfExists(tutorialLockOnSprite_, 0.0f);
    SetAlphaIfExists(tutorialAttackSprite_, 0.0f);
    SetAlphaIfExists(tutorialDodgeSprite_, 0.0f);

    for (auto& sprite : sprites_) {
        // 既存のHPバー取得
        if (sprite->GetName() == "playerHpBar") {
            playerHpBarSprite_ = sprite.get();
            playerHpBarMaxWidth_ = sprite->GetSize().x;
        }
        // ★ ミッション用スプライトを名前で一致させて変数に保存する
        else if (sprite->GetName() == "missionText_mission.png")
            missionText_mission_ = sprite.get();
        else if (sprite->GetName() == "missionText_line.png")
            missionText_line_ = sprite.get();
        else if (sprite->GetName() == "missionText_Mark.png")
            missionText_Mark_ = sprite.get();
        else if (sprite->GetName() == "missionText_lever.png")
            missionText_lever_ = sprite.get();
        else if (sprite->GetName() == "missionText_go.png")
            missionText_go_ = sprite.get();
        else if (sprite->GetName() == "missionText_boss.png")
            missionText_boss_ = sprite.get();
    }

    SetAlphaZero(missionText_mission_);
    SetAlphaZero(missionText_line_);
    SetAlphaZero(missionText_Mark_);
    SetAlphaZero(missionText_lever_);
    SetAlphaZero(missionText_go_);
    SetAlphaZero(missionText_boss_);

    missionInitialShown_ = false;
    missionGoShown_ = false;
    missionBossShown_ = false;
    hasTutorialMovieFinished_ = false; // ★ 追加

    // ミッション演出用の初期値を保存
    if (missionText_Mark_) missionMarkBaseSize_ = missionText_Mark_->GetSize();
    if (missionText_lever_) {
        missionLeverBasePos_ = missionText_lever_->GetPosition();
        missionLeverBaseSize_ = missionText_lever_->GetSize();
    }
    if (missionText_go_) {
        missionGoBasePos_ = missionText_go_->GetPosition();
        missionGoBaseSize_ = missionText_go_->GetSize();
    }
    if (missionText_boss_) {
        missionBossBasePos_ = missionText_boss_->GetPosition();
        missionBossBaseSize_ = missionText_boss_->GetSize();
    }

    // =======================================================
    // ★ 進行状況の復元：橋がすでに落ちている場合の処理
    // =======================================================
    if (GameProgress::GetInstance()->hasBridgeDropped) {
        // 1. シーン内の全ての「橋のブロック」を検索して消去・無効化
        auto& objects_ref = objectManager_->GetObjects();
        for (auto& obj : objects_ref) {
            std::string name = obj->GetName();
            // 名前が "Bridge_"" で始まるオブジェクトを全て対象にする
            if (name.find("Bridge_") != std::string::npos) {
                obj->SetCollisionAttribute(0); // 当たり判定を完全に消す
                if (name.find("Bridge_Block") !=
                    std::string::npos) {    // ブリッジブロックは完全に消す
                    obj->SetIsVisible(false); // 見えなくする
                    obj->isDead = true;       // 完全に消す（UpdateやDrawの対象から外す）
                }
            }
            else if (name.find("Battle_Field_Collision_Box_") !=
                std::string::npos) {
                obj->SetCollisionAttribute(kGround);
            }
            else if (name.find("Tutorial_") != std::string::npos) {
				obj->SetCollisionAttribute(0); // 当たり判定を消す
                obj->SetIsVisible(false); // 見えなくする
                obj->isDead = true;       // 完全に消す（UpdateやDrawの対象から外す）
			}
        }

        // 2. 演出フラグを立てて、ムービーが二度と再生されないようにする
        this->hasBridgeDropped_ = true;
        this->missionInitialShown_ = true; // チュートリアルミッションは表示済み
        this->missionGoShown_ = true;      // 次の「GO」を表示する状態にする
        // 3. プレイヤーの開始位置をボス前に飛ばし、チュートリアルをスキップ
        if (player_) {
            // 隊長が設定したボス前の座標を適用！
            player_->GetTransform()->translate = { 0.0f, 1.3f, -68.0f };
            player_->UpdateLocalMatrix();
            player_->UpdateWorldMatrix();

            // チュートリアル完了扱いにする（進行度クラスとシーン内フラグの両方を更新）
            GameProgress::GetInstance()->hasFinishedTutorial = true;
            this->hasFinishedTutorial_ = true;
            this->hasTutorialMovieFinished_ = true; // ★ スキップ時は完了扱い
            this->doorOpenProgress_ =
                1.0f; // チュートリアル部屋のドアも全開にしておく
        }
    }
    else {
        // 最初からプレイする場合の完全リセット
        // エディタ等でJSONが書き換わっていた場合でも確実に復活させる
        auto& objects_ref = objectManager_->GetObjects();
        for (auto& obj : objects_ref) {
            std::string name = obj->GetName();
            if (name.find("Tutorial_") != std::string::npos &&
                name.find("Ceiling") == std::string::npos &&
                name.find("Doll") == std::string::npos) {

                obj->SetIsVisible(true);
                obj->SetCollisionAttribute(kGround);

                // ドアは最初は閉まっている状態にする
                if (name == "Tutorial_Door_") {
                    obj->GetTransform()->translate.x = 0.0f; // 閉まった状態の位置
                }
                obj->UpdateWorldMatrix();
            }
            if (name.find("Bridge_") != std::string::npos) {
                if (name.find("Bridge_Collision") == std::string::npos) {
                    obj->SetIsVisible(true);
                }
                else if (name.find("Bridge_") !=
                    std::string::npos) { // ブリッジ関連は全て復活させる
                    if (name.find("Bridge_Collision") == std::string::npos) {
                        obj->SetIsVisible(true);
                    }
                    obj->SetCollisionAttribute(kGround);
                }
            }
            if (name.find("Battle_Field_Collision_") != std::string::npos) {
                obj->SetCollisionAttribute(kGround);
                if (name.find("Battle_Field_Collision_Box_South") !=
                    std::string::
                    npos) { // 南の当たり判定は最初は消しておく（橋が落ちるまでは通れるように）
                    obj->SetCollisionAttribute(0);
                }
            }
        }
        this->hasBridgeDropped_ = false;
        this->hasFinishedTutorial_ = false;
        this->doorOpenProgress_ = 0.0f; // ドアを閉める

        // =======================================================
        // ★ チュートリアルプラットフォーム降下演出の初期化
        // =======================================================
        for (auto& obj : objects_ref) {
            if (obj->GetName() == "Tutorial_Platform") {
                this->tutorialPlatform_ = obj.get();
                // 初期位置を y:100 に (念のため)
                obj->GetTransform()->translate.y = 100.0f;
                obj->UpdateWorldMatrix();
                break;
            }
        }

        if (this->tutorialPlatform_ && player_) {
            // プレイヤーをプラットフォームの真上に配置
            // 本来の重力時の位置関係を維持するため、現状の差分をオフセットとして記録
            Vector3 platformPos = this->tutorialPlatform_->GetTransform()->translate;

            // プレイヤーを初期位置へ (x, z はプラットフォームに合わせ、y
            // は適切な高さへ) ユーザーの 94.7f という数値は、プラットフォーム 100.0f
            // に対して -5.3f のオフセットを示唆
            this->tutorialPlatformOffset_ = -5.3f;
            player_->GetTransform()->translate = {
                0.0f, platformPos.y + tutorialPlatformOffset_, -244.0f };
            player_->UpdateLocalMatrix();
            player_->UpdateWorldMatrix();

            // 演出開始
            movieState_ = MovieState::kTutorialPlatformDescent;
            movieTimer_ = 0.0f;

            // 重力に任せると跳ねるため、物理を無効化して手動更新にする
            player_->SetIsControlActive(false);
            player_->SetIsPhysicsActive(false);
        }
    }

    // OptionUIの初期化
    optionUI_.Initialize(this, spriteCommon_.get());

    // =======================================================
    // ★ リスタート演出（電脳リブート）と完全初期化
    // =======================================================
    SceneManager* scm = SceneManager::GetInstance();
    PostEffect::GetInstance()->ResetToBaseParams();

    if (scm->ShouldSkipFade()) {
        CinematicFade::GetInstance()->StartOpen(0.3f);
        scm->ResetSkipFade();
    }
    else {
        CinematicFade::GetInstance()->StartOpen(0.5f);
    }
    // --- 6. カメラの初期状態を強制的に反映（1フレーム目の Glide 防止） ---
    if (player_) {
        CameraEditor::GetInstance()->Update(player_, false);
        CameraManager::GetInstance()->Update();
    }

    // --- 7. 初期HPの同期 (演出用変数の初期化) ---
    if (player_) {
        playerVisualHp_ = Math::Clamp(player_->GetHp() / player_->GetMaxHp(), 0.0f, 1.0f);
        playerPrevHpRatio_ = playerVisualHp_;
    }
    if (boss_) {
        bossVisualHp_ = Math::Clamp(boss_->GetHp() / boss_->GetMaxHp(), 0.0f, 1.0f);
        bossPrevHpRatio_ = bossVisualHp_;

        float bRatio = Math::Clamp(boss_->GetBarrierHp() / boss_->GetMaxBarrierHp(), 0.0f, 1.0f);
        barrierVisualMain_ = bRatio;
        barrierVisualDamage_ = bRatio;
        barrierPrevHpRatio_ = bRatio;
    }

    dxCommon_->FlushCommandQueue(false);
}

void GamePlayScene::Finalize() {
    MeshEffectManager::GetInstance()->Clear();
    CollisionManager::GetInstance()->ClearObjects();
    BulletManager::GetInstance()->Finalize();
    particleSystem_.reset();
    particleCommon_.reset();
    sprites_.clear();
    spriteCommon_.reset();
    object3dCommon_.reset();
    objectManager_.reset();
    lockOnSystem_.reset();

    // パーティクルの停止
    GPUParticleManager::GetInstance()->StopAutoEmitter(bossContainerTopParticleId_);
    GPUParticleManager::GetInstance()->StopAutoEmitter(bossContainerBottomParticleId_);
}

void GamePlayScene::Update(float deltaTime) {
    float originalDeltaTime = deltaTime;
    // プレイヤーが死亡して演出時間が経過したら、世界の時間を止める（ただし遷移中は止めない）
    if (player_ && player_->GetHp() <= 0.0f && player_->GetDeathTimer() > 3.5f && !isRestartTransition_ && !isTitleTransition_) {
        deltaTime = 0.0f;
    }
    // ---------------------------------------------------------
    // 0. ESCキーでの強制終了（オプション画面以外）
    // ---------------------------------------------------------
    if (!isOptionMenu_ && inputManager_->IsKeyTriggered(DIK_ESCAPE)) {
        if (isPaused_) {
            // ポーズ中ならポーズを閉じる
            isPaused_ = false;
            auto SetAlpha = [](Sprite* sprite, float a) {
                if (sprite) {
                    Vector4 c = sprite->GetColor();
                    c.w = a;
                    sprite->SetColor(c);
                }
                };
            SetAlpha(poseBackSprite_, 0.0f);
            SetAlpha(poseTextSprite_, 0.0f);
            SetAlpha(restartPoseTextSprite_, 0.0f);
            SetAlpha(titleTextPoseSprite_, 0.0f);
            currentPauseMenuIndex_ = (int)PauseMenuIndex::Restart;
        }
        else {
            // ゲームプレイ中なら今まで通り終了
            PostQuitMessage(0);
        }
        return;
    }

    // ---------------------------------------------------------
    // 1. 状態判定 (ゲームオーバー・ムービー中などのフラグ)
    // ---------------------------------------------------------
    bool isGameOver = (player_ && player_->GetHp() <= 0.0f);
    bool isCinematicMode = IsCinematicMode();

    // ムービー中などはロックオンを無効化（強制解除）する
    if (lockOnSystem_) {
        lockOnSystem_->SetEnabled(!isCinematicMode);
    }

    // 【Pキー】 か パッドの【STARTボタン】でポーズ切り替え (ムービー中は不可)
    if (!isGameOver && !isCinematicMode &&
        inputManager_->IsActionTriggered("pose")) {
        if (isOptionMenu_) {
            // オプション表示中は、OptionUI::Update内部でTabキーを処理し、
            // 段階的に戻る挙動を行うため、ここでは処理しない
        }
        else {
            isPaused_ = !isPaused_; // フラグを反転

            // 文字用のアルファ値 (1.0 = 完全不透明, 0.0 = 完全透明)
            float textAlpha = isPaused_ ? 1.0f : 0.0f;

            // 背景用のアルファ値 (0.6 = 半透明。もっと薄くしたければ 0.4 や 0.5 に！)
            float backAlpha = isPaused_ ? 0.6f : 0.0f;
            auto SetAlpha = [](Sprite* sprite, float a) {
                if (sprite) {
                    Vector4 c = sprite->GetColor();
                    c.w = a;
                    sprite->SetColor(c);
                }
                };
            // ★背景だけ backAlpha を使うように変更
            SetAlpha(poseBackSprite_, backAlpha);
            SetAlpha(poseTextSprite_, textAlpha);
            SetAlpha(restartPoseTextSprite_, textAlpha);
            SetAlpha(titleTextPoseSprite_, textAlpha);

            // 選択位置をリセット
            currentPauseMenuIndex_ = (int)PauseMenuIndex::Restart;
        }
    }

    // ---------------------------------------------------------
    // 2. ポーズ中のUI操作と遷移
    // ---------------------------------------------------------
    if (isOptionMenu_) {
        if (optionUI_.Update(deltaTime)) {
            isOptionMenu_ = false; // バック等で戻る
        }
    }
    else if (isPaused_) {
        // 上下キーで項目切り替え
        if (inputManager_->IsActionTriggered("Forward")) {
            currentPauseMenuIndex_--;
            if (currentPauseMenuIndex_ < 0)
                currentPauseMenuIndex_ = (int)PauseMenuIndex::Max - 1;
        }
        if (inputManager_->IsActionTriggered("Backward")) {
            currentPauseMenuIndex_++;
            if (currentPauseMenuIndex_ >= (int)PauseMenuIndex::Max)
                currentPauseMenuIndex_ = 0;
        }

        // 選択中の項目をハイライト
        Vector4 normalColor = { 0.5f, 0.5f, 0.5f, 1.0f };
        Vector4 selectColor = { 1.0f, 1.0f, 1.0f, 1.0f };

        if (restartPoseTextSprite_)
            restartPoseTextSprite_->SetColor(
                currentPauseMenuIndex_ == (int)PauseMenuIndex::Restart ? selectColor
                : normalColor);
        if (optionPoseTextSprite_)
            optionPoseTextSprite_->SetColor(
                currentPauseMenuIndex_ == (int)PauseMenuIndex::Option ? selectColor
                : normalColor);
        if (titleTextPoseSprite_)
            titleTextPoseSprite_->SetColor(
                currentPauseMenuIndex_ == (int)PauseMenuIndex::Title ? selectColor
                : normalColor);

        // optionTextはポーズメニュー時のみ表示
        if (optionPoseTextSprite_) {
            Vector4 color = optionPoseTextSprite_->GetColor();
            color.w = 1.0f;
            optionPoseTextSprite_->SetColor(color);
        }

        // 決定ボタンで遷移
        if (inputManager_->IsActionTriggered("Jump")) {
            PostEffect::Params* postParams = PostEffect::GetInstance()->GetParams();
            postParams->dangerVignette = 0.0f;
            postParams->blackout = 0.0f;
            if (currentPauseMenuIndex_ == (int)PauseMenuIndex::Restart) {
                SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
            }
            else if (currentPauseMenuIndex_ == (int)PauseMenuIndex::Option) {
                optionUI_.Reset();
                isOptionMenu_ = true; // 設定画面遷移
            }
            else if (currentPauseMenuIndex_ == (int)PauseMenuIndex::Title) {
                SceneManager::GetInstance()->ChangeScene("TITLE");
            }
        }
    }
    else {
        // ポーズメニュー以外はoptionText.pngを非表示
        if (optionPoseTextSprite_) {
            Vector4 color = optionPoseTextSprite_->GetColor();
            color.w = 0.0f;
            optionPoseTextSprite_->SetColor(color);
        }
    }

    // UI表示切り替え（オプション中は他のUIを隠す）
    for (auto& sprite : sprites_) {
        bool isOpt = optionUI_.IsOptionSprite(sprite.get());
        if (isOptionMenu_) {
            if (isOpt) {
                sprite->SetVisible(optionUI_.IsSpriteVisibleInCurrentTab(sprite.get()));
            }
            else {
                sprite->SetVisible(false);
            }
        }
        else {
            if (isOpt) {
                sprite->SetVisible(false);
            }
            else {
                sprite->SetVisible(true);
            }
        }
    }

    if (isPaused_ || isOptionMenu_) {
        for (auto& sprite : sprites_) {
            sprite->Update();
        }
        UpdateUI(originalDeltaTime); // ポーズ中もUIアニメーションは動かす
        return;
    }

    // =======================================================
    // ★ ゲームオーバー・リトライ遷移処理 (復旧)
    // =======================================================
    if (isRestartTransition_ || isTitleTransition_) {
        restartTimer_ += originalDeltaTime;
        float transitionDuration = 1.0f;
        float t = Math::Clamp(restartTimer_ / transitionDuration, 0.0f, 1.0f);

        PostEffect::Params* postParams = PostEffect::GetInstance()->GetParams();
        if (postParams) {
            // CRTシャットダウン演出（縦に潰れる）
            postParams->crtShutdown = t;
        }

        // 完全に終了（1秒経過）したらシーンを切り替え
        if (restartTimer_ >= transitionDuration) {
            if (isRestartTransition_) {
                SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
            }
            else {
                SceneManager::GetInstance()->ChangeScene("TITLE");
            }
        }
        return; // 遷移中はこれ以降の更新をスキップ
    }
    // =======================================================
    // チュートリアルドアの処理
    // =======================================================
    if (!hasFinishedTutorial_) {
        for (auto& obj : objectManager_->GetObjects()) {
            if (obj->GetName() == "Tutorial_Doll_Lever") {
                TutorialDoll* doll = dynamic_cast<TutorialDoll*>(obj.get());
                if (doll && doll->HasBeenDefeatedAtLeastOnce()) {
                    hasFinishedTutorial_ = true;

                    // ★ ムービー開始
                    movieState_ = MovieState::kTutorialDoorOpen;
                    movieTimer_ = 0.0f;
                    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
                    CameraEditor::GetInstance()->PlayOverrideCamera(camera, "tutorial movie");

                    break;
                }
            }
        }
    }

    if (hasFinishedTutorial_) {
        if (doorOpenProgress_ < 1.0f) {
            doorOpenProgress_ += deltaTime * 0.5f; // 2秒で開く
            if (doorOpenProgress_ > 1.0f) {
                doorOpenProgress_ = 1.0f;
                // ドアが完全に開いた瞬間モデルを消しておく
                for (auto& obj : objectManager_->GetObjects()) {
                    std::string name = obj->GetName();
					if (name.find("Tutorial_Door") != std::string::npos) {
                        obj->SetCollisionAttribute(0); // 当たり判定も消す
                    }
                }

                // ここで missionText_go を表示（1回だけ）
                if (!missionGoShown_ && missionText_go_) {
                    missionGoShown_ = true;
                }
            }
        }
        for (auto& obj : objectManager_->GetObjects()) {
            if (obj->GetName() == "Tutorial_Door_Left") {
                Transform* trans = obj->GetTransform();
				trans->translate.x = 9.8f * doorOpenProgress_; // 親のbaseの都合上、左ドアは正方向に動かす
                trans->isQuaternionMaster = false;
                obj->UpdateWorldMatrix();
            }
            else if (obj->GetName() == "Tutorial_Door_Right") {
                Transform* trans = obj->GetTransform();
				trans->translate.x = -9.8f * doorOpenProgress_; // 右ドアは負方向に動かす
                trans->isQuaternionMaster = false;
                obj->UpdateWorldMatrix();
            }
        }
    }

    static Math math;
    LightEditor::GetInstance()->Update();

    Camera* camera = CameraManager::GetInstance()->GetMainCamera();

    // =================================================================
    // ムービーの制御
    // =================================================================
    if (movieState_ == MovieState::kBridgeDrop) {
        // ムービー開始時の初期化
        if (movieTimer_ == 0.0f) {
            movieStoredPlayerPos_ = player_->GetWorldPosition();
            player_->SetVelocity({ 0.0f, 0.0f, 0.0f });
            player_->SetIsControlActive(false);
            player_->SetIsPhysicsActive(false);
        }

        movieTimer_ += deltaTime;

        // プレイヤーの座標を強制固定
        // (Player::Update側でも物理が無効化されているため)
        player_->SetTranslate(movieStoredPlayerPos_);

        // ムービー開始から1.5秒後にブリッジブロックの崩落演出を開始する

        // カメラ制御は GhostRecorder に任せるため、ブロックの崩落演出のみ実行する
        if (movieTimer_ > 1.5f) {
            // まず親の当たり判定を無効化する（プレイヤーが落ちるように）
            for (auto& obj : objectManager_->GetObjects()) {
                if (obj->GetName() == "Bridge_Block_Front") {
                    obj->SetCollisionAttribute(0);
                }
            }

            for (auto& obj : objectManager_->GetObjects()) {
                if (obj->GetName() == "Bridge_Block_Center") {
                    Transform* trans = obj->GetTransform();
                    trans->translate.y -= 26.0f * deltaTime;
                    trans->rotate.x -= 1.0f * deltaTime; // 自然な傾き（下へ折れ曲がる）
                    trans->isQuaternionMaster = false;
                    obj->UpdateWorldMatrix();
                }
                else if (movieTimer_ > 2.0f &&
                    obj->GetName() == "Bridge_Block_Back") {
                    // 少し遅れて奥のブロックもさらに崩れる
                    Transform* trans = obj->GetTransform();
                    trans->translate.y -= 32.0f * deltaTime;
                    trans->rotate.x += 1.8f * deltaTime; // 折れ曲がる
                    trans->isQuaternionMaster = false;
                    obj->UpdateWorldMatrix();
                }
                else if (movieTimer_ > 2.5f &&
                    obj->GetName() == "Bridge_Block_Front") {
                    // 最後に手前の親ブロックごと崩落する
                    Transform* trans = obj->GetTransform();
                    trans->translate.y -= 48.0f * deltaTime;
                    trans->rotate.x += 0.6f * deltaTime;
                    trans->isQuaternionMaster = false;
                    obj->UpdateWorldMatrix();
                }
            }
        }

        // ムービー終了判定
        // (ブリッジブロックの物理的な落下演出自体はカメラが終わる頃まで続く想定)
        if (movieTimer_ >= 5.5f) {
            auto& objects_ref = objectManager_->GetObjects();
            for (auto& obj : objects_ref) {
                std::string name = obj->GetName();
                // 名前が "Bridge_"" で始まるオブジェクトを全て対象にする
                if (name.find("Bridge_") != std::string::npos) {
                    obj->SetCollisionAttribute(0); // 当たり判定を完全に消す
                    if (name.find("Bridge_Block") !=
                        std::string::npos) {    // ブリッジブロックは完全に消す
                        obj->SetIsVisible(false); // 見えなくする
                        obj->isDead = true; // 完全に消す（UpdateやDrawの対象から外す）
                    }
                }
                else if (
                    name.find("Battle_Field_Collision_Box_South") !=
                    std::string::
                    npos) { // 南の当たり判定を復活させる（橋が落ちた後は通れなくする）
                    obj->SetCollisionAttribute(kGround);
                }
                else if (name.find("Tutorial_") != std::string::npos) {
                    obj->SetCollisionAttribute(0); // 当たり判定を消す
                    obj->SetIsVisible(false); // 見えなくする
                    obj->isDead = true;       // 完全に消す（UpdateやDrawの対象から外す）
                }
            }
            movieState_ = MovieState::kNone;
            player_->SetIsControlActive(true);
            player_->SetIsPhysicsActive(true);
            GameProgress::GetInstance()->hasBridgeDropped = true;
        }

        // ムービー中は通常のプレイヤー入力やカメラ操作をスキップ
    }
    else if (movieState_ == MovieState::kTutorialDoorOpen) {
        // ムービー開始時の初期化
        if (movieTimer_ == 0.0f) {
            player_->SetVelocity({ 0.0f, 0.0f, 0.0f });
            player_->SetIsControlActive(false);
            player_->SetIsPhysicsActive(false);
        }

        movieTimer_ += deltaTime;

        // 1.5秒後にムービー終了
        if (movieTimer_ > 2.5f) {
            movieState_ = MovieState::kNone;
            hasTutorialMovieFinished_ = true; // ★ ムービー終了
            missionSwitchDelayTimer_ = 0.5f;  // ★ 0.5秒待機
            player_->SetIsControlActive(true);
            player_->SetIsPhysicsActive(true);
            Camera* camera = CameraManager::GetInstance()->GetMainCamera();
            camera->EndOverride(1.5f);
        }
    }
    else if (movieState_ == MovieState::kTutorialPlatformDescent) {
        if (tutorialPlatform_ && player_) {
            player_->SetIsControlActive(false);
            player_->SetIsPhysicsActive(false);

            Transform* trans = tutorialPlatform_->GetTransform();
            if (trans->translate.y > 29.6f) {
                trans->translate.y -= 15.0f * deltaTime;
                if (trans->translate.y < 29.6f)
                    trans->translate.y = 29.6f;
                tutorialPlatform_->UpdateWorldMatrix();
            }
            else {
                // ★到着！ movieState_ が kNone
                // になるので、下のシャッター制御が「下げ」に転じます
                movieState_ = MovieState::kNone;
                missionSwitchDelayTimer_ = 0.5f; // ★ 0.5秒待機
                player_->SetIsControlActive(true);
                player_->SetIsPhysicsActive(true); // 物理復帰

                if (!hasFinishedTutorial_) {
                    tutorialStep_ = TutorialStep::kShowMove;
                    tutorialTimer_ = 0.0f;
                    if (tutorialMoveSprite_) {
                        Vector4 c = tutorialMoveSprite_->GetColor();
                        c.w = 1.0f;
                        tutorialMoveSprite_->SetColor(c);
                    }
                }

                if (!missionInitialShown_) {
                    missionInitialShown_ = true;
                }
            }
            player_->GetTransform()->translate.y =
                trans->translate.y + tutorialPlatformOffset_;
            player_->UpdateWorldMatrix();
        }
    }
    // チュートリアル状態機（順序：移動 → カメラ → ロックオン → 攻撃 → 回避）
    if (!tutorialUiCompleted_) {
        switch (tutorialStep_) {
        case TutorialStep::kShowMove:
            // 表示済みを確認して入力待ちへ
            tutorialStep_ = TutorialStep::kWaitForMove;
            tutorialTimer_ = 0.0f;
            break;

        case TutorialStep::kWaitForMove: {
            bool moved = false;
            if (inputManager_) {
                Vector2 left = inputManager_->GetLeftStick();
                if (std::abs(left.x) > 0.2f || std::abs(left.y) > 0.2f)
                    moved = true;
                if (inputManager_->IsKeyPressed(DIK_W) ||
                    inputManager_->IsKeyPressed(DIK_A) ||
                    inputManager_->IsKeyPressed(DIK_S) ||
                    inputManager_->IsKeyPressed(DIK_D)) {
                    moved = true;
                }
            }
            if (!moved && player_) {
                Vector3 vel = player_->GetVelocity();
                float speed = std::sqrt(vel.x * vel.x + vel.z * vel.z);
                if (speed > 0.1f)
                    moved = true;
            }
            if (moved) {
                // 次のカメラ説明表示へ切り替え
                if (tutorialMoveSprite_) {
                    Vector4 c = tutorialMoveSprite_->GetColor();
                    c.w = 0.0f;
                    tutorialMoveSprite_->SetColor(c);
                }
                if (tutorialCameraSprite_) {
                    Vector4 c = tutorialCameraSprite_->GetColor();
                    c.w = 1.0f;
                    tutorialCameraSprite_->SetColor(c);
                }
                tutorialStep_ = TutorialStep::kWaitForCamera;
                tutorialTimer_ = 0.0f;
            }
        } break;

        case TutorialStep::kWaitForCamera: {
            bool cameraUsed = false;
            if (inputManager_) {
                Vector2 right = inputManager_->GetRightStick();
                Vector2 mouseDelta = inputManager_->GetMouseMoveDelta();
                if (std::abs(right.x) > 0.2f || std::abs(right.y) > 0.2f)
                    cameraUsed = true;
                if (std::abs(mouseDelta.x) > 2.0f || std::abs(mouseDelta.y) > 2.0f)
                    cameraUsed = true;
            }
            if (cameraUsed) {
                if (tutorialCameraSprite_) {
                    Vector4 c = tutorialCameraSprite_->GetColor();
                    c.w = 0.0f;
                    tutorialCameraSprite_->SetColor(c);
                }
                if (tutorialLockOnSprite_) {
                    Vector4 c = tutorialLockOnSprite_->GetColor();
                    c.w = 1.0f;
                    tutorialLockOnSprite_->SetColor(c);
                }
                tutorialStep_ = TutorialStep::kWaitForLockOn;
                tutorialTimer_ = 0.0f;
            }
        } break;

        case TutorialStep::kWaitForLockOn:
            if (lockOnSystem_ && lockOnSystem_->IsLockingOn()) {
                if (tutorialLockOnSprite_) {
                    Vector4 c = tutorialLockOnSprite_->GetColor();
                    c.w = 0.0f;
                    tutorialLockOnSprite_->SetColor(c);
                }
                if (tutorialAttackSprite_) {
                    Vector4 c = tutorialAttackSprite_->GetColor();
                    c.w = 1.0f;
                    tutorialAttackSprite_->SetColor(c);
                }
                tutorialStep_ = TutorialStep::kWaitForAttack;
                tutorialTimer_ = 0.0f;
            }
            break;

        case TutorialStep::kWaitForAttack:
            if (inputManager_) {
                // 攻撃ボタン検出（KeyConfig の "Attack" に対応）
                if (inputManager_->IsActionTriggered("Attack")) {
                    if (tutorialAttackSprite_) {
                        Vector4 c = tutorialAttackSprite_->GetColor();
                        c.w = 0.0f;
                        tutorialAttackSprite_->SetColor(c);
                    }
                    if (tutorialDodgeSprite_) {
                        Vector4 c = tutorialDodgeSprite_->GetColor();
                        c.w = 1.0f;
                        tutorialDodgeSprite_->SetColor(c);
                    }
                    tutorialStep_ = TutorialStep::kWaitForDodge;
                    tutorialTimer_ = 0.0f;
                }
            }
            break;

        case TutorialStep::kWaitForDodge:
            if (inputManager_) {
                // ダッシュがトリガーされたか
                bool dashTriggered = inputManager_->IsActionTriggered("Dash");

                // 同時に移動入力があるかをチェック（左スティックまたはW/A/S/D、または速度による判定）
                bool moveInput = false;
                Vector2 left = inputManager_->GetLeftStick();
                if (std::abs(left.x) > 0.2f || std::abs(left.y) > 0.2f)
                    moveInput = true;
                if (inputManager_->IsKeyPressed(DIK_W) ||
                    inputManager_->IsKeyPressed(DIK_A) ||
                    inputManager_->IsKeyPressed(DIK_S) ||
                    inputManager_->IsKeyPressed(DIK_D)) {
                    moveInput = true;
                }
                if (!moveInput && player_) {
                    Vector3 vel = player_->GetVelocity();
                    float speed = std::sqrt(vel.x * vel.x + vel.z * vel.z);
                    if (speed > 0.1f)
                        moveInput = true;
                }

                // ダッシュかつ移動入力がある場合に回避完了とする
                if (dashTriggered && moveInput) {
                    if (tutorialDodgeSprite_) {
                        Vector4 c = tutorialDodgeSprite_->GetColor();
                        c.w = 0.0f;
                        tutorialDodgeSprite_->SetColor(c);
                    }
                    tutorialStep_ = TutorialStep::kCompleted;
                    tutorialUiCompleted_ = true;
                    // 必要なら hasFinishedTutorial_ をここで true にする
                }
            }
            break;

        default:
            break;
        }
    }

    // --- ロックオン & カメラ制御 ---
    lockOnSystem_->Update(objectManager_->GetObjects(), camera, player_);
    CameraEditor::GetInstance()->Update(player_, lockOnSystem_->IsLockingOn());
    // =================================================================
    // ロックオンアイコンの 2.5D 追従計算 (World To Screen)
    // =================================================================
    Object3d* target = lockOnSystem_->GetTarget();

    if (target && lockOnSystem_->IsLockingOn()) {
        isDrawLockOn_ = true;

        // =======================================================
        // ：AABB(当たり判定)から「真の中心」と「大きさ」を取得！
        // =======================================================
        AABB aabb = target->GetAABB();

        // ① ターゲットの「真の中心座標」を計算
        Vector3 targetCenter;
        targetCenter.x = (aabb.min.x + aabb.max.x) * 0.5f;
        targetCenter.y = (aabb.min.y + aabb.max.y) * 0.5f;
        targetCenter.z = (aabb.min.z + aabb.max.z) * 0.5f;

        // ② カメラのビュー行列とプロジェクション行列を掛け合わせる
        Matrix4x4 viewProj =
            math.Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix());

        // ③ ワールド座標(中心) → クリップ座標 (W除算) の計算
        float w = targetCenter.x * viewProj.m[0][3] +
            targetCenter.y * viewProj.m[1][3] +
            targetCenter.z * viewProj.m[2][3] + viewProj.m[3][3];

        // カメラの後ろ（画面外）にいる時は表示しない
        if (w > 0.001f) {
            Vector3 ndc;
            ndc.x = (targetCenter.x * viewProj.m[0][0] +
                targetCenter.y * viewProj.m[1][0] +
                targetCenter.z * viewProj.m[2][0] + viewProj.m[3][0]) /
                w;
            ndc.y = (targetCenter.x * viewProj.m[0][1] +
                targetCenter.y * viewProj.m[1][1] +
                targetCenter.z * viewProj.m[2][1] + viewProj.m[3][1]) /
                w;
            float screenWidth = static_cast<float>(WinApp::kClientWidth);
            float screenHeight = static_cast<float>(WinApp::kClientHeight);

            float screenX = (ndc.x + 1.0f) * 0.5f * screenWidth;
            float screenY = (1.0f - ndc.y) * 0.5f * screenHeight;

            lockOnSprite_->SetPosition({ screenX, screenY });

            // =======================================================
            // ：オブジェクトの大きさに応じたアイコンサイズの自動調整！
            // =======================================================
            float objSizeX = aabb.max.x - aabb.min.x;
            float objSizeY = aabb.max.y - aabb.min.y;
            float objSizeZ = aabb.max.z - aabb.min.z;
            float maxObjSize = std::max({ objSizeX, objSizeY, objSizeZ });

            float baseSize = maxObjSize * 25.0f;
            float distanceScale = 20.0f / w;

            float finalSize = baseSize * distanceScale;
            finalSize = std::max(32.0f, std::min(finalSize, 256.0f));

            lockOnSprite_->SetSize({ finalSize, finalSize });

            // （おまけ）ロックオンアイコンを毎フレーム少し回転させると超カッコよくなります
            float currentRot = lockOnSprite_->GetRotation();
            lockOnSprite_->SetRotation(currentRot + 2.0f * deltaTime);

            lockOnSprite_->Update();
        }
        else {
            isDrawLockOn_ = false; // カメラの裏にいる時は消す
        }
    }
    else {
        // =======================================================
        // ロックオンしていない時は確実に表示をオフにする！
        // =======================================================
        isDrawLockOn_ = false;
    }

    // 自由カメラモード以外の操作
    if (!CameraEditor::GetInstance()->IsEditorMode() && !isCinematicMode) {
        Camera::FollowMode currentMode = camera->GetFollowMode();

        if (player_ && player_->GetHp() > 0.0f &&
            (currentMode == Camera::FollowMode::kAimable ||
                currentMode == Camera::FollowMode::kFirstPerson)) {

            // =======================================================
            // ★ 1. マウスの移動量と、ゲームパッドの右スティック入力を両方取得！
            // =======================================================
            Vector2 mouseDelta = inputManager_->GetMouseMoveDelta();
            Vector2 rightStick = inputManager_->GetRightStick();

            // =======================================================
            // ★ 2. 入力値から「最終的な移動量」を出す（感度はCamera側で処理）
            // =======================================================
            Vector2 totalDelta;
            totalDelta.x = (mouseDelta.x + rightStick.x * 15.0f);
            totalDelta.y = (mouseDelta.y - rightStick.y * 15.0f); // スティックの上下は反転

#ifdef USE_IMGUI
            // ★ デバッグ(Develop)環境:
            // UI操作の誤爆を防ぐため「右クリック中」または「スティック入力中」のみ回転
            if (inputManager_->IsMouseButtonPressed(1) || rightStick.x != 0.0f ||
                rightStick.y != 0.0f) {
                if (totalDelta.x != 0.0f || totalDelta.y != 0.0f) {
                    camera->AddRotation(totalDelta);
                }
            }
#else
            // ★ Release環境限定: 右クリック不要！操作した分だけ回転する
            if (totalDelta.x != 0.0f || totalDelta.y != 0.0f) {
                camera->AddRotation(totalDelta);
            }
#endif
        }
    }
    // ムービー状態などに応じて黒帯の高さを決める
    float targetBarHeight = isCinematicMode ? 0.12f : 0.0f;

    // 現在の高さを滑らかに補間（5.0f は開閉スピード）
    currentCinemaBarHeight_ +=
        (targetBarHeight - currentCinemaBarHeight_) * 5.0f * deltaTime;

    // ポストエフェクトに反映
    PostEffect::Params* postParams = PostEffect::GetInstance()->GetParams();
    if (postParams) {
        postParams->cinemaBarHeight = currentCinemaBarHeight_;
    }

    // --- 全体更新 ---
    CameraManager::GetInstance()->Update();
    particleSystem_->Update(deltaTime);
    objectManager_->Update(deltaTime); // オブジェクト一括更新

    if (boss_) {
        boss_->ActuallySpawnShards();
    }

    if (timeAttackUI_) {
        timeAttackUI_->Update(deltaTime);
    }
    //// 溜まった発生命令をもとに、GPUに計算（Compute Shader）を走らせる
    GPUParticleManager::GetInstance()->Update(deltaTime);
    for (auto& sprite : sprites_) {
        sprite->Update();
    }
    // =========================================================
    // 💀 ゲームオーバー画面のフェードインとメニュー選択
    // =========================================================
    if (player_ && player_->GetHp() <= 0.0f) {

        // プレイヤーの点滅演出(3.5秒)が終わったら処理開始
        if (player_->GetDeathTimer() > 3.5f) {

            // --- 1. テキストのフェードイン ---
            if (!isGameOverUiReady_) {
                bool allFadedIn = true;

                auto FadeInSprite = [originalDeltaTime, &allFadedIn](Sprite* sprite) {
                    if (sprite) {
                        Vector4 color = sprite->GetColor();
                        if (color.w < 1.0f) {
                            color.w += originalDeltaTime * 0.5f; // 徐々に不透明にする
                            if (color.w > 1.0f)
                                color.w = 1.0f;
                            sprite->SetColor(color);
                            allFadedIn = false; // まだ透明なやつがいればフラグを下ろす
                        }
                    }
                    };

                FadeInSprite(gameOverTextSprite_);
                FadeInSprite(restartTextSprite_);
                FadeInSprite(titleTextSprite_);

                // 全部の文字が完全に出現したら準備完了！
                if (allFadedIn) {
                    isGameOverUiReady_ = true;
                }
            }
            // --- 2. メニュー選択とシーン遷移 ---
            else {
                InputManager* input = InputManager::GetInstance();

                // 上下キーで項目切り替え (パッドの十字キーにも対応)
                if (input->IsActionTriggered("Forward")) {
                    currentGameOverMenuIndex_--;
                    if (currentGameOverMenuIndex_ < 0)
                        currentGameOverMenuIndex_ = (int)GameOverMenuIndex::Max - 1;
                }
                if (input->IsActionTriggered("Backward")) {
                    currentGameOverMenuIndex_++;
                    if (currentGameOverMenuIndex_ >= (int)GameOverMenuIndex::Max)
                        currentGameOverMenuIndex_ = 0;
                }

                // 選択中の項目をハイライト
                // (選ばれてるほうを白、そうでないほうを少し暗くする)
                Vector4 normalColor = { 0.5f, 0.5f, 0.5f, 1.0f };
                Vector4 selectColor = { 1.0f, 1.0f, 1.0f, 1.0f };

                if (restartTextSprite_) {
                    restartTextSprite_->SetColor(currentGameOverMenuIndex_ ==
                        (int)GameOverMenuIndex::Restart
                        ? selectColor
                        : normalColor);
                }
                if (titleTextSprite_) {
                    titleTextSprite_->SetColor(currentGameOverMenuIndex_ ==
                        (int)GameOverMenuIndex::Title
                        ? selectColor
                        : normalColor);
                }
                // 決定ボタンで遷移！
                if (input->IsActionTriggered("Jump")) {

                    // 共通のUI透明化ラムダ式
                    auto SetAlphaZero = [](Sprite* sprite) {
                        if (sprite) {
                            Vector4 color = sprite->GetColor();
                            color.w = 0.0f;
                            sprite->SetColor(color);
                        }
                        };

                    if (currentGameOverMenuIndex_ == (int)GameOverMenuIndex::Restart) {
                        isRestartTransition_ = true;
                        restartTimer_ = 0.0f;

                        SetAlphaZero(gameOverTextSprite_);
                        SetAlphaZero(restartTextSprite_);
                        SetAlphaZero(titleTextSprite_);
                    }
                    else if (currentGameOverMenuIndex_ ==
                        (int)GameOverMenuIndex::Title) {

                        isTitleTransition_ = true;
                        restartTimer_ = 0.0f;

                        SetAlphaZero(gameOverTextSprite_);
                        SetAlphaZero(restartTextSprite_);
                        SetAlphaZero(titleTextSprite_);
                    }
                }
            }
        }
    }
    BulletManager::GetInstance()->Update(deltaTime);
    CollisionManager::GetInstance()->Update();
    MeshEffectManager::GetInstance()->Update(deltaTime);
    UpdateUI(deltaTime);

    // ========================================================
    // ★ ボス登場ムービー中の監視処理（時間で強制終了！）
    // ========================================================
    if (isBossMoviePlaying_ && boss_) {

        // ★ タイマーを進める！
        movieTimer_ += deltaTime;

        // プレイヤーがズレないように固定し続ける
        if (player_) {
            player_->SetTranslate(movieStoredPlayerPos_);
            player_->UpdateWorldMatrix();
        }

        // ====================================================
        // ★ 修正：全体時間を 3.0f から 4.0f に伸ばす！（1秒の待機が増えたため）
        // ====================================================
        if (movieTimer_ >= 4.0f) {
            isBossMoviePlaying_ = false;
            missionSwitchDelayTimer_ = 0.5f; // ★ 0.5秒待機

            if (Camera* camera = CameraManager::GetInstance()->GetMainCamera()) {
                camera->EndOverride(1.0f);
            }

            if (player_) {
                player_->SetIsControlActive(true);
                player_->SetIsPhysicsActive(true);
            }

            boss_->StartBattle();

            // ボス登場後に missionText_boss を表示（1回だけ）
            if (!missionBossShown_ && missionText_boss_) {
                missionBossShown_ = true;
            }

            if (timeAttackUI_) {
                timeAttackUI_->Start();
            }
        }
    }
    if (boss_) {
        // ボスが完全に消滅し、かつまだクリアシーケンスに入っていなければ開始
        if (boss_->IsCompletelyDead() && !isGameClearSequence_) {
            isGameClearSequence_ = true;
            gameClearTimer_ = 0.0f;

            // タイマーを止める
            if (timeAttackUI_) {
                timeAttackUI_->Stop();
            }
            float clearTime = timeAttackUI_->GetCurrentTime();
            SaveDataManager::GetInstance()->RecordClearTime(clearTime);

            DebugConsole::GetInstance()->AddLog(
                "クリアタイムを保存しました: " + std::to_string(clearTime) + " 秒");
            DebugConsole::GetInstance()->AddLog("【GAME CLEAR】 クリア演出開始！");
        }
    }

    // クリアシーケンス中の処理
    if (isGameClearSequence_) {
        gameClearTimer_ += deltaTime;

        // ボス消滅から 2.0 秒後に「CLEAR」シーンへ遷移！
        if (gameClearTimer_ > 2.0f) {
            SceneManager::GetInstance()->ChangeScene("GAMECLEAR");
        }
    }
}

void GamePlayScene::Draw() {
    // --- 一人称視点判定 ---
    bool isFirstPerson = false;
    Camera* camera = CameraManager::GetInstance()->GetMainCamera();
#ifndef _DEBUG
    if (camera->GetFollowTarget() &&
        camera->GetFollowMode() == Camera::FollowMode::kFirstPerson) {
        isFirstPerson = true;
    }
#endif

    // =========================================================
    // ★ 追加:
    // カメラがプレイヤーに近すぎたら、強制的に「非表示(一人称扱い)」にする！
    // =========================================================
    if (!isFirstPerson && player_ && camera) {
        Vector3 pPos = player_->GetWorldPosition();
        pPos.y += 1.0f; // プレイヤーの胸の高さを基準にする
        Vector3 cPos = camera->GetEye();
        Vector3 toCam = { cPos.x - pPos.x, cPos.y - pPos.y, cPos.z - pPos.z };
        float dist =
            std::sqrt(toCam.x * toCam.x + toCam.y * toCam.y + toCam.z * toCam.z);

        // 距離が 3.0m 未満なら、プレイヤーを完全に消す！
        if (dist < 3.0f) {
            isFirstPerson = true;
        }
    }

    ID3D12Resource* pointLightRes =
        LightManager::GetInstance()->GetPointLightResource();
    ID3D12Resource* spotLightRes =
        LightManager::GetInstance()->GetSpotLightResource();
    object3dCommon_->SetGraphicsCommand();

    auto& objects = objectManager_->GetObjects();

    // =========================================================
    // ★ ここに「完全自動カリング」のロジックを挿入！
    // =========================================================
    Frustum frustum = camera->GetFrustum();
    Math math;
    int drawCount = 0;
    int totalCount = 0;

    auto IsVisible = [&](Object3d* obj) {
        if (!obj->GetIsVisible())
            return false;

        // 1. オブジェクトから Model を取得
        // ※もし Object3d に GetModel() が無ければ追加してください！ ( return
        // model_; など )
        Model* model = obj->GetModel();
        if (!model)
            return true; // モデルが無い(空の)場合は安全のため描画を通す

        // 2. モデル本来のサイズ（ローカルAABB）を取得
        Vector3 lMin = model->GetLocalAabbMin();
        Vector3 lMax = model->GetLocalAabbMax();

        // 3. ローカルの「箱の8つの角（頂点）」を作成
        Vector3 corners[8] = { {lMin.x, lMin.y, lMin.z}, {lMax.x, lMin.y, lMin.z},
                              {lMin.x, lMax.y, lMin.z}, {lMax.x, lMax.y, lMin.z},
                              {lMin.x, lMin.y, lMax.z}, {lMax.x, lMin.y, lMax.z},
                              {lMin.x, lMax.y, lMax.z}, {lMax.x, lMax.y, lMax.z} };

        // 4. ワールド行列を使って、8つの角すべてをゲーム空間の座標に変換する
        Matrix4x4 wm = obj->GetWorldMatrix();
        Vector3 wMin = { FLT_MAX, FLT_MAX, FLT_MAX };
        Vector3 wMax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

        for (int i = 0; i < 8; ++i) {
            // math.Transform で座標に行列を掛ける
            Vector3 wPos = math.Transform(corners[i], wm);

            // 変換後の8つの点から、ワールド空間での新たな min / max を見つける
            wMin.x = (std::min)(wMin.x, wPos.x);
            wMin.y = (std::min)(wMin.y, wPos.y);
            wMin.z = (std::min)(wMin.z, wPos.z);
            wMax.x = (std::max)(wMax.x, wPos.x);
            wMax.y = (std::max)(wMax.y, wPos.y);
            wMax.z = (std::max)(wMax.z, wPos.z);
        }

        // 回転したことで箱が大きくなっても問題なし！確実にオブジェクトを包み込むAABBが完成。
        return math.IntersectFrustumAABB(frustum, wMin, wMax);
        };

    // --- 1. 不透明描画 ---
    for (auto& obj : objects) {

        bool isPlayerPart = false;
        if (isFirstPerson) {
            Object3d* current = obj.get();
            while (current) {
                if (current == player_) {
                    isPlayerPart = true;
                    break;
                }
                current = current->GetParent();
            }
        }
        if (isPlayerPart)
            continue; // プレイヤーの一部なら描画をスキップ！

        if (obj->GetMaterialType() == 1 || obj->GetMaterialType() == 7 || obj->GetMaterialType() == 9)
            continue;

        totalCount++;
        // ★ カリング判定！
        if (IsVisible(obj.get())) {
            obj->Draw(pointLightRes, spotLightRes);
            drawCount++;
        }
    }

    // --- 2. 中間描画 (弾・デバッグ) ---
    BulletManager::GetInstance()->Draw(pointLightRes, spotLightRes);
    if (debugEditor_)
        debugEditor_->DrawPreview(pointLightResource_.Get(),
            spotLightResource_.Get());
    LightEditor::GetInstance()->Draw3D();
    MeshEffectManager::GetInstance()->Draw(pointLightRes, spotLightRes);

    // --- 3. 透明描画 ---
    for (auto& obj : objects) {
        // ここでも同じくプレイヤー関連をスキップ
        bool isPlayerPart = false;
        if (isFirstPerson) {
            Object3d* current = obj.get();
            while (current) {
                if (current == player_) {
                    isPlayerPart = true;
                    break;
                }
                current = current->GetParent();
            }
        }
        if (isPlayerPart)
            continue;

        if (obj->GetMaterialType() == 1 || obj->GetMaterialType() == 9) { // 透明のみ描画
            totalCount++;
            // ★ カリング判定！
            if (IsVisible(obj.get())) {
                obj->Draw(pointLightRes, spotLightRes);
                drawCount++;
            }
        }
    }
    particleSystem_->Draw();

    // =======================================================
    // 4. ローカルフォグ (霧の箱) の描画！
    // =======================================================
    bool hasFog = false;
    for (auto& obj : objects) {
        if (obj->GetMaterialType() == 7)
            hasFog = true;
    }

    if (hasFog) {
        dxCommon_->PreDrawLocalFog();
        for (auto& obj : objects) {
            if (obj->GetMaterialType() == 7) {
                // ★ フォグの箱自体も画面外なら描画しないように最適化！
                if (IsVisible(obj.get())) {
                    obj->DrawLocalFog(dxCommon_->GetDepthSrvHandle());
                }
            }
        }
        dxCommon_->PostDrawLocalFog();
    }

    // =======================================================
    // 5. GPUパーティクルの描画！
    // =======================================================
    dxCommon_->UpdateGrabTexture();

    GPUParticleManager::GetInstance()->Draw(
        dxCommon_->GetCommandList(), camera->GetViewMatrix(),
        camera->GetProjectionMatrix(), gpuParticleTexHandle_,
        dxCommon_->GetDepthSrvHandle());

    // ★ カリングがどれくらい効いているか確認用のログ
    // DebugConsole::GetInstance()->AddLog("DrawCount: " +
    // std::to_string(drawCount) + " / Total: " + std::to_string(totalCount));
}
// ====================================================================
// UI描画専用の関数
// ====================================================================
void GamePlayScene::DrawUI() {
    bool isCinematic = IsCinematicMode();
    bool isGameOver = (player_ && player_->GetHp() <= 0.0f);

    // --- 4. 2D描画 (UIスプライト) ---
    spriteCommon_->SetPipeline(dxCommon_->GetCommandList());

    // ヘルパー: システムUIかどうかの判定
    auto IsPauseUI = [&](Sprite* sp) {
        return sp == poseBackSprite_ || sp == poseTextSprite_ ||
               sp == restartPoseTextSprite_ || sp == titleTextPoseSprite_ ||
               sp == optionPoseTextSprite_;
    };
    auto IsGameOverUI = [&](Sprite* sp) {
        return sp == gameOverTextSprite_ || sp == restartTextSprite_ ||
               sp == titleTextSprite_;
    };

    // スプライト一括描画の制御
    for (auto& sprite : sprites_) {
        Sprite* sp = sprite.get();
        if (!sp) continue;

        bool isPause = IsPauseUI(sp);
        bool isGameOverUI = IsGameOverUI(sp);
        bool isOption = optionUI_.IsOptionSprite(sp);

        if (isOptionMenu_) {
            // オプション中はオプション関連かつ現在のタブに該当するもののみ
            if (isOption && optionUI_.IsSpriteVisibleInCurrentTab(sp)) {
                sp->Draw();
            }
        }
        else if (isPaused_) {
            // ポーズ中はポーズ関連のみ
            if (isPause) sp->Draw();
        }
        else if (isGameOver) {
            // ゲームオーバー中はゲームオーバー関連のみ
            if (isGameOverUI) sp->Draw();
        }
        else if (!isCinematic) {
            // 通常時（シネマティックでない時）はゲーム用UI（システム系以外）を表示
            if (!isPause && !isGameOverUI && !isOption) {
                sp->Draw();
            }
        }
    }

    // ロックオンアイコンとタイムアタックUI
    if (isDrawLockOn_ && lockOnSprite_ && !isPaused_ && !isOptionMenu_ && !isCinematic && !isGameOver) {
        lockOnSprite_->Draw();
    }
    if (timeAttackUI_ && hasBossAppeared_ && !isPaused_ && !isOptionMenu_ && !isCinematic && !isGameOver) {
        timeAttackUI_->Draw();
    }

    // オプションUI固有の描画（動的生成アイコンなど）
    if (isOptionMenu_) {
        optionUI_.DrawKeyIcons();
    }
}

void GamePlayScene::DrawShadow() {
    if (objectManager_) {

        objectManager_->DrawShadow();
    }
}

bool GamePlayScene::IsCinematicMode() const {
    bool isBossDying = boss_ && boss_->IsDyingSequence();
    return (movieState_ != MovieState::kNone) || isBossMoviePlaying_ || isBossDying;
}

void GamePlayScene::UpdateUI(float deltaTime) {
    // 1. プレイヤーのHP同期
    if (player_ && playerHpBarSprite_) {
        float currentHp = player_->GetHp();
        float maxHp = player_->GetMaxHp();
        float hpRatio = Math::Clamp(currentHp / maxHp, 0.0f, 1.0f);

        // ダメージを受けた瞬間を検知してタイマーをセット
        if (hpRatio < playerPrevHpRatio_) {
            playerDamageDelayTimer_ = 0.6f; // 0.6秒待機してから減り始める
        }
        playerPrevHpRatio_ = hpRatio;

        // 緑色のメインバーは即座に反映
        playerHpBarSprite_->SetSize({ playerHpBarMaxWidth_ * hpRatio, playerHpBarSprite_->GetSize().y });

        // 赤色のダメージバー演出 (現在のHPを追いかける)
        if (playerVisualHp_ > hpRatio) {
            if (playerDamageDelayTimer_ > 0.0f) {
                // 待機中
                playerDamageDelayTimer_ -= deltaTime;
            } else {
                // 待機終了：徐々に減らす
                playerVisualHp_ -= 0.15f * deltaTime; // 秒間15%減少 (よりゆっくりに)
                if (playerVisualHp_ < hpRatio) playerVisualHp_ = hpRatio;
            }
        } else {
            // 回復した、または初期化：即座に追いつく
            playerVisualHp_ = hpRatio;
            playerDamageDelayTimer_ = 0.0f;
        }

        if (playerDamageBarSprite_) {
            playerDamageBarSprite_->SetSize({ playerHpBarMaxWidth_ * playerVisualHp_, playerDamageBarSprite_->GetSize().y });
        }
    }
    if (boss_) {
        // =======================================================
        // ボスUIの表示・非表示制御
        // ムービーが終了（!isBossMoviePlaying_）したら表示する
        // =======================================================
        float alpha = (hasBossAppeared_ && !isBossMoviePlaying_) ? 1.0f : 0.0f;

        auto SetAlpha = [](Sprite* s, float a) {
            if (s) {
                Vector4 c = s->GetColor();
                c.w = a;
                s->SetColor(c);
            }
            };

        SetAlpha(bossHpBarSprite_, alpha);
        SetAlpha(bossDamageBarSprite_, alpha); // ダメージバーも同期
        SetAlpha(barrierHpBarSprite_, alpha);
        SetAlpha(barrierDamageBarSprite_, alpha); // 追加
        SetAlpha(bossHpBackSprite_, alpha);
        SetAlpha(bossNameSprite_, alpha);
        SetAlpha(bossIconSprite_, alpha); // 追加
        SetAlpha(shieldIconSprite_, alpha); // 追加
        SetAlpha(bossHpFrameSprite_, alpha);
        SetAlpha(bariaFrameSprite_, alpha);
        SetAlpha(hpFrameSprite_, alpha);

        // --- A. メインHPバーの同期 ---
        if (bossHpBarSprite_) {
            float hpRatio = Math::Clamp(boss_->GetHp() / boss_->GetMaxHp(), 0.0f, 1.0f);
            
            // ダメージを受けた瞬間を検知
            if (hpRatio < bossPrevHpRatio_) {
                bossDamageDelayTimer_ = 0.8f; // ボスはより長く待機 (0.8秒)
                // ボスアイコンシェイクを設定
                bossIconShakeTimer_ = 0.3f; // 0.3秒シェイク
                bossIconShakeIntensity_ = 8.0f; // シェイクの強さ
            }
            bossPrevHpRatio_ = hpRatio;

            // 赤色のメインバーは即座に反映
            bossHpBarSprite_->SetSize({ bossHpBarMaxWidth_ * hpRatio, bossHpBarSprite_->GetSize().y });

            // 白色のダメージバー演出
            if (bossVisualHp_ > hpRatio) {
                if (bossDamageDelayTimer_ > 0.0f) {
                    bossDamageDelayTimer_ -= deltaTime;
                } else {
                    bossVisualHp_ -= 0.1f * deltaTime; // 秒間10%減少 (ボスは重厚感を出すためにさらにゆっくり)
                    if (bossVisualHp_ < hpRatio) bossVisualHp_ = hpRatio;
                }
            } else {
                bossVisualHp_ = hpRatio;
                bossDamageDelayTimer_ = 0.0f;
            }

            if (bossDamageBarSprite_) {
                bossDamageBarSprite_->SetSize({ bossHpBarMaxWidth_ * bossVisualHp_, bossDamageBarSprite_->GetSize().y });
            }
        }

        // --- B. バリアHPバーの同期 ---
        if (barrierHpBarSprite_) {
            float bRatio = Math::Clamp(
                boss_->GetBarrierHp() / boss_->GetMaxBarrierHp(), 0.0f, 1.0f);

            // スタン中かどうかの判定
            bool isBossStunned = (boss_->GetState() == BossCore::State::Weak);

            if (isBossStunned) {
                // スタン（ダウン）中はバーを0で固定し、復帰を待つ
                barrierVisualMain_ = 0.0f;
                barrierVisualDamage_ = 0.0f;
                barrierPrevHpRatio_ = bRatio; // 内部的には100%になっていても、ここでは現在の値（1.0）を追従させておく
            }
            else {
                // 1. ダメージを受けた瞬間を検知
                if (bRatio < barrierPrevHpRatio_) {
                    barrierDamageDelayTimer_ = 0.5f;
                    // バリアアイコンシェイクを設定
                    shieldIconShakeTimer_ = 0.3f; // 0.3秒シェイク
                    shieldIconShakeIntensity_ = 8.0f; // シェイクの強さ
                }
                barrierPrevHpRatio_ = bRatio;

                // 2. メインバー (Cyan) の更新
                if (barrierVisualMain_ < bRatio) {
                    // 復帰時: 徐々に増やす (Kirby-style 0->100 recovery)
                    barrierVisualMain_ += 0.4f * deltaTime; // 秒間40%回復
                    if (barrierVisualMain_ > bRatio) barrierVisualMain_ = bRatio;
                }
                else {
                    // 通常時・ダメージ時: 即座に反映
                    barrierVisualMain_ = bRatio;
                }

                // 3. ダメージバー (White) の更新
                if (barrierVisualDamage_ > bRatio) {
                    // ダメージ中: 待機後に徐々に減らす
                    if (barrierDamageDelayTimer_ > 0.0f) {
                        barrierDamageDelayTimer_ -= deltaTime;
                    }
                    else {
                        barrierVisualDamage_ -= 0.15f * deltaTime; // 秒間15%減少
                        if (barrierVisualDamage_ < bRatio) barrierVisualDamage_ = bRatio;
                    }
                }
                else {
                    // 回復中または安定: メインバーに同期
                    barrierVisualDamage_ = barrierVisualMain_;
                }
            }

            // スプライトに反映
            barrierHpBarSprite_->SetSize(
                { barrierHpBarMaxWidth_ * barrierVisualMain_, barrierHpBarSprite_->GetSize().y });

            if (barrierDamageBarSprite_) {
                barrierDamageBarSprite_->SetSize(
                    { barrierHpBarMaxWidth_ * barrierVisualDamage_, barrierDamageBarSprite_->GetSize().y });
            }
        }

        // --- C. アイコンのシェイク更新 ---
        if (bossIconSprite_) {
            if (bossIconShakeTimer_ > 0.0f) {
                bossIconShakeTimer_ -= deltaTime;
                float offsetX = ((float)rand() / RAND_MAX * 2.0f - 1.0f) * bossIconShakeIntensity_;
                float offsetY = ((float)rand() / RAND_MAX * 2.0f - 1.0f) * bossIconShakeIntensity_;
                bossIconSprite_->SetPosition({ bossIconBasePos_.x + offsetX, bossIconBasePos_.y + offsetY });
                if (bossIconShakeTimer_ <= 0.0f) {
                    bossIconSprite_->SetPosition(bossIconBasePos_);
                }
            }
        }

        if (shieldIconSprite_) {
            if (shieldIconShakeTimer_ > 0.0f) {
                shieldIconShakeTimer_ -= deltaTime;
                float offsetX = ((float)rand() / RAND_MAX * 2.0f - 1.0f) * shieldIconShakeIntensity_;
                float offsetY = ((float)rand() / RAND_MAX * 2.0f - 1.0f) * shieldIconShakeIntensity_;
                shieldIconSprite_->SetPosition({ shieldIconBasePos_.x + offsetX, shieldIconBasePos_.y + offsetY });
                if (shieldIconShakeTimer_ <= 0.0f) {
                    shieldIconSprite_->SetPosition(shieldIconBasePos_);
                }
            }
        }
    }
    // --- ミッションテキストのアニメーション演出 ---
    auto SetAlpha = [](Sprite* s, float a) {
        if (s) {
            Vector4 c = s->GetColor();
            c.w = a;
            s->SetColor(c);
        }
        };

    // 1. missionText_mission & missionText_line (既存の処理を尊重)
    if (hasBossAppeared_) {
        SetAlpha(missionText_mission_, 0.0f);
        SetAlpha(missionText_line_, 1.0f); // ボス戦中もラインは維持
    }
    else if (hasBridgeDropped_) {
        SetAlpha(missionText_mission_, 0.0f);
        SetAlpha(missionText_line_, 1.0f);
    }
    else if (missionInitialShown_) {
        SetAlpha(missionText_mission_, 1.0f);
        SetAlpha(missionText_line_, 1.0f);
    }

    // --- タイマー更新 ---
    if (missionSwitchDelayTimer_ > 0.0f) {
        missionSwitchDelayTimer_ -= deltaTime;
    }

    // 2. missionText_Mark (回転しながら出現)
    if (missionInitialShown_ && missionText_Mark_) {
        if (missionSwitchDelayTimer_ > 0.0f) return; // ★ 待機中
        missionMarkAnimProgress_ = std::min(1.0f, missionMarkAnimProgress_ + deltaTime * 2.0f);
        float scale = missionMarkAnimProgress_;
        float rot = (1.0f - missionMarkAnimProgress_) * 3.14159265f * 2.0f;
        missionText_Mark_->SetSize({ missionMarkBaseSize_.x * scale, missionMarkBaseSize_.y * scale });
        missionText_Mark_->SetRotation(rot);
        SetAlpha(missionText_Mark_, 1.0f);
    }

    // 3. lever -> go -> boss 遷移演出
    // A. Lever (初期ミッション)
    if (missionInitialShown_ && !isLeverOut_) {
        if (!hasTutorialMovieFinished_) {
            if (missionSwitchDelayTimer_ > 0.0f) return; // ★ 待機中

            // 出現アニメーション（または表示維持）
            missionLeverAnimProgress_ = std::min(1.0f, missionLeverAnimProgress_ + deltaTime * 2.0f);
            if (missionText_lever_) {
                float offsetY = (1.0f - missionLeverAnimProgress_) * 20.0f;
                missionText_lever_->SetPosition({ missionLeverBasePos_.x, missionLeverBasePos_.y + offsetY });
                SetAlpha(missionText_lever_, missionLeverAnimProgress_);
            }
        }
        else {
            if (missionSwitchDelayTimer_ > 0.0f) return; // ★ 0.2秒待機

            // 完了アニメーション (Yスケール -> 0)
            leverOutProgress_ = std::min(1.0f, leverOutProgress_ + deltaTime * 4.0f);
            if (missionText_lever_) {
                missionText_lever_->SetSize({ missionLeverBaseSize_.x, missionLeverBaseSize_.y * (1.0f - leverOutProgress_) });
            }
            if (leverOutProgress_ >= 1.0f) {
                isLeverOut_ = true;
                SetAlpha(missionText_lever_, 0.0f);
            }
        }
    }

    // B. Go (レバー完了後)
    if (isLeverOut_ && !isGoOut_) {
        if (!hasBossAppeared_ || isBossMoviePlaying_) { // ★ ムービー中も維持するように変更
            // 出現アニメーション
            missionGoAnimProgress_ = std::min(1.0f, missionGoAnimProgress_ + deltaTime * 2.0f);
            if (missionText_go_) {
                float offsetY = (1.0f - missionGoAnimProgress_) * 20.0f;
                missionText_go_->SetPosition({ missionGoBasePos_.x, missionGoBasePos_.y + offsetY });
                SetAlpha(missionText_go_, missionGoAnimProgress_);
            }
        }
        else {
            if (missionSwitchDelayTimer_ > 0.0f) return; // ★ 0.2秒待機

            // 完了アニメーション (Yスケール -> 0)
            goOutProgress_ = std::min(1.0f, goOutProgress_ + deltaTime * 4.0f);
            if (missionText_go_) {
                missionText_go_->SetSize({ missionGoBaseSize_.x, missionGoBaseSize_.y * (1.0f - goOutProgress_) });
            }
            if (goOutProgress_ >= 1.0f) {
                isGoOut_ = true;
                SetAlpha(missionText_go_, 0.0f);
            }
        }
    }

    // C. Boss (ボス戦中)
    if (isGoOut_) {
        missionBossAnimProgress_ = std::min(1.0f, missionBossAnimProgress_ + deltaTime * 2.0f);
        if (missionText_boss_) {
            float offsetY = (1.0f - missionBossAnimProgress_) * 20.0f;
            missionText_boss_->SetPosition({ missionBossBasePos_.x, missionBossBasePos_.y + offsetY });
            SetAlpha(missionText_boss_, missionBossAnimProgress_);
        }
    }
}

void GamePlayScene::StartBridgeDropMovie() {
    if (movieState_ != MovieState::kNone || hasBridgeDropped_)
        return;

    movieState_ = MovieState::kBridgeDrop;
    movieTimer_ = 0.0f;
    hasBridgeDropped_ = true;

    // CinematicCamera を探してムービーを再生する
    for (auto& obj : objectManager_->GetObjects()) {
        if (obj->GetName() == "Cinematic_Camera_Bridge") {
            if (obj->recorder_) {
                // Play(fileName, loop, isRelative, isCinematic)
                obj->recorder_->Play("bridge_movie", false, false, true);
            }
            break;
        }
    }
}

// ========================================================
// ★ ボス登場ムービーの開始処理
// ========================================================
void GamePlayScene::StartBossAppearanceMovie() {
    if (isBossMoviePlaying_ || !boss_ || hasBossAppeared_)
        return;

    isBossMoviePlaying_ = true;
    hasBossAppeared_ = true;
    movieTimer_ = 0.0f;

    // ボスコンテナのパーティクルを停止
    GPUParticleManager::GetInstance()->StopAutoEmitter(
        bossContainerTopParticleId_);
    GPUParticleManager::GetInstance()->StopAutoEmitter(
        bossContainerBottomParticleId_);
    bossContainerTopParticleId_ = 0;
    bossContainerBottomParticleId_ = 0;

    // プレイヤーを固定
    if (player_) {
        movieStoredPlayerPos_ = player_->GetWorldPosition();
        player_->SetVelocity({ 0.0f, 0.0f, 0.0f });
        player_->SetIsControlActive(false);
        player_->SetIsPhysicsActive(false);
    }

    // ====================================================
    // ★ 追加：a.json（カメラのアニメーション）を再生する！
    // ====================================================
    for (auto& obj : objectManager_->GetObjects()) {
        if (obj->GetName() ==
            "Cinematic_Camera_Boss") { // ボス用のシネマティックカメラオブジェクトを用意しておく
            if (obj->recorder_) {
                // "a" という名前のJSONを再生！
                obj->recorder_->Play("a", false, false, true);
            }
            break;
        }
    }

    // ボス側にはカメラ移動以外の演出（ブロックが集まる等）だけをやらせる
    boss_->StartAppearance();
}

void GamePlayScene::DrawImGui() {
#ifdef USE_IMGUI
    // ★ Begin/End を削除し、既存の Inspector ウィンドウ内に描画されるようにする
    if (ImGui::CollapsingHeader("Game Debug Controls", ImGuiTreeNodeFlags_DefaultOpen)) {

        if (ImGui::TreeNode("プレイヤー・ボス状態 (Player/Enemy Status)")) {
            if (ImGui::Button("プレイヤー HP -> 0")) {
                if (player_ && player_->param_.has_value()) {
                    player_->param_->hp = 0.0f;
                }
            }

            ImGui::Separator();

            if (ImGui::Button("ボス HP -> 51% (半減演出の直前)")) {
                if (boss_ && boss_->param_.has_value()) {
                    boss_->param_->hp = boss_->param_->maxHp * 0.51f;
                }
            }

            if (ImGui::Button("ボス HP -> 25%")) {
                if (boss_ && boss_->param_.has_value()) {
                    boss_->param_->hp = boss_->param_->maxHp * 0.25f;
                }
            }

            if (ImGui::Button("ボス HP -> 0% (強制爆散)")) {
                if (boss_) {
                    if (boss_->param_.has_value()) {
                        boss_->param_->hp = 0.0f;
                    }
                    boss_->StartDeathSequence();
                }
            }

            ImGui::Separator();

            if (ImGui::Button("ボス スタンゲージ -> 25%")) {
                if (boss_) {
                    boss_->SetBarrierHp(boss_->GetMaxBarrierHp() * 0.25f);
                }
            }

            if (ImGui::Button("ボス スタンゲージ -> 0% (強制ダウン)")) {
                if (boss_) {
                    // 現在のバリアHP分ダメージを与えて強制的にスタン演出をトリガーする
                    boss_->TakeBarrierDamage(boss_->GetBarrierHp(), nullptr);
                }
            }

            ImGui::TreePop();
        }

        if (ImGui::TreeNode("プレイヤーの攻撃力設定 (Player Attack Balance)")) {
            if (player_) {
                PlayerAttackParams& params = player_->GetAttackParams();
                ImGui::DragFloat("コンボ1 ダメージ", &params.damageCombo1, 0.1f, 0.0f, 100.0f);
                ImGui::DragFloat("コンボ2 ダメージ", &params.damageCombo2, 0.1f, 0.0f, 100.0f);
                ImGui::DragFloat("コンボ3 ダメージ", &params.damageCombo3, 0.1f, 0.0f, 200.0f);
                ImGui::DragFloat("落下攻撃 ダメージ", &params.damagePlunge, 0.1f, 0.0f, 100.0f);

                ImGui::Spacing();
                if (ImGui::Button("プレイヤーパラメータを保存")) {
                    player_->SaveAttackParams();
                    DebugConsole::GetInstance()->AddLog("【システム】 プレイヤーの攻撃パラメータをJSONに保存しました。");
                }
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("ボス・エネミーの攻撃力設定 (Boss Attack Balance)")) {
            if (boss_) {
                BossAttackParams& params = boss_->GetAttackParams();
                ImGui::DragFloat("突進攻撃力 (技1)", &params.damageRush, 0.5f, 0.0f, 150.0f);
                ImGui::DragFloat("弾幕攻撃力 (技2)", &params.damageShoot, 0.5f, 0.0f, 150.0f);
                ImGui::DragFloat("ハンマー攻撃力 (技3)", &params.damageHammer, 0.5f, 0.0f, 150.0f);
                ImGui::DragFloat("壁攻撃 (技4)", &params.damageWall, 0.5f, 0.0f, 150.0f);
                ImGui::DragFloat("人型叩きつけ攻撃力 (技5)", &params.damageHumanoid, 0.5f, 0.0f, 150.0f);
                ImGui::DragFloat("極太レーザー攻撃力 (技6)", &params.damageLaser, 0.5f, 0.0f, 250.0f);
                ImGui::DragFloat("ブロック吸収 (技7)", &params.damageAbsorb, 0.5f, 0.0f, 150.0f);
                ImGui::DragFloat("最終奥義メテオ攻撃力 (技8)", &params.damageFinal, 0.5f, 0.0f, 250.0f);
                ImGui::DragFloat("ファンネルレーザー攻撃力 (技9)", &params.damageFunnels, 0.5f, 0.0f, 150.0f);
                ImGui::DragFloat("スライム接触ダメージ", &params.damageSlime, 0.5f, 0.0f, 100.0f);
                ImGui::DragFloat("ボム爆発ダメージ", &params.damageBomb, 0.5f, 0.0f, 150.0f);

                ImGui::Separator();
                ImGui::Text("=== ボス攻撃パターン・確率設定 ===");

                // 攻撃名の定義
                const char* attackNames[] = {
                    "なし (None)",
                    "突進 (技1) [Rush]",
                    "弾幕 (技2) [Shoot]",
                    "ハンマー (技3) [Hammer]",
                    "壁攻撃 (技4) [Wall]",
                    "人型攻撃 (技5) [Humanoid]",
                    "極太レーザー (技6) [Laser]",
                    "ブロック吸収 (技7) [Absorb]",
                    "最終奥義 (技8) [Final]",
                    "ファンネル (技9) [Funnels]",
                    "スポーン (技10) [Spawn]"
                };
                int attackIds[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };

                auto DrawAttackList = [&](const char* label, std::vector<AttackWeight>& attackList) {
                    if (ImGui::TreeNode(label)) {
                        for (size_t i = 0; i < attackList.size(); ++i) {
                            ImGui::PushID(static_cast<int>(i));

                            // 選択中のインデックスを特定
                            int currentAttackIdx = 0;
                            for (int idx = 0; idx < 11; ++idx) {
                                if (attackIds[idx] == attackList[i].id) {
                                    currentAttackIdx = idx;
                                    break;
                                }
                            }

                            ImGui::SetNextItemWidth(200.0f);
                            if (ImGui::Combo("攻撃種類", &currentAttackIdx, attackNames, IM_ARRAYSIZE(attackNames))) {
                                attackList[i].id = attackIds[currentAttackIdx];
                            }

                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(100.0f);
                            ImGui::DragInt("確率", &attackList[i].weight, 1.0f, 0, 1000);

                            ImGui::SameLine();
                            if (ImGui::Button("❌ 削除")) {
                                attackList.erase(attackList.begin() + i);
                                ImGui::PopID();
                                break;
                            }

                            ImGui::PopID();
                        }

                        if (ImGui::Button("➕ 攻撃パターンを追加 (ADD)")) {
                            attackList.push_back({ 1, 30 }); // 突進・重み30をデフォルトで追加
                        }

                        ImGui::TreePop();
                    }
                };

                DrawAttackList("第一形態の攻撃確率 (HP > 50%)", params.phase1Attacks);
                DrawAttackList("第二形態の攻撃確率 (HP <= 50%)", params.phase2Attacks);

                ImGui::Separator();
                ImGui::DragFloat("ボス 最大バリアHP", &params.maxBarrierHp, 1.0f, 10.0f, 1000.0f);
                ImGui::DragFloat("装甲ブロック単体のHP", &params.maxArmorBlockHp, 1.0f, 10.0f, 1000.0f);

                if (ImGui::Button("ボスバリア・全装甲を全回復")) {
                    boss_->FullyRecoverBarrierAndArmor();
                    DebugConsole::GetInstance()->AddLog("【システム】 ボスのバリアと全装甲HPを全回復しました。");
                }

                ImGui::Spacing();
                if (ImGui::Button("ボスパラメータを保存")) {
                    boss_->SaveAttackParams();
                    DebugConsole::GetInstance()->AddLog("【システム】 ボスの攻撃パラメータおよび確率設定をJSONに保存しました。");
                }
            }
            ImGui::TreePop();
        }

        ImGui::Separator();

        if (ImGui::Button("Skip Tutorial", ImVec2(-1, 30))) {
            if (player_) {
                // 1. 速度と座標のリセット
                player_->SetVelocity({ 0.0f, 0.0f, 0.0f });
                player_->GetTransform()->translate = { 0.0f, 1.3f, -68.0f };
                player_->UpdateLocalMatrix();
                player_->UpdateWorldMatrix();

                // 2. カメラを即座にワープ地点へ同期（ラグを防ぐ）
                Camera* camera = CameraManager::GetInstance()->GetMainCamera();
                if (camera) {
                    camera->SetTarget(player_->GetWorldPosition());
                    camera->Update();
                }

                // 3. 各種フラグを「完了」にセット
                hasFinishedTutorial_ = true;
                hasTutorialMovieFinished_ = true; // ★ 追加
                hasBridgeDropped_ = true; // 橋の状態も同期
                GameProgress::GetInstance()->hasFinishedTutorial = true;
                doorOpenProgress_ = 1.0f;
                missionInitialShown_ = true;
                missionGoShown_ = true;

                // 4. チュートリアル関連のオブジェクト削除 ＆ ボスエリアの床を有効化
                for (auto& obj : objectManager_->GetObjects()) {
                    std::string name = obj->GetName();

                    // チュートリアル関係は消す
                    if (name.find("Bridge_") != std::string::npos || name.find("Tutorial_") != std::string::npos) {
                        obj->SetIsVisible(false);
                        obj->isDead = true;
                    }
                    // ボスエリアの地面の当たり判定をONにする
                    else if (name.find("Battle_Field_Collision_Box_") != std::string::npos) {
                        obj->SetCollisionAttribute(kGround);
                    }
                }
                DebugConsole::GetInstance()->AddLog("【DEBUG】 チュートリアルをスキップしました");
            }
        }
    }
#endif
}