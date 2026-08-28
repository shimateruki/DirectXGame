#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

// 前方宣言
class BaseScene;
class Object3d;

// JSON に保存されたステージ配置データを、シーン上のオブジェクトへ復元するローダー
class LevelLoader {
public:
    /// <summary>
    /// 3D オブジェクト配置データを読み込む。
    /// </summary>
    void LoadObjectLayout(
        BaseScene* scene,
        const std::string& filename,
        bool resolveSceneAssetPath = true);

    /// <summary>
    /// スプライト配置データを読み込む。
    /// </summary>
    void LoadSpriteLayout(BaseScene* scene, const std::string& filename);

    // Replayから敵を再生成した後、Player参照と派生敵のSpawn callbackを接続し直します。
    static void ConfigureEnemyRuntimeReferences(BaseScene* scene);

private:
    void LoadSingleJson(BaseScene* scene, const std::string& filename);

private:
    struct PendingParentReference {
        std::string guid;
        std::string legacyName;
    };

    // 子オブジェクト読み込み後に親子関係を解決するための待機リスト。
    std::map<Object3d*, PendingParentReference> parentPendingList_;
};
