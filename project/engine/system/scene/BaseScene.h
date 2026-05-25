#pragma once
#include <vector>
#include <memory>
#include <string>

// 継承先でも頻繁に使うためインクルード
#include "Object3d.h"
#include "Sprite.h"

// 前方宣言
class SceneManager;
class Object3dCommon;
class SpriteCommon;
class ParticleSystem;
class DebugEditor;
class Player; // ★追加: LevelLoader対応

#include "engine/utility/state/IEditable.h"

/// <summary>
/// シーンの基底クラス
/// </summary>
class BaseScene : public IEditable {
public:
    virtual ~BaseScene() = default;

    // --- IEditableの実装 ---
    virtual std::string GetName() override { return "Scene Settings"; }
    virtual void DrawImGui() override {}

    // --- 必須オーバーライド関数 ---
    virtual void Initialize() = 0;
    virtual void Update(float deltaTime) = 0;
    virtual void Draw() = 0;
    virtual void Finalize() = 0;

    // --- マネージャ設定 ---
    virtual void SetSceneManager(SceneManager* sceneManager) {
        sceneManager_ = sceneManager;
    }
    virtual void SetDebugEditor(DebugEditor* editor) {
        debugEditor_ = editor;
    }

    // --- オブジェクト管理 (LevelLoader / Editor用) ---

    // オブジェクトリスト取得 (デフォルト実装: 空リストを返す)
    virtual std::vector<std::unique_ptr<Object3d>>& GetObjects() {
        static std::vector<std::unique_ptr<Object3d>> empty;
        return empty;
    }
    // スプライトリスト取得
    virtual std::vector<std::unique_ptr<Sprite>>& GetSprites() {
        static std::vector<std::unique_ptr<Sprite>> empty;
        return empty;
    }

    // オブジェクト追加 (デフォルト実装: 何もしない)
    virtual void AddObject(std::unique_ptr<Object3d> object) { (void)object; }

    // 削除予約
    virtual void RequestRemoveObject(Object3d* object) { (void)object; }


    // --- 共通リソース取得 ---
    virtual Object3dCommon* GetObject3dCommon() { return nullptr; }
    virtual SpriteCommon* GetSpriteCommon() { return nullptr; }
    virtual ParticleSystem* GetParticleSystem() { return nullptr; }


    // --- Player連携 (LevelLoader対応) ---
    virtual Player* GetPlayer() const { return nullptr; }
    virtual void SetPlayer(Player* player) { (void)player; }


    // --- イベント関連 ---
    // 実装はBaseScene.cppで行う想定（GetObjects()を使って検索するため）
    void TriggerEvent(int targetID);
    void SetEventActive(int targetID, bool active);
    virtual Object3d* FindObjectByEventID(int eventID);
    virtual void DrawUI() {}

    void SetLoadedFilename(const std::string& name) { loadedFilename_ = name; }
    std::string GetLoadedFilename() const { return loadedFilename_; }
    virtual void DrawShadow() {}
    Sprite* GetSpriteByName(const std::string& name);
    // ==================================================
    void SetLoadedSpriteFilename(const std::string& name) { loadedSpriteFilename_ = name; }
    std::string GetLoadedSpriteFilename() const { return loadedSpriteFilename_; }
protected:
    SceneManager* sceneManager_ = nullptr;
    DebugEditor* debugEditor_ = nullptr;
    std::string loadedFilename_ = "scene_layout.json";
    std::string loadedSpriteFilename_ = "sprite_layout.json";
};
