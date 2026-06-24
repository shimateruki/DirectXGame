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
    void LoadObjectLayout(BaseScene* scene, const std::string& filename);

    /// <summary>
    /// スプライト配置データを読み込む。
    /// </summary>
    void LoadSpriteLayout(BaseScene* scene, const std::string& filename);

private:
    void LoadSingleJson(BaseScene* scene, const std::string& filename);

private:
    // 子オブジェクト読み込み後に親子関係を解決するための待機リスト。
    std::map<Object3d*, std::string> parentPendingList_;
};
