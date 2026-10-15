#define NOMINMAX
#include <fstream>
#include "Model.h"
#include "DirectXCommon.h"
#include "RenderStats.h"
#include "engine/utility/math/Math.h"
#include <sstream>
#include <cassert>
#include <filesystem>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include "SRVManager.h"
#include <DebugConsole.h>
#include <LightManager.h>



// ==========================================
// 初期化: メッシュごとにバッファを作る
// ==========================================

// ========================================================================
// Model 描画補助と動的メッシュ生成
// ------------------------------------------------------------------------
// 影用描画、メッシュのみの描画、プリミティブ生成など、読み込み以外の
// メッシュ利用処理を担当する。
// ========================================================================

// シャドウマップ生成用に、マテリアル描画を省いた深度向けメッシュ描画を行う。
void Model::DrawShadow(ID3D12Resource* wvpResource, int meshDrawIndex) {
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();
    engine::RenderStats::GetInstance()->RecordObjectDraw();

    // [0] WVP
    commandList->SetGraphicsRootConstantBufferView(0, wvpResource->GetGPUVirtualAddress());

    // [1] ボーン情報
    if (!modelData_.bones.empty()) {
        SRVManager::GetInstance()->SetGraphicsRootDescriptorTable(commandList, 1, boneSrvIndex_);
    }

    // 各メッシュの頂点を描画
    for (size_t meshIndex = 0; meshIndex < modelData_.meshes.size(); ++meshIndex) {
        if (meshDrawIndex >= 0 && meshIndex != static_cast<size_t>(meshDrawIndex)) {
            continue;
        }
        auto& mesh = modelData_.meshes[meshIndex];
        if (!mesh.vertexResource || !mesh.indexResource || mesh.indices.empty()) {
            continue;
        }
        const D3D12_VERTEX_BUFFER_VIEW& activeVertexBufferView = GetActiveVertexBufferView(mesh);
        commandList->IASetVertexBuffers(0, 1, &activeVertexBufferView);
        commandList->IASetIndexBuffer(&mesh.indexBufferView);
        commandList->DrawIndexedInstanced(UINT(mesh.indices.size()), 1, 0, 0, 0);
        engine::RenderStats::GetInstance()->RecordIndexedDraw(static_cast<uint32_t>(mesh.indices.size()));
    }
}

// 既に外側で必要な描画状態を設定している前提で、メッシュ本体だけを描画する。
void Model::DrawMeshOnly(int meshDrawIndex) {
    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();
    engine::RenderStats::GetInstance()->RecordObjectDraw();

    // メッシュごとに頂点バッファだけをセットして描画
    for (size_t meshIndex = 0; meshIndex < modelData_.meshes.size(); ++meshIndex) {
        if (meshDrawIndex >= 0 && meshIndex != static_cast<size_t>(meshDrawIndex)) {
            continue;
        }
        const auto& mesh = modelData_.meshes[meshIndex];
        if (!mesh.vertexResource || !mesh.indexResource || mesh.indices.empty()) {
            continue;
        }
        // 頂点バッファをセット
        const D3D12_VERTEX_BUFFER_VIEW& activeVertexBufferView = GetActiveVertexBufferView(mesh);
        commandList->IASetVertexBuffers(0, 1, &activeVertexBufferView);
        commandList->IASetIndexBuffer(&mesh.indexBufferView);
        commandList->DrawIndexedInstanced(UINT(mesh.indices.size()), 1, 0, 0, 0);
        engine::RenderStats::GetInstance()->RecordIndexedDraw(static_cast<uint32_t>(mesh.indices.size()));
    }
}

// 頂点配列から一時的なモデルデータを構築し、プリミティブやデバッグ表示に使える形へ変換する。
void Model::CreateFromVertices(ModelCommon* common, const std::vector<VertexData>& vertices, const std::vector<uint32_t>& indices) {
    computeSkinningAvailable_ = false;
    computeSkinningDirty_ = true;
    computeSkinningOutputReady_ = false;
    if (!common || vertices.empty() || indices.empty()) {
        modelData_.meshes.clear();
        return;
    }
    assert(common);

    common_ = common;
    DirectXCommon* dxCommon = common_->GetDxCommon();
    if (!dxCommon) {
        DebugConsole::GetInstance()->AddLog(LogLevel::Error, "[Model] CreateFromVertices: DirectX device is null.");
        modelData_.meshes.clear();
        return;
    }
    ID3D12Device* device = dxCommon->GetDevice();
    if (!device) {
        DebugConsole::GetInstance()->AddLog(LogLevel::Error, "[Model] CreateFromVertices: DirectX device is null.");
        modelData_.meshes.clear();
        return;
    }

    // 既存のメッシュデータがあればクリアする（エディタでのリアルタイム更新用）
    modelData_.meshes.clear();

    // 動的生成用のメッシュを1つ追加
    modelData_.meshes.emplace_back();
    auto& mesh = modelData_.meshes.back();

    // 頂点とインデックスのデータを保持
    mesh.vertices = vertices;
    mesh.indices = indices;
    mesh.materialIndex = 0; // ★マテリアルインデックスを初期化

    // ==========================================
    // 1. 頂点バッファ (Vertex Buffer) の作成
    // ==========================================
    UINT vbSize = static_cast<UINT>(sizeof(VertexData) * vertices.size());

    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC vbDesc{};
    vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    vbDesc.Width = vbSize;
    vbDesc.Height = 1;
    vbDesc.DepthOrArraySize = 1;
    vbDesc.MipLevels = 1;
    vbDesc.SampleDesc.Count = 1;
    vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    // ★ 修正: vertexResource に生成
    HRESULT hr = device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &vbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&mesh.vertexResource)
    );
    if (FAILED(hr) || !mesh.vertexResource) {
        DebugConsole::GetInstance()->AddLog(LogLevel::Error, "[Model] CreateFromVertices: failed to create vertex buffer.");
        modelData_.meshes.clear();
        return;
    }

    // ★ 修正: vertexResource に対してマップしてデータを流し込む
    void* mappedVerts = nullptr;
    hr = mesh.vertexResource->Map(0, nullptr, &mappedVerts);
    if (FAILED(hr) || !mappedVerts) {
        DebugConsole::GetInstance()->AddLog(LogLevel::Error, "[Model] CreateFromVertices: failed to map vertex buffer.");
        modelData_.meshes.clear();
        return;
    }
    memcpy(mappedVerts, vertices.data(), vbSize);
    mesh.vertexResource->Unmap(0, nullptr);

    // ★ 修正: vertexResource からGPUアドレスを取得
    mesh.vertexBufferView.BufferLocation = mesh.vertexResource->GetGPUVirtualAddress();
    mesh.vertexBufferView.SizeInBytes = vbSize;
    mesh.vertexBufferView.StrideInBytes = sizeof(VertexData);

    // ==========================================
    // 2. インデックスバッファ (Index Buffer) の作成
    // ==========================================
    UINT ibSize = static_cast<UINT>(sizeof(uint32_t) * indices.size());

    D3D12_RESOURCE_DESC ibDesc = vbDesc;
    ibDesc.Width = ibSize;

    // ★ 修正: indexResource に生成
    hr = device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &ibDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&mesh.indexResource)
    );
    if (FAILED(hr) || !mesh.indexResource) {
        DebugConsole::GetInstance()->AddLog(LogLevel::Error, "[Model] CreateFromVertices: failed to create index buffer.");
        modelData_.meshes.clear();
        return;
    }

    // ★ 修正: indexResource に対してマップしてデータを流し込む
    void* mappedIndices = nullptr;
    hr = mesh.indexResource->Map(0, nullptr, &mappedIndices);
    if (FAILED(hr) || !mappedIndices) {
        DebugConsole::GetInstance()->AddLog(LogLevel::Error, "[Model] CreateFromVertices: failed to map index buffer.");
        modelData_.meshes.clear();
        return;
    }
    memcpy(mappedIndices, indices.data(), ibSize);
    mesh.indexResource->Unmap(0, nullptr);

    // ★ 修正: indexResource からGPUアドレスを取得
    mesh.indexBufferView.BufferLocation = mesh.indexResource->GetGPUVirtualAddress();
    mesh.indexBufferView.SizeInBytes = ibSize;
    mesh.indexBufferView.Format = DXGI_FORMAT_R32_UINT;
}

// ==========================================
// Skeleton & Joint の実装 (深さ優先探索)
// ==========================================
