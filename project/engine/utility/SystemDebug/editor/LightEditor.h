#pragma once
#include <string>
#include <vector>   
#include <memory>   
#include "LightManager.h"
#include "Object3d.h"

class Model;

class LightEditor {
public:
    // シングルトンインスタンス取得
    static LightEditor* GetInstance();

    // 初期化 (ImGui周りなど)
    void Initialize();

    //  シーンが変わるたびに「このシーンのObject3dCommonを使ってね」と伝える関数
    void SetObject3dCommon(Object3dCommon* common);
    // 更新処理
    void Update();

    // 3D描画 
    void Draw3D();

    // ImGui描画 (毎フレーム呼ぶ)
    void DrawImGui();

private:
    LightManager* lightManager_ = nullptr;
    char currentFileName_[128] = "light_layout.json";

    //  可視化機能用 
    // 可視化を表示するかどうかのフラグ
    bool isVisibleGizmos_ = true;

    // Object3d生成に必要な共通データ
    Object3dCommon* common_ = nullptr;

    // ライトの数だけ用意するObject3dのリスト
    std::vector<std::unique_ptr<Object3d>> pointLightGizmos_;
    std::vector<std::unique_ptr<Object3d>> spotLightGizmos_;
};