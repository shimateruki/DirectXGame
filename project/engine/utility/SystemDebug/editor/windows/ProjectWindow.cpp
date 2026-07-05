#define NOMINMAX
#include "ProjectWindow.h"
#include "DebugEditor.h"
#include "imgui.h"
#include "ModelManager.h"
#include "PresetManager.h"
#include "DebugConsole.h"
#include "DirectXCommon.h"
#include "SrvManager.h"
#include "SceneManager.h"
#include "BaseScene.h"
#include "Object3d.h"
#include "WinApp.h"
#include "CameraManager.h"
#include "Object3dCommon.h"
#include "IconsFontAwesome5.h"
#include <algorithm>
#include <cctype>
#include <filesystem>


namespace fs = std::filesystem;

namespace {
std::string ToLowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

bool IsModelAssetFile(const fs::path& path) {
    const std::string ext = ToLowerAscii(path.extension().string());
    return ext == ".obj" || ext == ".gltf" || ext == ".glb";
}

bool IsGeneratedLodAsset(const fs::path& path) {
    return ToLowerAscii(path.stem().string()).find("_lod") != std::string::npos;
}

void ApplyThumbnailLightOverride(Object3d* object) {
    if (!object) {
        return;
    }

    if (auto* material = object->GetMaterialData()) {
        material->enableLighting = 0;
        material->selectedLighting = 0;
        material->emissive = (std::max)(material->emissive, 1.0f);
    }

    for (Object3d* child : object->GetChildren()) {
        ApplyThumbnailLightOverride(child);
    }
}
}

void ProjectWindow::Initialize(DebugEditor* editor, DirectXCommon* dxCommon) {
    editor_ = editor;
    dxCommon_ = dxCommon;
    if (dxCommon_) {
        previewObject3dCommon_ = std::make_unique<Object3dCommon>();
        previewObject3dCommon_->Initialize(dxCommon_);
    }
}

// ==========================================================
// 画用紙（レンダーターゲット）の作成
// ==========================================================
void ProjectWindow::CreateThumbnailResource(const std::string& modelName) {
    if (!dxCommon_) return;
    auto device = dxCommon_->GetDevice();
    ThumbnailData& data = thumbnailAlbum_[modelName];

    // 1. 画用紙（テクスチャ）の作成
    D3D12_RESOURCE_DESC resDesc{};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resDesc.Width = kThumbnailSize;
    resDesc.Height = kThumbnailSize;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    resDesc.SampleDesc.Count = 1;
    resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = resDesc.Format;
    clearValue.Color[0] = 0.15f; clearValue.Color[1] = 0.15f; clearValue.Color[2] = 0.15f; clearValue.Color[3] = 1.0f; // 背景色

    device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue, IID_PPV_ARGS(&data.resource));

    // 2. RTV（描き込むための準備）
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = 1;
    device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&data.rtvHeap));

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = resDesc.Format;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device->CreateRenderTargetView(data.resource.Get(), &rtvDesc, data.rtvHeap->GetCPUDescriptorHandleForHeapStart());

    // 3. SRV（ImGuiに表示するための画像ハンドル）
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = resDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    data.srvHandle = SRVManager::GetInstance()->CreateSRV(data.resource.Get(), srvDesc);

    data.isCaptured = false;
}

void ProjectWindow::CreatePresetThumbnailResource(const std::string& presetName) {
    if (!dxCommon_) return;
    auto device = dxCommon_->GetDevice();
    ThumbnailData& data = presetThumbnailAlbum_[presetName];

    // 画用紙（テクスチャ）の作成
    D3D12_RESOURCE_DESC resDesc{};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resDesc.Width = kThumbnailSize;
    resDesc.Height = kThumbnailSize;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    resDesc.SampleDesc.Count = 1;
    resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = resDesc.Format;
    clearValue.Color[0] = 0.2f; clearValue.Color[1] = 0.2f; clearValue.Color[2] = 0.25f; clearValue.Color[3] = 1.0f; // プリセット用は少し色を変える

    device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue, IID_PPV_ARGS(&data.resource));

    // RTV
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = 1;
    device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&data.rtvHeap));

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = resDesc.Format;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device->CreateRenderTargetView(data.resource.Get(), &rtvDesc, data.rtvHeap->GetCPUDescriptorHandleForHeapStart());

    // SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = resDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    data.srvHandle = SRVManager::GetInstance()->CreateSRV(data.resource.Get(), srvDesc);

    data.isCaptured = false;
}

uint64_t ProjectWindow::GetPresetThumbnailGpuPtr(const std::string& presetName) {
    if (presetName.empty()) {
        return 0;
    }

    if (presetThumbnailAlbum_.find(presetName) == presetThumbnailAlbum_.end()) {
        CreatePresetThumbnailResource(presetName);
    }

    auto it = presetThumbnailAlbum_.find(presetName);
    if (it == presetThumbnailAlbum_.end() || it->second.srvHandle == 0) {
        return 0;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = SRVManager::GetInstance()->GetGPUDescriptorHandle(it->second.srvHandle);
    return static_cast<uint64_t>(gpuHandle.ptr);
}

// ==========================================================
// 撮影処理（Game::Drawの最初で呼ばれる）
// ==========================================================
void ProjectWindow::CapturePendingThumbnails() {
    if (!dxCommon_) return;
    auto device = dxCommon_->GetDevice();
    auto commandList = dxCommon_->GetCommandList();

    // =======================================================
    //  シーンの進行状況に依存しない、安全な専用 Object3dCommon を使う！
    // =======================================================
    Object3dCommon* objCommon = previewObject3dCommon_.get();
    if (!objCommon) return;

    // --- Zバッファの生成 ---
    static Microsoft::WRL::ComPtr<ID3D12Resource> s_studioDepth;
    static Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> s_studioDsvHeap;
    if (!s_studioDepth) {
        D3D12_RESOURCE_DESC depthDesc{};
        depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depthDesc.Width = kThumbnailSize;
        depthDesc.Height = kThumbnailSize;
        depthDesc.DepthOrArraySize = 1;
        depthDesc.MipLevels = 1;
        depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_CLEAR_VALUE clearValue{};
        clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;;
        clearValue.DepthStencil.Depth = 1.0f;

        device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue, IID_PPV_ARGS(&s_studioDepth));

        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.NumDescriptors = 1;
        device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&s_studioDsvHeap));

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        device->CreateDepthStencilView(s_studioDepth.Get(), &dsvDesc, s_studioDsvHeap->GetCPUDescriptorHandleForHeapStart());
    }

    D3D12_VIEWPORT originalViewport{};
    originalViewport.Width = (float)WinApp::kClientWidth;
    originalViewport.Height = (float)WinApp::kClientHeight;
    originalViewport.TopLeftX = 0;
    originalViewport.TopLeftY = 0;
    originalViewport.MinDepth = 0.0f;
    originalViewport.MaxDepth = 1.0f;

    D3D12_RECT originalScissor{};
    originalScissor.left = 0;
    originalScissor.right = WinApp::kClientWidth;
    originalScissor.top = 0;
    originalScissor.bottom = WinApp::kClientHeight;

    if (!isStudioCameraInitialized_) {
        studioCamera_.Initialize();
        studioCamera_.SetInputEnabled(false);
        isStudioCameraInitialized_ = true;
    }
    studioCamera_.Update(); // 行列を計算

    // メインカメラから、このスタジオ用カメラにすり替える！
    CameraManager::GetInstance()->SetActiveCamera(&studioCamera_);
    // =================================================================

    for (auto& pair : thumbnailAlbum_) {
        std::string modelName = pair.first;
        ThumbnailData& data = pair.second;
        if (data.isCaptured) continue;
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = data.resource.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        commandList->ResourceBarrier(1, &barrier);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = data.rtvHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = s_studioDsvHeap->GetCPUDescriptorHandleForHeapStart();
        commandList->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);

        const float clearColor[] = { 0.15f, 0.15f, 0.15f, 1.0f };
        commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
        commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        D3D12_VIEWPORT viewport{};
        viewport.Width = (float)kThumbnailSize;
        viewport.Height = (float)kThumbnailSize;
        viewport.TopLeftX = 0;
        viewport.TopLeftY = 0;
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        commandList->RSSetViewports(1, &viewport);

        D3D12_RECT scissorRect{};
        scissorRect.left = 0;
        scissorRect.right = kThumbnailSize;
        scissorRect.top = 0;
        scissorRect.bottom = kThumbnailSize;
        commandList->RSSetScissorRects(1, &scissorRect);

        objCommon->SetGraphicsCommand();
        objCommon->SetPipelineState(BlendMode::kNormal);

        if (!data.previewObject) {
            data.previewObject = std::make_shared<Object3d>();
            data.previewObject->Initialize(objCommon);
            data.previewObject->SetIsUIPreview(true);

            // モデルをロードしてポインタを取得
            Model* model = ModelManager::GetInstance()->LoadModel(modelName);
            data.previewObject->SetModel(modelName);

            // 初期フィット計算
            float autoScale = 1.0f;
            Vector3 autoTranslate = { 0.0f, 0.0f, 0.0f };

            if (model) {
                Vector3 modelSize = model->GetSize();
                Vector3 modelCenter = model->GetCenter();

                // 最も長い辺を見つける
                float maxDim = (std::max)({ modelSize.x, modelSize.y, modelSize.z });
                if (maxDim > 0.001f) {
                    // モデルのサイズと中心点に基づき、フィットするスケールとオフセットを計算
                    autoScale = 8.5f / maxDim;
                    autoTranslate.x = -modelCenter.x * autoScale;
                    autoTranslate.y = -modelCenter.y * autoScale + 1.1f;
                    autoTranslate.z = -modelCenter.z * autoScale;
                }
            }

            // 計算したスケールと位置をセット（これ以降は保持される）
            data.previewObject->GetTransform()->scale = { autoScale, autoScale, autoScale };
            data.previewObject->GetTransform()->translate = autoTranslate;

            // スタジオカメラの角度に合わせ、モデルを上向きにして正面を向くように調整
            data.previewObject->GetTransform()->rotate.x = -0.2f;
        }

        auto& targetObj = data.previewObject;
        ApplyThumbnailLightOverride(targetObj.get());

        // くるくる回転
        targetObj->GetTransform()->rotate.y += 0.02f;

        // 行列の更新と描画
        targetObj->UpdateLocalMatrix();
        targetObj->UpdateWorldMatrix();

        targetObj->Draw(nullptr, nullptr);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        commandList->ResourceBarrier(1, &barrier);

        data.isCaptured = true;
    }

    // --- プリセットの撮影ループ ---
    for (auto& pair : presetThumbnailAlbum_) {
        std::string presetName = pair.first;
        ThumbnailData& data = pair.second;
        if (data.isCaptured) continue;

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = data.resource.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        commandList->ResourceBarrier(1, &barrier);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = data.rtvHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = s_studioDsvHeap->GetCPUDescriptorHandleForHeapStart();
        commandList->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);

        const float clearColor[] = { 0.2f, 0.2f, 0.25f, 1.0f };
        commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
        commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        D3D12_VIEWPORT viewport = { 0, 0, (float)kThumbnailSize, (float)kThumbnailSize, 0.0f, 1.0f };
        commandList->RSSetViewports(1, &viewport);
        D3D12_RECT scissor = { 0, 0, kThumbnailSize, kThumbnailSize };
        commandList->RSSetScissorRects(1, &scissor);

        objCommon->SetGraphicsCommand();
        objCommon->SetPipelineState(BlendMode::kNormal);

        if (!data.previewObject) {
            data.previewObject = std::make_shared<Object3d>();
            data.previewObject->Initialize(objCommon);
            data.previewObject->SetIsUIPreview(true);
        }

        // プリセットの設定を適用！
        PresetManager::GetInstance()->ApplyPresetToObject(presetName, data.previewObject.get());

        // プレビュー用に姿勢を微調整（くるくる回す）
        static float rotationY = 0.0f;
        rotationY += 0.02f;
        data.previewObject->GetTransform()->rotate.y = rotationY;
        data.previewObject->GetTransform()->rotate.x = -0.2f;

        // モデルの自動フィット計算 (モデルが設定されている場合のみ)
        if (!data.previewObject->GetModelName().empty()) {
            Model* model = ModelManager::GetInstance()->LoadModel(data.previewObject->GetModelName());
            if (model) {
                Vector3 modelSize = model->GetSize();
                Vector3 modelCenter = model->GetCenter();
                float maxDim = (std::max)({ modelSize.x, modelSize.y, modelSize.z });
                if (maxDim > 0.001f) {
                    float autoScale = 8.0f / maxDim;
                    data.previewObject->GetTransform()->scale = { autoScale, autoScale, autoScale };
                    data.previewObject->GetTransform()->translate.x = -modelCenter.x * autoScale;
                    data.previewObject->GetTransform()->translate.y = -modelCenter.y * autoScale + 1.1f;
                    data.previewObject->GetTransform()->translate.z = -modelCenter.z * autoScale;
                }
            }
        }

        ApplyThumbnailLightOverride(data.previewObject.get());

        data.previewObject->UpdateLocalMatrix();
        data.previewObject->UpdateWorldMatrix();
        data.previewObject->Draw(nullptr, nullptr);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        commandList->ResourceBarrier(1, &barrier);

        data.isCaptured = true;
    }


    CameraManager::GetInstance()->SetActiveCamera(nullptr);

    commandList->RSSetViewports(1, &originalViewport);
    commandList->RSSetScissorRects(1, &originalScissor);
}
void ProjectWindow::Draw() {
#ifdef USE_IMGUI
    // ---------------------------------------------------------
    // ウィンドウ開始
    // ---------------------------------------------------------
    ImGui::Begin("Project (Assets)");

    // =================================================================================
    // 1. モデルファイル一覧 (Raw Models)
    // =================================================================================
    if (ImGui::CollapsingHeader("Models (Source)", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::string baseDirectory = "Resources/3DModel/";

        if (currentModelDirectory_.empty() || currentModelDirectory_.find(baseDirectory) != 0) {
            currentModelDirectory_ = baseDirectory;
        }

        if (currentModelDirectory_ != baseDirectory) {
            if (ImGui::Button(ICON_FA_ARROW_UP " Back")) {
                fs::path p(currentModelDirectory_);
                currentModelDirectory_ = p.parent_path().parent_path().generic_string() + "/";
                if (currentModelDirectory_.length() < baseDirectory.length() ||
                    currentModelDirectory_.find(baseDirectory) == std::string::npos) {
                    currentModelDirectory_ = baseDirectory;
                }
            }
            ImGui::SameLine();
        }
        ImGui::TextDisabled("%s", currentModelDirectory_.c_str());
        ImGui::Separator();

        if (fs::exists(currentModelDirectory_) && fs::is_directory(currentModelDirectory_)) {
            ImGui::TextDisabled("Drag & Drop to Scene to Place");
            ImGui::Separator();

            float thumbnailSize = 64.0f;
            float padding = 16.0f;
            float cellSize = thumbnailSize + padding;
            float panelWidth = ImGui::GetContentRegionAvail().x;
            int columnCount = std::max(1, (int)(panelWidth / cellSize));

            if (ImGui::BeginTable("ModelAssetTable", columnCount)) {
                for (const auto& entry : fs::directory_iterator(currentModelDirectory_)) {
                    if (entry.is_directory()) {
                        std::string folderName = entry.path().filename().string();
                        std::string payloadName = fs::relative(entry.path(), baseDirectory).generic_string();
                        std::replace(payloadName.begin(), payloadName.end(), '\\', '/');
                        std::string displayModelName = payloadName; // ModelManagerのキー名として使用
                        
                        bool isModelFolder = false;
                        for (const auto& subEntry : fs::directory_iterator(entry.path())) {
                            if (subEntry.is_regular_file()) {
                                std::string subExt = subEntry.path().extension().string();
                                std::transform(subExt.begin(), subExt.end(), subExt.begin(), ::tolower);
                                if (subExt == ".obj" || subExt == ".gltf" || subExt == ".glb") {
                                    isModelFolder = true;
                                    break;
                                }
                            }
                        }

                        ImGui::TableNextColumn();
                        if (isModelFolder) {
                            // 1. アルバムにこのモデル用の画用紙が無ければ作る
                            if (thumbnailAlbum_.find(displayModelName) == thumbnailAlbum_.end()) {
                                CreateThumbnailResource(displayModelName);
                            }

                            // 2. 作った（あるいは既にある）画用紙の画像ハンドルを取得
                            uint32_t srvHandle = thumbnailAlbum_[displayModelName].srvHandle;
                            D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = SRVManager::GetInstance()->GetGPUDescriptorHandle(srvHandle);

                            ImGui::PushID(displayModelName.c_str());
                            ImGui::BeginGroup();

                            // 3. 画用紙を表示
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                            ImGui::ImageButton(displayModelName.c_str(), (ImTextureID)(uintptr_t)gpuHandle.ptr, ImVec2(thumbnailSize, thumbnailSize));
                            ImGui::PopStyleColor();

                            // ドラッグ＆ドロップ処理
                            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                                ImGui::SetDragDropPayload("MODEL_ASSET", payloadName.c_str(), payloadName.size() + 1);
                                ImGui::Image((ImTextureID)(uintptr_t)gpuHandle.ptr, ImVec2(32.0f, 32.0f));
                                ImGui::SameLine();
                                ImGui::Text("Place: %s", folderName.c_str());
                                ImGui::EndDragDropSource();
                            }

                            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + thumbnailSize);
                            ImGui::TextWrapped("%s", folderName.c_str());
                            ImGui::PopTextWrapPos();

                            ImGui::EndGroup();
                            ImGui::PopID();
                        } else {
                            // カテゴリフォルダとして扱う
                            ImGui::PushID(folderName.c_str());
                            ImGui::BeginGroup();

                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.2f));
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.2f, 1.0f));

                            ImGui::SetWindowFontScale(3.0f);
                            if (ImGui::Button(ICON_FA_FOLDER, ImVec2(thumbnailSize, thumbnailSize))) {
                                currentModelDirectory_ = entry.path().generic_string() + "/";
                            }
                            ImGui::SetWindowFontScale(1.0f);
                            ImGui::PopStyleColor(4);

                            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + thumbnailSize);
                            ImGui::TextWrapped("%s", folderName.c_str());
                            ImGui::PopTextWrapPos();

                            ImGui::EndGroup();
                            ImGui::PopID();
                        }
                    }
                    else if (entry.is_regular_file() && IsModelAssetFile(entry.path())) {
                        const std::string fileName = entry.path().filename().string();
                        std::string payloadName = fs::relative(entry.path(), baseDirectory).generic_string();
                        std::replace(payloadName.begin(), payloadName.end(), '\\', '/');
                        const bool isGeneratedLod = IsGeneratedLodAsset(entry.path());

                        if (thumbnailAlbum_.find(payloadName) == thumbnailAlbum_.end()) {
                            CreateThumbnailResource(payloadName);
                        }

                        uint32_t srvHandle = thumbnailAlbum_[payloadName].srvHandle;
                        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = SRVManager::GetInstance()->GetGPUDescriptorHandle(srvHandle);

                        ImGui::TableNextColumn();
                        ImGui::PushID(payloadName.c_str());
                        ImGui::BeginGroup();

                        if (isGeneratedLod) {
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.28f, 0.05f, 0.9f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.55f, 0.43f, 0.08f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.52f, 0.10f, 1.0f));
                        }
                        else {
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.2f));
                        }
                        ImGui::ImageButton(payloadName.c_str(), (ImTextureID)(uintptr_t)gpuHandle.ptr, ImVec2(thumbnailSize, thumbnailSize));
                        ImGui::PopStyleColor(3);

                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                            ImGui::SetDragDropPayload("MODEL_ASSET", payloadName.c_str(), payloadName.size() + 1);
                            ImGui::Image((ImTextureID)(uintptr_t)gpuHandle.ptr, ImVec2(32.0f, 32.0f));
                            ImGui::SameLine();
                            ImGui::Text("Place: %s", fileName.c_str());
                            ImGui::EndDragDropSource();
                        }

                        if (isGeneratedLod) {
                            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.25f, 1.0f), "LOD");
                        }
                        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + thumbnailSize);
                        ImGui::TextWrapped("%s", fileName.c_str());
                        ImGui::PopTextWrapPos();

                        ImGui::EndGroup();
                        ImGui::PopID();
                    }
                }
                ImGui::EndTable();
            }
        }
        else {
            ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "Directory Not Found: %s", currentModelDirectory_.c_str());
        }
    }

    // =================================================================================
    // 2. プリセット一覧 (Presets)
    // =================================================================================
    if (ImGui::CollapsingHeader("Presets (Configured)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), ICON_FA_PLUS_SQUARE " Create New Preset");
        ImGui::TextDisabled(" (Use '/' in name for folders, e.g. 'Enemy/Slime')");

        if (editor_->GetSelectedObject()) {
            static char presetNameBuf[64] = "NewPreset";
            ImGui::PushItemWidth(200);
            ImGui::InputText("##PresetName", presetNameBuf, 64);
            ImGui::PopItemWidth();

            ImGui::SameLine();

            if (ImGui::Button(ICON_FA_SAVE " Save Selection")) {
                if (strlen(presetNameBuf) > 0) {
                    std::string pName = presetNameBuf;
                    PresetManager::GetInstance()->AddPresetFromObject(pName, editor_->GetSelectedObject());
                    if (presetThumbnailAlbum_.count(pName)) presetThumbnailAlbum_[pName].isCaptured = false;
                    DebugConsole::GetInstance()->AddLog("Saved Preset: " + pName);
                }
            }
        }
        else {
            ImGui::TextDisabled("(Select an object in Scene to save)");
        }

        ImGui::Separator();
        ImGui::Spacing();

        const auto& presets = PresetManager::GetInstance()->GetPresets();

        if (presets.empty()) {
            ImGui::TextDisabled("(No Presets Saved)");
        }
        else {
            // プリセットをフォルダ（スラッシュ区切り）で分類
            std::map<std::string, std::vector<std::string>> folders;
            std::vector<std::string> rootPresets;

            for (const auto& [name, data] : presets) {
                size_t slashPos = name.find('/');
                if (slashPos != std::string::npos) {
                    folders[name.substr(0, slashPos)].push_back(name);
                }
                else {
                    rootPresets.push_back(name);
                }
            }

            // 描画用ヘルパーラムダ
            auto DrawPresetGrid = [&](const std::vector<std::string>& list, const char* tableId) {
                float thumbnailSize = 80.0f;
                float padding = 16.0f;
                float cellSize = thumbnailSize + padding;
                float panelWidth = ImGui::GetContentRegionAvail().x;
                int cols = std::max(1, (int)(panelWidth / cellSize));

                if (ImGui::BeginTable(tableId, cols)) {
                    for (const std::string& name : list) {
                        ImGui::TableNextColumn();
                        if (presetThumbnailAlbum_.find(name) == presetThumbnailAlbum_.end()) CreatePresetThumbnailResource(name);

                        uint32_t srvHandle = presetThumbnailAlbum_[name].srvHandle;
                        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = SRVManager::GetInstance()->GetGPUDescriptorHandle(srvHandle);

                        ImGui::PushID(name.c_str());
                        ImGui::BeginGroup();
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.1f, 0.15f, 1.0f));
                        ImGui::ImageButton(name.c_str(), (ImTextureID)(uintptr_t)gpuHandle.ptr, ImVec2(thumbnailSize, thumbnailSize));
                        ImGui::PopStyleColor();

                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                            ImGui::SetDragDropPayload("PRESET_ASSET", name.c_str(), name.size() + 1);
                            ImGui::Image((ImTextureID)(uintptr_t)gpuHandle.ptr, ImVec2(32.0f, 32.0f));
                            ImGui::SameLine(); ImGui::Text("Preset: %s", name.c_str());
                            ImGui::EndDragDropSource();
                        }

                        size_t lastSlash = name.find_last_of('/');
                        std::string shortName = (lastSlash != std::string::npos) ? name.substr(lastSlash + 1) : name;
                        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + thumbnailSize);
                        ImGui::TextWrapped("%s", shortName.c_str());
                        ImGui::PopTextWrapPos();

                        bool openRename = false; // リネーム画面を開くフラグ

                        // コンテキストメニュー
                        if (ImGui::BeginPopupContextItem("PresetContextMenu")) {
                            if (ImGui::MenuItem(ICON_FA_EDIT " Rename")) {
                                openRename = true; // 直接開かずフラグを立てる
                            }
                            if (ImGui::MenuItem(ICON_FA_SAVE " Overwrite")) {
                                if (editor_->GetSelectedObject()) {
                                    PresetManager::GetInstance()->AddPresetFromObject(name, editor_->GetSelectedObject());
                                    presetThumbnailAlbum_[name].isCaptured = false;
                                }
                            }
                            if (ImGui::MenuItem(ICON_FA_CAMERA " Update Thumbnail")) presetThumbnailAlbum_[name].isCaptured = false;

                            ImGui::Separator();

                            if (ImGui::MenuItem(ICON_FA_TRASH_ALT " Delete")) {
                                PresetManager::GetInstance()->RemovePreset(name);
                                // presetThumbnailAlbum_.erase(name); // GPUクラッシュ防止のため意図的に残さない
                                ImGui::EndPopup(); ImGui::EndGroup(); ImGui::PopID();
                                continue;
                            }
                            ImGui::EndPopup();
                        }

                        // フラグが立ったらポップアップを開く
                        if (openRename) {
                            ImGui::OpenPopup("RenamePresetPopup");
                        }

                        // 名前変更ポップアップの実処理
                        if (ImGui::BeginPopup("RenamePresetPopup")) {
                            static char renameBuf[64] = "";

                            // 開いた瞬間だけ文字列をコピーして初期化する
                            if (openRename) {
                                strncpy_s(renameBuf, name.c_str(), sizeof(renameBuf));
                            }

                            ImGui::Text("New Name:");
                            bool isEnter = ImGui::InputText("##NewName", renameBuf, sizeof(renameBuf), ImGuiInputTextFlags_EnterReturnsTrue);

                            bool isOk = ImGui::Button("OK");
                            ImGui::SameLine();
                            bool isCancel = ImGui::Button("Cancel");

                            // エンターかOKが押されたらリネーム実行
                            if (isEnter || isOk) {
                                std::string newName = renameBuf;
                                if (!newName.empty() && newName != name) {
                                    PresetManager::GetInstance()->RenamePreset(name, newName);
                                    if (presetThumbnailAlbum_.count(name)) {
                                        presetThumbnailAlbum_[newName] = std::move(presetThumbnailAlbum_[name]);
                                        presetThumbnailAlbum_.erase(name);
                                    }
                                }
                                ImGui::CloseCurrentPopup();
                            }
                            else if (isCancel) {
                                ImGui::CloseCurrentPopup();
                            }
                            ImGui::EndPopup();
                        }

                        ImGui::EndGroup();
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                };

            // フォルダごとの表示
            for (const auto& [folderName, list] : folders) {
                if (ImGui::TreeNodeEx(folderName.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                    DrawPresetGrid(list, folderName.c_str());
                    ImGui::TreePop();
                }
            }
            // ルート直下の表示
            if (!rootPresets.empty()) {
                if (folders.empty()) DrawPresetGrid(rootPresets, "RootPresetsTable");
                else if (ImGui::TreeNodeEx("Others", ImGuiTreeNodeFlags_DefaultOpen)) {
                    DrawPresetGrid(rootPresets, "RootPresetsTable");
                    ImGui::TreePop();
                }
            }
        }
    }

    // =================================================================================
    // 3. パーティクル一覧 (GPU Particles)
    // =================================================================================
    
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Prefabs (v1)", ImGuiTreeNodeFlags_DefaultOpen)) {
            static char prefabNameBuf[64] = "NewPrefab";
            ImGui::InputText("Prefab名", prefabNameBuf, IM_ARRAYSIZE(prefabNameBuf));

            Object3d* selectedObject = editor_ ? editor_->GetSelectedObject() : nullptr;
            if (!selectedObject) {
                ImGui::TextDisabled("保存するObjectをHierarchyまたはGame Viewで選択してください。");
            }

            if (ImGui::Button("選択ObjectをPrefab保存") && selectedObject && prefabNameBuf[0] != '\0') {
                PresetManager::GetInstance()->AddPrefabFromObject(prefabNameBuf, selectedObject);
            }

            ImGui::TextDisabled("Prefab v1は階層を複製保存します。元Prefabとのリンク/Overrideは未対応です。");

            const auto& prefabs = PresetManager::GetInstance()->GetPrefabs();
            if (prefabs.empty()) {
                ImGui::TextDisabled("Prefabはまだありません。");
            }

            int prefabIndex = 0;
            for (const auto& [prefabName, prefabJson] : prefabs) {
                (void)prefabJson;
                ImGui::PushID(prefabName.c_str());
                ImGui::BeginGroup();
                ImGui::Button("Prefab", ImVec2(86.0f, 46.0f));
                if (ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("PREFAB_ASSET", prefabName.c_str(), prefabName.size() + 1);
                    ImGui::Text("Prefab: %s", prefabName.c_str());
                    ImGui::EndDragDropSource();
                }
                ImGui::TextWrapped("%s", prefabName.c_str());
                ImGui::EndGroup();
                ImGui::PopID();

                ++prefabIndex;
                if (prefabIndex % 4 != 0) {
                    ImGui::SameLine();
                }
            }
        }
if (ImGui::CollapsingHeader("VFX / Particles", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::string particleDir = "Resources/json/gpu_particles/";
        if (fs::exists(particleDir)) {
            float thumbnailSize = 64.0f;
            float padding = 16.0f;
            float cellSize = thumbnailSize + padding;
            float panelWidth = ImGui::GetContentRegionAvail().x;
            int columnCount = std::max(1, (int)(panelWidth / cellSize));
            
            if (ImGui::BeginTable("ParticleAssetTable", columnCount)) {
                for (const auto& entry : fs::directory_iterator(particleDir)) {
                    if (entry.path().extension() == ".json") {
                        std::string presetName = entry.path().stem().string();
                        ImGui::TableNextColumn();
                        
                        ImGui::PushID(("Particle_" + presetName).c_str());
                        ImGui::BeginGroup();
                        
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.1f, 0.1f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.15f, 0.15f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.2f, 0.2f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.0f, 1.0f));
                        
                        ImGui::SetWindowFontScale(2.5f);
                        ImGui::Button(ICON_FA_FIRE, ImVec2(thumbnailSize, thumbnailSize));
                        ImGui::SetWindowFontScale(1.0f);
                        ImGui::PopStyleColor(4);
                        
                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                            ImGui::SetDragDropPayload("PARTICLE_ASSET", presetName.c_str(), presetName.size() + 1);
                            ImGui::Text("Place Particle: %s", presetName.c_str());
                            ImGui::EndDragDropSource();
                        }
                        
                        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + thumbnailSize);
                        ImGui::TextWrapped("%s", presetName.c_str());
                        ImGui::PopTextWrapPos();
                        
                        ImGui::EndGroup();
                        ImGui::PopID();
                    }
                }
                ImGui::EndTable();
            }
        }
    }

    ImGui::End();
#endif
}
