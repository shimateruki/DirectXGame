
#include <fstream>
#include "Model.h"
#include "DirectXCommon.h"
#include "engine/utility/math/Math.h"
#include <sstream>
#include <cassert>
#include <filesystem>
#include "SRVManager.h"


// ==========================================
// 初期化: メッシュごとにバッファを作る
// ==========================================
void Model::Initialize(ModelCommon* common, const std::string& directoryPath, const std::string& filename) {
    assert(common);
    common_ = common;
    DirectXCommon* dxCommon = common_->GetDxCommon();

    // 1. ファイル読み込み (Mesh分けされたデータが返ってくる)
    modelData_ = LoadFile(directoryPath, filename);

    // 2. マテリアルごとにテクスチャをロード
    for (auto& material : modelData_.materials) {
        material.textureHandle = TextureManager::GetInstance()->Load(material.textureFilePath);
    }

    // 3. メッシュごとに頂点バッファを作成
    for (auto& mesh : modelData_.meshes) {
        // バッファ作成
        mesh.vertexResource = dxCommon->CreateBufferResource(sizeof(VertexData) * mesh.vertices.size());

        // VBV設定
        mesh.vertexBufferView.BufferLocation = mesh.vertexResource->GetGPUVirtualAddress();
        mesh.vertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * mesh.vertices.size());
        mesh.vertexBufferView.StrideInBytes = sizeof(VertexData);

        // データ書き込み
        VertexData* vertexData = nullptr;
        mesh.vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
        std::memcpy(vertexData, mesh.vertices.data(), sizeof(VertexData) * mesh.vertices.size());
        mesh.vertexResource->Unmap(0, nullptr);
    }

    // 4. 定数バッファ(Material)の作成 (これは共通のまま)
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
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();

    // 共通の定数バッファをセット
    commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());
    if (cameraResource) commandList->SetGraphicsRootConstantBufferView(4, cameraResource->GetGPUVirtualAddress());
    if (pointLightResource) commandList->SetGraphicsRootConstantBufferView(5, pointLightResource->GetGPUVirtualAddress());
    if (spotLightResource) commandList->SetGraphicsRootConstantBufferView(6, spotLightResource->GetGPUVirtualAddress());

    //  メッシュごとの描画ループ
    for (const auto& mesh : modelData_.meshes) {
        // 1. このメッシュの頂点バッファをセット
        commandList->IASetVertexBuffers(0, 1, &mesh.vertexBufferView);

        // 2. このメッシュのマテリアルに対応するテクスチャをセット
        if (mesh.materialIndex < modelData_.materials.size()) {
            uint32_t handle = modelData_.materials[mesh.materialIndex].textureHandle;
            // ルートパラメータ 2番 にテクスチャをセット (Object3dでやっていたことをここでやる！)
            SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 2, handle);
        }

        // 3. 描画
        commandList->DrawInstanced(UINT(mesh.vertices.size()), 1, 0, 0);
    }
}


// ==========================================
// 読み込み: Assimpのメッシュごとにデータを分ける
// ==========================================
Model::ModelData Model::LoadFile(const std::string& directoryPath, const std::string& filename) {
    ModelData modelData;
    Assimp::Importer importer;

    // パスの結合処理 (末尾にスラッシュがあるかチェック)
    std::string sep = (directoryPath.back() == '/' || directoryPath.back() == '\\') ? "" : "/";
    std::string filePath = directoryPath + sep + filename;

    // ファイル読み込み (三角形化, UV上下反転, 左手座標系変換)
    const aiScene* scene = importer.ReadFile(filePath, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_ConvertToLeftHanded);

    // 読み込み失敗チェック
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        return modelData; // 失敗時は空のデータを返す
    }

    // 1. ノード読み込み (当たり判定・階層構造用)
    // ※ReadNode関数は以前修正した「行列転置版」を使ってください
    modelData.rootNode = ReadNode(scene->mRootNode, modelData.nodes);

    // 2. マテリアルの読み込み
    modelData.materials.resize(scene->mNumMaterials);
    for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
        aiMaterial* aiMat = scene->mMaterials[i];
        aiString texPath;
        std::string textureFilePath;

        // --- テクスチャパスの取得 ---
        // 優先度1: Diffuse (OBJ形式など)
        if (aiMat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {
            textureFilePath = texPath.C_Str();
        }
        // 優先度2: BaseColor (glTF形式)
        else if (aiMat->GetTexture(aiTextureType_BASE_COLOR, 0, &texPath) == AI_SUCCESS) {
            textureFilePath = texPath.C_Str();
        }

        // --- パスの結合処理 ---
        if (!textureFilePath.empty()) {
            // ファイル名だけを取り出す (例: "C:/User/.../wood.png" -> "wood.png")
            std::string texFilename = std::filesystem::path(textureFilePath).filename().string();
            // ディレクトリパスと結合
            modelData.materials[i].textureFilePath = directoryPath + sep + texFilename;
        } else {
            // テクスチャがない、または見つからない場合は白画像 (パスはユーザー環境に合わせる)
            modelData.materials[i].textureFilePath = "resouces/sprite/white.png";
        }
    }

    // 3. メッシュの解析
    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        aiMesh* aiMesh = scene->mMeshes[i];
        Mesh mesh; // 新しいメッシュ

        // マテリアルインデックスを記録
        mesh.materialIndex = aiMesh->mMaterialIndex;

        // 頂点データの抽出
        for (unsigned int f = 0; f < aiMesh->mNumFaces; f++) {
            aiFace face = aiMesh->mFaces[f];
            for (unsigned int j = 0; j < face.mNumIndices; j++) {
                unsigned int index = face.mIndices[j];
                VertexData vertex;

                // 位置
                vertex.position = { aiMesh->mVertices[index].x, aiMesh->mVertices[index].y, aiMesh->mVertices[index].z, 1.0f };

                // 法線 (なければ上向き)
                if (aiMesh->HasNormals()) {
                    vertex.normal = { aiMesh->mNormals[index].x, aiMesh->mNormals[index].y, aiMesh->mNormals[index].z };
                } else {
                    vertex.normal = { 0.0f, 1.0f, 0.0f };
                }

                // UV座標 (なければ0,0)
                if (aiMesh->HasTextureCoords(0)) {
                    vertex.texcoord = { aiMesh->mTextureCoords[0][index].x, aiMesh->mTextureCoords[0][index].y };
                } else {
                    vertex.texcoord = { 0.0f, 0.0f };
                }

                mesh.vertices.push_back(vertex);
            }
        }

        // メッシュリストに追加
        modelData.meshes.push_back(mesh);
    }

    return modelData;
}
// ノード読み込みの再帰関数
Model::Node Model::ReadNode(aiNode* node, std::vector<Node>& nodes) {
    Node result;

    // 1. ノード名の取得
    result.name = node->mName.C_Str();

    // 2. 変換行列の取得 
    aiMatrix4x4 transform = node->mTransformation;

    // --- 行列のコピー ---
    result.localMatrix.m[0][0] = transform.a1; result.localMatrix.m[0][1] = transform.b1; result.localMatrix.m[0][2] = transform.c1; result.localMatrix.m[0][3] = transform.d1;
    result.localMatrix.m[1][0] = transform.a2; result.localMatrix.m[1][1] = transform.b2; result.localMatrix.m[1][2] = transform.c2; result.localMatrix.m[1][3] = transform.d2;
    result.localMatrix.m[2][0] = transform.a3; result.localMatrix.m[2][1] = transform.b3; result.localMatrix.m[2][2] = transform.c3; result.localMatrix.m[2][3] = transform.d3;
    result.localMatrix.m[3][0] = transform.a4; result.localMatrix.m[3][1] = transform.b4; result.localMatrix.m[3][2] = transform.c4; result.localMatrix.m[3][3] = transform.d4;

    // 3. 全ノードリストに自分を追加
    nodes.push_back(result);

    // 4. 子ノードへ進む
    result.children.resize(node->mNumChildren);
    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        result.children[i] = ReadNode(node->mChildren[i], nodes);
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