#include "Model.h"
#include "engine/base/DirectXCommon.h"
#include <fstream>
#include <sstream>
#include <cassert>

// モデルの初期化処理
void Model::Initialize(ModelCommon* common, const std::string& directoryPath, const std::string& filename) {
    // NULLチェック
    assert(common);
    common_ = common;
    DirectXCommon* dxCommon = common_->GetDxCommon();

    // モデルデータ(.objファイル)の読み込み
    modelData_ = LoadObjFile(directoryPath, filename); // 引数をそのまま渡す
    // 読み込んだモデルデータに紐づくテクスチャをロードし、ハンドルを保存
    modelData_.material.textureHandle = TextureManager::GetInstance()->Load(modelData_.material.textureFilePath);

    // --- 頂点バッファの作成 ---
    // 頂点データ全体のサイズ分のリソースを確保
    vertexResource_ = dxCommon->CreateBufferResource(sizeof(VertexData) * modelData_.vertices.size());
    // 頂点バッファビュー(VBV)の設定
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress(); // リソースの仮想GPUアドレス
    vertexBufferView_.SizeInBytes = UINT(sizeof(VertexData) * modelData_.vertices.size()); // 全体のサイズ
    vertexBufferView_.StrideInBytes = sizeof(VertexData); // 1頂点あたりのサイズ

    // --- 頂点データをリソースに書き込む ---
    VertexData* vertexData = nullptr;
    // マップ: CPUからアクセス可能なポインタを取得する
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
    // 頂点データをGPU上のメモリにコピー
    std::memcpy(vertexData, modelData_.vertices.data(), sizeof(VertexData) * modelData_.vertices.size());
    // アンマップ: CPUからのアクセスを終える
    vertexResource_->Unmap(0, nullptr);

    // --- マテリアル用定数バッファ(CBV)の作成 ---
    materialResource_ = dxCommon->CreateBufferResource(sizeof(Material));
    // マテリアルデータをリソースに書き込む (こちらもマップ・アンマップ方式)
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
    // 初期値を設定
    materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f }; // 色を白に
    materialData_->enableLighting = true; // ライティングを有効に
    materialData_->selectedLighting = 1;  // ライティングの種類を選択
    Math math;
    materialData_->uvTransform = math.makeIdentity4x4(); // UV座標変換行列を単位行列に
}

// モデルの描画処理
void Model::Draw(ID3D12GraphicsCommandList* commandList, ID3D12Resource* wvpResource, ID3D12Resource* directionalLightResource) {
    // 頂点バッファをIAステージに設定
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);

    // ルートシグネチャに各定数バッファを設定
    commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress()); // マテリアル
    commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress());       // WVP行列
    commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress()); // 平行光源

    // テクスチャをルートシグネチャに設定
    commandList->SetGraphicsRootDescriptorTable(2, TextureManager::GetInstance()->GetGPUHandle(modelData_.material.textureHandle));

    // 描画コマンドの発行
    commandList->DrawInstanced(UINT(modelData_.vertices.size()), 1, 0, 0);
}

// OBJファイルからモデルデータを読み込む関数
Model::ModelData Model::LoadObjFile(const std::string& directoryPath, const std::string& filename) {
    ModelData modelData;
    std::vector<Vector4> positions;   // 頂点座標
    std::vector<Vector3> normals;     // 法線ベクトル
    std::vector<Vector2> texcoords;   // テクスチャ座標
    std::string line; // ファイルから読み込んだ1行を格納する変数

    // ファイルを開く
    std::ifstream file(directoryPath + "/" + filename);
    assert(file.is_open()); // ファイルオープン失敗をチェック

    // 1行ずつ読み込むループ
    while (std::getline(file, line)) {
        std::string identifier;
        std::istringstream s(line);
        s >> identifier; // 先頭の識別子を読む

        // 頂点座標 (v)
        if (identifier == "v") {
            Vector4 position;
            s >> position.x >> position.y >> position.z;
            position.x *= -1.0f; // ★座標系の変換 (右手系 -> 左手系)
            position.w = 1.0f;
            positions.push_back(position);
        }
        // テクスチャ座標 (vt)
        else if (identifier == "vt") {
            Vector2 texcoord;
            s >> texcoord.x >> texcoord.y;
            texcoord.y = 1.0f - texcoord.y; // ★V方向の反転 (上下逆)
            texcoords.push_back(texcoord);
        }
        // 法線ベクトル (vn)
        else if (identifier == "vn") {
            Vector3 normal;
            s >> normal.x >> normal.y >> normal.z;
            normal.x *= -1.0f; // ★座標系の変換
            normals.push_back(normal);
        }
        // 面情報 (f)
        else if (identifier == "f") {
            VertexData triangle[3];
            // 面は3つの頂点で構成される
            for (int32_t i = 0; i < 3; ++i) {
                std::string vertexDefinition;
                s >> vertexDefinition; // "1/1/1" のような頂点定義文字列を読む

                std::istringstream v(vertexDefinition);
                uint32_t posIndex, tcIndex, nIndex;

                // '/' 区切りでインデックスを読み込む
                v >> posIndex;
                v.seekg(1, std::ios_base::cur); // '/' を1文字スキップ
                v >> tcIndex;
                v.seekg(1, std::ios_base::cur); // '/' を1文字スキップ
                v >> nIndex;

                // OBJは1ベースインデックスなので、-1して0ベースに変換
                triangle[i].position = positions[posIndex - 1];
                triangle[i].texcoord = texcoords[tcIndex - 1];
                triangle[i].normal = normals[nIndex - 1];
            }
            // ★頂点の巻き順を逆にする (DirectXは時計回りが表)
            modelData.vertices.push_back(triangle[2]);
            modelData.vertices.push_back(triangle[1]);
            modelData.vertices.push_back(triangle[0]);
        }
        // マテリアルテンプレートライブラリ (mtllib)
        else if (identifier == "mtllib") {
            std::string materialFilename;
            s >> materialFilename;
            // マテリアルファイルを読み込む
            modelData.material = LoadMaterialTemplateFile(directoryPath, materialFilename);
        }
    }
    return modelData;
}

// MTLファイルからマテリアルデータを読み込む関数
Model::MaterialData Model::LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename) {
    MaterialData materialData;
    std::string line;

    // ファイルを開く
    std::ifstream file(directoryPath + "/" + filename);
    assert(file.is_open());

    // 1行ずつ読み込むループ
    while (std::getline(file, line)) {
        std::string identifier;
        std::istringstream s(line);
        s >> identifier;

        // map_Kd: ディフューズテクスチャマップ
        if (identifier == "map_Kd") {
            std::string textureFilename;
            s >> textureFilename;
            // ディレクトリパスと連結して、テクスチャへの完全なパスを保存
            materialData.textureFilePath = directoryPath + "/" + textureFilename;
        }
    }
    return materialData;
}