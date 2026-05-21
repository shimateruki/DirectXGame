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
    if (models_.contains(modelName)) {
        return models_[modelName].get();
    }

    std::filesystem::path path(modelName);
    std::string parentPath = path.parent_path().string(); // "enemy_core_shards"
    std::string stem = path.stem().string();              // "enemy_core1"
    std::string ext = path.extension().string();

    std::string directoryPath;
    std::string fileName;

    // ==========================================
    // ★ 修正：モデル自身のフォルダ名(stem)もしっかりパスに含める！
    // ==========================================
    if (!parentPath.empty()) {
        // "enemy_core_shards" + "/" + "enemy_core1" + "/"
        directoryPath = kDefaultBaseDirectory + parentPath + "/" + stem + "/";
    }
    else {
        directoryPath = kDefaultBaseDirectory + stem + "/";
    }

    if (!ext.empty()) {
        fileName = path.filename().string();
    }
    else {
        fileName = stem + ".obj";
    }

    std::string fullPath = directoryPath + fileName;
    OutputDebugStringA(("【LoadModel】探索パス: " + fullPath + "\n").c_str());

    auto newModel = std::make_unique<Model>();
    newModel->Initialize(modelCommon_.get(), directoryPath, fileName);

    models_[modelName] = std::move(newModel);
    return models_[modelName].get();
}std::vector<std::string> ModelManager::GetLoadedModelNames() const {
    std::vector<std::string> names;
    for (const auto& pair : models_) {
        names.push_back(pair.first);
    }
    return names;
}

#include <Windows.h> // ★ OutputDebugStringA を使うために追加

// ---------------------------------------------------------
// ★修正版：フォルダ内を自動スキャンして一括ロード（ログ出力付き）
// ---------------------------------------------------------
void ModelManager::LoadAllModels() {
    if (!std::filesystem::exists(kDefaultBaseDirectory)) return;

    OutputDebugStringA("=== 【ModelManager】事前ロード開始 ===\n");

    for (const auto& entry : std::filesystem::recursive_directory_iterator(kDefaultBaseDirectory)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            if (ext == ".obj" || ext == ".gltf" || ext == ".glb") {

                // ファイルの本当の場所をそのまま使う！
                std::string dirPath = entry.path().parent_path().string() + "/";
                std::string fileName = entry.path().filename().string();

                // ==========================================
                // ★ 修正：LoadModelを使わず、相対パスから完璧な「登録キー」を自動生成する
                // ==========================================
                std::filesystem::path relPath = std::filesystem::relative(entry.path(), kDefaultBaseDirectory);
                std::string keyName = relPath.parent_path().string(); // フォルダまでのパスを取得
                std::replace(keyName.begin(), keyName.end(), '\\', '/'); // Windowsの「\」を「/」に統一

                // 登録キー例: "enemy_core_shards/enemy_core1"
                if (!models_.contains(keyName)) {
                    auto newModel = std::make_unique<Model>();
                    newModel->Initialize(modelCommon_.get(), dirPath, fileName);
                    models_[keyName] = std::move(newModel);

                    std::string logMsg = "[事前ロード成功] キー: [" + keyName + "] パス: " + dirPath + fileName + "\n";
                    OutputDebugStringA(logMsg.c_str());
                }
            }
        }
    }
    OutputDebugStringA("=== 【ModelManager】事前ロード終了 ===\n");
}