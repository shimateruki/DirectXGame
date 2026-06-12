#pragma once
#include "Model.h"
#include <string>
#include <memory>
#include <vector> 
#include <map>    

class DirectXCommon;
class ModelCommon;

class ModelManager {
public:
    static ModelManager* GetInstance();
    void Initialize(DirectXCommon* dxCommon);
    void Finalize();

    // モデル名で「探して、なければ読み込む」賢い関数に
    Model* LoadModel(const std::string& modelName);
    bool ReloadModel(const std::string& modelName);
    /// <summary>
    /// ロード済みのすべてのモデル名を取得する
    /// </summary>
    std::vector<std::string> GetLoadedModelNames() const;
    void LoadAllModels();
    ModelCommon* GetModelCommon() const { return modelCommon_.get(); }
private:
    ModelManager() = default;
    ~ModelManager() = default;
    ModelManager(const ModelManager&) = delete;
    ModelManager& operator=(const ModelManager&) = delete;
    bool ResolveModelPath(const std::string& modelName, std::string& directoryPath, std::string& fileName) const;

private:
    static ModelManager* instance;

    //ModelCommonはManagerが一元管理する
    std::unique_ptr<ModelCommon> modelCommon_;
    std::map<std::string, std::unique_ptr<Model>> models_;

    // デフォルトのパスと拡張子を定数として定義
    static const std::string kDefaultBaseDirectory;
    static const std::string kDefaultModelExtension;
};
