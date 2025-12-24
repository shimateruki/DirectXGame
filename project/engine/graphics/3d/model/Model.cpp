
#include <fstream>
#include "Model.h"
#include "DirectXCommon.h"
#include "engine/utility/math/Math.h"
#include <sstream>
#include <cassert>


// モデルの初期化処理
void Model::Initialize(ModelCommon* common, const std::string& directoryPath, const std::string& filename) {
    // NULLチェック
    assert(common);
    common_ = common;
    DirectXCommon* dxCommon = common_->GetDxCommon();

    // モデルデータ(.objファイル)の読み込み
    modelData_ = LoadObjFile(directoryPath, filename);
    // 読み込んだモデルデータに紐づくテクスチャをロードし、ハンドルを保存
    modelData_.material.textureHandle = TextureManager::GetInstance()->Load(modelData_.material.textureFilePath);

    // --- 頂点バッファの作成 ---
    vertexResource_ = dxCommon->CreateBufferResource(sizeof(VertexData) * modelData_.vertices.size());
    // 頂点バッファビュー(VBV)の設定
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = UINT(sizeof(VertexData) * modelData_.vertices.size());
    vertexBufferView_.StrideInBytes = sizeof(VertexData);

    // --- 頂点データをリソースに書き込む ---
    VertexData* vertexData = nullptr;
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
    std::memcpy(vertexData, modelData_.vertices.data(), sizeof(VertexData) * modelData_.vertices.size());
    vertexResource_->Unmap(0, nullptr);

    // --- マテリアル用定数バッファ(CBV)の作成 ---
    materialResource_ = dxCommon->CreateBufferResource(sizeof(Material));
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
    materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    materialData_->enableLighting = true;
    materialData_->selectedLighting = 2;
    materialData_->shininess = 50;
    Math math;
    materialData_->uvTransform = math.makeIdentity4x4();
}

// モデルの描画処理
void Model::Draw(ID3D12Resource* wvpResource, ID3D12Resource* directionalLightResource, ID3D12Resource* cameraResource, ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource) {
    // common_経由でコマンドリストを取得
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();

    // 頂点バッファをIAステージに設定
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);

    // ルートシグネチャに各定数バッファを設定
    commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());
    if (cameraResource) {
        commandList->SetGraphicsRootConstantBufferView(4, cameraResource->GetGPUVirtualAddress());
    }
    if (pointLightResource) {
        commandList->SetGraphicsRootConstantBufferView(5, pointLightResource->GetGPUVirtualAddress());
    }
    if (spotLightResource) {
        commandList->SetGraphicsRootConstantBufferView(6, spotLightResource->GetGPUVirtualAddress());
    }
    // 描画コマンドの発行
    commandList->DrawInstanced(UINT(modelData_.vertices.size()), 1, 0, 0);
}





// ==========================================
// モデルファイル読み込み (OBJ, GLTF, FBX等)
// ==========================================
Model::ModelData Model::LoadObjFile(const std::string& directoryPath, const std::string& filename) {
    ModelData modelData;
    Assimp::Importer importer;
    std::string filePath = directoryPath + "/" + filename;

    // Assimpで読み込み
    // aiProcess_Triangulate: 三角形化
    // aiProcess_FlipUVs: UV座標のYを反転 (DirectX用)
    // aiProcess_ConvertToLeftHanded: 左手系座標に変換 (DirectX用)
    const aiScene* scene = importer.ReadFile(filePath,
        aiProcess_Triangulate |
        aiProcess_FlipUVs |
        aiProcess_ConvertToLeftHanded
    );

    // 読み込みエラーチェック
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::string error = importer.GetErrorString();
        assert(false && "Assimp ReadFile Error");
        return modelData;
    }

    // ルートノードから再帰的に階層構造を読み取る
    modelData.rootNode = ReadNode(scene->mRootNode);


    // メッシュを走査（現在の描画システムに合わせて、頂点データは一つにまとめる）
    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[i];

        // --- 1. 頂点データの取得 ---
        for (unsigned int f = 0; f < mesh->mNumFaces; f++) {
            aiFace face = mesh->mFaces[f];
            for (unsigned int j = 0; j < face.mNumIndices; j++) {
                unsigned int index = face.mIndices[j];
                VertexData vertex;

                // 座標
                vertex.position = {
                    mesh->mVertices[index].x,
                    mesh->mVertices[index].y,
                    mesh->mVertices[index].z,
                    1.0f
                };

                // 法線
                if (mesh->HasNormals()) {
                    vertex.normal = {
                        mesh->mNormals[index].x,
                        mesh->mNormals[index].y,
                        mesh->mNormals[index].z
                    };
                } else {
                    vertex.normal = { 0.0f, 1.0f, 0.0f };
                }

                // UV座標
                if (mesh->HasTextureCoords(0)) {
                    vertex.texcoord = {
                        mesh->mTextureCoords[0][index].x,
                        mesh->mTextureCoords[0][index].y
                    };
                } else {
                    vertex.texcoord = { 0.0f, 0.0f };
                }

                modelData.vertices.push_back(vertex);
            }
        }

        // --- 2. マテリアル（テクスチャパス）の取得 ---
        if (mesh->mMaterialIndex >= 0) {
            aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
            aiString texPath;
            if (material->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {
                std::string fullPath = texPath.C_Str();
                std::string texFilename = fullPath.substr(fullPath.find_last_of("/\\") + 1);
                modelData.material.textureFilePath = directoryPath + "/" + texFilename;
            }
        }
    }

    // デフォルトテクスチャ処理
    if (modelData.material.textureFilePath.empty()) {
        modelData.material.textureFilePath = directoryPath + "/white1x1.png";
    }

    return modelData;
}

Model::Node Model::ReadNode(aiNode* node) {
    Node result;

    // 1. ノードの変換行列を取得 (assimp -> 自作Matrix4x4)
    aiMatrix4x4 src = node->mTransformation;

    // Assimpの行列は [行][列] でアクセスできます
    // エンジンのMatrix4x4の定義に合わせて代入します（一般的なDirectX系として記述）
    result.localMatrix.m[0][0] = src.a1; result.localMatrix.m[0][1] = src.a2; result.localMatrix.m[0][2] = src.a3; result.localMatrix.m[0][3] = src.a4;
    result.localMatrix.m[1][0] = src.b1; result.localMatrix.m[1][1] = src.b2; result.localMatrix.m[1][2] = src.b3; result.localMatrix.m[1][3] = src.b4;
    result.localMatrix.m[2][0] = src.c1; result.localMatrix.m[2][1] = src.c2; result.localMatrix.m[2][2] = src.c3; result.localMatrix.m[2][3] = src.c4;
    result.localMatrix.m[3][0] = src.d1; result.localMatrix.m[3][1] = src.d2; result.localMatrix.m[3][2] = src.d3; result.localMatrix.m[3][3] = src.d4;

    // 2. ノード名の取得
    result.name = node->mName.C_Str();

    // 3. 子供の数だけ確保
    result.children.resize(node->mNumChildren);

    // 4. 再帰的に子供を読み込む
    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        // 次の階層のNodeを読み込んで、children配列に格納
        result.children[i] = ReadNode(node->mChildren[i]);
    }

    return result;
}
// ノードの行列を更新する再帰関数
void Model::UpdateNodeMatrix(Node& node, const Matrix4x4& parentMatrix) {

    // 自分のワールド行列 = 親のワールド行列 × 自分のローカル行列
    Matrix4x4 worldMatrix =math_.Multiply(node.localMatrix, parentMatrix);

    // ここで計算した worldMatrix を、描画用定数バッファなどにセットすることになります

    // 子供たちにも「今の私の場所（親の場所）」を渡して更新させる
    for (auto& child : node.children) {
        UpdateNodeMatrix(child, worldMatrix);
    }
}
// 毎フレーム呼ぶ更新処理
void Model::Update() {
    // 基準となる行列（最初は単位行列）を作成
    Matrix4x4 identity = math_.makeIdentity4x4();

    // ルートノードから更新開始！
    UpdateNodeMatrix(modelData_.rootNode, identity);
}



//// MTLファイルからマテリアルデータを読み込む関数
//Model::MaterialData Model::LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename) {
//    MaterialData materialData;
//    std::string line;
//
//    std::ifstream file(directoryPath + "/" + filename);
//    assert(file.is_open());
//
//    while (std::getline(file, line)) {
//        std::string identifier;
//        std::istringstream s(line);
//        s >> identifier;
//
//        if (identifier == "map_Kd") {
//            std::string textureFilename;
//            s >> textureFilename;
//            materialData.textureFilePath = directoryPath + "/" + textureFilename;
//        }
//    }
//    return materialData;
//}