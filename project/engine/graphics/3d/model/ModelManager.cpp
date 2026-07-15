#include "ModelManager.h"
#include "ModelCommon.h"
#include <cassert>
#include <filesystem> 
#include <algorithm>
#include "ProfilerManager.h"
#include <chrono>


// 静的メンバ変数の実体定義
// シングルトンインスタンスと既定のモデル検索設定を保持する。
ModelManager* ModelManager::instance = nullptr;
const std::string ModelManager::kDefaultBaseDirectory = "Resources/3DModel/";
const std::string ModelManager::kDefaultModelExtension = ".obj";
// ModelManagerのシングルトンインスタンスを生成または取得する。

ModelManager* ModelManager::GetInstance() {
    if (instance == nullptr) {
        instance = new ModelManager();
    }
    return instance;
}
// モデル共通描画基盤を初期化し、各Modelが共有するDirectXリソースを準備する。

void ModelManager::Initialize(DirectXCommon* dxCommon) {
    assert(dxCommon);
    modelCommon_ = std::make_unique<ModelCommon>();
    modelCommon_->Initialize(dxCommon);
}
// 読み込み済みモデルと共通リソースを解放し、シングルトンを破棄する。

void ModelManager::Finalize() {
    models_.clear(); // 読み込み済みモデルをすべて解放する。
    modelCommon_.reset(); // ModelCommonを先に破棄し、GPU関連の参照を残さない。
    delete instance;
    instance = nullptr;
}
// モデル名から実ファイルを解決して読み込み、同じキーのモデルは再利用する。


Model* ModelManager::LoadModel(const std::string& modelName) {
    // すでに同じキーで読み込まれている場合は、GPUリソースを作り直さず再利用する。
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (models_.contains(modelName)) {
            return models_[modelName].get();
        }
    }

    std::filesystem::path path(modelName);
    std::string parentPath = path.parent_path().string();
    std::replace(parentPath.begin(), parentPath.end(), '\\', '/');
    std::string stem = path.stem().string();
    std::string ext = path.extension().string();

    // 事前ロードは拡張子なしのキーで登録されるため、gltf指定でも同じモデルを再利用する
    // 拡張子付きで指定された場合でも、拡張子なしキーやフォルダキーの既存登録を優先して探す。
    if (!ext.empty()) {
        std::string extensionlessKey = parentPath.empty() ? stem : parentPath + "/" + stem;
        std::replace(extensionlessKey.begin(), extensionlessKey.end(), '\\', '/');
        const bool isFolderMainModel = !parentPath.empty() &&
            std::filesystem::path(parentPath).filename().string() == stem;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            if (models_.contains(extensionlessKey)) {
                return models_[extensionlessKey].get();
            }
            if (isFolderMainModel && models_.contains(parentPath)) {
                return models_[parentPath].get();
            }
        }
    }

    std::string directoryPath;
    std::string fileName;

    // ==========================================
    // ★ 修正：モデル自身のフォルダ名(stem)もしっかりパスに含める！
    // ==========================================
    // サブフォルダ指定がある場合は、モデル自身と同名のフォルダ配下をまず候補にする。
    if (!parentPath.empty()) {
        // "enemy_core_shards" + "/" + "enemy_core1" + "/"
        directoryPath = kDefaultBaseDirectory + parentPath + "/" + stem + "/";
    }
    else {
        directoryPath = kDefaultBaseDirectory + stem + "/";
    }

    // 拡張子付きで指定された場合でも、拡張子なしキーやフォルダキーの既存登録を優先して探す。
    if (!ext.empty()) {
        fileName = path.filename().string();
        std::string legacyDirectory = directoryPath;
        std::string directDirectory = kDefaultBaseDirectory + (parentPath.empty() ? "" : parentPath + "/");
        // 旧配置と直下配置の両方を見て、既存アセットの読み込み互換性を保つ。
        if (std::filesystem::exists(legacyDirectory + fileName)) {
            directoryPath = legacyDirectory;
        }
        else if (std::filesystem::exists(directDirectory + fileName)) {
            directoryPath = directDirectory;
        }
    }
    else {
        const std::string objName = stem + ".obj";
        const std::string gltfName = stem + ".gltf";
        const std::string glbName = stem + ".glb";

        // 拡張子なし指定では、OBJ/GLTF/GLBの順に実在する代表ファイルを探す。
        if (std::filesystem::exists(directoryPath + objName)) {
            fileName = objName;
        }
        else if (std::filesystem::exists(directoryPath + gltfName)) {
            fileName = gltfName;
        }
        else if (std::filesystem::exists(directoryPath + glbName)) {
            fileName = glbName;
        }
        else if (std::filesystem::exists(directoryPath)) {
            for (const auto& entry : std::filesystem::directory_iterator(directoryPath)) {
                if (!entry.is_regular_file()) continue;
                std::string candidateExt = entry.path().extension().string();
                if (candidateExt == ".obj" || candidateExt == ".gltf" || candidateExt == ".glb") {
                    fileName = entry.path().filename().string();
                    break;
                }
            }
            if (fileName.empty()) {
                fileName = objName;
            }
        }
        else {
            fileName = objName;
        }
    }

    std::string fullPath = directoryPath + fileName;
    OutputDebugStringA(("【LoadModel】探索パス: " + fullPath + "\n").c_str());

    auto start = std::chrono::high_resolution_clock::now();

    auto newModel = std::make_unique<Model>();
    ModelCommon* modelCommon = nullptr;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        modelCommon = modelCommon_.get();
    }
    newModel->Initialize(modelCommon, directoryPath, fileName);

    auto end = std::chrono::high_resolution_clock::now();
    float duration = std::chrono::duration<float, std::milli>(end - start).count();
    ProfilerManager::GetInstance()->RecordLoadTime("Model", modelName, duration);

    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto existingIt = models_.find(modelName);
        if (existingIt != models_.end()) {
            return existingIt->second.get();
        }

        auto inserted = models_.emplace(modelName, std::move(newModel));
        return inserted.first->second.get();
    }
}
// 指定モデルをキャッシュから外して再読み込みし、ファイル更新を反映する。

bool ModelManager::ReloadModel(const std::string& modelName) {
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        models_.erase(modelName);
    }
    return LoadModel(modelName) != nullptr;
}
// 現在読み込み済みのモデルキー一覧を返し、エディタやデバッグ表示で使えるようにする。

std::vector<std::string> ModelManager::GetLoadedModelNames() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    std::vector<std::string> names;
    for (const auto& pair : models_) {
        names.push_back(pair.first);
    }
    return names;
}

#include <Windows.h> // OutputDebugStringAで読み込みログを出力するために使用。

// ---------------------------------------------------------
// ★修正版：フォルダ内を自動スキャンして一括ロード（ログ出力付き）
// ---------------------------------------------------------
// Resources/3DModel配下を走査し、見つかったモデルファイルを事前にまとめて読み込む。
void ModelManager::LoadAllModels() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

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
                // 事前ロード時はモデルファイル名ではなく、モデルフォルダまでの相対パスを登録キーにする。
                std::filesystem::path relPath = std::filesystem::relative(entry.path(), kDefaultBaseDirectory);
                std::string keyName = relPath.parent_path().string(); // フォルダまでのパスを取得
                std::replace(keyName.begin(), keyName.end(), '\\', '/'); // Windowsの「\」を「/」に統一

                // 登録キー例: "enemy_core_shards/enemy_core1"
                // 同じフォルダキーを二重登録しないようにし、最初に見つかった代表モデルだけを読み込む。
                if (!models_.contains(keyName)) {
                    auto start = std::chrono::high_resolution_clock::now();

                    auto newModel = std::make_unique<Model>();
                    newModel->Initialize(modelCommon_.get(), dirPath, fileName);
                    models_[keyName] = std::move(newModel);

                    auto end = std::chrono::high_resolution_clock::now();
                    float duration = std::chrono::duration<float, std::milli>(end - start).count();
                    ProfilerManager::GetInstance()->RecordLoadTime("Model", keyName, duration);

                    std::string logMsg = "[事前ロード成功] キー: [" + keyName + "] パス: " + dirPath + fileName + "\n";
                    OutputDebugStringA(logMsg.c_str());
                }
            }
        }
    }
    OutputDebugStringA("=== 【ModelManager】事前ロード終了 ===\n");
}
