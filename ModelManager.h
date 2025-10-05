#pragma once

#include "Model.h"
#include <string>
#include <map>
#include <memory>

class DirectXCommon;
class ModelCommon;

// モデルデータを管理するクラス（シングルトン）
class ModelManager {
public:
    /// <summary>
    /// シングルトンインスタンスの取得
    /// </summary>
    static ModelManager* GetInstance();

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize(DirectXCommon* dxCommon);

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize();

    /// <summary>
    /// モデルの読み込み
    /// </summary>
    /// <param name="filePath">モデルのファイルパス</param>
    void LoadModel(const std::string& filePath);

    /// <summary>
    /// モデルデータを取得
    /// </summary>
    /// <param name="filePath">モデルのファイルパス</param>
    /// <returns>見つかったモデルデータ。見つからなければnullptr</returns>
    Model* FindModel(const std::string& filePath);

private:
    ModelManager() = default;
    ~ModelManager() = default;
    ModelManager(const ModelManager&) = delete;
    ModelManager& operator=(const ModelManager&) = delete;

private:
    static ModelManager* instance;

    std::unique_ptr<ModelCommon> modelCommon_;
    std::map<std::string, std::unique_ptr<Model>> models_;
};