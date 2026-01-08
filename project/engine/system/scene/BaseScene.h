#pragma once
#include <vector>
#include <memory>
#include"Object3d.h"
#include"Sprite.h"

class SceneManager; 
class Object3dCommon;
class SpriteCommon;
class ParticleSystem;
class DebugEditor;
/// <summary>
/// シーンの基底クラス
/// </summary>
class BaseScene {
public:
    /// <summary>
    /// デストラクタ
    /// </summary>
    virtual ~BaseScene() = default;

    /// <summary>
    /// 初期化
    /// </summary>
    virtual void Initialize() = 0;

    /// <summary>
    /// 更新
    /// </summary>
    virtual void Update(float deltaTime) = 0;

    /// <summary>
    /// 描画
    /// </summary>
    virtual void Draw() = 0;

    virtual void Finalize() = 0;

    /// <summary>
    /// シーンマネージャのポインタを設定する（仮想関数）
    /// </summary>
    virtual void SetSceneManager(SceneManager* sceneManager) {
        sceneManager_ = sceneManager; // ポインタをメンバ変数に保持
    }


    // (Editor用) シーンが持つ Object3d のリストを取得
    virtual std::vector<std::unique_ptr<Object3d>>& GetObjects() {
        static std::vector<std::unique_ptr<Object3d>> empty;
        return empty;
    }
    // (Editor用) シーンが持つ Sprite のリストを取得
    virtual std::vector<std::unique_ptr<Sprite>>& GetSprites() {
        static std::vector<std::unique_ptr<Sprite>> empty;
        return empty;
    }
    // (Spawner用) シーンに Object3d を追加
    virtual void AddObject(std::unique_ptr<Object3d> object) { (void)object; }

    // (Spawner用) 3Dオブジェクトの共通基盤を取得
    virtual Object3dCommon* GetObject3dCommon() { return nullptr; }
    // (Gizmo用) 2Dスプライトの共通基盤を取得
    virtual SpriteCommon* GetSpriteCommon() { return nullptr; }
    /// <summary>
    /// このシーンの ParticleSystem を取得する (仮想)
    /// </summary>
    virtual ParticleSystem* GetParticleSystem() { return nullptr; }

    /// <summary>
      /// オブジェクトの削除を予約する (純粋仮想)
      /// </summary>
    virtual void RequestRemoveObject(Object3d* object) = 0;

    void TriggerEvent(int targetID);
    virtual Object3d* FindObjectByEventID(int eventID);

    void SetDebugEditor(DebugEditor* editor) {
        debugEditor_ = editor;
    }
protected:
    SceneManager* sceneManager_ = nullptr;
    DebugEditor* debugEditor_ = nullptr;
};