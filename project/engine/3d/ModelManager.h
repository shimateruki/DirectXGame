#pragma once
#include "Model.h"
#include <string>
#include <map>
#include <memory>

class DirectXCommon;
class ModelCommon;

class ModelManager {
public:
    static ModelManager* GetInstance();
    void Initialize(DirectXCommon* dxCommon);
    void Finalize();

    // ★★★ モデル名で「探して、なければ読み込む」賢い関数に ★★★
    Model* LoadModel(const std::string& modelName);

private:
    ModelManager() = default;
    ~ModelManager() = default;
    ModelManager(const ModelManager&) = delete;
    ModelManager& operator=(const ModelManager&) = delete;

private:
    static ModelManager* instance;

    // ★★★ ModelCommonはManagerが一元管理する ★★★
    std::unique_ptr<ModelCommon> modelCommon_;
    std::map<std::string, std::unique_ptr<Model>> models_;

    // ★★★ デフォルトのパスと拡張子を定数として定義 ★★★
    static const std::string kDefaultBaseDirectory;
    static const std::string kDefaultModelExtension;
};