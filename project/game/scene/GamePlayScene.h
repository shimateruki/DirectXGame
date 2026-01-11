#pragma once
#include "BaseScene.h" 
#include "Object3dCommon.h"
#include "SpriteCommon.h"
#include "Object3d.h"
#include "Sprite.h"
#include "AudioPlayer.h"
#include "ParticleSystem.h" 
#include "ParticleCommon.h" 
#include"SpriteDebugEditor.h"
#include"Player.h"
#include"Text.h"
#include "Event.h"
#include "BulletManager.h"
#include"Camera.h"

#include <memory>
#include <vector>


// --- 前方宣言 (ポインタで持つものだけ) ---
class DirectXCommon;
class InputManager;
class SceneManager;

#include "DebugEditor.h" 
#include <GhostRecorder.h>
#include <GameRule.h>


/// <summary>
/// ゲームプレイシーン
/// </summary>
class GamePlayScene : public BaseScene {
public:

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize() override;

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize() override;

    /// <summary>
    /// 更新
    /// </summary>
    void Update(float deltaTime) override;

    /// <summary>
    /// 描画
    /// </summary>
    void Draw() override;

    //Edotor用
    std::vector<std::unique_ptr<Object3d>>& GetObjects() override { return objects_; }
    std::vector<std::unique_ptr<Sprite>>& GetSprites() override { return sprites_; }
    void AddObject(std::unique_ptr<Object3d> object) override;
    Object3dCommon* GetObject3dCommon() override { return object3dCommon_.get(); }
    SpriteCommon* GetSpriteCommon() override { return spriteCommon_.get(); }
    ParticleSystem* GetParticleSystem() override { return particleSystem_.get(); }
    void RequestRemoveObject(Object3d* object) override;

private:
    // --- オブジェクトレイアウト読み込み関数 ---
    void LoadObjectLayout(const std::string& filename);
    void LoadSpriteLayout(const std::string& filename);
 
    /// <summary>
    /// PlayerHitEvent を受け取ったときに呼ばれるコールバック関数
    /// </summary>
    void OnPlayerHit(const PlayerHitEvent& event);

    void OnBulletHit(const BulletHitEvent& event);
    void SwitchActivePlayer(Player* newMainPlayer);

private:

    // --- エンジンシステムへのポインタ ---
    DirectXCommon* dxCommon_ = nullptr;
    InputManager* inputManager_ = nullptr;
    AudioPlayer* audioPlayer_ = nullptr;


    // --- ゲームオブジェクト ---
    std::unique_ptr<Object3dCommon> object3dCommon_ = nullptr;
    std::unique_ptr<SpriteCommon> spriteCommon_ = nullptr;
    std::unique_ptr<ParticleCommon> particleCommon_ = nullptr;
    std::vector<std::unique_ptr<Object3d>> objects_;
    std::vector<std::unique_ptr<Sprite>> sprites_;
    std::unique_ptr<ParticleSystem> particleSystem_ = nullptr;
    std::unique_ptr<Text>  debugText_;
    std::unique_ptr<GameRule> gameRule_; // 管理人


    Player* player_ = nullptr;

    // --- BGM・SE ---
    uint32_t bgmHandle_ = 0;
    bool isBGMPlaying_ = false;
    uint32_t particleSEHandle_ = 0;

    // --- ImGui用フラグ ---
    bool isDrawParticles_ = false;

    //全体ライト(太陽の光)
    Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource_;
    Object3d::PointLight* pointLightData_ = nullptr;
   
    // スポットライト
    Microsoft::WRL::ComPtr<ID3D12Resource> spotLightResource_;
    Object3d::SpotLight* spotLightData_ = nullptr;

    /// <summary>
    /// 削除予約されたオブジェクトのリスト
    /// </summary>
    std::vector<Object3d*> removalList_;

    /// <summary>
    /// 予約されたオブジェクトを安全に削除する
    /// </summary>
    void ProcessRemovals();

    Object3d* lockOnTarget_ = nullptr; // 現在ロックオンしている敵
    bool isLockingOn_ = false;           // ロックオン中フラグ

    /// <summary>
    /// ロックオン処理（入力、対象検索、状態更新）
    /// </summary>
    void UpdateLockOn();

    std::unique_ptr<Object3d> CreateStaticBlock(const Vector3& position, const std::string& name, const Vector3& collisionHalfSize);

    /// <summary>
    /// ロックオン対象として最適な敵を探す
    /// </summary>
    Object3d* FindBestLockOnTarget(Camera* camera);

    /// <summary>
    /// シーン内の敵リストを取得する
    /// </summary>
    std::vector<Object3d*> FindEnemies();

    Math* math_;

};