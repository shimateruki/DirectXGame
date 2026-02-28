#include "ImGuiManager.h"
#include "SRVManager.h"

ImGuiManager* ImGuiManager::GetInstance() {
    static ImGuiManager instance;
    return &instance;
}

void ImGuiManager::Initialize(WinApp* winApp, DirectXCommon* dxCommon) {
    (void)winApp;
#ifdef USE_IMGUI
    dxCommon_ = dxCommon;

    // 1. ImGuiのコンテキストを生成
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;    // ドッキング有効化
    // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // 必要に応じて有効化

    // ImGuiのスタイルを設定
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();

    // --- Unity Dark風のスタイル調整 ---
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.PopupRounding = 0.0f;
    style.TabRounding = 2.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;

    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    style.Colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    style.Colors[ImGuiCol_TabActive] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    style.Colors[ImGuiCol_DockingPreview] = ImVec4(0.30f, 0.50f, 0.80f, 0.70f);

    // フォント読み込み
    io.Fonts->AddFontFromFileTTF(
        "Resources/sprite/meiryo.ttc",
        18.0f,
        nullptr,
        io.Fonts->GetGlyphRangesJapanese()
    );

    // プラットフォームのバックエンドを初期化
    ImGui_ImplWin32_Init(winApp->GetHwnd());

    // =================================================================
    // 2. DX12レンダラーの初期化 (マルチテクスチャ対応版)
    // =================================================================

    // SRVManagerからヒープを取得
    ID3D12DescriptorHeap* srvDescriptorHeap = SRVManager::GetInstance()->GetDescriptorHeap();

    // ImGuiが内部でデスクリプタを確保・解放するためのコールバック関数

    auto AllocSrv = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle) {
        uint32_t index = SRVManager::GetInstance()->Allocate(); // 空きスロットを1つ確保
        *out_cpu_handle = SRVManager::GetInstance()->GetCPUDescriptorHandle(index);
        *out_gpu_handle = SRVManager::GetInstance()->GetGPUDescriptorHandle(index);
        };

    auto FreeSrv = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle) {
  
        };

    ImGui_ImplDX12_InitInfo initInfo = {};
    initInfo.Device = dxCommon_->GetDevice();
    initInfo.CommandQueue = dxCommon_->GetCommandQueue();
    initInfo.NumFramesInFlight = 2;
    initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    initInfo.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    initInfo.SrvDescriptorHeap = srvDescriptorHeap;

    // ★修正の要：Legacy設定を0にし、関数ポインタをセットする
    initInfo.LegacySingleSrvCpuDescriptor = { 0 };
    initInfo.LegacySingleSrvGpuDescriptor = { 0 };
    initInfo.SrvDescriptorAllocFn = AllocSrv;
    initInfo.SrvDescriptorFreeFn = FreeSrv;

    ImGui_ImplDX12_Init(&initInfo);

#endif
}
void ImGuiManager::Finalize() {
#ifdef USE_IMGUI
    // バックエンドをシャットダウン
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    // ImGuiのコンテキストを破棄
    ImGui::DestroyContext();
#endif
}

void ImGuiManager::BeginFrame() {
#ifdef USE_IMGUI
    // フレームの開始をImGuiに伝える
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
#endif
}

void ImGuiManager::Draw() {
#ifdef USE_IMGUI
    // 1. まず「描画内容」を確定させる
    ImGui::Render();

    // 2. DX12のコマンドリストに記録
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    assert(commandList != nullptr);
    SRVManager::GetInstance()->SetDescriptorHeaps(commandList);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
#endif
}

void ImGuiManager::EndFrame() {
#ifdef USE_IMGUI
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        // 1. 今のコンテキストをバックアップ
        ImGuiContext* backup_current_context = ImGui::GetCurrentContext();

        // 2. ウィンドウの位置などを更新
        ImGui::UpdatePlatformWindows();

        // 3. サブウィンドウを描画
        // ※最新版では、第1引数と第2引数は nullptr で内部解決させ、
        //   メインのコマンドリストに記録させるのが最も安定します。
        ImGui::RenderPlatformWindowsDefault(nullptr, nullptr);

        // 4. コンテキストを復元
        ImGui::SetCurrentContext(backup_current_context);
    }
#endif
}