#include "ModelManager.h"
#include "ModelCommon.h"
#include <cassert>
#include <filesystem> 


// 静的メンバ変数の実体定義
ModelManager* ModelManager::instance = nullptr;
const std::string ModelManager::kDefaultBaseDirectory = "Resources/3DModel/";
const std::string ModelManager::kDefaultModelExtension = ".obj";

ModelManager* ModelManager::GetInstance() {
    if (instance == nullptr) {
        instance = new ModelManager();
    }
    return instance;
}

void ModelManager::Initialize(DirectXCommon* dxCommon) {
    assert(dxCommon);
    modelCommon_ = std::make_unique<ModelCommon>();
    modelCommon_->Initialize(dxCommon);
}

void ModelManager::Finalize() {
    models_.clear(); // モデルを全て解放
    modelCommon_.reset(); // ModelCommonを解放
    delete instance;
    instance = nullptr;
}



Model* ModelManager::LoadModel(const std::string& modelName) {
    // 1. 既に読み込んでいるかチェック
    if (models_.contains(modelName)) {
        return models_[modelName].get();
    }

    std::string directoryPath;
    std::string fileName;

    // 2. 拡張子があるかチェック (.obj, .gltf, .glb)
    if (modelName.find(".obj") != std::string::npos ||
        modelName.find(".gltf") != std::string::npos ||
        modelName.find(".glb") != std::string::npos) {


        // 拡張子抜きの名前（フォルダ名）を取得
        std::string folderName = std::filesystem::path(modelName).stem().string();

        // ディレクトリパス: "resources/" + "sampleBlock" + "/"
        directoryPath = kDefaultBaseDirectory + folderName + "/";
        fileName = modelName;
    } else {
        // Bパターン: 拡張子がない場合 (既存のOBJ互換用)
        directoryPath = kDefaultBaseDirectory + modelName + "/";
        fileName = modelName + ".obj";
    }

    // 3. 読み込み実行
    auto newModel = std::make_unique<Model>();
    newModel->Initialize(modelCommon_.get(), directoryPath, fileName);

    // 4. 登録
    models_[modelName] = std::move(newModel);
    return models_[modelName].get();
}

std::vector<std::string> ModelManager::GetLoadedModelNames() const {
    std::vector<std::string> names;
    for (const auto& pair : models_) {
        names.push_back(pair.first);
    }
    return names;
}


// ---------------------------------------------------------
// ★修正版：フォルダ内を自動スキャンして一括ロード
// ---------------------------------------------------------
void ModelManager::LoadAllModels() {
    if (!std::filesystem::exists(kDefaultBaseDirectory)) {
        return; // フォルダが存在しなければ終了
    }

    // ★修正点: recursive_directory_iterator を使ってサブフォルダの中の「ファイル」まで直接探す
    for (const auto& entry : std::filesystem::recursive_directory_iterator(kDefaultBaseDirectory)) {

        // フォルダではなく「ファイル」だった場合のみ処理する
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();

            // パターンA: .obj ファイルの場合
            if (ext == ".obj") {
                // これまでの書き方に合わせて、拡張子なしの名前でロード (例: "player")
                std::string stemName = entry.path().stem().string();
                LoadModel(stemName);
            }
            // パターンB: .gltf, .glb ファイルの場合
            else if (ext == ".gltf" || ext == ".glb") {
                // 拡張子付きの名前でロード (例: "sampleBlock.gltf")
                std::string fileName = entry.path().filename().string();
                LoadModel(fileName);
            }
            // .png や .mtl や .bin などは自動的に無視されるので安全！
        }
    }
}