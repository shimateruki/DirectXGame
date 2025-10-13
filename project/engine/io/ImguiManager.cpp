#include "ImGuiManager.h"
#include "engine/base/SRVManager.h"

ImGuiManager* ImGuiManager::GetInstance() {
    static ImGuiManager instance;
    return &instance;
}

void ImGuiManager::Initialize(WinApp* winApp, DirectXCommon* dxCommon) {
    dxCommon_ = dxCommon;

    // ImGuiのコンテキストを生成
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    // ImGuiのスタイルを設定
    ImGui::StyleColorsDark();

    // プラットフォームとレンダラーのバックエンドを初期化
    ImGui_ImplWin32_Init(winApp->GetHwnd());

    // SRVManagerからSRV用のデスクリプタヒープを取得
    ID3D12DescriptorHeap* srvDescriptorHeap = SRVManager::GetInstance()->GetDescriptorHeap();

    ImGui_ImplDX12_Init(
        dxCommon_->GetDevice(),
        static_cast<int>(dxCommon_->GetBackBufferCount()),
        dxCommon_->GetRTVFormat(),
        srvDescriptorHeap,
        srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
        srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart()
    );
}

void ImGuiManager::Finalize() {
    // バックエンドをシャットダウン
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    // ImGuiのコンテキストを破棄
    ImGui::DestroyContext();
}

void ImGuiManager::BeginFrame() {
    // フレームの開始をImGuiに伝える
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void ImGuiManager::EndFrame() {

}

void ImGuiManager::Draw() {
    // ImGuiの内部コマンドを生成
    ImGui::Render();

    // ImGuiの描画コマンドをコマンドリストに記録
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), dxCommon_->GetCommandList());
}