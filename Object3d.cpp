#include "Object3d.h"
#include <fstream>
#include <sstream>
#include <cassert>

void Object3d::Initialize(Object3dCommon* common, const std::string& modelFilePath) {
    assert(common);
    common_ = common;

    // .objファイル名の解析
    std::string directoryPath = modelFilePath.substr(0, modelFilePath.find_last_of('/'));
    std::string fileName = modelFilePath.substr(modelFilePath.find_last_of('/') + 1);

    // モデルデータの読み込み
    modelData_ = LoadObjFile(directoryPath, fileName);
    // テクスチャの読み込みとハンドルの保存
    modelData_.material.textureHandle = TextureManager::GetInstance()->Load(modelData_.material.textureFilePath);

    // 頂点バッファの作成
    vertexResource_ = common_->GetDxCommon()->CreateBufferResource(sizeof(VertexData) * modelData_.vertices.size());
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = UINT(sizeof(VertexData) * modelData_.vertices.size());
    vertexBufferView_.StrideInBytes = sizeof(VertexData);
    // 頂点データの書き込み
    VertexData* vertexData = nullptr;
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
    std::memcpy(vertexData, modelData_.vertices.data(), sizeof(VertexData) * modelData_.vertices.size());
    vertexResource_->Unmap(0, nullptr);

    // マテリアル用定数バッファの作成
    materialResource_ = common_->GetDxCommon()->CreateBufferResource(sizeof(Material));
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
    materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    materialData_->enableLighting = true;
    materialData_->selectedLighting = 0;
    Math math;
    materialData_->uvTransform = math.makeIdentity4x4();

    // WVP用定数バッファの作成
    wvpResource_ = common_->GetDxCommon()->CreateBufferResource(sizeof(TransformationMatrix));
    wvpResource_->Map(0, nullptr, reinterpret_cast<void**>(&wvpData_));
    wvpData_->WVP = math.makeIdentity4x4();
    wvpData_->world = math.makeIdentity4x4();

    // 平行光源用定数バッファの作成
    directionalLightResource_ = common_->GetDxCommon()->CreateBufferResource(sizeof(DirectionalLight));
    directionalLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData_));
    directionalLightData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    directionalLightData_->direction = { 0.0f, -1.0f, 0.0f };
    directionalLightData_->intensity = 1.0f;
}

void Object3d::Update(const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix) {
    Math math;
    Matrix4x4 worldMatrix = math.MakeAffineMatrix(transform_.scale, transform_.rotate, transform_.translate);
    Matrix4x4 worldViewProjectionMatrix = math.Multiply(worldMatrix, math.Multiply(viewMatrix, projectionMatrix));
    wvpData_->WVP = worldViewProjectionMatrix;
    wvpData_->world = worldMatrix;

    // 平行光源の向きを正規化
    directionalLightData_->direction = math.Normalize(directionalLightData_->direction);
}

void Object3d::Draw(ID3D12GraphicsCommandList* commandList) {
    // 頂点バッファを設定
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);

    // 定数バッファを設定
    commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, wvpResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource_->GetGPUVirtualAddress());

    // テクスチャを設定
    commandList->SetGraphicsRootDescriptorTable(2, TextureManager::GetInstance()->GetGPUHandle(modelData_.material.textureHandle));

    // 描画！
    commandList->DrawInstanced(UINT(modelData_.vertices.size()), 1, 0, 0);
}

Object3d::ModelData Object3d::LoadObjFile(const std::string& directoryPath, const std::string& filename) {
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

Object3d::MaterialData Object3d::LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename) {
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