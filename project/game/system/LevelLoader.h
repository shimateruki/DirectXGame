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
    /// 分割 JSON が一つでもあればカテゴリ別ファイルを優先して読み込む。
    /// 生成後に GUID を優先して親子関係を結び、敵の Player 参照を設定する。
    /// </summary>
    void LoadObjectLayout(BaseScene* scene, const std::string& filename, bool resolveSceneAssetPath = true);

    /// <summary>
    /// スプライト配置データを読み込む。Scene Asset の参照先へ解決してから配置する。
    /// </summary>
    void LoadSpriteLayout(BaseScene* scene, const std::string& filename);

    // Replay から敵を再生成した後、Player 参照と派生敵の Spawn callback を接続し直す。
    static void ConfigureEnemyRuntimeReferences(BaseScene* scene);

  private:
    void LoadSingleJson(BaseScene* scene, const std::string& filename);

  private:
    // 全 Object 生成後に親を解決する識別情報。GUID と旧形式の名前参照を保持する。
    struct PendingParentReference {
        std::string guid;
        std::string legacyName;
    };

    // 子オブジェクト読み込み後に親子関係を解決するための待機リスト。
    std::map<Object3d*, PendingParentReference> parentPendingList_;
};
