#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "json.hpp"
#include "RuntimePresetCatalog.h"

// 前方宣言
class BaseScene;
namespace engine { class Object3d; }

// JSON に保存されたステージ配置データを、シーン上のオブジェクトへ復元するローダー
class LevelLoader {
  public:
    /// <summary>
    /// 分割 JSON が一つでもあればカテゴリ別ファイルを優先して読み込む。
    /// 生成後に GUID を優先して親子関係を結び、敵の Player 参照を設定する。
    /// </summary>
    void LoadObjectLayout(BaseScene* scene, const std::string& filename, bool resolveSceneAssetPath = true);

    /// <summary>ゲームシーンの配置を数フレームに分けて復元するための開始処理。</summary>
    void BeginObjectLayoutLoad(BaseScene* scene, const std::string& filename, bool resolveSceneAssetPath = true);

    /// <summary>最大 maxObjects 件を生成する。全ファイルの生成と参照解決が終わると true。</summary>
    bool ContinueObjectLayoutLoad(BaseScene* scene, size_t maxObjects);

    /// <summary>現在のファイルと生成済み件数から、配置復元の概算進捗を返す。</summary>
    float GetObjectLayoutLoadProgress() const;

    /// <summary>
    /// スプライト配置データを読み込む。Scene Asset の参照先へ解決してから配置する。
    /// </summary>
    void LoadSpriteLayout(BaseScene* scene, const std::string& filename);

    /// <summary>Replay復元後の敵へ、Player参照と派生敵の生成Callbackを接続し直す。</summary>
    static void ConfigureEnemyRuntimeReferences(BaseScene* scene);

  private:
    /// <summary>現在の配置JSONから残り件数内のObjectを生成する。</summary>
    /// <returns>ファイルの読み取りとObject生成を続行できた場合はtrue。</returns>
    bool LoadSingleJson(BaseScene* scene, const std::string& filename, size_t& remainingObjects);

  private:
    // 全 Object 生成後に親を解決する識別情報。GUID と旧形式の名前参照を保持する。
    struct PendingParentReference {
        std::string guid;
        std::string legacyName;
    };

    // 子オブジェクト読み込み後に親子関係を解決するための待機リスト。
    std::map<engine::Object3d*, PendingParentReference> parentPendingList_;
    std::vector<std::string> pendingObjectFiles_;
    size_t pendingFileIndex_ = 0;
    size_t pendingObjectIndex_ = 0;
    nlohmann::json pendingObjects_ = nlohmann::json::array();
    bool pendingFileLoaded_ = false;
    bool objectLayoutLoading_ = false;
    int preparationStep_ = 0;
    RuntimePresetCatalog runtimePresets_;
};
