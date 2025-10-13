
#include <fstream>
#include "Model.h"
#include "engine/base/DirectXCommon.h"
#include "engine/base/Math.h"
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
    materialData_->selectedLighting = 1;
    Math math;
    materialData_->uvTransform = math.makeIdentity4x4();
}

// モデルの描画処理
void Model::Draw( ID3D12Resource* wvpResource, ID3D12Resource* directionalLightResource) {

    // ★★★ common_経由でコマンドリストを取得 ★★★
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();

    // 頂点バッファをIAステージに設定
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);

    // ルートシグネチャに各定数バッファを設定
    commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress());
    // ★★★ 平行光源のインデックスを 3 に修正 ★★★
    commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());

    // 描画コマンドの発行
    commandList->DrawInstanced(UINT(modelData_.vertices.size()), 1, 0, 0);
}



// OBJファイルからモデルデータを読み込む関数
Model::ModelData Model::LoadObjFile(const std::string& directoryPath, const std::string& filename) {
    ModelData modelData;
    std::vector<Vector4> positions;
    std::vector<Vector3> normals;
    std::vector<Vector2> texcoords;
    std::string line;

    std::ifstream file(directoryPath + "/" + filename);
    assert(file.is_open());

    while (std::getline(file, line)) {
        std::string identifier;
        std::istringstream s(line);
        s >> identifier;

        if (identifier == "v") {
            Vector4 position;
            s >> position.x >> position.y >> position.z;
            position.x *= -1.0f;
            position.w = 1.0f;
            positions.push_back(position);
        } else if (identifier == "vt") {
            Vector2 texcoord;
            s >> texcoord.x >> texcoord.y;
            texcoord.y = 1.0f - texcoord.y;
            texcoords.push_back(texcoord);
        } else if (identifier == "vn") {
            Vector3 normal;
            s >> normal.x >> normal.y >> normal.z;
            normal.x *= -1.0f;
            normals.push_back(normal);
        } else if (identifier == "f") {
            VertexData triangle[3];
            for (int32_t i = 0; i < 3; ++i) {
                std::string vertexDefinition;
                s >> vertexDefinition;

                std::istringstream v(vertexDefinition);
                uint32_t posIndex, tcIndex, nIndex;

                v >> posIndex;
                v.seekg(1, std::ios_base::cur);
                v >> tcIndex;
                v.seekg(1, std::ios_base::cur);
                v >> nIndex;

                triangle[i].position = positions[posIndex - 1];
                triangle[i].texcoord = texcoords[tcIndex - 1];
                triangle[i].normal = normals[nIndex - 1];
            }
            modelData.vertices.push_back(triangle[2]);
            modelData.vertices.push_back(triangle[1]);
            modelData.vertices.push_back(triangle[0]);
        } else if (identifier == "mtllib") {
            std::string materialFilename;
            s >> materialFilename;
            modelData.material = LoadMaterialTemplateFile(directoryPath, materialFilename);
        }
    }
    return modelData;
}

// MTLファイルからマテリアルデータを読み込む関数
Model::MaterialData Model::LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename) {
    MaterialData materialData;
    std::string line;

    std::ifstream file(directoryPath + "/" + filename);
    assert(file.is_open());

    while (std::getline(file, line)) {
        std::string identifier;
        std::istringstream s(line);
        s >> identifier;

        if (identifier == "map_Kd") {
            std::string textureFilename;
            s >> textureFilename;
            materialData.textureFilePath = directoryPath + "/" + textureFilename;
        }
    }
    return materialData;
}