#define NOMINMAX
#include <fstream>
#include "Model.h"
#include "DirectXCommon.h"
#include "engine/utility/math/Math.h"
#include <sstream>
#include <cassert>
#include <filesystem>
#include "SRVManager.h"
#include <DebugConsole.h>
#include <LightManager.h>

// ==========================================
// 初期化: メッシュごとにバッファを作る
// ==========================================
void Model::Initialize(ModelCommon* common, const std::string& directoryPath, const std::string& filename) {
    assert(common);
    common_ = common;
    DirectXCommon* dxCommon = common_->GetDxCommon();

    // 1. ファイル読み込み (Mesh分けされたデータが返ってくる)
    // ※ ここでボーンがない場合のダミーボーン生成も行われます
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

    // 4. 定数バッファ(Material)の作成
    materialResource_ = dxCommon->CreateBufferResource(sizeof(Material));
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
    materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    materialData_->enableLighting = true;
    materialData_->selectedLighting = 2;
    materialData_->shininess = 50;
    materialData_->materialType = 0; // 通常
    materialData_->roughness = 0.5f; // 程よくザラザラ（光沢が広がる）
    materialData_->metallic = 0.0f;  // 非金属（景色を反射しない）
    Math math;
    materialData_->uvTransform = math.MakeIdentity4x4();

    // 5. ボーン用バッファの作成 
    CreateBoneBuffer();
}

// ==========================================
// ボーンバッファの作成とSRV登録 
// ==========================================
void Model::CreateBoneBuffer() {
    DirectXCommon* dxCommon = common_->GetDxCommon();

    // LoadFileでダミーボーンを作っているので、必ず1つ以上ボーンがある状態になります
    // (なので empty チェックで return はしません)

    // 1. リソース作成
    UINT sizeInBytes = sizeof(BoneForGPU) * static_cast<UINT>(modelData_.bones.size());
    boneResource_ = dxCommon->CreateBufferResource(sizeInBytes);

    // 2. マッピング
    boneResource_->Map(0, nullptr, reinterpret_cast<void**>(&boneMappedData_));

    // ★重要: 初期値を「単位行列」で埋めておく
    // これをしないと、アニメーション更新が走る前の1フレーム目にモデルが消えます
    Math math;
    for (size_t i = 0; i < modelData_.bones.size(); ++i) {
        boneMappedData_[i].finalMatrix = math.MakeIdentity4x4();
    }

    // 3. SRVを作成 (StructuredBuffer)
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = UINT(modelData_.bones.size());
    srvDesc.Buffer.StructureByteStride = sizeof(BoneForGPU);
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    // SRVManagerでディスクリプタを確保して作成
    boneSrvIndex_ = SRVManager::GetInstance()->Allocate();
    SRVManager::GetInstance()->CreateSRVforResource(boneSrvIndex_, boneResource_.Get(), srvDesc);
}

// ==========================================
// ボーン行列の更新 
// ==========================================
void Model::UpdateBoneBuffer() {
    // ボーンごとに計算
    for (size_t i = 0; i < modelData_.bones.size(); ++i) {
        // ボーン名に対応するNodeを探す
        Node* node = FindNode(modelData_.rootNode, modelData_.bones[i].name);

        // FinalMatrix = InverseBindPose * GlobalMatrix
        if (node) {
            boneMappedData_[i].finalMatrix =
                math_.Multiply(modelData_.bones[i].inverseBindPoseMatrix, node->globalMatrix);
        } else {
            // ノードが見つからない(ダミーボーンなど)場合は単位行列を入れる
            boneMappedData_[i].finalMatrix = math_.MakeIdentity4x4();
        }
    }
}

// モデルの描画処理
void Model::Draw(ID3D12Resource* wvpResource, ID3D12Resource* directionalLightResource, ID3D12Resource* cameraResource, ID3D12Resource* pointLightResource, ID3D12Resource* spotLightResource, ID3D12Resource* overrideMaterialResource) {
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();

    // 1. マテリアル設定
    if (overrideMaterialResource) {
        commandList->SetGraphicsRootConstantBufferView(0, overrideMaterialResource->GetGPUVirtualAddress());
    } else if (materialResource_) {
        commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
    }

    // 2. 定数バッファ設定
    if (wvpResource) commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress());
    // (RootParam[2] はテクスチャ)
    if (directionalLightResource) commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());
    if (cameraResource) commandList->SetGraphicsRootConstantBufferView(4, cameraResource->GetGPUVirtualAddress());
    if (pointLightResource) commandList->SetGraphicsRootConstantBufferView(5, pointLightResource->GetGPUVirtualAddress());
    if (spotLightResource) commandList->SetGraphicsRootConstantBufferView(6, spotLightResource->GetGPUVirtualAddress());

 
    if (!modelData_.bones.empty()) {
        SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 7, boneSrvIndex_);
    }

    uint32_t envMapHandle = LightManager::GetInstance()->GetEnvironmentMapHandle();
    if (envMapHandle != 0) { // 念のため0（未読み込み）じゃないかチェック
        SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 8, envMapHandle);
    }
    // 3. メッシュごとの描画ループ
    for (const auto& mesh : modelData_.meshes) {
        commandList->IASetVertexBuffers(0, 1, &mesh.vertexBufferView);

        if (mesh.materialIndex < modelData_.materials.size()) {
            uint32_t handle = modelData_.materials[mesh.materialIndex].textureHandle;
            SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 2, handle);
        }

        commandList->DrawInstanced(UINT(mesh.vertices.size()), 1, 0, 0);
    }
}

// ==========================================
// 読み込み: Assimpのメッシュごとにデータを分ける
// ==========================================
Model::ModelData Model::LoadFile(const std::string& directoryPath, const std::string& filename) {

    ModelData modelData;
    Assimp::Importer importer;

    std::string sep = (directoryPath.back() == '/' || directoryPath.back() == '\\') ? "" : "/";
    std::string filePath = directoryPath + sep + filename;

    const aiScene* scene = importer.ReadFile(filePath, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_ConvertToLeftHanded);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        return modelData;
    }

    // 1. ノード読み込み
    modelData.rootNode = ReadNode(scene->mRootNode, modelData.nodes);

    // 2. マテリアルの読み込み
    modelData.materials.resize(scene->mNumMaterials);
    for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
        aiMaterial* aiMat = scene->mMaterials[i];
        aiString texPath;
        std::string textureFilePath;

        if (aiMat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {
            textureFilePath = texPath.C_Str();
        } else if (aiMat->GetTexture(aiTextureType_BASE_COLOR, 0, &texPath) == AI_SUCCESS) {
            textureFilePath = texPath.C_Str();
        }

        if (!textureFilePath.empty()) {
            std::string texFilename = std::filesystem::path(textureFilePath).filename().string();
            modelData.materials[i].textureFilePath = directoryPath + sep + texFilename;
        } else {
            modelData.materials[i].textureFilePath = "Resources/sprite/white.png";
        }
    }

    // 3. メッシュの解析
    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        aiMesh* aiMesh = scene->mMeshes[i];
        Mesh mesh;
        mesh.materialIndex = aiMesh->mMaterialIndex;

        std::vector<VertexData> tempVertices;
        tempVertices.resize(aiMesh->mNumVertices);

        for (unsigned int v = 0; v < aiMesh->mNumVertices; ++v) {
            VertexData& vertex = tempVertices[v];
            vertex.position = { aiMesh->mVertices[v].x, aiMesh->mVertices[v].y, aiMesh->mVertices[v].z, 1.0f };
            if (aiMesh->HasNormals()) {
                vertex.normal = { aiMesh->mNormals[v].x, aiMesh->mNormals[v].y, aiMesh->mNormals[v].z };
            } else {
                vertex.normal = { 0.0f, 1.0f, 0.0f };
            }
            if (aiMesh->HasTextureCoords(0)) {
                vertex.texcoord = { aiMesh->mTextureCoords[0][v].x, aiMesh->mTextureCoords[0][v].y };
            } else {
                vertex.texcoord = { 0.0f, 0.0f };
            }
            vertex.boneWeights = { 0.0f, 0.0f, 0.0f, 0.0f };
            vertex.boneIndices = { 0.0f, 0.0f, 0.0f, 0.0f };
        }

        // ボーン解析
        for (unsigned int b = 0; b < aiMesh->mNumBones; ++b) {
            aiBone* aiBone = aiMesh->mBones[b];
            std::string boneName = aiBone->mName.C_Str();
            int boneIndex = -1;
            for (int k = 0; k < modelData.bones.size(); ++k) {
                if (modelData.bones[k].name == boneName) {
                    boneIndex = k;
                    break;
                }
            }
            if (boneIndex == -1) {
                boneIndex = (int)modelData.bones.size();
                Bone newBone;
                newBone.name = boneName;
                aiMatrix4x4 offset = aiBone->mOffsetMatrix;
                newBone.inverseBindPoseMatrix.m[0][0] = offset.a1; newBone.inverseBindPoseMatrix.m[0][1] = offset.b1; newBone.inverseBindPoseMatrix.m[0][2] = offset.c1; newBone.inverseBindPoseMatrix.m[0][3] = offset.d1;
                newBone.inverseBindPoseMatrix.m[1][0] = offset.a2; newBone.inverseBindPoseMatrix.m[1][1] = offset.b2; newBone.inverseBindPoseMatrix.m[1][2] = offset.c2; newBone.inverseBindPoseMatrix.m[1][3] = offset.d2;
                newBone.inverseBindPoseMatrix.m[2][0] = offset.a3; newBone.inverseBindPoseMatrix.m[2][1] = offset.b3; newBone.inverseBindPoseMatrix.m[2][2] = offset.c3; newBone.inverseBindPoseMatrix.m[2][3] = offset.d3;
                newBone.inverseBindPoseMatrix.m[3][0] = offset.a4; newBone.inverseBindPoseMatrix.m[3][1] = offset.b4; newBone.inverseBindPoseMatrix.m[3][2] = offset.c4; newBone.inverseBindPoseMatrix.m[3][3] = offset.d4;
                modelData.bones.push_back(newBone);
            }

            for (unsigned int w = 0; w < aiBone->mNumWeights; ++w) {
                unsigned int vertexId = aiBone->mWeights[w].mVertexId;
                float weight = aiBone->mWeights[w].mWeight;
                if (vertexId < tempVertices.size()) {
                    auto& v = tempVertices[vertexId];
                    if (v.boneWeights.x == 0.0f) { v.boneWeights.x = weight; v.boneIndices.x = (float)boneIndex; } else if (v.boneWeights.y == 0.0f) { v.boneWeights.y = weight; v.boneIndices.y = (float)boneIndex; } else if (v.boneWeights.z == 0.0f) { v.boneWeights.z = weight; v.boneIndices.z = (float)boneIndex; } else if (v.boneWeights.w == 0.0f) { v.boneWeights.w = weight; v.boneIndices.w = (float)boneIndex; }
                }
            }
        }

        // メッシュ構築
        for (unsigned int f = 0; f < aiMesh->mNumFaces; f++) {
            aiFace face = aiMesh->mFaces[f];
            for (unsigned int j = 0; j < face.mNumIndices; j++) {
                mesh.vertices.push_back(tempVertices[face.mIndices[j]]);
            }
        }
        modelData.meshes.push_back(mesh);
    }

    // =========================================================
    // : ボーンがない場合の対処 (ダミーボーン作戦)
    // =========================================================
    if (modelData.bones.empty()) {
        // 1. ダミーボーンを作る (単位行列)
        Bone dummyBone;
        dummyBone.name = "DummyBone";
        Math math;
        dummyBone.inverseBindPoseMatrix = math.MakeIdentity4x4();
        modelData.bones.push_back(dummyBone);

        // 2. すべての頂点にダミーボーンの影響(100%)を与える
        for (auto& mesh : modelData.meshes) {
            for (auto& v : mesh.vertices) {
                if (v.boneWeights.x == 0.0f && v.boneWeights.y == 0.0f &&
                    v.boneWeights.z == 0.0f && v.boneWeights.w == 0.0f) {

                    v.boneWeights = { 1.0f, 0.0f, 0.0f, 0.0f };
                    v.boneIndices = { 0.0f, 0.0f, 0.0f, 0.0f };
                }
            }
        }
    }

    // =========================================================
    // 4. アニメーションの読み込み 
    // =========================================================
    for (unsigned int i = 0; i < scene->mNumAnimations; ++i) {
        aiAnimation* aiAnim = scene->mAnimations[i];
        Animation animation;
        if (aiAnim->mName.length > 0) animation.name = aiAnim->mName.C_Str();
        else animation.name = "animation_" + std::to_string(i);
        float ticksPerSecond = (float)(aiAnim->mTicksPerSecond != 0 ? aiAnim->mTicksPerSecond : 25.0);

        animation.duration = (float)aiAnim->mDuration / ticksPerSecond;
        animation.ticksPerSecond = ticksPerSecond;

        for (unsigned int c = 0; c < aiAnim->mNumChannels; ++c) {
            aiNodeAnim* aiChannel = aiAnim->mChannels[c];
            NodeAnimation nodeAnim;
            nodeAnim.name = aiChannel->mNodeName.C_Str();

            // デバッグログ (必要に応じて残す)
            if (nodeAnim.name.find("Hips") != std::string::npos) {
                std::string log = "AnimNode: " + nodeAnim.name +
                    " | PosKeys: " + std::to_string(aiChannel->mNumPositionKeys) + "\n";
                DebugConsole::GetInstance()->AddLog(log.c_str());
            }

      

            // Position
            for (unsigned int k = 0; k < aiChannel->mNumPositionKeys; ++k) {
                aiVectorKey& key = aiChannel->mPositionKeys[k];
                KeyframeVector3 kf;
                kf.time = (float)key.mTime / ticksPerSecond; 
                kf.value = { key.mValue.x, key.mValue.y, key.mValue.z };
                nodeAnim.translate.push_back(kf);
            }

            // Rotation
            for (unsigned int k = 0; k < aiChannel->mNumRotationKeys; ++k) {
                aiQuatKey& key = aiChannel->mRotationKeys[k];
                KeyframeQuaternion kf;
                kf.time = (float)key.mTime / ticksPerSecond; 
                kf.value = { key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w };
                nodeAnim.rotate.push_back(kf);
            }

            // Scaling
            for (unsigned int k = 0; k < aiChannel->mNumScalingKeys; ++k) {
                aiVectorKey& key = aiChannel->mScalingKeys[k];
                KeyframeVector3 kf;
                kf.time = (float)key.mTime / ticksPerSecond;
                kf.value = { key.mValue.x, key.mValue.y, key.mValue.z };
                nodeAnim.scale.push_back(kf);
            }
            animation.nodeAnimations.push_back(nodeAnim);
        }
        modelData.animations.push_back(animation);
    }

    return modelData;
}

// ノード読み込み
Model::Node Model::ReadNode(aiNode* node, std::vector<Node>& nodes) {
    Node result;
    result.name = node->mName.C_Str();
    aiMatrix4x4 transform = node->mTransformation;
    result.localMatrix.m[0][0] = transform.a1; result.localMatrix.m[0][1] = transform.b1; result.localMatrix.m[0][2] = transform.c1; result.localMatrix.m[0][3] = transform.d1;
    result.localMatrix.m[1][0] = transform.a2; result.localMatrix.m[1][1] = transform.b2; result.localMatrix.m[1][2] = transform.c2; result.localMatrix.m[1][3] = transform.d2;
    result.localMatrix.m[2][0] = transform.a3; result.localMatrix.m[2][1] = transform.b3; result.localMatrix.m[2][2] = transform.c3; result.localMatrix.m[2][3] = transform.d3;
    result.localMatrix.m[3][0] = transform.a4; result.localMatrix.m[3][1] = transform.b4; result.localMatrix.m[3][2] = transform.c4; result.localMatrix.m[3][3] = transform.d4;
    nodes.push_back(result);
    result.children.resize(node->mNumChildren);
    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        result.children[i] = ReadNode(node->mChildren[i], nodes);
    }
    return result;
}

// ノード行列の更新
void Model::UpdateNodeMatrix(Node& node, const Matrix4x4& parentMatrix) {
    // 自分のワールド行列 = 親のワールド行列 × 自分のローカル行列
    node.globalMatrix = math_.Multiply(node.localMatrix, parentMatrix);

    for (auto& child : node.children) {
        UpdateNodeMatrix(child, node.globalMatrix);
    }
}

// 毎フレーム呼ぶ更新処理
void Model::Update() {
    Matrix4x4 identity = math_.MakeIdentity4x4();
    UpdateNodeMatrix(modelData_.rootNode, identity);

    //  ボーン情報の更新
    UpdateBoneBuffer();
}

// --- アニメーション計算用ヘルパー ---
// =========================================================
// 座標・スケール用 (Vector3) の補間
// =========================================================
Vector3 Model::CalculateValue(const std::vector<KeyframeVector3>& keyframes, float time) {
    // キーがない場合
    if (keyframes.empty()) {
        return { 0.0f, 0.0f, 0.0f };
    }
    // キーが1つだけ、または時間が最初のキーより前の場合
    if (keyframes.size() == 1 || time <= keyframes[0].time) {
        return keyframes[0].value;
    }

    // 時間の範囲を探す (線形探索)
    for (size_t i = 0; i < keyframes.size() - 1; ++i) {
        // 現在の時間が、このキーと次のキーの間にあるか？
        if (time >= keyframes[i].time && time <= keyframes[i + 1].time) {
            // 0.0～1.0 の割合(t)を計算
            float t = (time - keyframes[i].time) / (keyframes[i + 1].time - keyframes[i].time);

            // 線形補間 (Lerp)
            Vector3 result;
            result.x = std::lerp(keyframes[i].value.x, keyframes[i + 1].value.x, t);
            result.y = std::lerp(keyframes[i].value.y, keyframes[i + 1].value.y, t);
            result.z = std::lerp(keyframes[i].value.z, keyframes[i + 1].value.z, t);
            return result;
        }
    }

    // 時間が最後のキーを超えている場合は、最後の値を返す
    return keyframes.back().value;
}

// =========================================================
// 回転用 (Quaternion) の補間
// =========================================================
Quaternion Model::CalculateValue(const std::vector<KeyframeQuaternion>& keyframes, float time) {
    // キーがない場合
    if (keyframes.empty()) {
        return { 0.0f, 0.0f, 0.0f, 1.0f }; // 単位クォータニオン
    }
    // キーが1つ、または時間が最初より前
    if (keyframes.size() == 1 || time <= keyframes[0].time) {
        return keyframes[0].value;
    }

    // 時間の範囲を探す
    for (size_t i = 0; i < keyframes.size() - 1; ++i) {
        if (time >= keyframes[i].time && time <= keyframes[i + 1].time) {
            float t = (time - keyframes[i].time) / (keyframes[i + 1].time - keyframes[i].time);

            // 球面線形補間 (Slerp)
            return Math::Slerp(keyframes[i].value, keyframes[i + 1].value, t);
        }
    }

    return keyframes.back().value;
}
Model::Node* Model::FindNode(Node& node, const std::string& name) {
    if (node.name == name) return &node;
    for (auto& child : node.children) {
        Node* result = FindNode(child, name);
        if (result) return result;
    }
    return nullptr;
}

void Model::ApplyAnimation(const Animation& animation, float time) {
    for (const auto& nodeAnim : animation.nodeAnimations) {
        Node* targetNode = FindNode(modelData_.rootNode, nodeAnim.name);
        if (!targetNode) {
            std::string log = "Node Missing: " + nodeAnim.name + "\n";
			DebugConsole::GetInstance()->AddLog("owata");
            continue; // 次の骨へ
        }

        Vector3 scale = CalculateValue(nodeAnim.scale, time);
        Quaternion rotate = CalculateValue(nodeAnim.rotate, time);
        Vector3 translate = CalculateValue(nodeAnim.translate, time);

        Matrix4x4 mS = math_.MakeScaleMatrix(scale);
        Matrix4x4 mR = Math::MakeRotateQuaternionMatrix(rotate);
        Matrix4x4 mT = math_.MakeTranslateMatrix(translate);
      
        targetNode->localMatrix = math_.Multiply(mS, math_.Multiply(mR, mT));
    }
}
const Model::Animation* Model::GetAnimation(const std::string& name) const {
    for (const auto& animation : modelData_.animations) {
        if (animation.name == name) {
            return &animation;
        }
    }
    // 見つからなければ nullptr
    return nullptr;
}