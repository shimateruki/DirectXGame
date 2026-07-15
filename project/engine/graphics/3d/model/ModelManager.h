#pragma once
#include "Model.h"
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class DirectXCommon;
class ModelCommon;

/// <summary>
/// モデルの読み込み、再読み込み、共有管理を行うシングルトン。
/// </summary>
// ModelManagerは、モデルの読み込み、キャッシュ、名前解決を一元管理します。
class ModelManager {
public:
        // 共通利用するモデル管理インスタンスを取得します。
static ModelManager* GetInstance();

    /// <summary>
    /// モデル共通情報を初期化する。
    /// </summary>
    void Initialize(DirectXCommon* dxCommon);

    /// <summary>
    /// 読み込み済みモデルと共通リソースを解放する。
    /// </summary>
    void Finalize();

    /// <summary>
    /// モデル名から読み込み済みモデルを取得し、未読み込みなら読み込む。
    /// </summary>
    Model* LoadModel(const std::string& modelName);

    /// <summary>
    /// 指定モデルを再読み込みする。
    /// </summary>
    bool ReloadModel(const std::string& modelName);

    /// <summary>
    /// 読み込み済みのモデル名一覧を取得する。
    /// </summary>
    std::vector<std::string> GetLoadedModelNames() const;

    /// <summary>
    /// 既定のモデルディレクトリにあるモデルを一括読み込みする。
    /// </summary>
    void LoadAllModels();

    ModelCommon* GetModelCommon() const { return modelCommon_.get(); }

private:
    ModelManager() = default;
    ~ModelManager() = default;
    ModelManager(const ModelManager&) = delete;
    ModelManager& operator=(const ModelManager&) = delete;

    /// <summary>
    /// モデル名からディレクトリパスとファイル名を解決する。
    /// </summary>
    bool ResolveModelPath(const std::string& modelName, std::string& directoryPath, std::string& fileName) const;

private:
    static ModelManager* instance;

    // モデル描画共通情報と読み込み済みモデルのキャッシュ。
    std::unique_ptr<ModelCommon> modelCommon_;
    std::map<std::string, std::unique_ptr<Model>> models_;
    mutable std::recursive_mutex mutex_;

    // 既定のモデル検索パスと拡張子。
    static const std::string kDefaultBaseDirectory;
    static const std::string kDefaultModelExtension;
};
