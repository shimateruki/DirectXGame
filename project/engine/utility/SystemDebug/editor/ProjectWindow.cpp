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
#include <filesystem>

namespace fs = std::filesystem;

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

        // ▼ 修正：パイプラインの要求に合わせて D24_UNORM_S8_UINT に変更
        depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

        depthDesc.SampleDesc.Count = 1;
        depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_CLEAR_VALUE clearValue{};
        // ▼ 修正：クリア値のフォーマットも合わせる
        clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        clearValue.DepthStencil.Depth = 1.0f;
        clearValue.DepthStencil.Stencil = 0; // ステンシル値も初期化

        device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue, IID_PPV_ARGS(&s_studioDepth));

        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.NumDescriptors = 1;
        device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&s_studioDsvHeap));

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        // ▼ 修正：ビューのフォーマットも合わせる
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

    Camera studioCamera;
    studioCamera.Initialize();           // 初期値: eye(0,5,-20) -> target(0,0,0)
    studioCamera.SetInputEnabled(false); // エラー回避のため入力を無視
    studioCamera.Update();               // 行列を計算

    // メインカメラから、このスタジオ用カメラにすり替える！
    CameraManager::GetInstance()->SetActiveCamera(&studioCamera);
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

            // ★ ここで LoadModel しつつ、Model実体のポインタを受け取る！
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
                    // ★ ジャストサイズと中心点を計算
                    autoScale = 8.5f / maxDim;
                    autoTranslate.x = -modelCenter.x * autoScale;
                    autoTranslate.y = -modelCenter.y * autoScale + 1.1f;
                    autoTranslate.z = -modelCenter.z * autoScale;
                }
            }

            // 計算したスケールと位置をセット（これ以降は保持される）
            data.previewObject->GetTransform()->scale = { autoScale, autoScale, autoScale };
            data.previewObject->GetTransform()->translate = autoTranslate;

            // ★ スタジオカメラが少し見下ろしているため、モデルを上向きにして正面からのフレーミングにする
            data.previewObject->GetTransform()->rotate.x = -0.2f;
        }

        auto& targetObj = data.previewObject;

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
        std::string baseDirectory = "Resources/3DModel";

        if (fs::exists(baseDirectory) && fs::is_directory(baseDirectory)) {
            ImGui::TextDisabled("Drag & Drop to Scene to Place");
            ImGui::Separator();

            // ★ サムネイル用レイアウトの計算
            float thumbnailSize = 64.0f;
            float padding = 16.0f;
            float cellSize = thumbnailSize + padding;
            float panelWidth = ImGui::GetContentRegionAvail().x;
            int columnCount = std::max(1, (int)(panelWidth / cellSize));

            // ★ テーブルを使った自動折り返しのグリッドレイアウト
            if (ImGui::BeginTable("ModelAssetTable", columnCount)) {
                for (const auto& entry : fs::directory_iterator(baseDirectory)) {
                    std::string displayModelName = ""; // ボタン表示名
                    std::string payloadName = "";      // ロード用パス/名前

                    // 隊長の拡張子解析ロジック（そのまま活かします！）
                    if (entry.is_directory()) {
                        std::string folderName = entry.path().filename().string();
                        for (const auto& subEntry : fs::directory_iterator(entry.path())) {
                            std::string subExt = subEntry.path().extension().string();
                            std::transform(subExt.begin(), subExt.end(), subExt.begin(), ::tolower);

                            if (subExt == ".obj") {
                                displayModelName = folderName;
                                payloadName = folderName;
                                break;
                            }
                            else if (subExt == ".gltf" || subExt == ".glb") {
                                displayModelName = subEntry.path().filename().string();
                                payloadName = subEntry.path().filename().string();
                                break;
                            }
                        }
                    }

                    // モデルが正しく見つかった場合のみサムネイルを描画
                    if (!displayModelName.empty()) {
                        ImGui::TableNextColumn(); // 次のマスへ移動

                        // ==========================================================
                        // ★ フェーズ1の魔法: 画用紙の準備と表示
                        // ==========================================================

                        // 1. アルバムにこのモデル用の画用紙が無ければ作る
                        if (thumbnailAlbum_.find(displayModelName) == thumbnailAlbum_.end()) {
                            CreateThumbnailResource(displayModelName);
                        }

                        // 2. 作った（あるいは既にある）画用紙の画像ハンドルを取得
                        uint32_t srvHandle = thumbnailAlbum_[displayModelName].srvHandle;
                        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = SRVManager::GetInstance()->GetGPUDescriptorHandle(srvHandle);

                        ImGui::PushID(displayModelName.c_str());
                        ImGui::BeginGroup(); // 画像とテキストをグループ化

                        // 3. 画用紙を表示！（フェーズ2でここにモデルが写ります）
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0)); // 背景透過
                        ImGui::ImageButton(displayModelName.c_str(), (ImTextureID)(uintptr_t)gpuHandle.ptr, ImVec2(thumbnailSize, thumbnailSize));
                        ImGui::PopStyleColor();

                        // ドラッグ＆ドロップ処理（プレビューにも画像を表示！）
                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                            ImGui::SetDragDropPayload("MODEL_ASSET", payloadName.c_str(), payloadName.size() + 1);
                            ImGui::Image((ImTextureID)(uintptr_t)gpuHandle.ptr, ImVec2(32.0f, 32.0f));
                            ImGui::SameLine();
                            ImGui::Text("Place: %s", displayModelName.c_str());
                            ImGui::EndDragDropSource();
                        }

                        // 名前表示 (長すぎたら自動折り返し)
                        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + thumbnailSize);
                        ImGui::TextWrapped("%s", displayModelName.c_str());
                        ImGui::PopTextWrapPos();

                        ImGui::EndGroup();
                        ImGui::PopID();
                    }
                }
                ImGui::EndTable();
            }
        }
        else {
            ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "Directory Not Found: %s", baseDirectory.c_str());
        }
    }

    // =================================================================================
    // 2. プリセット一覧 (Presets)
    // =================================================================================
    if (ImGui::CollapsingHeader("Presets (Configured)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "▼ Create New Preset");

        if (editor_->GetSelectedObject()) {
            static char presetNameBuf[64] = "NewPreset";
            ImGui::PushItemWidth(150);
            ImGui::InputText("##PresetName", presetNameBuf, 64);
            ImGui::PopItemWidth();

            ImGui::SameLine();

            if (ImGui::Button("Save Selection")) {
                if (strlen(presetNameBuf) > 0) {
                    PresetManager::GetInstance()->AddPresetFromObject(presetNameBuf, editor_->GetSelectedObject());
                    DebugConsole::GetInstance()->AddLog("Saved Preset: " + std::string(presetNameBuf));
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Save the currently selected object's settings as a new preset.");
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
            // プリセット一覧も Table でレスポンシブに綺麗に並べるように改良
            float buttonWidth = 100.0f;
            float padding = ImGui::GetStyle().ItemSpacing.x;
            float panelWidth = ImGui::GetContentRegionAvail().x;
            int presetCols = std::max(1, (int)(panelWidth / (buttonWidth + padding)));

            if (ImGui::BeginTable("PresetTable", presetCols)) {
                for (const auto& [name, data] : presets) {
                    ImGui::TableNextColumn();

                    ImGui::PushID(name.c_str());
                    ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.3f, 0.6f, 0.6f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0.3f, 0.7f, 0.7f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0.3f, 0.8f, 0.8f));

                    ImGui::Button(name.c_str(), ImVec2(buttonWidth, 0));

                    ImGui::PopStyleColor(3);

                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                        ImGui::SetDragDropPayload("PRESET_ASSET", name.c_str(), name.size() + 1);
                        ImGui::Text("Preset: %s", name.c_str());
                        ImGui::EndDragDropSource();
                    }

                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
    }

    ImGui::End();
#endif
}