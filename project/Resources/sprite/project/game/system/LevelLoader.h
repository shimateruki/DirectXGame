#pragma once
#include <string>
#include <vector>
#include <memory>
#include <map>

// 前方宣言
class BaseScene;
class Object3d;

class LevelLoader {
public:
    /// <summary>
    /// オブジェクト配置データを読み込む
    /// </summary>
    void LoadObjectLayout(BaseScene* scene, const std::string& filename);

    /// <summary>
    /// スプライト配置データを読み込む
    /// </summary>
    void LoadSpriteLayout(BaseScene* scene, const std::string& filename);


private:
    void LoadSingleJson(BaseScene* scene, const std::string& filename);
private:
    // 親子関係解決用リスト (子オブジェクト -> 親の名前)
    std::map<Object3d*, std::string> parentPendingList_;

};